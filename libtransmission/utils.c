/*
 * This file Copyright (C) 2009 Charles Kerr <charles@transmissionbt.com>
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
#include <ctype.h> /* isalpha, tolower */
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h> /* strerror, memset */

#include <libgen.h> /* basename */
#include <sys/time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h> /* usleep, stat, getcwd */

#include "event.h"

#ifdef WIN32
 #include <direct.h> /* _getcwd */
 #include <windows.h> /* Sleep */
#endif

#include "transmission.h"
#include "ConvertUTF.h"
#include "list.h"
#include "utils.h"
#include "platform.h"
#include "version.h"

static tr_lock *      messageLock = NULL;
static int            messageLevel = 0;
static tr_bool        messageQueuing = FALSE;
static tr_msg_list *  messageQueue = NULL;
static tr_msg_list ** messageQueueTail = &messageQueue;

#ifndef WIN32
    /* make null versions of these win32 functions */
    static int IsDebuggerPresent( void ) { return FALSE; }
    static void OutputDebugString( const void * unused UNUSED ) { }
#endif

static void
tr_msgInit( void )
{
    static tr_bool initialized = FALSE;

    if( !initialized )
    {
        char * env = getenv( "TR_DEBUG" );
        messageLevel = ( env ? atoi( env ) : 0 ) + 1;
        messageLevel = MAX( 1, messageLevel );

        messageLock = tr_lockNew( );

        initialized = TRUE;
    }
}

FILE*
tr_getLog( void )
{
    static int    initialized = FALSE;
    static FILE * file = NULL;

    if( !initialized )
    {
        const char * str = getenv( "TR_DEBUG_FD" );
        int          fd = 0;
        if( str && *str )
            fd = atoi( str );
        switch( fd )
        {
            case 1:
                file = stdout; break;

            case 2:
                file = stderr; break;

            default:
                file = NULL; break;
        }
        initialized = TRUE;
    }

    return file;
}

void
tr_setMessageLevel( int level )
{
    tr_msgInit( );
    tr_lockLock( messageLock );

    messageLevel = MAX( 0, level );

    tr_lockUnlock( messageLock );
}

int
tr_getMessageLevel( void )
{
    int ret;
    tr_msgInit( );
    tr_lockLock( messageLock );

    ret = messageLevel;

    tr_lockUnlock( messageLock );
    return ret;
}

void
tr_setMessageQueuing( tr_bool enabled )
{
    tr_msgInit( );
    tr_lockLock( messageLock );

    messageQueuing = enabled;

    tr_lockUnlock( messageLock );
}

tr_bool
tr_getMessageQueuing( void )
{
    int ret;
    tr_msgInit( );
    tr_lockLock( messageLock );

    ret = messageQueuing;

    tr_lockUnlock( messageLock );
    return ret;
}

tr_msg_list *
tr_getQueuedMessages( void )
{
    tr_msg_list * ret;
    tr_msgInit( );
    tr_lockLock( messageLock );

    ret = messageQueue;
    messageQueue = NULL;
    messageQueueTail = &messageQueue;

    tr_lockUnlock( messageLock );
    return ret;
}

void
tr_freeMessageList( tr_msg_list * list )
{
    tr_msg_list * next;

    while( NULL != list )
    {
        next = list->next;
        free( list->message );
        free( list->name );
        free( list );
        list = next;
    }
}

/**
***
**/

static struct tm *
tr_localtime_r( time_t *_clock, struct tm *_result )
{
#ifdef HAVE_LOCALTIME_R
    return localtime_r( _clock, _result );
#else
    struct tm *p = localtime( _clock );
    if( p )
        *(_result) = *p;
    return p;
#endif
}

char*
tr_getLogTimeStr( char * buf, int buflen )
{
    char           tmp[64];
    time_t         now;
    struct tm      now_tm;
    struct timeval tv;
    int            milliseconds;

    now = time( NULL );
    gettimeofday( &tv, NULL );

    tr_localtime_r( &now, &now_tm );
    strftime( tmp, sizeof( tmp ), "%H:%M:%S", &now_tm );
    milliseconds = (int)( tv.tv_usec / 1000 );
    tr_snprintf( buf, buflen, "%s.%03d", tmp, milliseconds );

    return buf;
}

void
tr_assertImpl( const char * file, int line, const char * test, const char * fmt, ... )
{
    char buf[64];
    fprintf( stderr, "[%s] Transmission %s Assertion \"%s\" failed at %s:%d.  ",
                     tr_getLogTimeStr( buf, sizeof( buf ) ),
                      LONG_VERSION_STRING, test, file, line );
    if( fmt && *fmt ) {
        va_list args;
        fputc( '(', stderr );
        va_start( args, fmt );
        vfprintf( stderr, fmt, args );
        va_end( args );
        fputs( ")  ", stderr );
    }
    fputs( "Please report this bug at <http://trac.transmissionbt.com/newticket>; Thank you.\n", stderr );
    abort( );
}


tr_bool
tr_deepLoggingIsActive( void )
{
    static int8_t deepLoggingIsActive = -1;

    if( deepLoggingIsActive < 0 )
        deepLoggingIsActive = IsDebuggerPresent() || (tr_getLog()!=NULL);

    return deepLoggingIsActive != 0;
}

void
tr_deepLog( const char  * file,
            int           line,
            const char  * name,
            const char  * fmt,
            ... )
{
    FILE * fp = tr_getLog( );
    if( fp || IsDebuggerPresent( ) )
    {
        va_list           args;
        char              timestr[64];
        struct evbuffer * buf = tr_getBuffer( );
        char *            base = tr_basename( file );

        evbuffer_add_printf( buf, "[%s] ",
                            tr_getLogTimeStr( timestr, sizeof( timestr ) ) );
        if( name )
            evbuffer_add_printf( buf, "%s ", name );
        va_start( args, fmt );
        evbuffer_add_vprintf( buf, fmt, args );
        va_end( args );
        evbuffer_add_printf( buf, " (%s:%d)\n", base, line );
        OutputDebugString( EVBUFFER_DATA( buf ) );
        if(fp)
            (void) fwrite( EVBUFFER_DATA( buf ), 1, EVBUFFER_LENGTH( buf ), fp );

        tr_free( base );
        tr_releaseBuffer( buf );
    }
}

/***
****
***/
    

int
tr_msgLoggingIsActive( int level )
{
    tr_msgInit( );

    return messageLevel >= level;
}

void
tr_msg( const char * file,
        int          line,
        int          level,
        const char * name,
        const char * fmt,
        ... )
{
    FILE * fp;
    tr_msgInit( );
    tr_lockLock( messageLock );

    fp = tr_getLog( );

    if( messageLevel >= level )
    {
        char buf[MAX_STACK_ARRAY_SIZE];
        va_list ap;

        /* build the text message */
        *buf = '\0';
        va_start( ap, fmt );
        evutil_vsnprintf( buf, sizeof( buf ), fmt, ap );
        va_end( ap );

        OutputDebugString( buf );

        if( *buf )
        {
            if( messageQueuing )
            {
                tr_msg_list * newmsg;
                newmsg = tr_new0( tr_msg_list, 1 );
                newmsg->level = level;
                newmsg->when = time( NULL );
                newmsg->message = tr_strdup( buf );
                newmsg->file = file;
                newmsg->line = line;
                newmsg->name = tr_strdup( name );

                *messageQueueTail = newmsg;
                messageQueueTail = &newmsg->next;
            }
            else
            {
                char timestr[64];

                if( fp == NULL )
                    fp = stderr;

                tr_getLogTimeStr( timestr, sizeof( timestr ) );

                if( name )
                    fprintf( fp, "[%s] %s: %s\n", timestr, name, buf );
                else
                    fprintf( fp, "[%s] %s\n", timestr, buf );
                fflush( fp );
            }
        }
    }

    tr_lockUnlock( messageLock );
}

/***
****
***/

void
tr_set_compare( const void * va,
                size_t aCount,
                const void * vb,
                size_t bCount,
                int compare( const void * a, const void * b ),
                size_t elementSize,
                tr_set_func in_a_cb,
                tr_set_func in_b_cb,
                tr_set_func in_both_cb,
                void * userData )
{
    const uint8_t * a = (const uint8_t *) va;
    const uint8_t * b = (const uint8_t *) vb;
    const uint8_t * aend = a + elementSize * aCount;
    const uint8_t * bend = b + elementSize * bCount;

    while( a != aend || b != bend )
    {
        if( a == aend )
        {
            ( *in_b_cb )( (void*)b, userData );
            b += elementSize;
        }
        else if( b == bend )
        {
            ( *in_a_cb )( (void*)a, userData );
            a += elementSize;
        }
        else
        {
            const int val = ( *compare )( a, b );

            if( !val )
            {
                ( *in_both_cb )( (void*)a, userData );
                a += elementSize;
                b += elementSize;
            }
            else if( val < 0 )
            {
                ( *in_a_cb )( (void*)a, userData );
                a += elementSize;
            }
            else if( val > 0 )
            {
                ( *in_b_cb )( (void*)b, userData );
                b += elementSize;
            }
        }
    }
}

/***
****
***/

#ifdef DISABLE_GETTEXT

const char*
tr_strip_positional_args( const char* str )
{
    static size_t bufsize = 0;
    static char * buf = NULL;
    const size_t  len = strlen( str );
    char *        out;

    if( bufsize < len )
    {
        bufsize = len * 2;
        buf = tr_renew( char, buf, bufsize );
    }

    for( out = buf; *str; ++str )
    {
        *out++ = *str;
        if( ( *str == '%' ) && isdigit( str[1] ) )
        {
            const char * tmp = str + 1;
            while( isdigit( *tmp ) )
                ++tmp;

            if( *tmp == '$' )
                str = tmp;
        }
    }
    *out = '\0';

    return buf;
}

#endif

/**
***
**/

void
tr_timevalMsec( uint64_t milliseconds, struct timeval * setme )
{
    const uint64_t microseconds = milliseconds * 1000;
    assert( setme != NULL );
    setme->tv_sec  = microseconds / 1000000;
    setme->tv_usec = microseconds % 1000000;
}

uint8_t *
tr_loadFile( const char * path,
             size_t *     size )
{
    uint8_t *    buf;
    struct stat  sb;
    FILE *       file;
    const char * err_fmt = _( "Couldn't read \"%1$s\": %2$s" );

    /* try to stat the file */
    errno = 0;
    if( stat( path, &sb ) )
    {
        const int err = errno;
        tr_dbg( err_fmt, path, tr_strerror( errno ) );
        errno = err;
        return NULL;
    }

    if( ( sb.st_mode & S_IFMT ) != S_IFREG )
    {
        tr_err( err_fmt, path, _( "Not a regular file" ) );
        errno = EISDIR;
        return NULL;
    }

    /* Load the torrent file into our buffer */
    file = fopen( path, "rb" );
    if( !file )
    {
        const int err = errno;
        tr_err( err_fmt, path, tr_strerror( errno ) );
        errno = err;
        return NULL;
    }
    buf = malloc( sb.st_size );
    if( !buf )
    {
        const int err = errno;
        tr_err( err_fmt, path, _( "Memory allocation failed" ) );
        fclose( file );
        errno = err;
        return NULL;
    }
    if( fread( buf, sb.st_size, 1, file ) != 1 )
    {
        const int err = errno;
        tr_err( err_fmt, path, tr_strerror( errno ) );
        fclose( file );
        free( buf );
        errno = err;
        return NULL;
    }

    fclose( file );
    *size = sb.st_size;
    return buf;
}

char*
tr_basename( const char * path )
{
    char * tmp = tr_strdup( path );
    char * ret = tr_strdup( basename( tmp ) );
    tr_free( tmp );
    return ret;
}

char*
tr_dirname( const char * path )
{
    char * tmp = tr_strdup( path );
    char * ret = tr_strdup( dirname( tmp ) );
    tr_free( tmp );
    return ret;
}

int
tr_mkdir( const char * path,
          int permissions
#ifdef WIN32
                       UNUSED
#endif
        )
{
#ifdef WIN32
    if( path && isalpha( path[0] ) && path[1] == ':' && !path[2] )
        return 0;
    return mkdir( path );
#else
    return mkdir( path, permissions );
#endif
}

int
tr_mkdirp( const char * path_in,
           int          permissions )
{
    char *      path = tr_strdup( path_in );
    char *      p, * pp;
    struct stat sb;
    int         done;

    /* walk past the root */
    p = path;
    while( *p == TR_PATH_DELIMITER )
        ++p;

    pp = p;
    done = 0;
    while( ( p =
                strchr( pp, TR_PATH_DELIMITER ) ) || ( p = strchr( pp, '\0' ) ) )
    {
        if( !*p )
            done = 1;
        else
            *p = '\0';

        if( stat( path, &sb ) )
        {
            /* Folder doesn't exist yet */
            if( tr_mkdir( path, permissions ) )
            {
                const int err = errno;
                tr_err( _(
                           "Couldn't create \"%1$s\": %2$s" ), path,
                       tr_strerror( err ) );
                tr_free( path );
                errno = err;
                return -1;
            }
        }
        else if( ( sb.st_mode & S_IFMT ) != S_IFDIR )
        {
            /* Node exists but isn't a folder */
            char * buf = tr_strdup_printf( _( "File \"%s\" is in the way" ), path );
            tr_err( _( "Couldn't create \"%1$s\": %2$s" ), path_in, buf );
            tr_free( buf );
            tr_free( path );
            errno = ENOTDIR;
            return -1;
        }

        if( done )
            break;

        *p = TR_PATH_DELIMITER;
        p++;
        pp = p;
    }

    tr_free( path );
    return 0;
}

char*
tr_buildPath( const char *first_element, ... )
{
    size_t bufLen = 0;
    const char * element;
    char * buf;
    char * pch;
    va_list vl;

    /* pass 1: allocate enough space for the string */
    va_start( vl, first_element );
    element = first_element;
    while( element ) {
        bufLen += strlen( element ) + 1;
        element = (const char*) va_arg( vl, const char* );
    }
    pch = buf = tr_new( char, bufLen );
    va_end( vl );

    /* pass 2: build the string piece by piece */
    va_start( vl, first_element );
    element = first_element;
    while( element ) {
        const size_t elementLen = strlen( element );
        memcpy( pch, element, elementLen );
        pch += elementLen;
        *pch++ = TR_PATH_DELIMITER;
        element = (const char*) va_arg( vl, const char* );
    }
    va_end( vl );

    /* terminate the string.  if nonempty, eat the unwanted trailing slash */
    if( pch != buf )
        --pch;
    *pch++ = '\0';

    /* sanity checks & return */
    assert( pch - buf == (off_t)bufLen );
    return buf;
}

/****
*****
****/

char*
tr_strndup( const void * in, int len )
{
    char * out = NULL;

    if( len < 0 )
    {
        out = tr_strdup( in );
    }
    else if( in )
    {
        out = tr_malloc( len + 1 );
        memcpy( out, in, len );
        out[len] = '\0';
    }

    return out;
}

char*
tr_strdup_printf( const char * fmt, ... )
{
    char *            ret = NULL;
    struct evbuffer * buf;
    va_list           ap;

    buf = tr_getBuffer( );
    va_start( ap, fmt );

    if( evbuffer_add_vprintf( buf, fmt, ap ) != -1 )
        ret = tr_strdup( EVBUFFER_DATA( buf ) );

    va_end( ap );
    tr_releaseBuffer( buf );
    return ret;
}

const char*
tr_strerror( int i )
{
    const char * ret = strerror( i );

    if( ret == NULL )
        ret = "Unknown Error";
    return ret;
}

/****
*****
****/

char*
tr_strstrip( char * str )
{
    if( str != NULL )
    {
        size_t pos;
        size_t len = strlen( str );

        while( len && isspace( str[len - 1] ) )
            --len;

        str[len] = '\0';

        for( pos = 0; pos < len && isspace( str[pos] ); )
            ++pos;

        len -= pos;
        memmove( str, str + pos, len );
        str[len] = '\0';
    }

    return str;
}

/****
*****
****/

tr_bitfield*
tr_bitfieldConstruct( tr_bitfield * b, size_t bitCount )
{
    b->bitCount = bitCount;
    b->byteCount = ( bitCount + 7u ) / 8u;
    b->bits = tr_new0( uint8_t, b->byteCount );
    return b;
}

tr_bitfield*
tr_bitfieldDestruct( tr_bitfield * b )
{
    if( b )
        tr_free( b->bits );
    return b;
}

tr_bitfield*
tr_bitfieldDup( const tr_bitfield * in )
{
    tr_bitfield * ret = tr_new0( tr_bitfield, 1 );

    ret->bitCount = in->bitCount;
    ret->byteCount = in->byteCount;
    ret->bits = tr_memdup( in->bits, in->byteCount );
    return ret;
}

void
tr_bitfieldClear( tr_bitfield * bitfield )
{
    memset( bitfield->bits, 0, bitfield->byteCount );
}

int
tr_bitfieldIsEmpty( const tr_bitfield * bitfield )
{
    size_t i;

    for( i = 0; i < bitfield->byteCount; ++i )
        if( bitfield->bits[i] )
            return 0;

    return 1;
}

int
tr_bitfieldAdd( tr_bitfield * bitfield,
                size_t        nth )
{
    assert( bitfield );
    assert( bitfield->bits );

    if( nth >= bitfield->bitCount )
        return -1;

    bitfield->bits[nth >> 3u] |= ( 0x80 >> ( nth & 7u ) );
    return 0;
}

/* Sets bit range [begin, end) to 1 */
int
tr_bitfieldAddRange( tr_bitfield * b,
                     size_t        begin,
                     size_t        end )
{
    size_t        sb, eb;
    unsigned char sm, em;

    end--;

    if( ( end >= b->bitCount ) || ( begin > end ) )
        return -1;

    sb = begin >> 3;
    sm = ~( 0xff << ( 8 - ( begin & 7 ) ) );
    eb = end >> 3;
    em = 0xff << ( 7 - ( end & 7 ) );

    if( sb == eb )
    {
        b->bits[sb] |= ( sm & em );
    }
    else
    {
        b->bits[sb] |= sm;
        b->bits[eb] |= em;
        if( ++sb < eb )
            memset ( b->bits + sb, 0xff, eb - sb );
    }

    return 0;
}

int
tr_bitfieldRem( tr_bitfield * bitfield,
                size_t        nth )
{
    assert( bitfield );
    assert( bitfield->bits );

    if( nth >= bitfield->bitCount )
        return -1;

    bitfield->bits[nth >> 3u] &= ( 0xff7f >> ( nth & 7u ) );
    return 0;
}

/* Clears bit range [begin, end) to 0 */
int
tr_bitfieldRemRange( tr_bitfield * b,
                     size_t        begin,
                     size_t        end )
{
    size_t        sb, eb;
    unsigned char sm, em;

    end--;

    if( ( end >= b->bitCount ) || ( begin > end ) )
        return -1;

    sb = begin >> 3;
    sm = 0xff << ( 8 - ( begin & 7 ) );
    eb = end >> 3;
    em = ~( 0xff << ( 7 - ( end & 7 ) ) );

    if( sb == eb )
    {
        b->bits[sb] &= ( sm | em );
    }
    else
    {
        b->bits[sb] &= sm;
        b->bits[eb] &= em;
        if( ++sb < eb )
            memset ( b->bits + sb, 0, eb - sb );
    }

    return 0;
}

tr_bitfield*
tr_bitfieldOr( tr_bitfield *       a,
               const tr_bitfield * b )
{
    uint8_t *      ait;
    const uint8_t *aend, *bit;

    assert( a->bitCount == b->bitCount );

    for( ait = a->bits, bit = b->bits, aend = ait + a->byteCount;
         ait != aend; )
        *ait++ |= *bit++;

    return a;
}

/* set 'a' to all the flags that were in 'a' but not 'b' */
void
tr_bitfieldDifference( tr_bitfield *       a,
                       const tr_bitfield * b )
{
    uint8_t *      ait;
    const uint8_t *aend, *bit;

    assert( a->bitCount == b->bitCount );

    for( ait = a->bits, bit = b->bits, aend = ait + a->byteCount;
         ait != aend; )
        *ait++ &= ~( *bit++ );
}

size_t
tr_bitfieldCountTrueBits( const tr_bitfield* b )
{
    size_t           ret = 0;
    const uint8_t *  it, *end;
    static const int trueBitCount[512] = {
        0, 1, 1, 2, 1, 2, 2, 3, 1, 2, 2, 3, 2, 3, 3, 4, 1, 2, 2, 3, 2, 3, 3,
        4, 2, 3, 3, 4, 3, 4, 4, 5,
        1, 2, 2, 3, 2, 3, 3, 4, 2, 3, 3, 4, 3, 4, 4, 5, 2, 3, 3, 4, 3, 4, 4,
        5, 3, 4, 4, 5, 4, 5, 5, 6,
        1, 2, 2, 3, 2, 3, 3, 4, 2, 3, 3, 4, 3, 4, 4, 5, 2, 3, 3, 4, 3, 4, 4,
        5, 3, 4, 4, 5, 4, 5, 5, 6,
        2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6, 3, 4, 4, 5, 4, 5, 5,
        6, 4, 5, 5, 6, 5, 6, 6, 7,
        1, 2, 2, 3, 2, 3, 3, 4, 2, 3, 3, 4, 3, 4, 4, 5, 2, 3, 3, 4, 3, 4, 4,
        5, 3, 4, 4, 5, 4, 5, 5, 6,
        2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6, 3, 4, 4, 5, 4, 5, 5,
        6, 4, 5, 5, 6, 5, 6, 6, 7,
        2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6, 3, 4, 4, 5, 4, 5, 5,
        6, 4, 5, 5, 6, 5, 6, 6, 7,
        3, 4, 4, 5, 4, 5, 5, 6, 4, 5, 5, 6, 5, 6, 6, 7, 4, 5, 5, 6, 5, 6, 6,
        7, 5, 6, 6, 7, 6, 7, 7, 8,
        1, 2, 2, 3, 2, 3, 3, 4, 2, 3, 3, 4, 3, 4, 4, 5, 2, 3, 3, 4, 3, 4, 4,
        5, 3, 4, 4, 5, 4, 5, 5, 6,
        2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6, 3, 4, 4, 5, 4, 5, 5,
        6, 4, 5, 5, 6, 5, 6, 6, 7,
        2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6, 3, 4, 4, 5, 4, 5, 5,
        6, 4, 5, 5, 6, 5, 6, 6, 7,
        3, 4, 4, 5, 4, 5, 5, 6, 4, 5, 5, 6, 5, 6, 6, 7, 4, 5, 5, 6, 5, 6, 6,
        7, 5, 6, 6, 7, 6, 7, 7, 8,
        2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6, 3, 4, 4, 5, 4, 5, 5,
        6, 4, 5, 5, 6, 5, 6, 6, 7,
        3, 4, 4, 5, 4, 5, 5, 6, 4, 5, 5, 6, 5, 6, 6, 7, 4, 5, 5, 6, 5, 6, 6,
        7, 5, 6, 6, 7, 6, 7, 7, 8,
        3, 4, 4, 5, 4, 5, 5, 6, 4, 5, 5, 6, 5, 6, 6, 7, 4, 5, 5, 6, 5, 6, 6,
        7, 5, 6, 6, 7, 6, 7, 7, 8,
        4, 5, 5, 6, 5, 6, 6, 7, 5, 6, 6, 7, 6, 7, 7, 8, 5, 6, 6, 7, 6, 7, 7,
        8, 6, 7, 7, 8, 7, 8, 8, 9
    };

    if( !b )
        return 0;

    for( it = b->bits, end = it + b->byteCount; it != end; ++it )
        ret += trueBitCount[*it];

    return ret;
}

/***
****
***/

uint64_t
tr_date( void )
{
    struct timeval tv;

    gettimeofday( &tv, NULL );
    return (uint64_t) tv.tv_sec * 1000 + ( tv.tv_usec / 1000 );
}

void
tr_wait( uint64_t delay_milliseconds )
{
#ifdef WIN32
    Sleep( (DWORD)delay_milliseconds );
#else
    usleep( 1000 * delay_milliseconds );
#endif
}

/***
****
***/

int
tr_snprintf( char *       buf,
             size_t       buflen,
             const char * fmt,
             ... )
{
    int     len;
    va_list args;

    va_start( args, fmt );
    len = evutil_vsnprintf( buf, buflen, fmt, args );
    va_end( args );
    return len;
}

/*
 * Copy src to string dst of size siz.  At most siz-1 characters
 * will be copied.  Always NUL terminates (unless siz == 0).
 * Returns strlen(src); if retval >= siz, truncation occurred.
 */
size_t
tr_strlcpy( char *       dst,
            const void * src,
            size_t       siz )
{
#ifdef HAVE_STRLCPY
    return strlcpy( dst, src, siz );
#else
    char *      d = dst;
    const char *s = src;
    size_t      n = siz;

    assert( s );
    assert( d );

    /* Copy as many bytes as will fit */
    if( n != 0 )
    {
        while( --n != 0 )
        {
            if( ( *d++ = *s++ ) == '\0' )
                break;
        }
    }

    /* Not enough room in dst, add NUL and traverse rest of src */
    if( n == 0 )
    {
        if( siz != 0 )
            *d = '\0'; /* NUL-terminate dst */
        while( *s++ )
            ;
    }

    return s - (char*)src - 1;  /* count does not include NUL */
#endif
}

/***
****
***/

double
tr_getRatio( double numerator,
             double denominator )
{
    double ratio;

    if( denominator )
        ratio = numerator / denominator;
    else if( numerator )
        ratio = TR_RATIO_INF;
    else
        ratio = TR_RATIO_NA;

    return ratio;
}

void
tr_sha1_to_hex( char *          out,
                const uint8_t * sha1 )
{
    static const char hex[] = "0123456789abcdef";
    int               i;

    for( i = 0; i < 20; i++ )
    {
        unsigned int val = *sha1++;
        *out++ = hex[val >> 4];
        *out++ = hex[val & 0xf];
    }
    *out = '\0';
}

/***
****
***/

int
tr_httpIsValidURL( const char * url )
{
    const char *        c;
    static const char * rfc2396_valid_chars =
        "abcdefghijklmnopqrstuvwxyz" /* lowalpha */
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ" /* upalpha */
        "0123456789"                 /* digit */
        "-_.!~*'()"                  /* mark */
        ";/?:@&=+$,"                 /* reserved */
        "<>#%<\""                    /* delims */
        "{}|\\^[]`";                 /* unwise */

    if( url == NULL )
        return FALSE;

    for( c = url; c && *c; ++c )
        if( !strchr( rfc2396_valid_chars, *c ) )
            return FALSE;

    return !tr_httpParseURL( url, -1, NULL, NULL, NULL );
}

int
tr_httpParseURL( const char * url_in,
                 int          len,
                 char **      setme_host,
                 int *        setme_port,
                 char **      setme_path )
{
    int          err;
    int          port = 0;
    int          n;
    char *       tmp;
    char *       pch;
    const char * protocol = NULL;
    const char * host = NULL;
    const char * path = NULL;

    tmp = tr_strndup( url_in, len );
    if( ( pch = strstr( tmp, "://" ) ) )
    {
        *pch = '\0';
        protocol = tmp;
        pch += 3;
/*fprintf( stderr, "protocol is [%s]... what's left is [%s]\n", protocol, pch
  );*/
        if( ( n = strcspn( pch, ":/" ) ) )
        {
            const int havePort = pch[n] == ':';
            host = pch;
            pch += n;
            *pch++ = '\0';
/*fprintf( stderr, "host is [%s]... what's left is [%s]\n", host, pch );*/
            if( havePort )
            {
                char * end;
                port = strtol( pch, &end, 10 );
                pch = end;
/*fprintf( stderr, "port is [%d]... what's left is [%s]\n", port, pch );*/
            }
            path = pch;
/*fprintf( stderr, "path is [%s]\n", path );*/
        }
    }

    err = !host || !path || !protocol
          || ( strcmp( protocol, "http" ) && strcmp( protocol, "https" ) );

    if( !err && !port )
    {
        if( !strcmp( protocol, "http" ) ) port = 80;
        if( !strcmp( protocol, "https" ) ) port = 443;
    }

    if( !err )
    {
        if( setme_host ){ ( (char*)host )[-3] = ':'; *setme_host =
                              tr_strdup( protocol ); }
        if( setme_path ){ ( (char*)path )[-1] = '/'; *setme_path =
                              tr_strdup( path - 1 ); }
        if( setme_port ) *setme_port = port;
    }


    tr_free( tmp );
    return err;
}

#include <string.h>
#include <openssl/sha.h>
#include <openssl/hmac.h>
#include <openssl/evp.h>
#include <openssl/bio.h>
#include <openssl/buffer.h>

char *
tr_base64_encode( const void * input,
                  int          length,
                  int *        setme_len )
{
    char *    ret;
    BIO *     b64;
    BIO *     bmem;
    BUF_MEM * bptr;

    if( length < 1 )
        length = strlen( input );

    bmem = BIO_new( BIO_s_mem( ) );
    b64 = BIO_new( BIO_f_base64( ) );
    b64 = BIO_push( b64, bmem );
    BIO_write( b64, input, length );
    (void) BIO_flush( b64 );
    BIO_get_mem_ptr( b64, &bptr );
    ret = tr_strndup( bptr->data, bptr->length );
    if( setme_len )
        *setme_len = bptr->length;

    BIO_free_all( b64 );
    return ret;
}

char *
tr_base64_decode( const void * input,
                  int          length,
                  int *        setme_len )
{
    char * ret;
    BIO *  b64;
    BIO *  bmem;
    int    retlen;

    if( length < 1 )
        length = strlen( input );

    ret = tr_new0( char, length );
    b64 = BIO_new( BIO_f_base64( ) );
    bmem = BIO_new_mem_buf( (unsigned char*)input, length );
    bmem = BIO_push( b64, bmem );
    retlen = BIO_read( bmem, ret, length );
    if( !retlen )
    {
        /* try again, but with the BIO_FLAGS_BASE64_NO_NL flag */
        BIO_free_all( bmem );
        b64 = BIO_new( BIO_f_base64( ) );
        BIO_set_flags( b64, BIO_FLAGS_BASE64_NO_NL );
        bmem = BIO_new_mem_buf( (unsigned char*)input, length );
        bmem = BIO_push( b64, bmem );
        retlen = BIO_read( bmem, ret, length );
    }

    if( setme_len )
        *setme_len = retlen;

    BIO_free_all( bmem );
    return ret;
}

int
tr_ptr2int( void* v )
{
    return (intptr_t)v;
}

void*
tr_int2ptr( int i )
{
    return (void*)(intptr_t)i;
}

/***
****
***/

static tr_list * _bufferList = NULL;

static tr_lock *
getBufferLock( void )
{
    static tr_lock * lock = NULL;
    if( lock == NULL )
        lock = tr_lockNew( );
    return lock;
}

struct evbuffer*
tr_getBuffer( void )
{
    struct evbuffer * buf;
    tr_lock * l = getBufferLock( );
    tr_lockLock( l );

    buf = tr_list_pop_front( &_bufferList );
    if( buf == NULL )
        buf = evbuffer_new( );

    tr_lockUnlock( l );
    return buf;
}

void
tr_releaseBuffer( struct evbuffer * buf )
{
    tr_lock * l = getBufferLock( );
    tr_lockLock( l );

    evbuffer_drain( buf, EVBUFFER_LENGTH( buf ) );
    assert( EVBUFFER_LENGTH( buf ) == 0 );
    tr_list_prepend( &_bufferList, buf );

    tr_lockUnlock( l );
}

/***
****
***/

int
tr_lowerBound( const void * key,
               const void * base,
               size_t       nmemb,
               size_t       size,
               int       (* compar)(const void* key, const void* arrayMember),
               tr_bool    * exact_match )
{
    size_t first = 0;
    const char * cbase = base;

    while( nmemb )
    {
        const size_t half = nmemb / 2;
        const size_t middle = first + half;
        const int c = compar( key, cbase + size*middle );

        if( c < 0 )
        {
            first = middle + 1;
            nmemb = nmemb - half - 1;
        }
        else if( !c )
        {
            if( exact_match )
                *exact_match = TRUE;
            return middle;
        }
        else
        {
            nmemb = half;
        }
    }

    if( exact_match )
        *exact_match = FALSE;

    return first;
}

/***
****
***/

char*
tr_utf8clean( const char * str, int max_len, tr_bool * err )
{
    const char zero = '\0';
    char * ret;
    struct evbuffer * buf = evbuffer_new( );
    const char * end;

    if( err != NULL )
        *err = FALSE;

    if( max_len < 0 )
        max_len = (int) strlen( str );

    while( !tr_utf8_validate ( str, max_len, &end ) )
    {
        const int good_len = end - str;

        evbuffer_add( buf, str, good_len );
        max_len -= ( good_len + 1 );
        str += ( good_len + 1 );
        evbuffer_add( buf, "?", 1 );

        if( err != NULL )
            *err = TRUE;
    }

    evbuffer_add( buf, str, max_len );
    evbuffer_add( buf, &zero, 1 );
    ret = tr_memdup( EVBUFFER_DATA( buf ), EVBUFFER_LENGTH( buf ) );
    assert( tr_utf8_validate( ret, -1, NULL ) );
    evbuffer_free( buf );
    return ret;
}

/***
****
***/

struct number_range
{
    int low;
    int high;
};

/**
 * This should be a single number (ex. "6") or a range (ex. "6-9").
 * Anything else is an error and will return failure.
 */
static tr_bool
parseNumberSection( const char * str, int len, struct number_range * setme )
{
    long a, b;
    tr_bool success;
    char * end;
    const int error = errno;
    char * tmp = tr_strndup( str, len );

    errno = 0;
    a = b = strtol( tmp, &end, 10 );
    if( errno || ( end == tmp ) ) {
        success = FALSE;
    } else if( *end != '-' ) {
        b = a;
        success = TRUE;
    } else {
        const char * pch = end + 1;
        b = strtol( pch, &end, 10 );
        if( errno || ( pch == end ) )
            success = FALSE;
        else if( *end ) /* trailing data */
            success = FALSE;
        else
            success = TRUE;
    }
    tr_free( tmp );

    setme->low = MIN( a, b );
    setme->high = MAX( a, b );

    errno = error;
    return success;
}

static int
compareInt( const void * va, const void * vb )
{
    const int a = *(const int *)va;
    const int b = *(const int *)vb;
    return a - b;
}

/**
 * Given a string like "1-4" or "1-4,6,9,14-51", this allocates and returns an
 * array of setmeCount ints of all the values in the array.
 * For example, "5-8" will return [ 5, 6, 7, 8 ] and setmeCount will be 4.
 * It's the caller's responsibility to call tr_free() on the returned array. 
 * If a fragment of the string can't be parsed, NULL is returned.
 */
int*
tr_parseNumberRange( const char * str_in, int len, int * setmeCount )
{
    int n = 0;
    int * uniq = NULL;
    char * str = tr_strndup( str_in, len );
    const char * walk;
    tr_list * ranges = NULL;
    tr_bool success = TRUE;

    walk = str;
    while( walk && *walk && success ) {
        struct number_range range;
        const char * pch = strchr( walk, ',' );
        if( pch ) {
            success = parseNumberSection( walk, pch-walk, &range );
            walk = pch + 1;
        } else {
            success = parseNumberSection( walk, strlen( walk ), &range );
            walk += strlen( walk );
        }
        if( success )
            tr_list_append( &ranges, tr_memdup( &range, sizeof( struct number_range ) ) );
    }

    if( !success )
    {
        *setmeCount = 0;
        uniq = NULL;
    }
    else
    {
        int i;
        int n2;
        tr_list * l;
        int * sorted = NULL;

        /* build a sorted number array */
        n = n2 = 0;
        for( l=ranges; l!=NULL; l=l->next ) {
            const struct number_range * r = l->data;
            n += r->high + 1 - r->low;
        }
        sorted = tr_new( int, n );
        for( l=ranges; l!=NULL; l=l->next ) {
            const struct number_range * r = l->data;
            int i;
            for( i=r->low; i<=r->high; ++i )
                sorted[n2++] = i;
        }
        qsort( sorted, n, sizeof( int ), compareInt );
        assert( n == n2 );

        /* remove duplicates */
        uniq = tr_new( int, n );
        for( i=n=0; i<n2; ++i )
            if( !n || uniq[n-1] != sorted[i] )
                uniq[n++] = sorted[i];

        tr_free( sorted );
    }

    /* cleanup */
    tr_list_free( &ranges, tr_free );
    tr_free( str );

    /* return the result */
    *setmeCount = n;
    return uniq;
}

/***
****
***/

static void
printf_double_without_rounding( char * buf, int buflen, double d, int places )
{
    char * pch;
    char tmp[128];
    int len;
    tr_snprintf( tmp, sizeof( tmp ), "%'.64f", d );
    pch = tmp;
    while( isdigit( *pch ) ) ++pch; /* walk to the decimal point */
    ++pch; /* walk over the decimal point */
    pch += places;
    len = MIN( buflen - 1, pch - tmp );
    memcpy( buf, tmp, len );
    buf[len] = '\0';
}

char*
tr_strratio( char * buf, size_t buflen, double ratio, const char * infinity )
{
    if( (int)ratio == TR_RATIO_NA )
        tr_strlcpy( buf, _( "None" ), buflen );
    else if( (int)ratio == TR_RATIO_INF )
        tr_strlcpy( buf, infinity, buflen );
    else if( ratio < 10.0 )
        printf_double_without_rounding( buf, buflen, ratio, 2 );
    else if( ratio < 100.0 )
        printf_double_without_rounding( buf, buflen, ratio, 1 );
    else
        tr_snprintf( buf, buflen, "%'.0f", ratio );
    return buf;
}
