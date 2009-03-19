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

#ifndef TR_PLATFORM_H
#define TR_PLATFORM_H

#if defined( WIN32 )
 #define TR_PATH_DELIMITER '\\'
 #define TR_PATH_DELIMITER_STR "\\"
#else
 #define TR_PATH_DELIMITER '/'
 #define TR_PATH_DELIMITER_STR "/"
#endif

#ifdef WIN32
 #include <windows.h>
 #define MAX_PATH_LENGTH  MAX_PATH
#else
 #define MAX_PATH_LENGTH  2048
#endif

#define MAX_STACK_ARRAY_SIZE 7168

typedef struct tr_lock   tr_lock;
typedef struct tr_thread tr_thread;

void                tr_setConfigDir( tr_session * session,
                                     const char * configDir );

const char *        tr_getResumeDir( const tr_session * );

const char *        tr_getTorrentDir( const tr_session * );

const char *        tr_getClutchDir( const tr_session * );


tr_thread*          tr_threadNew( void   ( *func )(void *),
                                  void * arg );

int                 tr_amInThread( const tr_thread * );

tr_lock *           tr_lockNew( void );

void                tr_lockFree( tr_lock * );

void                tr_lockLock( tr_lock * );

void                tr_lockUnlock( tr_lock * );

int                 tr_lockHave( const tr_lock * );

tr_lockfile_state_t tr_lockfile( const char * filename );

#ifdef WIN32
void *              mmap( void *ptr,
                          long  size,
                          long  prot,
                          long  type,
                          long  handle,
                          long  arg );

long                munmap( void *ptr,
                            long  size );

#endif

#endif
