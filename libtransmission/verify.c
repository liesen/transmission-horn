/*
 * This file Copyright (C) 2007-2009 Charles Kerr <charles@transmissionbt.com>
 *
 * This file is licensed by the GPL version 2.  Works owned by the
 * Transmission project are granted a special exemption to clause 2(b)
 * so that the bulk of its code can remain under the MIT license.
 * This exemption does not extend to derived works not owned by
 * the Transmission project.
 *
 * $Id$
 */

#include <unistd.h> /* S_ISREG */
#include <sys/stat.h>

#include "transmission.h"
#include "completion.h"
#include "resume.h" /* tr_torrentSaveResume() */
#include "inout.h"
#include "list.h"
#include "platform.h"
#include "torrent.h"
#include "utils.h" /* tr_buildPath */
#include "verify.h"

/**
***
**/

struct verify_node
{
    tr_torrent *         torrent;
    tr_verify_done_cb    verify_done_cb;
};

static void
fireCheckDone( tr_torrent * tor, tr_verify_done_cb verify_done_cb )
{
    assert( tr_isTorrent( tor ) );

    if( verify_done_cb )
        verify_done_cb( tor );
}

static struct verify_node currentNode;
static tr_list * verifyList = NULL;
static tr_thread * verifyThread = NULL;
static int stopCurrent = FALSE;

static tr_lock*
getVerifyLock( void )
{
    static tr_lock * lock = NULL;

    if( lock == NULL )
        lock = tr_lockNew( );
    return lock;
}

static int
checkFile( tr_torrent      * tor,
           void            * buffer,
           size_t            buflen,
           tr_file_index_t   fileIndex,
           int             * abortFlag )
{
    tr_piece_index_t i;
    int              changed = FALSE;
    int              nofile;
    struct stat      sb;
    char           * path;
    const tr_file  * file = &tor->info.files[fileIndex];

    path = tr_buildPath( tor->downloadDir, file->name, NULL );
    nofile = stat( path, &sb ) || !S_ISREG( sb.st_mode );

    for( i = file->firstPiece;
         i <= file->lastPiece && i < tor->info.pieceCount && ( !*abortFlag );
         ++i )
    {
        if( nofile )
        {
            tr_torrentSetHasPiece( tor, i, 0 );
        }
        else if( !tr_torrentIsPieceChecked( tor, i ) )
        {
            const int wasComplete = tr_cpPieceIsComplete( &tor->completion, i );

            if( tr_ioTestPiece( tor, i, buffer, buflen ) ) /* yay */
            {
                tr_torrentSetHasPiece( tor, i, TRUE );
                if( !wasComplete )
                    changed = TRUE;
            }
            else
            {
                /* if we were wrong about it being complete,
                 * reset and start again.  if we were right about
                 * it being incomplete, do nothing -- we don't
                 * want to lose blocks in those incomplete pieces */

                if( wasComplete )
                {
                    tr_torrentSetHasPiece( tor, i, FALSE );
                    changed = TRUE;
                }
            }
        }

        tr_torrentSetPieceChecked( tor, i, TRUE );
    }

    tr_free( path );

    return changed;
}

static void
verifyThreadFunc( void * unused UNUSED )
{
    for( ;; )
    {
        int                  changed = 0;
        tr_file_index_t      i;
        tr_torrent         * tor;
        struct verify_node * node;
        void               * buffer;

        tr_lockLock( getVerifyLock( ) );
        stopCurrent = FALSE;
        node = (struct verify_node*) verifyList ? verifyList->data : NULL;
        if( node == NULL )
        {
            currentNode.torrent = NULL;
            break;
        }

        currentNode = *node;
        tor = currentNode.torrent;
        tr_list_remove_data( &verifyList, node );
        tr_free( node );
        tr_lockUnlock( getVerifyLock( ) );

        tr_torinf( tor, _( "Verifying torrent" ) );
        assert( tr_isTorrent( tor ) );
        tor->verifyState = TR_VERIFY_NOW;
        buffer = tr_new( uint8_t, tor->info.pieceSize );
        for( i = 0; i < tor->info.fileCount && !stopCurrent; ++i )
            changed |= checkFile( tor, buffer, tor->info.pieceSize, i, &stopCurrent );
        tr_free( buffer );
        tor->verifyState = TR_VERIFY_NONE;
        assert( tr_isTorrent( tor ) );

        if( !stopCurrent )
        {
            if( changed )
                tr_torrentSaveResume( tor );
            fireCheckDone( tor, currentNode.verify_done_cb );
        }
    }

    verifyThread = NULL;
    tr_lockUnlock( getVerifyLock( ) );
}

void
tr_verifyAdd( tr_torrent *      tor,
              tr_verify_done_cb verify_done_cb )
{
    const int uncheckedCount = tr_torrentCountUncheckedPieces( tor );

    assert( tr_isTorrent( tor ) );

    if( !uncheckedCount )
    {
        /* doesn't need to be checked... */
        fireCheckDone( tor, verify_done_cb );
    }
    else
    {
        struct verify_node * node;

        tr_torinf( tor, _( "Queued for verification" ) );

        node = tr_new( struct verify_node, 1 );
        node->torrent = tor;
        node->verify_done_cb = verify_done_cb;

        tr_lockLock( getVerifyLock( ) );
        tor->verifyState = verifyList ? TR_VERIFY_WAIT : TR_VERIFY_NOW;
        tr_list_append( &verifyList, node );
        if( verifyThread == NULL )
            verifyThread = tr_threadNew( verifyThreadFunc, NULL );
        tr_lockUnlock( getVerifyLock( ) );
    }
}

static int
compareVerifyByTorrent( const void * va,
                        const void * vb )
{
    const struct verify_node * a = va;
    const tr_torrent *         b = vb;

    return a->torrent - b;
}

int
tr_verifyInProgress( const tr_torrent * tor )
{
    int found = FALSE;
    tr_lock * lock = getVerifyLock( );
    tr_lockLock( lock );

    assert( tr_isTorrent( tor ) );

    found = ( tor == currentNode.torrent )
         || ( tr_list_find( verifyList, tor, compareVerifyByTorrent ) != NULL );

    tr_lockUnlock( lock );
    return found;
}

void
tr_verifyRemove( tr_torrent * tor )
{
    tr_lock * lock = getVerifyLock( );
    tr_lockLock( lock );

    assert( tr_isTorrent( tor ) );

    if( tor == currentNode.torrent )
    {
        stopCurrent = TRUE;
        while( stopCurrent )
        {
            tr_lockUnlock( lock );
            tr_wait( 100 );
            tr_lockLock( lock );
        }
    }
    else
    {
        tr_free( tr_list_remove( &verifyList, tor, compareVerifyByTorrent ) );
        tor->verifyState = TR_VERIFY_NONE;
    }

    tr_lockUnlock( lock );
}

