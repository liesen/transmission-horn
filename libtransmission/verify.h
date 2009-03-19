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

#ifndef __TRANSMISSION__
#error only libtransmission should #include this header.
#endif

#ifndef TR_VERIFY_H
#define TR_VERIFY_H 1

typedef void ( *tr_verify_done_cb )( tr_torrent * tor );

void tr_verifyAdd( tr_torrent *      tor,
                   tr_verify_done_cb recheck_done_cb );

void tr_verifyRemove( tr_torrent * tor );

int tr_verifyInProgress( const tr_torrent * tor );

#endif
