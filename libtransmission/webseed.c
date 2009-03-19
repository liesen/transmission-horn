/*
 * This file Copyright (C) 2008-2009 Charles Kerr <charles@transmissionbt.com>
 *
 * This file is licensed by the GPL version 2.  Works owned by the
 * Transmission project are granted a special exemption to clause 2(b)
 * so that the bulk of its code can remain under the MIT license.
 * This exemption does not extend to derived works not owned by
 * the Transmission project.
 *
 * $Id$
 */

#include <assert.h>
#include <string.h> /* strlen */

#include <event.h>

#include "transmission.h"
#include "inout.h"
#include "list.h"
#include "ratecontrol.h"
#include "torrent.h"
#include "utils.h"
#include "web.h"
#include "webseed.h"

struct tr_webseed
{
    tr_bool             busy;
    tr_bool             dead;

    uint8_t             hash[SHA_DIGEST_LENGTH];

    char              * url;

    tr_delivery_func  * callback;
    void *              callback_userdata;

    tr_piece_index_t    pieceIndex;
    uint32_t            pieceOffset;
    uint32_t            byteCount;

    tr_ratecontrol      rateDown;

    tr_session        * session;

    struct evbuffer   * content;
};

/***
****
***/

static const tr_peer_event blankEvent = { 0, 0, 0, 0, 0.0f, 0, 0, 0 };

static void
publish( tr_webseed *    w,
         tr_peer_event * e )
{
    if( w->callback )
        w->callback( NULL, e, w->callback_userdata );
}

static void
fireNeedReq( tr_webseed * w )
{
    tr_peer_event e = blankEvent;
    e.eventType = TR_PEER_NEED_REQ;
    publish( w, &e );
}

static void
fireClientGotBlock( tr_webseed * w, uint32_t pieceIndex, uint32_t offset, uint32_t length )
{
    tr_peer_event e = blankEvent;
    e.eventType = TR_PEER_CLIENT_GOT_BLOCK;
    e.pieceIndex = pieceIndex;
    e.offset = offset;
    e.length = length;
    publish( w, &e );
}

static void
fireClientGotData( tr_webseed * w, uint32_t length )
{
    tr_peer_event e = blankEvent;
    e.eventType = TR_PEER_CLIENT_GOT_DATA;
    e.length = length;
    e.wasPieceData = TRUE;
    publish( w, &e );
}

/***
****
***/

static char*
makeURL( tr_webseed *    w,
         const tr_file * file )
{
    char *            ret;
    struct evbuffer * out = tr_getBuffer( );
    const char *      url = w->url;
    const size_t      url_len = strlen( url );

    evbuffer_add( out, url, url_len );

    /* if url ends with a '/', add the torrent name */
    if( url[url_len - 1] == '/' )
    {
        const char * str = file->name;

        /* this is like curl_escape() but doesn't munge the
         * '/' directory separators in the path */
        while( str && *str )
        {
            switch( *str )
            {
                case ',': case '-': case '.': case '/':
                case '0': case '1': case '2': case '3': case '4':
                case '5': case '6': case '7': case '8': case '9':
                case 'a': case 'b': case 'c': case 'd': case 'e':
                case 'f': case 'g': case 'h': case 'i': case 'j':
                case 'k': case 'l': case 'm': case 'n': case 'o':
                case 'p': case 'q': case 'r': case 's': case 't':
                case 'u': case 'v': case 'w': case 'x': case 'y': case 'z':
                case 'A': case 'B': case 'C': case 'D': case 'E':
                case 'F': case 'G': case 'H': case 'I': case 'J':
                case 'K': case 'L': case 'M': case 'N': case 'O':
                case 'P': case 'Q': case 'R': case 'S': case 'T':
                case 'U': case 'V': case 'W': case 'X': case 'Y': case 'Z':
                    evbuffer_add( out, str, 1 );
                    break;
                default:
                    evbuffer_add_printf( out, "%%%02X", *str );
                    break;
            }
            str++;
        }
    }

    ret = tr_strndup( EVBUFFER_DATA( out ), EVBUFFER_LENGTH( out ) );
    tr_releaseBuffer( out );
    return ret;
}

static void requestNextChunk( tr_webseed * w );

static void
webResponseFunc( tr_session    * session,
                 long            response_code,
                 const void    * response,
                 size_t          response_byte_count,
                 void          * vw )
{
    tr_webseed * w = vw;
    tr_torrent * tor = tr_torrentFindFromHash( session, w->hash );
    const int    success = ( response_code == 206 );

/*fprintf( stderr, "server responded with code %ld and %lu bytes\n",
  response_code, (unsigned long)response_byte_count );*/
    if( !success )
    {
        /* FIXME */
    }
    else if( tor != NULL )
    {
        evbuffer_add( w->content, response, response_byte_count );
        if( !w->dead )
        {
            fireClientGotData( w, response_byte_count );
            tr_rcTransferred( &w->rateDown, response_byte_count );
        }

        if( EVBUFFER_LENGTH( w->content ) < w->byteCount )
            requestNextChunk( w );
        else {
            tr_ioWrite( tor, w->pieceIndex, w->pieceOffset, w->byteCount, EVBUFFER_DATA(w->content) );
            evbuffer_drain( w->content, EVBUFFER_LENGTH( w->content ) );
            w->busy = 0;
            if( w->dead )
                tr_webseedFree( w );
            else  {
                fireClientGotBlock( w, w->pieceIndex, w->pieceOffset, w->byteCount );
                fireNeedReq( w );
            }
        }
    }
}

static void
requestNextChunk( tr_webseed * w )
{
    tr_torrent * tor = tr_torrentFindFromHash( w->session, w->hash );

    if( tor != NULL )
    {
        const tr_info * inf = tr_torrentInfo( tor );
        const uint32_t have = EVBUFFER_LENGTH( w->content );
        const uint32_t left = w->byteCount - have;
        const uint32_t pieceOffset = w->pieceOffset + have;
        tr_file_index_t fileIndex;
        uint64_t fileOffset;
        uint32_t thisPass;
        char * url;
        char * range;

        tr_ioFindFileLocation( tor, w->pieceIndex, pieceOffset,
                               &fileIndex, &fileOffset );
        thisPass = MIN( left, inf->files[fileIndex].length - fileOffset );

        url = makeURL( w, &inf->files[fileIndex] );
/*fprintf( stderr, "url is [%s]\n", url );*/
        range = tr_strdup_printf( "%"PRIu64"-%"PRIu64, fileOffset, fileOffset + thisPass - 1 );
/*fprintf( stderr, "range is [%s] ... we want %lu total, we have %lu, so %lu are left, and we're asking for %lu this time\n", range, (unsigned long)w->byteCount, (unsigned long)have, (unsigned long)left, (unsigned long)thisPass );*/
        tr_webRun( w->session, url, range, webResponseFunc, w );
        tr_free( range );
        tr_free( url );
    }
}

tr_addreq_t
tr_webseedAddRequest( tr_webseed  * w,
                      uint32_t      pieceIndex,
                      uint32_t      pieceOffset,
                      uint32_t      byteCount )
{
    int ret;

    if( w->busy || w->dead )
    {
        ret = TR_ADDREQ_FULL;
    }
    else
    {
        w->busy = 1;
        w->pieceIndex = pieceIndex;
        w->pieceOffset = pieceOffset;
        w->byteCount = byteCount;
        evbuffer_drain( w->content, EVBUFFER_LENGTH( w->content ) );
        requestNextChunk( w );
        ret = TR_ADDREQ_OK;
    }

    return ret;
}

int
tr_webseedIsActive( const tr_webseed * w )
{
    return w->busy != 0;
}

int
tr_webseedGetSpeed( const tr_webseed * w, uint64_t now, float * setme_KiBs )
{
    const int isActive = tr_webseedIsActive( w );

    *setme_KiBs = isActive ? tr_rcRate( &w->rateDown, now ) : 0.0f;
    return isActive;
}

/***
****
***/

tr_webseed*
tr_webseedNew( struct tr_torrent * torrent,
               const char *        url,
               tr_delivery_func    callback,
               void *              callback_userdata )
{
    tr_webseed * w = tr_new0( tr_webseed, 1 );

    memcpy( w->hash, torrent->info.hash, SHA_DIGEST_LENGTH );
    w->session = torrent->session;
    w->content = evbuffer_new( );
    w->url = tr_strdup( url );
    w->callback = callback;
    w->callback_userdata = callback_userdata;
    tr_rcConstruct( &w->rateDown );
/*fprintf( stderr, "w->callback_userdata is %p\n", w->callback_userdata );*/
    return w;
}

void
tr_webseedFree( tr_webseed * w )
{
    if( w )
    {
        if( w->busy )
        {
            w->dead = 1;
        }
        else
        {
            evbuffer_free( w->content );
            tr_rcDestruct( &w->rateDown );
            tr_free( w->url );
            tr_free( w );
        }
    }
}
