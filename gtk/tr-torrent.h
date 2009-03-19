/******************************************************************************
 * $Id$
 *
 * Copyright (c) 2006-2008 Transmission authors and contributors
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

#ifndef TR_TORRENT_H
#define TR_TORRENT_H

#include <glib-object.h>
#include <libtransmission/transmission.h>
#include <libtransmission/bencode.h>
#include "util.h"

#define TR_TORRENT_TYPE       ( tr_torrent_get_type ( ) )
#define TR_TORRENT( obj ) \
    ( G_TYPE_CHECK_INSTANCE_CAST ( ( obj ), TR_TORRENT_TYPE, TrTorrent ) )
#define TR_TORRENT_CLASS( klass ) \
    ( G_TYPE_CHECK_CLASS_CAST ( ( klass ), TR_TORRENT_TYPE, TrTorrentClass ) )
#define TR_IS_TORRENT( obj ) \
    ( G_TYPE_CHECK_INSTANCE_TYPE ( ( obj ), TR_TORRENT_TYPE ) )
#define TR_IS_TORRENT_CLASS( klass ) \
    ( G_TYPE_CHECK_CLASS_TYPE ( ( klass ), TR_TORRENT_TYPE ) )
#define TR_TORRENT_GET_CLASS( obj ) \
    ( G_TYPE_INSTANCE_GET_CLASS ( ( obj ), TR_TORRENT_TYPE, TrTorrentClass ) )

typedef struct _TrTorrent
{
    GObject    parent;
    struct TrTorrentPrivate  * priv;
}
TrTorrent;

typedef struct TrTorrentClass
{
    GObjectClass    parent;
}
TrTorrentClass;

GType          tr_torrent_get_type( void );

void           tr_torrent_clear( TrTorrent * tor );

tr_torrent *   tr_torrent_handle( TrTorrent *tor );

const tr_stat *tr_torrent_stat( TrTorrent *tor );

const tr_info *tr_torrent_info( TrTorrent *tor );

char*          tr_torrent_status_str( TrTorrent * tor );

void           tr_torrent_delete_files( TrTorrent * tor );

void           tr_torrent_open_folder( TrTorrent * tor );

TrTorrent *    tr_torrent_new_preexisting( tr_torrent * tor );

TrTorrent *    tr_torrent_new_ctor( tr_session  * session,
                                    tr_ctor     * ctor,
                                    int         * errcode );

void           tr_torrent_set_remove_flag( TrTorrent *,
                                           gboolean );

#endif
