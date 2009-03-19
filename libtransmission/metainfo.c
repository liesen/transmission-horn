/******************************************************************************
 * $Id$
 *
 * Copyright (c) 2005-2008 Transmission authors and contributors
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *****************************************************************************/

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h> /* unlink, stat */

#include <event.h> /* struct evbuffer */

#include "transmission.h"
#include "session.h"
#include "bencode.h"
#include "crypto.h" /* tr_sha1 */
#include "metainfo.h"
#include "platform.h"
#include "utils.h"

/***
****
***/

static char*
getTorrentFilename( const tr_session * session,
                    const tr_info *   inf )
{
    return tr_strdup_printf( "%s%c%s.%16.16s.torrent",
                             tr_getTorrentDir( session ),
                             TR_PATH_DELIMITER,
                             inf->name,
                             inf->hashString );
}

static char*
getOldTorrentFilename( const tr_session * session,
                       const tr_info *   inf )
{
    char *            ret;
    struct evbuffer * buf = tr_getBuffer( );

    evbuffer_add_printf( buf, "%s%c%s", tr_getTorrentDir( session ),
                         TR_PATH_DELIMITER,
                         inf->hashString );
    if( session->tag )
        evbuffer_add_printf( buf, "-%s", session->tag );

    ret = tr_strndup( EVBUFFER_DATA( buf ), EVBUFFER_LENGTH( buf ) );
    tr_releaseBuffer( buf );
    return ret;
}

void
tr_metainfoMigrate( tr_session * session,
                    tr_info *   inf )
{
    struct stat new_sb;
    char *      name = getTorrentFilename( session, inf );

    if( stat( name, &new_sb ) || ( ( new_sb.st_mode & S_IFMT ) != S_IFREG ) )
    {
        char *    old_name = getOldTorrentFilename( session, inf );
        size_t    contentLen;
        uint8_t * content;

        tr_mkdirp( tr_getTorrentDir( session ), 0777 );
        if( ( content = tr_loadFile( old_name, &contentLen ) ) )
        {
            FILE * out;
            errno = 0;
            out = fopen( name, "wb+" );
            if( !out )
            {
                tr_nerr( inf->name, _( "Couldn't create \"%1$s\": %2$s" ),
                        name, tr_strerror( errno ) );
            }
            else
            {
                if( fwrite( content, sizeof( uint8_t ), contentLen, out )
                    == contentLen )
                {
                    tr_free( inf->torrent );
                    inf->torrent = tr_strdup( name );
                    tr_sessionSetTorrentFile( session, inf->hashString, name );
                    unlink( old_name );
                }
                fclose( out );
            }
        }

        tr_free( content );
        tr_free( old_name );
    }

    tr_free( name );
}

/***
****
***/

static int
getfile( char        ** setme,
         const char   * root,
         tr_benc      * path )
{
    int err;

    if( !tr_bencIsList( path ) )
    {
        err = TR_EINVALID;
    }
    else
    {
        struct evbuffer * buf = tr_getBuffer( );
        int               n = tr_bencListSize( path );
        int               i;

        evbuffer_add( buf, root, strlen( root ) );
        for( i = 0; i < n; ++i )
        {
            const char * str;
            if( tr_bencGetStr( tr_bencListChild( path, i ), &str )
              && strcmp( str, ".." ) )
            {
                evbuffer_add( buf, TR_PATH_DELIMITER_STR, 1 );
                evbuffer_add( buf, str, strlen( str ) );
            }
        }

        *setme = tr_utf8clean( (char*)EVBUFFER_DATA( buf ), EVBUFFER_LENGTH( buf ), NULL );
        /* fprintf( stderr, "[%s]\n", *setme ); */
        tr_releaseBuffer( buf );
        err = 0;
    }

    return err;
}

static const char*
parseFiles( tr_info *       inf,
            tr_benc *       files,
            const tr_benc * length )
{
    int64_t len;

    inf->totalSize = 0;

    if( tr_bencIsList( files ) ) /* multi-file mode */
    {
        tr_file_index_t i;

        inf->isMultifile = 1;
        inf->fileCount   = tr_bencListSize( files );
        inf->files       = tr_new0( tr_file, inf->fileCount );

        for( i = 0; i < inf->fileCount; ++i )
        {
            tr_benc * file;
            tr_benc * path;

            file = tr_bencListChild( files, i );
            if( !tr_bencIsDict( file ) )
                return "files";

            if( !tr_bencDictFindList( file, "path.utf-8", &path ) )
                if( !tr_bencDictFindList( file, "path", &path ) )
                    return "path";

            if( getfile( &inf->files[i].name, inf->name, path ) )
                return "path";

            if( !tr_bencDictFindInt( file, "length", &len ) )
                return "length";

            inf->files[i].length = len;
            inf->totalSize      += len;
        }
    }
    else if( tr_bencGetInt( length, &len ) ) /* single-file mode */
    {
        inf->isMultifile      = 0;
        inf->fileCount        = 1;
        inf->files            = tr_new0( tr_file, 1 );
        inf->files[0].name    = tr_strdup( inf->name );
        inf->files[0].length  = len;
        inf->totalSize       += len;
    }
    else
    {
        return "length";
    }

    return NULL;
}

static char *
announceToScrape( const char * announce )
{
    char *       scrape = NULL;
    const char * s;

    /* To derive the scrape URL use the following steps:
     * Begin with the announce URL. Find the last '/' in it.
     * If the text immediately following that '/' isn't 'announce'
     * it will be taken as a sign that that tracker doesn't support
     * the scrape convention. If it does, substitute 'scrape' for
     * 'announce' to find the scrape page.  */
    if( ( ( s =
               strrchr( announce, '/' ) ) ) && !strncmp( ++s, "announce", 8 ) )
    {
        struct evbuffer * buf = tr_getBuffer( );
        evbuffer_add( buf, announce, s - announce );
        evbuffer_add( buf, "scrape", 6 );
        evbuffer_add_printf( buf, "%s", s + 8 );
        scrape = tr_strdup( EVBUFFER_DATA( buf ) );
        tr_releaseBuffer( buf );
    }

    return scrape;
}

static const char*
getannounce( tr_info * inf,
             tr_benc * meta )
{
    const char *      str;
    tr_tracker_info * trackers = NULL;
    int               trackerCount = 0;
    tr_benc *         tiers;

    /* Announce-list */
    if( tr_bencDictFindList( meta, "announce-list", &tiers ) )
    {
        int       n;
        int       i, j, validTiers;
        const int numTiers = tr_bencListSize( tiers );

        n = 0;
        for( i = 0; i < numTiers; ++i )
            n += tr_bencListSize( tr_bencListChild( tiers, i ) );

        trackers = tr_new0( tr_tracker_info, n );
        trackerCount = 0;

        for( i = 0, validTiers = 0; i < numTiers; ++i )
        {
            tr_benc * tier = tr_bencListChild( tiers, i );
            const int tierSize = tr_bencListSize( tier );
            tr_bool anyAdded = FALSE;
            for( j = 0; j < tierSize; ++j )
            {
                if( tr_bencGetStr( tr_bencListChild( tier, j ), &str ) )
                {
                    char * url = tr_strstrip( tr_strdup( str ) );
                    if( tr_httpIsValidURL( url ) )
                    {
                        tr_tracker_info * t = trackers + trackerCount++;
                        t->tier = validTiers;
                        t->announce = tr_strdup( url );
                        t->scrape = announceToScrape( url );
                        
                        anyAdded = TRUE;
                    }
                    tr_free( url );
                }
            }
            
            if( anyAdded )
                ++validTiers;
        }

        /* did we use any of the tiers? */
        if( !trackerCount )
        {
            tr_free( trackers );
            trackers = NULL;
        }
    }

    /* Regular announce value */
    if( !trackerCount
      && tr_bencDictFindStr( meta, "announce", &str ) )
    {
        char * url = tr_strstrip( tr_strdup( str ) );
        if( tr_httpIsValidURL( url ) )
        {
            trackers = tr_new0( tr_tracker_info, 1 );
            trackers[trackerCount].tier = 0;
            trackers[trackerCount].announce = tr_strdup( url );
            trackers[trackerCount++].scrape = announceToScrape( url );
            /*fprintf( stderr, "single announce: [%s]\n", url );*/
        }
        tr_free( url );
    }

    inf->trackers = trackers;
    inf->trackerCount = trackerCount;

    return inf->trackerCount ? NULL : "announce";
}

static void
geturllist( tr_info * inf,
            tr_benc * meta )
{
    tr_benc * urls;

    if( tr_bencDictFindList( meta, "url-list", &urls ) )
    {
        int          i;
        const char * url;
        const int    n = tr_bencListSize( urls );

        inf->webseedCount = 0;
        inf->webseeds = tr_new0( char*, n );

        for( i = 0; i < n; ++i )
            if( tr_bencGetStr( tr_bencListChild( urls, i ), &url ) )
                inf->webseeds[inf->webseedCount++] = tr_strdup( url );
    }
}

static const char*
tr_metainfoParseImpl( const tr_session * session,
                      tr_info *         inf,
                      const tr_benc *   meta_in )
{
    int64_t         i;
    size_t          raw_len;
    const char *    str;
    const uint8_t * raw;
    tr_benc *       beInfo;
    tr_benc *       meta = (tr_benc *) meta_in;
    tr_bool         err;

    /* info_hash: urlencoded 20-byte SHA1 hash of the value of the info key
     * from the Metainfo file. Note that the value will be a bencoded
     * dictionary, given the definition of the info key above. */
    if( !tr_bencDictFindDict( meta, "info", &beInfo ) )
        return "info";
    else
    {
        int    len;
        char * bstr = tr_bencSave( beInfo, &len );
        tr_sha1( inf->hash, bstr, len, NULL );
        tr_sha1_to_hex( inf->hashString, inf->hash );
        tr_free( bstr );
    }

    /* name */
    if( !tr_bencDictFindStr( beInfo, "name.utf-8", &str ) )
        if( !tr_bencDictFindStr( beInfo, "name", &str ) )
            str = "";
    if( !str || !*str )
        return "name";
    tr_free( inf->name );
    inf->name = tr_utf8clean( str, -1, &err );

    /* comment */
    if( !tr_bencDictFindStr( meta, "comment.utf-8", &str ) )
        if( !tr_bencDictFindStr( meta, "comment", &str ) )
            str = "";
    tr_free( inf->comment );
    inf->comment = tr_utf8clean( str, -1, &err );

    /* created by */
    if( !tr_bencDictFindStr( meta, "created by.utf-8", &str ) )
        if( !tr_bencDictFindStr( meta, "created by", &str ) )
            str = "";
    tr_free( inf->creator );
    inf->creator = tr_utf8clean( str, -1, &err );

    /* creation date */
    if( !tr_bencDictFindInt( meta, "creation date", &i ) )
        i = 0;
    inf->dateCreated = i;

    /* private */
    if( !tr_bencDictFindInt( beInfo, "private", &i ) )
        if( !tr_bencDictFindInt( meta, "private", &i ) )
            i = 0;
    inf->isPrivate = i != 0;

    /* piece length */
    if( !tr_bencDictFindInt( beInfo, "piece length", &i ) || ( i < 1 ) )
        return "piece length";
    inf->pieceSize = i;

    /* pieces */
    if( !tr_bencDictFindRaw( beInfo, "pieces", &raw,
                             &raw_len ) || ( raw_len % SHA_DIGEST_LENGTH ) )
        return "pieces";
    inf->pieceCount = raw_len / SHA_DIGEST_LENGTH;
    inf->pieces = tr_new0( tr_piece, inf->pieceCount );
    for( i = 0; i < inf->pieceCount; ++i )
        memcpy( inf->pieces[i].hash, &raw[i * SHA_DIGEST_LENGTH],
                SHA_DIGEST_LENGTH );

    /* files */
    if( ( str = parseFiles( inf, tr_bencDictFind( beInfo, "files" ),
                                 tr_bencDictFind( beInfo, "length" ) ) ) )
        return str;
    if( !inf->fileCount || !inf->totalSize )
        return "files";
    if( (uint64_t) inf->pieceCount !=
       ( inf->totalSize + inf->pieceSize - 1 ) / inf->pieceSize )
        return "files";

    /* get announce or announce-list */
    if( ( str = getannounce( inf, meta ) ) )
        return str;

    /* get the url-list */
    geturllist( inf, meta );

    /* filename of Transmission's copy */
    tr_free( inf->torrent );
    inf->torrent = getTorrentFilename( session, inf );

    return NULL;
}

int
tr_metainfoParse( const tr_session * session,
                  tr_info *         inf,
                  const tr_benc *   meta_in )
{
    const char * badTag = tr_metainfoParseImpl( session, inf, meta_in );

    if( badTag )
    {
        tr_nerr( inf->name, _( "Invalid metadata entry \"%s\"" ), badTag );
        tr_metainfoFree( inf );
        return TR_EINVALID;
    }
    return 0;
}

void
tr_metainfoFree( tr_info * inf )
{
    tr_file_index_t ff;
    int             i;

    for( i = 0; i < inf->webseedCount; ++i )
        tr_free( inf->webseeds[i] );

    for( ff = 0; ff < inf->fileCount; ++ff )
        tr_free( inf->files[ff].name );

    tr_free( inf->webseeds );
    tr_free( inf->pieces );
    tr_free( inf->files );
    tr_free( inf->comment );
    tr_free( inf->creator );
    tr_free( inf->torrent );
    tr_free( inf->name );

    for( i = 0; i < inf->trackerCount; ++i )
    {
        tr_free( inf->trackers[i].announce );
        tr_free( inf->trackers[i].scrape );
    }
    tr_free( inf->trackers );

    memset( inf, '\0', sizeof( tr_info ) );
}

void
tr_metainfoRemoveSaved( const tr_session * session,
                        const tr_info *   inf )
{
    char * filename;

    filename = getTorrentFilename( session, inf );
    unlink( filename );
    tr_free( filename );

    filename = getOldTorrentFilename( session, inf );
    unlink( filename );
    tr_free( filename );
}

