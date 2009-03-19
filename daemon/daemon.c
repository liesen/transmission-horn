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
#include <errno.h>
#include <stdio.h> /* printf */
#include <stdlib.h> /* exit, atoi */
#include <string.h> /* strcmp */

#include <sys/types.h> /* umask*/
#include <sys/stat.h> /* umask*/

#include <fcntl.h> /* open */
#include <signal.h>
#include <unistd.h> /* daemon */

#include <event.h>

#include <libtransmission/transmission.h>
#include <libtransmission/bencode.h>
#include <libtransmission/tr-getopt.h>
#include <libtransmission/utils.h>
#include <libtransmission/version.h>

#include "watch.h"

#define MY_NAME "transmission-daemon"

#define PREF_KEY_DIR_WATCH          "watch-dir"
#define PREF_KEY_DIR_WATCH_ENABLED  "watch-dir-enabled"


static tr_bool closing = FALSE;
static tr_session * mySession = NULL;

/***
****  Config File
***/

static const char *
getUsage( void )
{
    return "Transmission " LONG_VERSION_STRING
           "  http://www.transmissionbt.com/\n"
           "A fast and easy BitTorrent client\n"
           "\n"
           MY_NAME " is a headless Transmission session\n"
           "that can be controlled via transmission-remote or Clutch.\n"
           "\n"
           "Usage: " MY_NAME " [options]";
}

static const struct tr_option options[] =
{
    { 'a', "allowed", "Allowed IP addresses.  (Default: " TR_DEFAULT_RPC_WHITELIST ")", "a", 1, "<list>" },
    { 'b', "blocklist", "Enable peer blocklists", "b", 0, NULL },
    { 'B', "no-blocklist", "Disable peer blocklists", "B", 0, NULL },
    { 'c', "watch-dir", "Directory to watch for new .torrent files", "c", 1, "<directory>" },
    { 'C', "no-watch-dir", "Disable the watch-dir", "C", 0, NULL },
    { 'd', "dump-settings", "Dump the settings and exit", "d", 0, NULL },
    { 'f', "foreground", "Run in the foreground instead of daemonizing", "f", 0, NULL },
    { 'g', "config-dir", "Where to look for configuration files", "g", 1, "<path>" },
    { 'p', "port", "RPC port (Default: " TR_DEFAULT_RPC_PORT_STR ")", "p", 1, "<port>" },
    { 't', "auth", "Require authentication", "t", 0, NULL },
    { 'T', "no-auth", "Don't require authentication", "T", 0, NULL },
    { 'u', "username", "Set username for authentication", "u", 1, "<username>" },
    { 'v', "password", "Set password for authentication", "v", 1, "<password>" },
    { 'V', "version", "Show version number and exit", "V", 0, NULL },
    { 'w', "download-dir", "Where to save downloaded data", "w", 1, "<path>" },
    { 'P', "peerport", "Port for incoming peers (Default: " TR_DEFAULT_PEER_PORT_STR ")", "P", 1, "<port>" },
    { 'm', "portmap", "Enable portmapping via NAT-PMP or UPnP", "m", 0, NULL },
    { 'M', "no-portmap", "Disable portmapping", "M", 0, NULL },
    { 'L', "peerlimit-global", "Maximum overall number of peers (Default: " TR_DEFAULT_PEER_LIMIT_GLOBAL_STR ")", "L", 1, "<limit>" },
    { 'l', "peerlimit-torrent", "Maximum number of peers per torrent (Default: " TR_DEFAULT_PEER_LIMIT_TORRENT_STR ")", "l", 1, "<limit>" },
    { 910, "encryption-required",  "Encrypt all peer connections", "er", 0, NULL },
    { 911, "encryption-preferred", "Prefer encrypted peer connections", "ep", 0, NULL },
    { 912, "encryption-tolerated", "Prefer unencrypted peer connections", "et", 0, NULL },
    { 0, NULL, NULL, NULL, 0, NULL }
};

static void
showUsage( void )
{
    tr_getopt_usage( MY_NAME, getUsage( ), options );
    exit( 0 );
}

static void
gotsig( int sig UNUSED )
{
    closing = TRUE;
}

#if defined(WIN32)
 #define USE_NO_DAEMON
#elif !defined(HAVE_DAEMON) || defined(__UCLIBC__)
 #define USE_TR_DAEMON
#else
 #define USE_OS_DAEMON
#endif

static int
tr_daemon( int nochdir, int noclose )
{
#if defined(USE_OS_DAEMON)
    return daemon( nochdir, noclose );
#elif defined(USE_TR_DAEMON)
    pid_t pid = fork( );
    if( pid < 0 )
        return -1;
    else if( pid > 0 )
        _exit( 0 );
    else {
        pid = setsid( );
        if( pid < 0 )
            return -1;

        pid = fork( );
        if( pid < 0 )
            return -1;
        else if( pid > 0 )
            _exit( 0 );
        else {

            if( !nochdir )
                if( chdir( "/" ) < 0 )
                    return -1;

            umask( (mode_t)0 );

            if( !noclose ) {
                /* send stdin, stdout, and stderr to /dev/null */
                int i;
                int fd = open( "/dev/null", O_RDWR, 0 );
                for( i=0; i<3; ++i ) {
                    if( close( i ) )
                        return -1;
                    dup2( fd, i );
                }
                close( fd );
            }

            return 0;
        }
    }
#else /* USE_NO_DAEMON */
    return 0;
#endif
}

static const char*
getConfigDir( int argc, const char ** argv )
{
    int c;
    const char * configDir = NULL;
    const char * optarg;
    const int ind = tr_optind;

    while(( c = tr_getopt( getUsage( ), argc, argv, options, &optarg ))) {
        if( c == 'g' ) {
            configDir = optarg;
            break;
        }
    }

    tr_optind = ind;

    if( configDir == NULL )
        configDir = tr_getDefaultConfigDir( MY_NAME );

    return configDir;
}

static void
onFileAdded( tr_session * session, const char * dir, const char * file )
{
    if( strstr( file, ".torrent" ) != NULL )
    {
        char * filename = tr_buildPath( dir, file, NULL );
        tr_ctor * ctor = tr_ctorNew( session );

        int err = tr_ctorSetMetainfoFromFile( ctor, filename );
        if( !err )
            tr_torrentNew( session, ctor, &err );

        tr_ctorFree( ctor );
        tr_free( filename );
    }
}

int
main( int argc, char ** argv )
{
    int c;
    int64_t i;
    const char * optarg;
    tr_benc settings;
    tr_bool foreground = FALSE;
    tr_bool dumpSettings = FALSE;
    const char * configDir = NULL;
    dtr_watchdir * watchdir = NULL;

    signal( SIGINT, gotsig );
    signal( SIGTERM, gotsig );
#ifndef WIN32 
    signal( SIGQUIT, gotsig );
    signal( SIGPIPE, SIG_IGN );
    signal( SIGHUP, SIG_IGN );
#endif

    /* load settings from defaults + config file */
    tr_bencInitDict( &settings, 0 );
    configDir = getConfigDir( argc, (const char**)argv );
    tr_sessionLoadSettings( &settings, configDir, MY_NAME );
    tr_bencDictAddInt( &settings, TR_PREFS_KEY_RPC_ENABLED, 1 );

    /* overwrite settings from the comamndline */
    tr_optind = 1;
    while(( c = tr_getopt( getUsage(), argc, (const char**)argv, options, &optarg ))) {
        switch( c ) {
            case 'a': tr_bencDictAddStr( &settings, TR_PREFS_KEY_RPC_WHITELIST, optarg );
                      tr_bencDictAddInt( &settings, TR_PREFS_KEY_RPC_WHITELIST_ENABLED, 1 );
                      break;
            case 'b': tr_bencDictAddInt( &settings, TR_PREFS_KEY_BLOCKLIST_ENABLED, 1 );
                      break;
            case 'B': tr_bencDictAddInt( &settings, TR_PREFS_KEY_BLOCKLIST_ENABLED, 0 );
                      break;
            case 'c': tr_bencDictAddStr( &settings, PREF_KEY_DIR_WATCH, optarg );
                      tr_bencDictAddInt( &settings, PREF_KEY_DIR_WATCH_ENABLED, 1 );
                      break;
            case 'C': tr_bencDictAddInt( &settings, PREF_KEY_DIR_WATCH_ENABLED, 0 );
                      break;
            case 'd': dumpSettings = TRUE;
                      break;
            case 'f': foreground = TRUE;
                      break;
            case 'g': /* handled above */
                      break;
	    case 'V': /* version */
		      fprintf(stderr, "Transmission %s\n", LONG_VERSION_STRING);
		      exit( 0 );
            case 'p': tr_bencDictAddInt( &settings, TR_PREFS_KEY_RPC_PORT, atoi( optarg ) );
                      break;
            case 't': tr_bencDictAddInt( &settings, TR_PREFS_KEY_RPC_AUTH_REQUIRED, 1 );
                      break;
            case 'T': tr_bencDictAddInt( &settings, TR_PREFS_KEY_RPC_AUTH_REQUIRED, 0 );
                      break;
            case 'u': tr_bencDictAddStr( &settings, TR_PREFS_KEY_RPC_USERNAME, optarg );
                      break;
            case 'v': tr_bencDictAddStr( &settings, TR_PREFS_KEY_RPC_PASSWORD, optarg );
                      break;
            case 'w': tr_bencDictAddStr( &settings, TR_PREFS_KEY_DOWNLOAD_DIR, optarg );
                      break;
            case 'P': tr_bencDictAddInt( &settings, TR_PREFS_KEY_PEER_PORT, atoi( optarg ) );
                      break;
            case 'm': tr_bencDictAddInt( &settings, TR_PREFS_KEY_PORT_FORWARDING, 1 );
                      break;
            case 'M': tr_bencDictAddInt( &settings, TR_PREFS_KEY_PORT_FORWARDING, 0 );
                      break;
            case 'L': tr_bencDictAddInt( &settings, TR_PREFS_KEY_PEER_LIMIT_GLOBAL, atoi( optarg ) );
                      break;
            case 'l': tr_bencDictAddInt( &settings, TR_PREFS_KEY_PEER_LIMIT_TORRENT, atoi( optarg ) );
                      break;
            case 910: tr_bencDictAddInt( &settings, TR_PREFS_KEY_ENCRYPTION, TR_ENCRYPTION_REQUIRED );
                      break;
            case 911: tr_bencDictAddInt( &settings, TR_PREFS_KEY_ENCRYPTION, TR_ENCRYPTION_PREFERRED );
                      break;
            case 912: tr_bencDictAddInt( &settings, TR_PREFS_KEY_ENCRYPTION, TR_CLEAR_PREFERRED );
                      break;
            default:  showUsage( );
                      break;
        }
    }

    if( dumpSettings )
    {
        struct evbuffer * buf = tr_getBuffer( );

        tr_bencSaveAsJSON( &settings, buf );
        fprintf( stderr, "%s", (char*)EVBUFFER_DATA(buf) );

        tr_releaseBuffer( buf );
        return 0;
    }

    if( !foreground && tr_daemon( TRUE, FALSE ) < 0 )
    {
        fprintf( stderr, "failed to daemonize: %s\n", strerror( errno ) );
        exit( 1 );
    }

    /* start the session */
    mySession = tr_sessionInit( "daemon", configDir, FALSE, &settings );

    if( tr_bencDictFindInt( &settings, TR_PREFS_KEY_RPC_AUTH_REQUIRED, &i ) && i!=0 )
        tr_ninf( MY_NAME, "requiring authentication" );

    /* maybe add a watchdir */
    {
        int64_t doWatch;
        const char * dir;

        if( tr_bencDictFindInt( &settings, PREF_KEY_DIR_WATCH_ENABLED, &doWatch )
            && doWatch
            && tr_bencDictFindStr( &settings, PREF_KEY_DIR_WATCH, &dir )
            && dir
            && *dir )
        {
            tr_inf( "Watching \"%s\" for new .torrent files", dir );
            watchdir = dtr_watchdir_new( mySession, dir, onFileAdded );
        }
    }

    /* load the torrents */
    {
        tr_ctor * ctor = tr_ctorNew( mySession );
        tr_torrent ** torrents = tr_sessionLoadTorrents( mySession, ctor, NULL );
        tr_free( torrents );
        tr_ctorFree( ctor );
    }

    while( !closing )
    {
        tr_wait( 1000 ); /* sleep one second */
        dtr_watchdir_update( watchdir );
    }

    /* shutdown */
    printf( "Closing transmission session..." );
    tr_sessionSaveSettings( mySession, configDir, &settings );
    dtr_watchdir_free( watchdir );
    tr_sessionClose( mySession );
    printf( " done.\n" );

    /* cleanup */
    tr_bencFree( &settings );
    return 0;
}
