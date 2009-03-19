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

#ifndef TR_ENCRYPTION_H
#define TR_ENCRYPTION_H

#ifndef __TRANSMISSION__
#error only libtransmission should #include this header.
#endif

#include <inttypes.h>

#include "utils.h" /* TR_GNUC_NULL_TERMINATED */

/**
***
**/

struct evbuffer;
typedef struct tr_crypto tr_crypto;

/**
***
**/

tr_crypto*     tr_cryptoNew( const uint8_t * torrentHash,
                             int             isIncoming );

void           tr_cryptoFree( tr_crypto * crypto );

/**
***
**/

void           tr_cryptoSetTorrentHash( tr_crypto *     crypto,
                                        const uint8_t * torrentHash );

const uint8_t* tr_cryptoGetTorrentHash( const tr_crypto * crypto );

int            tr_cryptoHasTorrentHash( const tr_crypto * crypto );

/**
***
**/

const uint8_t* tr_cryptoComputeSecret( tr_crypto *     crypto,
                                       const uint8_t * peerPublicKey );

const uint8_t* tr_cryptoGetMyPublicKey( const tr_crypto * crypto,
                                        int *             setme_len );

void           tr_cryptoDecryptInit( tr_crypto * crypto );

void           tr_cryptoDecrypt( tr_crypto *  crypto,
                                 size_t       buflen,
                                 const void * buf_in,
                                 void *       buf_out );

/**
***
**/

void           tr_cryptoEncryptInit( tr_crypto * crypto );

void           tr_cryptoEncrypt( tr_crypto *  crypto,
                                 size_t       buflen,
                                 const void * buf_in,
                                 void *       buf_out );

void           tr_sha1( uint8_t *    setme,
                        const void * content1,
                        int          content1_len,
                        ... ) TR_GNUC_NULL_TERMINATED;


/** Returns a random number in the range of [0...n) */
int            tr_cryptoRandInt( int n );

/** Returns a vaguely random number in the range of [0...n).
    This is faster -- BUT WEAKER -- than tr_cryptoRandInt()
    and should only be used when tr_cryptoRandInt() is too
    slow, and NEVER in sensitive cases */
int            tr_cryptoWeakRandInt( int n );

/** Fills a buffer with random bytes */
void           tr_cryptoRandBuf( unsigned char *buf,
                                 size_t         len );

char*          tr_crypt( const void * plaintext );


#endif
