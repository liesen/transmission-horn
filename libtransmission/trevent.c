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

#include <assert.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include <signal.h>

#include "transmission.h"
#include "session.h"

#ifdef WIN32

#include <WinSock2.h> 
 
static int 
pgpipe( int handles[2] ) 
{
        SOCKET s;
        struct sockaddr_in serv_addr;
        int len = sizeof( serv_addr );
 
        handles[0] = handles[1] = INVALID_SOCKET;
 
        if ( ( s = socket( AF_INET, SOCK_STREAM, 0 ) ) == INVALID_SOCKET )
        {
/*              ereport(LOG, (errmsg_internal("pgpipe failed to create socket: %ui", WSAGetLastError()))); */
                return -1;
        }
 
        memset( &serv_addr, 0, sizeof( serv_addr ) );
        serv_addr.sin_family = AF_INET;
        serv_addr.sin_port = htons(0);
        serv_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        if (bind(s, (SOCKADDR *) & serv_addr, len) == SOCKET_ERROR)
        {
/*              ereport(LOG, (errmsg_internal("pgpipe failed to bind: %ui", WSAGetLastError()))); */
                closesocket(s);
                return -1;
        }
        if (listen(s, 1) == SOCKET_ERROR)
        {
/*              ereport(LOG, (errmsg_internal("pgpipe failed to listen: %ui", WSAGetLastError()))); */
                closesocket(s);
                return -1;
        }
        if (getsockname(s, (SOCKADDR *) & serv_addr, &len) == SOCKET_ERROR)
        {
/*              ereport(LOG, (errmsg_internal("pgpipe failed to getsockname: %ui", WSAGetLastError()))); */
                closesocket(s);
                return -1;
        }
        if ((handles[1] = socket(PF_INET, SOCK_STREAM, 0)) == INVALID_SOCKET)
        {
/*              ereport(LOG, (errmsg_internal("pgpipe failed to create socket 2: %ui", WSAGetLastError()))); */
                closesocket(s);
                return -1;
        }
 
        if (connect(handles[1], (SOCKADDR *) & serv_addr, len) == SOCKET_ERROR)
        {
/*              ereport(LOG, (errmsg_internal("pgpipe failed to connect socket: %ui", WSAGetLastError()))); */
                closesocket(s);
                return -1;
        }
        if ((handles[0] = accept(s, (SOCKADDR *) & serv_addr, &len)) == INVALID_SOCKET)
        {
/*              ereport(LOG, (errmsg_internal("pgpipe failed to accept socket: %ui", WSAGetLastError()))); */
                closesocket(handles[1]);
                handles[1] = INVALID_SOCKET;
                closesocket(s);
                return -1;
        }
        closesocket(s);
        return 0;
}
 
static int 
piperead( int s, char *buf, int len ) 
{ 
        int ret = recv(s, buf, len, 0); 
 
        if (ret < 0 && WSAGetLastError() == WSAECONNRESET) 
                /* EOF on the pipe! (win32 socket based implementation) */ 
                ret = 0; 
        return ret; 
} 
 
#define pipe(a) pgpipe(a) 
#define pipewrite(a,b,c) send(a,(char*)b,c,0) 

#else
#define piperead(a,b,c) read(a,b,c) 
#define pipewrite(a,b,c) write(a,b,c) 
#endif

#include <unistd.h> 

#include <event.h>

#include "transmission.h"
#include "platform.h"
#include "trevent.h"
#include "utils.h"

/***
****
***/

typedef struct tr_event_handle
{
    uint8_t      die;
    int          fds[2];
    tr_lock *    lock;
    tr_session *  session;
    tr_thread *  thread;
    struct event_base * base;
    struct event pipeEvent;
}
tr_event_handle;

typedef int timer_func ( void* );

struct tr_timer
{
    tr_bool                   inCallback;
    timer_func *              func;
    void *                    user_data;
    struct tr_event_handle *  eh;
    struct timeval            tv;
    struct event              event;
};

struct tr_run_data
{
    void    ( *func )( void * );
    void *  user_data;
};

#define dbgmsg( ... ) \
    do { \
        if( tr_deepLoggingIsActive( ) ) \
            tr_deepLog( __FILE__, __LINE__, "event", __VA_ARGS__ ); \
    } while( 0 )

static void
readFromPipe( int    fd,
              short  eventType,
              void * veh )
{
    char              ch;
    int               ret;
    tr_event_handle * eh = veh;

    dbgmsg( "readFromPipe: eventType is %hd", eventType );

    /* read the command type */
    ch = '\0';
    do
    {
        ret = piperead( fd, &ch, 1 );
    }
    while( !eh->die && ret < 0 && errno == EAGAIN );

    dbgmsg( "command is [%c], ret is %d, errno is %d", ch, ret, (int)errno );

    switch( ch )
    {
        case 'r': /* run in libevent thread */
        {
            struct tr_run_data data;
            const size_t       nwant = sizeof( data );
            const ssize_t      ngot = piperead( fd, &data, nwant );
            if( !eh->die && ( ngot == (ssize_t)nwant ) )
            {
                dbgmsg( "invoking function in libevent thread" );
                ( data.func )( data.user_data );
            }
            break;
        }

        case '\0': /* eof */
        {
            dbgmsg( "pipe eof reached... removing event listener" );
            event_del( &eh->pipeEvent );
            break;
        }

        default:
        {
            assert( 0 && "unhandled command type!" );
            break;
        }
    }
}

static void
logFunc( int severity, const char * message )
{
    if( severity >= _EVENT_LOG_ERR )
        tr_err( "%s", message );
    else
        tr_dbg( "%s", message );
}

static void
libeventThreadFunc( void * veh )
{
    tr_event_handle * eh = veh;
    tr_dbg( "Starting libevent thread" );

#ifndef WIN32
    /* Don't exit when writing on a broken socket */
    signal( SIGPIPE, SIG_IGN );
#endif

    eh->base = event_init( );
    eh->session->events = eh;

    /* listen to the pipe's read fd */
    event_set( &eh->pipeEvent, eh->fds[0], EV_READ | EV_PERSIST, readFromPipe, veh );
    event_add( &eh->pipeEvent, NULL );
    event_set_log_callback( logFunc );

    /* loop until all the events are done */
    event_dispatch( );

    /* shut down the thread */
    tr_lockFree( eh->lock );
    event_base_free( eh->base );
    eh->session->events = NULL;
    tr_free( eh );
    tr_dbg( "Closing libevent thread" );
}

void
tr_eventInit( tr_session * session )
{
    tr_event_handle * eh;

    session->events = NULL;

    eh = tr_new0( tr_event_handle, 1 );
    eh->lock = tr_lockNew( );
    pipe( eh->fds );
    eh->session = session;
    eh->thread = tr_threadNew( libeventThreadFunc, eh );

    /* wait until the libevent thread is running */
    while( session->events == NULL )
        tr_wait( 100 );
}

void
tr_eventClose( tr_session * session )
{
    assert( tr_isSession( session ) );

    session->events->die = TRUE;
    tr_deepLog( __FILE__, __LINE__, NULL, "closing trevent pipe" );
    EVUTIL_CLOSESOCKET( session->events->fds[1] );
}

/**
***
**/

tr_bool
tr_amInEventThread( tr_session * session )
{
    assert( tr_isSession( session ) );
    assert( session->events != NULL );

    return tr_amInThread( session->events->thread );
}

/**
***
**/

static void
timerCallback( int    fd UNUSED,
               short  event UNUSED,
               void * vtimer )
{
    int               more;
    struct tr_timer * timer = vtimer;

    timer->inCallback = 1;
    more = ( *timer->func )( timer->user_data );
    timer->inCallback = 0;

    if( more )
        evtimer_add( &timer->event, &timer->tv );
    else
        tr_timerFree( &timer );
}

void
tr_timerFree( tr_timer ** ptimer )
{
    tr_timer * timer;

    /* zero out the argument passed in */
    assert( ptimer );
    timer = *ptimer;
    *ptimer = NULL;

    /* destroy the timer directly or via the command queue */
    if( timer && !timer->inCallback )
    {
        assert( tr_amInEventThread( timer->eh->session ) );
        event_del( &timer->event );
        tr_free( timer );
    }
}

tr_timer*
tr_timerNew( tr_session  * session,
             timer_func    func,
             void        * user_data,
             uint64_t      interval_milliseconds )
{
    tr_timer * timer;

    assert( tr_amInEventThread( session ) );

    timer = tr_new0( tr_timer, 1 );
    timer->func = func;
    timer->user_data = user_data;
    timer->eh = session->events;

    tr_timevalMsec( interval_milliseconds, &timer->tv );
    evtimer_set( &timer->event, timerCallback, timer );
    evtimer_add( &timer->event,  &timer->tv );

    return timer;
}

void
tr_runInEventThread( tr_session * session,
                     void func( void* ), void * user_data )
{
    assert( tr_isSession( session ) );
    assert( session->events != NULL );

    if( tr_amInThread( session->events->thread ) )
    {
        (func)( user_data );
    }
    else
    {
        const char         ch = 'r';
        int                fd = session->events->fds[1];
        tr_lock *          lock = session->events->lock;
        struct tr_run_data data;

        tr_lockLock( lock );
        pipewrite( fd, &ch, 1 );
        data.func = func;
        data.user_data = user_data;
        pipewrite( fd, &data, sizeof( data ) );
        tr_lockUnlock( lock );
    }
}

struct event_base *
tr_eventGetBase( tr_session * session )
{
    assert( tr_isSession( session ) );

    return session->events->base;
}
