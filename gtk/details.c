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

#include <errno.h>
#include <math.h> /* ceil() */
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <glib/gi18n.h>
#include <gtk/gtk.h>

#include <libtransmission/transmission.h>
#include <libtransmission/utils.h> /* tr_httpIsValidURL */

#include "actions.h"
#include "details.h"
#include "file-list.h"
#include "tr-torrent.h"
#include "tracker-list.h"
#include "hig.h"
#include "util.h"

#define UPDATE_INTERVAL_SECONDS 2

struct ResponseData
{
    gpointer    gtor;
    TrCore    * core;
    guint       handler;
};

/****
*****  PEERS TAB
****/

enum
{
    WEBSEED_COL_URL,
    WEBSEED_COL_DOWNLOAD_RATE,
    N_WEBSEED_COLS
};

static const char * webseed_column_names[N_WEBSEED_COLS] =
{
    N_( "Web Seeds" ),
    /* 'download speed' column header. terse to keep the column narrow. */
    N_( "Down" )
};

static GtkTreeModel*
webseed_model_new( const tr_torrent * tor )
{
    int             i;
    const tr_info * inf = tr_torrentInfo( tor );
    float *         speeds = tr_torrentWebSpeeds( tor );
    GtkListStore *  store = gtk_list_store_new( N_WEBSEED_COLS,
                                                G_TYPE_STRING,
                                                G_TYPE_FLOAT );

    for( i = 0; i < inf->webseedCount; ++i )
    {
        GtkTreeIter iter;
        gtk_list_store_append( store, &iter );
        gtk_list_store_set( store, &iter,
                            WEBSEED_COL_URL, inf->webseeds[i],
                            WEBSEED_COL_DOWNLOAD_RATE, speeds[i],
                            -1 );
    }

    tr_free( speeds );
    return GTK_TREE_MODEL( store );
}

enum
{
    PEER_COL_ADDRESS,
    PEER_COL_DOWNLOAD_RATE,
    PEER_COL_UPLOAD_RATE,
    PEER_COL_CLIENT,
    PEER_COL_PROGRESS,
    PEER_COL_IS_ENCRYPTED,
    PEER_COL_STATUS,
    N_PEER_COLS
};

static const char* peer_column_names[N_PEER_COLS] =
{
    N_( "Address" ),
    /* 'download speed' column header. terse to keep the column narrow. */
    N_( "Down" ),
    /* 'upload speed' column header.  terse to keep the column narrow. */
    N_( "Up" ),
    N_( "Client" ),
    /* 'percent done' column header. terse to keep the column narrow. */
    N_( "%" ),
    " ",
    N_( "Status" )
};

static int
compare_peers( const void * a, const void * b )
{
    const tr_peer_stat * pa = a;
    const tr_peer_stat * pb = b;

    return strcmp( pa->addr, pb->addr );
}

static int
compare_addr_to_peer( const void * addr, const void * b )
{
    const tr_peer_stat * peer = b;

    return strcmp( addr, peer->addr );
}

static void
peer_row_set( GtkListStore        * store,
              GtkTreeIter         * iter,
              const tr_peer_stat  * peer )
{
    const char * client = peer->client;

    if( !client || !strcmp( client, "Unknown Client" ) )
        client = " ";

    gtk_list_store_set( store, iter,
                        PEER_COL_ADDRESS, peer->addr,
                        PEER_COL_CLIENT, client,
                        PEER_COL_IS_ENCRYPTED, peer->isEncrypted,
                        PEER_COL_PROGRESS, (int)( 100.0 * peer->progress ),
                        PEER_COL_DOWNLOAD_RATE, peer->rateToClient,
                        PEER_COL_UPLOAD_RATE, peer->rateToPeer,
                        PEER_COL_STATUS, peer->flagStr,
                        -1 );
}

static void
append_peers_to_model( GtkListStore *       store,
                       const tr_peer_stat * peers,
                       int                  n_peers )
{
    int i;

    for( i = 0; i < n_peers; ++i )
    {
        GtkTreeIter iter;
        gtk_list_store_append( store, &iter );
        peer_row_set( store, &iter, &peers[i] );
    }
}

static GtkTreeModel*
peer_model_new( tr_torrent * tor )
{
    GtkListStore * m = gtk_list_store_new( N_PEER_COLS,
                                           G_TYPE_STRING, /* addr */
                                           G_TYPE_FLOAT, /* downloadFromRate */
                                           G_TYPE_FLOAT, /* uploadToRate */
                                           G_TYPE_STRING, /* client */
                                           G_TYPE_INT,   /* progress [0..100] */
                                           G_TYPE_BOOLEAN, /* isEncrypted */
                                           G_TYPE_STRING ); /* flagString */

    int            n_peers = 0;
    tr_peer_stat * peers = tr_torrentPeers( tor, &n_peers );

    qsort( peers, n_peers, sizeof( tr_peer_stat ), compare_peers );
    append_peers_to_model( m, peers, n_peers );
    tr_torrentPeersFree( peers, 0 );
    return GTK_TREE_MODEL( m );
}

static void
render_encrypted( GtkTreeViewColumn  * column UNUSED,
                  GtkCellRenderer *           renderer,
                  GtkTreeModel *              tree_model,
                  GtkTreeIter *               iter,
                  gpointer             data   UNUSED )
{
    gboolean is_encrypted = FALSE;

    gtk_tree_model_get( tree_model, iter, PEER_COL_IS_ENCRYPTED, &is_encrypted, -1 );
    g_object_set( renderer, "xalign", (gfloat)0.0,
                            "yalign", (gfloat)0.5,
                            "stock-id", ( is_encrypted ? "transmission-lock" : NULL ),
                            NULL );
}

static void
render_speed( GtkCellRenderer  * renderer,
              GtkTreeModel     * tree_model,
              GtkTreeIter      * iter,
              int                col )
{
    float rate = 0.0;
    char str[64];
    gtk_tree_model_get( tree_model, iter, col, &rate, -1 );
    if( rate < 0.01 )
        *str = '\0';
    else
        tr_strlspeed( str, rate, sizeof( str ) );
    g_object_set( renderer, "text", str, NULL );
}

static void
render_ul_rate( GtkTreeViewColumn  * column UNUSED,
                GtkCellRenderer    * renderer,
                GtkTreeModel       * tree_model,
                GtkTreeIter        * iter,
                gpointer             data   UNUSED )
{
    render_speed( renderer, tree_model, iter, PEER_COL_UPLOAD_RATE );
}

static void
render_dl_rate( GtkTreeViewColumn  * column UNUSED,
                GtkCellRenderer    * renderer,
                GtkTreeModel       * tree_model,
                GtkTreeIter        * iter,
                gpointer             data   UNUSED )
{
    render_speed( renderer, tree_model, iter, PEER_COL_DOWNLOAD_RATE );
}

static void
render_client( GtkTreeViewColumn   * column UNUSED,
               GtkCellRenderer *            renderer,
               GtkTreeModel *               tree_model,
               GtkTreeIter *                iter,
               gpointer              data   UNUSED )
{
    char * client = NULL;

    gtk_tree_model_get( tree_model, iter, PEER_COL_CLIENT, &client, -1 );
    g_object_set( renderer, "text", ( client ? client : "" ), NULL );
    g_free( client );
}

typedef struct
{
    TrTorrent *     gtor;
    GtkTreeModel *  model; /* same object as store, but recast */
    GtkListStore *  store; /* same object as model, but recast */
    GtkListStore *  webseeds;
    GtkWidget *     completeness;
    GtkWidget *     seeders_lb;
    GtkWidget *     leechers_lb;
    GtkWidget *     completed_lb;
    GtkWidget *     peer_tree_view;
}
PeerData;

static void
fmtpeercount( GtkWidget * l,
              int         count )
{
    if( 0 > count )
    {
        gtk_label_set_text( GTK_LABEL( l ), "?" );
    }
    else
    {
        char str[16];
        g_snprintf( str, sizeof str, "%'d", count );
        gtk_label_set_text( GTK_LABEL( l ), str );
    }
}

static void
refresh_peers( GtkWidget * top )
{
    int             i;
    int             n_peers;
    GtkTreeIter     iter;
    PeerData *      p = (PeerData*) g_object_get_data( G_OBJECT( top ), "peer-data" );
    tr_torrent *    tor = tr_torrent_handle( p->gtor );
    GtkTreeModel *  model = p->model;
    GtkListStore *  store = p->store;
    tr_peer_stat *  peers;
    const tr_stat * stat = tr_torrent_stat( p->gtor );
    const tr_info * inf = tr_torrent_info( p->gtor );

    if( inf->webseedCount )
    {
        float * speeds = tr_torrentWebSpeeds( tor );
        for( i = 0; i < inf->webseedCount; ++i )
        {
            GtkTreeIter iter;
            gtk_tree_model_iter_nth_child( GTK_TREE_MODEL( p->webseeds ), &iter, NULL, i );
            gtk_list_store_set( p->webseeds, &iter, WEBSEED_COL_DOWNLOAD_RATE, speeds[i], -1 );
        }
        tr_free( speeds );
    }

    /**
    ***  merge the peer diffs into the tree model.
    ***
    ***  this is more complicated than creating a new model,
    ***  but is also (a) more efficient and (b) doesn't undo
    ***  the view's visible area and sorting on every refresh.
    **/

    n_peers = 0;
    peers = tr_torrentPeers( tor, &n_peers );
    qsort( peers, n_peers, sizeof( tr_peer_stat ), compare_peers );

    if( gtk_tree_model_get_iter_first( model, &iter ) ) do
        {
            char *         addr = NULL;
            tr_peer_stat * peer = NULL;
            gtk_tree_model_get( model, &iter, PEER_COL_ADDRESS, &addr, -1 );
            peer = bsearch( addr, peers, n_peers, sizeof( tr_peer_stat ),
                            compare_addr_to_peer );
            g_free( addr );

            if( peer ) /* update a pre-existing row */
            {
                const int pos = peer - peers;
                const int n_rhs = n_peers - ( pos + 1 );
                g_assert( n_rhs >= 0 );

                peer_row_set( store, &iter, peer );

                /* remove it from the tr_peer_stat list */
                g_memmove( peer, peer + 1, sizeof( tr_peer_stat ) * n_rhs );
                --n_peers;
            }
            else if( !gtk_list_store_remove( store, &iter ) )
                break; /* we removed the model's last item */
        }
        while( gtk_tree_model_iter_next( model, &iter ) );

    append_peers_to_model( store, peers, n_peers ); /* all these are new */

    fmtpeercount( p->seeders_lb, stat->seeders );
    fmtpeercount( p->leechers_lb, stat->leechers );
    fmtpeercount( p->completed_lb, stat->timesCompleted );

    free( peers );

    gtk_widget_queue_draw( p->peer_tree_view );
}

#if GTK_CHECK_VERSION( 2, 12, 0 )
static gboolean
onPeerViewQueryTooltip( GtkWidget *            widget,
                        gint                   x,
                        gint                   y,
                        gboolean               keyboard_tip,
                        GtkTooltip *           tooltip,
                        gpointer     user_data UNUSED )
{
    gboolean       show_tip = FALSE;
    GtkTreeModel * model;
    GtkTreeIter    iter;

    if( gtk_tree_view_get_tooltip_context( GTK_TREE_VIEW( widget ),
                                           &x, &y, keyboard_tip,
                                           &model, NULL, &iter ) )
    {
        const char * pch;
        char *       str = NULL;
        GString *    gstr = g_string_new( NULL );
        gtk_tree_model_get( model, &iter, PEER_COL_STATUS, &str, -1 );
        for( pch = str; pch && *pch; ++pch )
        {
            const char * txt = NULL;
            switch( *pch )
            {
                case 'O': txt = _( "Optimistic unchoke" ); break;
                case 'D': txt = _( "Downloading from this peer" ); break; 
                case 'd': txt = _( "We would download from this peer if they would let us" ); break;
                case 'U': txt = _( "Uploading to peer" ); break; 
                case 'u': txt = _( "We would upload to this peer if they asked" ); break;
                case 'K': txt = _( "Peer has unchoked us, but we're not interested" ); break;
                case '?': txt = _( "We unchoked this peer, but they're not interested" ); break;
                case 'E': txt = _( "Encrypted connection" ); break; 
                case 'X': txt = _( "Peer was discovered through Peer Exchange (PEX)" ); break;
                case 'I': txt = _( "Peer is an incoming connection" ); break;
            }
            if( txt )
                g_string_append_printf( gstr, "%c: %s\n", *pch, txt );
        }
        if( gstr->len ) /* remove the last linefeed */
            g_string_set_size( gstr, gstr->len - 1 );
        gtk_tooltip_set_text( tooltip, gstr->str );
        g_string_free( gstr, TRUE );
        g_free( str );
        show_tip = TRUE;
    }

    return show_tip;
}

#endif

static GtkWidget*
peer_page_new( TrTorrent * gtor )
{
    guint           i;
    GtkTreeModel *  m;
    GtkWidget *     v, *w, *ret, *sw, *l, *vbox, *hbox;
    GtkWidget *     webtree = NULL;
    tr_torrent *    tor = tr_torrent_handle( gtor );
    PeerData *      p = g_new( PeerData, 1 );
    const tr_info * inf = tr_torrent_info( gtor );

    /* TODO: make this configurable? */
    int view_columns[] = { PEER_COL_IS_ENCRYPTED,
                           PEER_COL_UPLOAD_RATE,
                           PEER_COL_DOWNLOAD_RATE,
                           PEER_COL_PROGRESS,
                           PEER_COL_STATUS,
                           PEER_COL_ADDRESS,
                           PEER_COL_CLIENT };


    if( inf->webseedCount )
    {
        GtkTreeViewColumn * c;
        GtkCellRenderer *   r;
        const char *        t;
        GtkWidget *         w;
        GtkWidget *         v;

        m = webseed_model_new( tr_torrent_handle( gtor ) );
        v = gtk_tree_view_new_with_model( m );
        g_signal_connect( v, "button-release-event", G_CALLBACK( on_tree_view_button_released ), NULL );
        gtk_tree_view_set_rules_hint( GTK_TREE_VIEW( v ), TRUE );
        p->webseeds = GTK_LIST_STORE( m );
        g_object_unref( G_OBJECT( m ) );

        t = _( webseed_column_names[WEBSEED_COL_URL] );
        r = gtk_cell_renderer_text_new( );
        g_object_set( G_OBJECT( r ), "ellipsize", PANGO_ELLIPSIZE_END, NULL );
        c = gtk_tree_view_column_new_with_attributes( t, r, "text", WEBSEED_COL_URL, NULL );
        g_object_set( G_OBJECT( c ), "expand", TRUE, NULL );
        gtk_tree_view_column_set_sort_column_id( c, WEBSEED_COL_URL );
        gtk_tree_view_append_column( GTK_TREE_VIEW( v ), c );

        t = _( webseed_column_names[WEBSEED_COL_DOWNLOAD_RATE] );
        r = gtk_cell_renderer_text_new( );
        c = gtk_tree_view_column_new_with_attributes( t, r, "text", WEBSEED_COL_DOWNLOAD_RATE, NULL );
        gtk_tree_view_column_set_cell_data_func( c, r, render_dl_rate, NULL, NULL );
        gtk_tree_view_column_set_sort_column_id( c, WEBSEED_COL_DOWNLOAD_RATE );
        gtk_tree_view_append_column( GTK_TREE_VIEW( v ), c );

        w = gtk_scrolled_window_new( NULL, NULL );
        gtk_scrolled_window_set_policy( GTK_SCROLLED_WINDOW( w ), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC );
        gtk_scrolled_window_set_shadow_type( GTK_SCROLLED_WINDOW( w ), GTK_SHADOW_IN );
        gtk_container_add( GTK_CONTAINER( w ), v );

        webtree = w;
    }

    m  = peer_model_new( tor );
    v = GTK_WIDGET( g_object_new( GTK_TYPE_TREE_VIEW,
                                  "model",  gtk_tree_model_sort_new_with_model( m ),
                                  "rules-hint", TRUE,
#if GTK_CHECK_VERSION( 2, 12, 0 )
                                  "has-tooltip", TRUE,
#endif
                                  NULL ) );
    p->peer_tree_view = v;

#if GTK_CHECK_VERSION( 2, 12, 0 )
    g_signal_connect( v, "query-tooltip",
                      G_CALLBACK( onPeerViewQueryTooltip ), NULL );
#endif
    gtk_widget_set_size_request( v, 550, 0 );
    g_object_unref( G_OBJECT( m ) );
    g_signal_connect( v, "button-release-event",
                      G_CALLBACK( on_tree_view_button_released ), NULL );

    for( i = 0; i < G_N_ELEMENTS( view_columns ); ++i )
    {
        const int           col = view_columns[i];
        const char *        t = _( peer_column_names[col] );
        GtkTreeViewColumn * c;
        GtkCellRenderer *   r;

        switch( col )
        {
            case PEER_COL_ADDRESS:
                r = gtk_cell_renderer_text_new( );
                c = gtk_tree_view_column_new_with_attributes( t, r, "text", col, NULL );
                break;

            case PEER_COL_CLIENT:
                r = gtk_cell_renderer_text_new( );
                c = gtk_tree_view_column_new_with_attributes( t, r, "text", col, NULL );
                gtk_tree_view_column_set_cell_data_func( c, r, render_client, NULL, NULL );
                break;

            case PEER_COL_PROGRESS:
                r = gtk_cell_renderer_progress_new( );
                c = gtk_tree_view_column_new_with_attributes( t, r, "value", PEER_COL_PROGRESS, NULL );
                break;

            case PEER_COL_IS_ENCRYPTED:
                r = gtk_cell_renderer_pixbuf_new( );
                c = gtk_tree_view_column_new_with_attributes( t, r, NULL );
                gtk_tree_view_column_set_sizing( c, GTK_TREE_VIEW_COLUMN_FIXED );
                gtk_tree_view_column_set_fixed_width( c, 20 );
                gtk_tree_view_column_set_cell_data_func( c, r, render_encrypted, NULL, NULL );
                break;

            case PEER_COL_DOWNLOAD_RATE:
                r = gtk_cell_renderer_text_new( );
                c = gtk_tree_view_column_new_with_attributes( t, r, "text", col, NULL );
                gtk_tree_view_column_set_cell_data_func( c, r, render_dl_rate, NULL, NULL );
                break;

            case PEER_COL_UPLOAD_RATE:
                r = gtk_cell_renderer_text_new( );
                c = gtk_tree_view_column_new_with_attributes( t, r, "text", col, NULL );
                gtk_tree_view_column_set_cell_data_func( c, r, render_ul_rate, NULL, NULL );
                break;

            case PEER_COL_STATUS:
                r = gtk_cell_renderer_text_new( );
                c = gtk_tree_view_column_new_with_attributes( t, r, "text", col, NULL );
                break;

            default:
                abort( );
        }

        gtk_tree_view_column_set_resizable( c, FALSE );
        gtk_tree_view_column_set_sort_column_id( c, col );
        gtk_tree_view_append_column( GTK_TREE_VIEW( v ), c );
    }

    /* the 'expander' column has a 10-pixel margin on the left
       that doesn't look quite correct in any of these columns...
       so create a non-visible column and assign it as the
       'expander column. */
    {
        GtkTreeViewColumn *c = gtk_tree_view_column_new( );
        gtk_tree_view_column_set_visible( c, FALSE );
        gtk_tree_view_append_column( GTK_TREE_VIEW( v ), c );
        gtk_tree_view_set_expander_column( GTK_TREE_VIEW( v ), c );
    }

    w = sw = gtk_scrolled_window_new( NULL, NULL );
    gtk_scrolled_window_set_policy( GTK_SCROLLED_WINDOW( w ),
                                    GTK_POLICY_AUTOMATIC,
                                    GTK_POLICY_AUTOMATIC );
    gtk_scrolled_window_set_shadow_type( GTK_SCROLLED_WINDOW( w ),
                                         GTK_SHADOW_IN );
    gtk_container_add( GTK_CONTAINER( w ), v );


    vbox = gtk_vbox_new( FALSE, GUI_PAD );
    gtk_container_set_border_width( GTK_CONTAINER( vbox ), GUI_PAD_BIG );

    if( webtree == NULL )
        gtk_box_pack_start( GTK_BOX( vbox ), sw, TRUE, TRUE, 0 );
    else {
        GtkWidget * vpaned = gtk_vpaned_new( );
        gtk_paned_pack1( GTK_PANED( vpaned ), webtree, FALSE, TRUE );
        gtk_paned_pack2( GTK_PANED( vpaned ), sw, TRUE, TRUE );
        gtk_box_pack_start( GTK_BOX( vbox ), vpaned, TRUE, TRUE, 0 );
    }

    hbox = gtk_hbox_new( FALSE, GUI_PAD );
    l = gtk_label_new( NULL );
    gtk_label_set_markup( GTK_LABEL( l ), _( "<b>Seeders:</b>" ) );
    gtk_box_pack_start( GTK_BOX( hbox ), l, FALSE, FALSE, 0 );
    l = p->seeders_lb = gtk_label_new( NULL );
    gtk_box_pack_start( GTK_BOX( hbox ), l, FALSE, FALSE, 0 );
    gtk_box_pack_start( GTK_BOX( hbox ), gtk_alignment_new( 0.0f, 0.0f, 0.0f, 0.0f ), TRUE, TRUE, 0 );
    l = gtk_label_new( NULL );
    gtk_label_set_markup( GTK_LABEL( l ), _( "<b>Leechers:</b>" ) );
    gtk_box_pack_start( GTK_BOX( hbox ), l, FALSE, FALSE, 0 );
    l = p->leechers_lb = gtk_label_new( NULL );
    gtk_box_pack_start( GTK_BOX( hbox ), l, FALSE, FALSE, 0 );
    gtk_box_pack_start( GTK_BOX( hbox ), gtk_alignment_new( 0.0f, 0.0f, 0.0f, 0.0f ), TRUE, TRUE, 0 );
    l = gtk_label_new( NULL );
    gtk_label_set_markup( GTK_LABEL( l ), _( "<b>Times Completed:</b>" ) );
    gtk_box_pack_start( GTK_BOX( hbox ), l, FALSE, FALSE, 0 );
    l = p->completed_lb = gtk_label_new( NULL );
    gtk_box_pack_start( GTK_BOX( hbox ), l, FALSE, FALSE, 0 );
    gtk_box_pack_start( GTK_BOX( vbox ), hbox, FALSE, FALSE, 0 );

    ret = vbox;
    p->gtor = gtor;
    p->model = m;
    p->store = GTK_LIST_STORE( m );
    g_object_set_data_full( G_OBJECT( ret ), "peer-data", p, g_free );
    return ret;
}

/****
*****  INFO TAB
****/

static void
refresh_time_lb( GtkWidget * l,
                 time_t      t )
{
    const char * never = _( "Never" );

    if( !t )
        gtk_label_set_text( GTK_LABEL( l ), never );
    else
    {
        char * str = gtr_localtime( t );
        gtk_label_set_text( GTK_LABEL( l ), str );
        g_free( str );
    }
}

static GtkWidget*
info_page_new( tr_torrent * tor )
{
    int             row = 0;
    GtkWidget *     t = hig_workarea_create( );
    GtkWidget *     l, *w, *fr;
    char *          pch;
    char            sizeStr[128];
    char            countStr[128];
    char            buf[256];
    GtkTextBuffer * b;
    const tr_info * info = tr_torrentInfo( tor );

    hig_workarea_add_section_title( t, &row, _( "Details" ) );

    g_snprintf( countStr, sizeof( countStr ),
                ngettext( "%'d Piece", "%'d Pieces", info->pieceCount ),
                info->pieceCount );
    tr_strlsize( sizeStr, info->pieceSize, sizeof( sizeStr ) );
    g_snprintf( buf, sizeof( buf ),
                /* %1$s is number of pieces;
                   %2$s is how big each piece is */
                _( "%1$s @ %2$s" ),
                countStr, sizeStr );

    l = gtk_label_new( buf );
    hig_workarea_add_row( t, &row, _( "Pieces:" ), l, NULL );

    l = g_object_new( GTK_TYPE_LABEL, "label", info->hashString,
                      "selectable", TRUE,
                      "ellipsize", PANGO_ELLIPSIZE_END,
                      NULL );
    hig_workarea_add_row( t, &row, _( "Hash:" ), l, NULL );

    pch = ( info->isPrivate )
          ? _( "Private to this tracker -- PEX disabled" )
          : _( "Public torrent" );
    l = gtk_label_new( pch );
    hig_workarea_add_row( t, &row, _( "Privacy:" ), l, NULL );

    b = gtk_text_buffer_new( NULL );
    if( info->comment )
        gtk_text_buffer_set_text( b, info->comment, -1 );
    w = gtk_text_view_new_with_buffer( b );
    gtk_widget_set_size_request( w, 0u, 100u );
    gtk_text_view_set_wrap_mode( GTK_TEXT_VIEW( w ), GTK_WRAP_WORD );
    gtk_text_view_set_editable( GTK_TEXT_VIEW( w ), FALSE );
    fr = gtk_frame_new( NULL );
    gtk_frame_set_shadow_type( GTK_FRAME( fr ), GTK_SHADOW_IN );
    gtk_container_add( GTK_CONTAINER( fr ), w );
    w = hig_workarea_add_row( t, &row, _( "Comment:" ), fr, NULL );
    gtk_misc_set_alignment( GTK_MISC( w ), 0.0f, 0.0f );

    hig_workarea_add_section_divider( t, &row );
    hig_workarea_add_section_title( t, &row, _( "Origins" ) );

    l = gtk_label_new( *info->creator ? info->creator : _( "Unknown" ) );
    gtk_label_set_ellipsize( GTK_LABEL( l ), PANGO_ELLIPSIZE_END );
    hig_workarea_add_row( t, &row, _( "Creator:" ), l, NULL );

    l = gtk_label_new( NULL );
    refresh_time_lb( l, info->dateCreated );
    hig_workarea_add_row( t, &row, _( "Date:" ), l, NULL );

    hig_workarea_add_section_divider( t, &row );
    hig_workarea_add_section_title( t, &row, _( "Location" ) );

    l = g_object_new( GTK_TYPE_LABEL, "label", tr_torrentGetDownloadDir( tor ),
                                      "selectable", TRUE,
                                      "ellipsize", PANGO_ELLIPSIZE_END,
                                      NULL );
    hig_workarea_add_row( t, &row, _( "Destination folder:" ), l, NULL );

    l = g_object_new( GTK_TYPE_LABEL, "label", info->torrent,
                                      "selectable", TRUE,
                                      "ellipsize", PANGO_ELLIPSIZE_END,
                                      NULL );
    hig_workarea_add_row( t, &row, _( "Torrent file:" ), l, NULL );

    hig_workarea_finish( t, &row );
    return t;
}

/****
*****  ACTIVITY TAB
****/

typedef struct
{
    GtkWidget * state_lb;
    GtkWidget * progress_lb;
    GtkWidget * have_lb;
    GtkWidget * dl_lb;
    GtkWidget * ul_lb;
    GtkWidget * failed_lb;
    GtkWidget * ratio_lb;
    GtkWidget * err_lb;
    GtkWidget * swarm_lb;
    GtkWidget * date_added_lb;
    GtkWidget * last_activity_lb;
    GtkWidget * availability_da;
    TrTorrent * gtor;
}
Activity;

static void
refresh_activity( GtkWidget * top )
{
    int i;
    char * pch;
    char buf1[128];
    char buf2[128];
    Activity * a = g_object_get_data( G_OBJECT( top ), "activity-data" );
    const tr_stat * stat = tr_torrent_stat( a->gtor );
    const tr_info * info = tr_torrent_info( a->gtor );
    const double complete = stat->percentComplete * 100.0;
    const double done = stat->percentDone * 100.0;
    const double verifiedPieceCount = (double)stat->haveValid / info->pieceSize;

    pch = tr_torrent_status_str( a->gtor );
    gtk_label_set_text( GTK_LABEL( a->state_lb ), pch );
    g_free( pch );

    if( (int)complete == (int)done )
        pch = g_strdup_printf( _( "%.1f%%" ), complete );
    else
        /* %1$.1f is percent of how much of what we want's been downloaded,
         * %2$.1f is percent of how much of the whole torrent we've downloaded */
        pch = g_strdup_printf( _( "%1$.1f%% (%2$.1f%% selected)" ),
                               complete, done );
    gtk_label_set_text( GTK_LABEL( a->progress_lb ), pch );
    g_free( pch );

    i = (int) ceil( verifiedPieceCount );
    tr_strlsize( buf1,  stat->haveValid + stat->haveUnchecked, sizeof( buf1 ) );
    tr_strlsize( buf2, stat->haveValid, sizeof( buf2 ) );
    /* %1$s is total size of what we've saved to disk
     * %2$s is how much of it's passed the checksum test
     * %3$s is how many pieces are verified */
    if( !i )
        pch = tr_strdup( buf1 );
    else
        pch = g_strdup_printf( ngettext( "%1$s (%2$s verified in %3$d piece)",
                                         "%1$s (%2$s verified in %3$d pieces)", i ),
                               buf1, buf2, i );
    gtk_label_set_text( GTK_LABEL( a->have_lb ), pch );
    g_free( pch );

    tr_strlsize( buf1, stat->downloadedEver, sizeof( buf1 ) );
    gtk_label_set_text( GTK_LABEL( a->dl_lb ), buf1 );

    tr_strlsize( buf1, stat->uploadedEver, sizeof( buf1 ) );
    gtk_label_set_text( GTK_LABEL( a->ul_lb ), buf1 );

    tr_strlsize( buf1, stat->corruptEver, sizeof( buf1 ) );
    gtk_label_set_text( GTK_LABEL( a->failed_lb ), buf1 );

    tr_strlratio( buf1, stat->ratio, sizeof( buf1 ) );
    gtk_label_set_text( GTK_LABEL( a->ratio_lb ), buf1 );

    tr_strlspeed( buf1, stat->swarmSpeed, sizeof( buf1 ) );
    gtk_label_set_text( GTK_LABEL( a->swarm_lb ), buf1 );

    gtk_label_set_text( GTK_LABEL( a->err_lb ),
                        *stat->errorString ? stat->errorString : _( "None" ) );

    refresh_time_lb( a->date_added_lb, stat->addedDate );

    refresh_time_lb( a->last_activity_lb, stat->activityDate );
}

static GtkWidget*
activity_page_new( TrTorrent * gtor )
{
    Activity * a = g_new( Activity, 1 );
    int        row = 0;
    GtkWidget *t = hig_workarea_create( );
    GtkWidget *l;

    a->gtor = gtor;

    hig_workarea_add_section_title( t, &row, _( "Transfer" ) );

    l = a->state_lb = gtk_label_new( NULL );
    hig_workarea_add_row( t, &row, _( "State:" ), l, NULL );

    l = a->progress_lb = gtk_label_new( NULL );
    hig_workarea_add_row( t, &row, _( "Progress:" ), l, NULL );

    l = a->have_lb = gtk_label_new( NULL );
    /* "Have" refers to how much of the torrent we have */
    hig_workarea_add_row( t, &row, _( "Have:" ), l, NULL );

    l = a->dl_lb = gtk_label_new( NULL );
    hig_workarea_add_row( t, &row, _( "Downloaded:" ), l, NULL );

    l = a->ul_lb = gtk_label_new( NULL );
    hig_workarea_add_row( t, &row, _( "Uploaded:" ), l, NULL );

    /* how much downloaded data was corrupt */
    l = a->failed_lb = gtk_label_new( NULL );
    hig_workarea_add_row( t, &row, _( "Failed DL:" ), l, NULL );

    l = a->ratio_lb = gtk_label_new( NULL );
    hig_workarea_add_row( t, &row, _( "Ratio:" ), l, NULL );

    l = a->swarm_lb = gtk_label_new( NULL );
    hig_workarea_add_row( t, &row, _( "Swarm rate:" ), l, NULL );

    l = a->err_lb = gtk_label_new( NULL );
    hig_workarea_add_row( t, &row, _( "Error:" ), l, NULL );

    hig_workarea_add_section_divider( t, &row );
    hig_workarea_add_section_title( t, &row, _( "Dates" ) );

    l = a->date_added_lb = gtk_label_new( NULL );
    hig_workarea_add_row( t, &row, _( "Started at:" ), l, NULL );

    l = a->last_activity_lb = gtk_label_new( NULL );
    hig_workarea_add_row( t, &row, _( "Last activity at:" ), l, NULL );

    hig_workarea_add_section_divider( t, &row );
    hig_workarea_finish( t, &row );
    g_object_set_data_full( G_OBJECT( t ), "activity-data", a, g_free );
    return t;
}

/****
*****  OPTIONS
****/

static void
up_speed_toggled_cb( GtkToggleButton *tb,
                     gpointer         gtor )
{
    tr_torrent * tor = tr_torrent_handle( gtor );
    const gboolean b = gtk_toggle_button_get_active( tb );

    tr_torrentUseSpeedLimit( tor, TR_UP, b );
}

static void
down_speed_toggled_cb( GtkToggleButton *tb,
                       gpointer         gtor )
{
    tr_torrent * tor = tr_torrent_handle( gtor );
    const gboolean b = gtk_toggle_button_get_active( tb );

    tr_torrentUseSpeedLimit( tor, TR_DOWN, b );
}

static void
global_speed_toggled_cb( GtkToggleButton * tb, gpointer gtor )
{
    tr_torrent * tor = tr_torrent_handle( gtor );
    const gboolean b = gtk_toggle_button_get_active( tb );

    tr_torrentUseGlobalSpeedLimit( tor, TR_UP, b );
    tr_torrentUseGlobalSpeedLimit( tor, TR_DOWN, b );
}

#define RATIO_MODE_KEY "ratio-mode"

static void
ratio_mode_changed_cb( GtkToggleButton * tb, gpointer gtor )
{
    if( gtk_toggle_button_get_active( tb ) )
    {
        tr_torrent * tor = tr_torrent_handle( gtor );
        const int mode = GPOINTER_TO_INT( g_object_get_data( G_OBJECT( tb ), RATIO_MODE_KEY ) );
        tr_torrentSetRatioMode( tor, mode );
    }
}

static void
sensitize_from_check_cb( GtkToggleButton * toggle, gpointer w )
{
    gtk_widget_set_sensitive( GTK_WIDGET( w ),
                              gtk_toggle_button_get_active( toggle ) );
}

static void
setSpeedLimit( GtkSpinButton* spin, gpointer gtor, int up_or_down )
{
    tr_torrent * tor = tr_torrent_handle( gtor );
    const int kb_sec = gtk_spin_button_get_value_as_int( spin );

    tr_torrentSetSpeedLimit( tor, up_or_down, kb_sec );
}

static void
up_speed_spun_cb( GtkSpinButton * spin, gpointer gtor )
{
    setSpeedLimit( spin, gtor, TR_UP );
}

static void
down_speed_spun_cb( GtkSpinButton * spin, gpointer gtor )
{
    setSpeedLimit( spin, gtor, TR_DOWN );
}

static void
ratio_spun_cb( GtkSpinButton * spin, gpointer gtor )
{
    tr_torrent * tor = tr_torrent_handle( gtor );
    float        ratio = gtk_spin_button_get_value( spin );

    tr_torrentSetRatioLimit( tor, ratio );
}

static void
max_peers_spun_cb( GtkSpinButton * spin, gpointer gtor )
{
    const uint16_t n = gtk_spin_button_get_value( spin );

    tr_torrentSetPeerLimit( tr_torrent_handle( gtor ), n );
}

static char*
get_global_ratio_radiobutton_string( void )
{
    char * s;
    const gboolean b = pref_flag_get( TR_PREFS_KEY_RATIO_ENABLED );
    const double d = pref_double_get( TR_PREFS_KEY_RATIO );

    if( b )
        s = g_strdup_printf( _( "Use _Global setting  (currently: stop seeding when a torrent's ratio reaches %.2f)" ), d );
    else
        s = g_strdup( _( "Use _Global setting  (currently: seed regardless of ratio)" ) );

    return s;
}

static void
prefsChanged( TrCore * core UNUSED, const char *  key, gpointer rb )
{
    if( !strcmp( key, TR_PREFS_KEY_RATIO_ENABLED ) || !strcmp( key, TR_PREFS_KEY_RATIO ) )
    {
        char * s = get_global_ratio_radiobutton_string( );
        gtk_button_set_label( GTK_BUTTON( rb ), s );
        g_free( s );
    }
}

static GtkWidget*
options_page_new( struct ResponseData * data )
{
    uint16_t     maxConnectedPeers;
    int          i, row;
    double       d;
    gboolean     b;
    char       * s;
    GSList     * group;
    GtkWidget  * t, *w, *tb, *h;
    tr_ratiolimit mode;
    TrCore     * core = data->core;
    TrTorrent  * gtor = data->gtor;
    tr_torrent * tor = tr_torrent_handle( gtor );

    row = 0;
    t = hig_workarea_create( );
    hig_workarea_add_section_title( t, &row, _( "Speed Limits" ) );

        b = tr_torrentIsUsingGlobalSpeedLimit( tor, TR_UP );
        tb = hig_workarea_add_wide_checkbutton( t, &row, _( "Honor global _limits" ), b );
        g_signal_connect( tb, "toggled", G_CALLBACK( global_speed_toggled_cb ), gtor );

        tb = gtk_check_button_new_with_mnemonic( _( "Limit _download speed (KB/s):" ) );
        b = tr_torrentIsUsingSpeedLimit( tor, TR_DOWN );
        gtk_toggle_button_set_active( GTK_TOGGLE_BUTTON( tb ), b );
        g_signal_connect( tb, "toggled", G_CALLBACK( down_speed_toggled_cb ), gtor );

        i = tr_torrentGetSpeedLimit( tor, TR_DOWN );
        w = gtk_spin_button_new_with_range( 1, INT_MAX, 5 );
        gtk_spin_button_set_value( GTK_SPIN_BUTTON( w ), i );
        g_signal_connect( w, "value-changed", G_CALLBACK( down_speed_spun_cb ), gtor );
        g_signal_connect( tb, "toggled", G_CALLBACK( sensitize_from_check_cb ), w );
        sensitize_from_check_cb( GTK_TOGGLE_BUTTON( tb ), w );
        hig_workarea_add_row_w( t, &row, tb, w, NULL );

        tb = gtk_check_button_new_with_mnemonic( _( "Limit _upload speed (KB/s):" ) );
        b = tr_torrentIsUsingSpeedLimit( tor, TR_UP );
        gtk_toggle_button_set_active( GTK_TOGGLE_BUTTON( tb ), b );
        g_signal_connect( tb, "toggled", G_CALLBACK( up_speed_toggled_cb ), gtor );

        i = tr_torrentGetSpeedLimit( tor, TR_UP );
        w = gtk_spin_button_new_with_range( 1, INT_MAX, 5 );
        gtk_spin_button_set_value( GTK_SPIN_BUTTON( w ), i );
        g_signal_connect( w, "value-changed", G_CALLBACK( up_speed_spun_cb ), gtor );
        g_signal_connect( tb, "toggled", G_CALLBACK( sensitize_from_check_cb ), w );
        sensitize_from_check_cb( GTK_TOGGLE_BUTTON( tb ), w );
        hig_workarea_add_row_w( t, &row, tb, w, NULL );

    hig_workarea_add_section_divider( t, &row );
    hig_workarea_add_section_title( t, &row, _( "Seed-Until Ratio" ) );

        group = NULL;
        mode = tr_torrentGetRatioMode( tor );
        s = get_global_ratio_radiobutton_string( );
        w = gtk_radio_button_new_with_mnemonic( group, s );
        data->handler = g_signal_connect( core, "prefs-changed", G_CALLBACK( prefsChanged ), w );
        group = gtk_radio_button_get_group( GTK_RADIO_BUTTON( w ) );
        gtk_toggle_button_set_active( GTK_TOGGLE_BUTTON( w ), mode == TR_RATIOLIMIT_GLOBAL);
        hig_workarea_add_wide_control( t, &row, w );
        g_free( s );
        g_object_set_data( G_OBJECT( w ), RATIO_MODE_KEY, GINT_TO_POINTER( TR_RATIOLIMIT_GLOBAL ) );
        g_signal_connect( w, "toggled", G_CALLBACK( ratio_mode_changed_cb ), gtor );

        w = gtk_radio_button_new_with_mnemonic( group, _( "Seed _regardless of ratio" ) );
        group = gtk_radio_button_get_group( GTK_RADIO_BUTTON( w ) );
        gtk_toggle_button_set_active( GTK_TOGGLE_BUTTON( w ), mode == TR_RATIOLIMIT_UNLIMITED);
        hig_workarea_add_wide_control( t, &row, w );
        g_object_set_data( G_OBJECT( w ), RATIO_MODE_KEY, GINT_TO_POINTER( TR_RATIOLIMIT_UNLIMITED ) );
        g_signal_connect( w, "toggled", G_CALLBACK( ratio_mode_changed_cb ), gtor );

        h = gtk_hbox_new( FALSE, GUI_PAD );
        w = gtk_radio_button_new_with_mnemonic( group, _( "_Stop seeding when a torrent's ratio reaches" ) );
        g_object_set_data( G_OBJECT( w ), RATIO_MODE_KEY, GINT_TO_POINTER( TR_RATIOLIMIT_SINGLE ) );
        gtk_toggle_button_set_active( GTK_TOGGLE_BUTTON( w ), mode == TR_RATIOLIMIT_SINGLE);
        g_signal_connect( w, "toggled", G_CALLBACK( ratio_mode_changed_cb ), gtor );
        group = gtk_radio_button_get_group( GTK_RADIO_BUTTON( w ) );
        gtk_box_pack_start( GTK_BOX( h ), w, FALSE, FALSE, 0 );
        d = tr_torrentGetRatioLimit( tor );
        w = gtk_spin_button_new_with_range( 0.5, INT_MAX, .05 );
        gtk_spin_button_set_digits( GTK_SPIN_BUTTON( w ), 2 );
        gtk_spin_button_set_value( GTK_SPIN_BUTTON( w ), d );
        g_signal_connect( w, "value-changed", G_CALLBACK( ratio_spun_cb ), gtor );
        gtk_box_pack_start( GTK_BOX( h ), w, FALSE, FALSE, 0 );
        hig_workarea_add_wide_control( t, &row, h );
   
    hig_workarea_add_section_divider( t, &row );
    hig_workarea_add_section_title( t, &row, _( "Peer Connections" ) );

    maxConnectedPeers = tr_torrentGetPeerLimit( tor );
    w = gtk_spin_button_new_with_range( 1, 3000, 5 );
    gtk_spin_button_set_value( GTK_SPIN_BUTTON( w ), maxConnectedPeers );
    hig_workarea_add_row( t, &row, _( "_Maximum peers:" ), w, w );
    g_signal_connect( w, "value-changed", G_CALLBACK(
                          max_peers_spun_cb ), gtor );

    hig_workarea_finish( t, &row );
    return t;
}

/****
*****  TRACKER
****/

#define TRACKER_PAGE "tracker-page"

struct tracker_page
{
    TrTorrent *         gtor;

    GtkTreeView *       view;
    GtkListStore *      store;
    GtkTreeSelection *  sel;

    GtkWidget *         add_button;
    GtkWidget *         remove_button;
    GtkWidget *         save_button;
    GtkWidget *         revert_button;

    GtkWidget *         last_scrape_time_lb;
    GtkWidget *         last_scrape_response_lb;
    GtkWidget *         next_scrape_countdown_lb;

    GtkWidget *         last_announce_time_lb;
    GtkWidget *         last_announce_response_lb;
    GtkWidget *         next_announce_countdown_lb;
    GtkWidget *         manual_announce_countdown_lb;
};

static GtkWidget*
tracker_page_new( TrTorrent * gtor )
{
    GtkWidget *           t;
    GtkWidget *           l;
    GtkWidget *           w;
    int                   row = 0;
    const char *          s;
    struct tracker_page * page = g_new0( struct tracker_page, 1 );
    const tr_info *       info = tr_torrent_info( gtor );

    page->gtor = gtor;

    t = hig_workarea_create( );
    hig_workarea_add_section_title( t, &row, _( "Trackers" ) );

    w = tracker_list_new( gtor );
    hig_workarea_add_wide_control( t, &row, w );

    hig_workarea_add_section_divider( t, &row );
    hig_workarea_add_section_title( t, &row, _( "Scrape" ) );

    s = _( "Last scrape at:" );
    l = gtk_label_new( NULL );
    page->last_scrape_time_lb = l;
    hig_workarea_add_row( t, &row, s, l, NULL );

    s = _( "Tracker responded:" );
    l = gtk_label_new( NULL );
    page->last_scrape_response_lb = l;
    hig_workarea_add_row( t, &row, s, l, NULL );

    s = _( "Next scrape in:" );
    l = gtk_label_new( NULL );
    page->next_scrape_countdown_lb = l;
    hig_workarea_add_row( t, &row, s, l, NULL );

    hig_workarea_add_section_divider( t, &row );
    hig_workarea_add_section_title( t, &row, _( "Announce" ) );

    l = gtk_label_new( info->trackers[0].announce );
    gtk_label_set_ellipsize( GTK_LABEL( l ), PANGO_ELLIPSIZE_END );
    hig_workarea_add_row( t, &row, _( "Tracker:" ), l, NULL );

    s = _( "Last announce at:" );
    l = gtk_label_new( NULL );
    page->last_announce_time_lb = l;
    hig_workarea_add_row( t, &row, s, l, NULL );

    s = _( "Tracker responded:" );
    l = gtk_label_new( NULL );
    page->last_announce_response_lb = l;
    hig_workarea_add_row( t, &row, s, l, NULL );

    s = _( "Next announce in:" );
    l = gtk_label_new( NULL );
    page->next_announce_countdown_lb = l;
    hig_workarea_add_row( t, &row, s, l, NULL );

    /* how long until the tracker will honor user
    * pressing the "ask for more peers" button */
    s = _( "Manual announce allowed in:" );
    l = gtk_label_new( NULL );
    page->manual_announce_countdown_lb = l;
    hig_workarea_add_row( t, &row, s, l, NULL );

    hig_workarea_finish( t, &row );
    g_object_set_data_full( G_OBJECT( t ), TRACKER_PAGE, page, g_free );
    return t;
}

static void
refresh_countdown_lb( GtkWidget *  w,
                      time_t       t,
                      const char * countdown_done )
{
    const time_t now = time( NULL );
    GtkLabel *   l = GTK_LABEL( w );

    if( t == 1 )
        gtk_label_set_text( l, _( "In progress" ) );
    else if( t < now )
        gtk_label_set_text( l, countdown_done );
    else {
        char buf[512];
        const int seconds = t - now;
        tr_strltime( buf, seconds, sizeof( buf ) );
        gtk_label_set_text( l, buf );
    }
}

static void
refresh_tracker( GtkWidget * w )
{
    GtkWidget *           l;
    time_t                t;
    struct tracker_page * page = g_object_get_data( G_OBJECT(
                                                        w ), TRACKER_PAGE );
    const tr_stat *       torStat = tr_torrent_stat( page->gtor );

    l = page->last_scrape_time_lb;
    t = torStat->lastScrapeTime;
    refresh_time_lb( l, t );

    l = page->last_scrape_response_lb;
    gtk_label_set_text( GTK_LABEL( l ), torStat->scrapeResponse );

    l = page->next_scrape_countdown_lb;
    t = torStat->nextScrapeTime;
    refresh_countdown_lb( l, t, _( "Never" ) );

    l = page->last_announce_time_lb;
    t = torStat->lastAnnounceTime;
    refresh_time_lb( l, t );

    l = page->last_announce_response_lb;
    gtk_label_set_text( GTK_LABEL( l ), torStat->announceResponse );

    l = page->next_announce_countdown_lb;
    t = torStat->nextAnnounceTime;
    refresh_countdown_lb( l, t, _( "Never" ) );

    l = page->manual_announce_countdown_lb;
    t = torStat->manualAnnounceTime;
    refresh_countdown_lb( l, t, _( "Now" ) );
}

/****
*****  DIALOG
****/

static void
torrent_destroyed( gpointer dialog, GObject * dead_torrent UNUSED )
{
    gtk_widget_destroy( GTK_WIDGET( dialog ) );
}

static void
remove_tag( gpointer tag )
{
    g_source_remove( GPOINTER_TO_UINT( tag ) ); /* stop the periodic refresh */
}

static void
response_cb( GtkDialog *  dialog,
             int response UNUSED,
             gpointer     data )
{
    struct ResponseData *rd = data;
    TrCore * core = rd->core;
    gulong handler = rd-> handler;

    g_signal_handler_disconnect( core, handler );
    g_object_weak_unref( G_OBJECT( rd->gtor ), torrent_destroyed, dialog );
    gtk_widget_destroy( GTK_WIDGET( dialog ) );

    g_free( rd );
}

static gboolean
periodic_refresh( gpointer data )
{
    refresh_tracker   ( g_object_get_data( G_OBJECT( data ), "tracker-top" ) );
    refresh_peers     ( g_object_get_data( G_OBJECT( data ), "peers-top" ) );
    refresh_activity  ( g_object_get_data( G_OBJECT( data ), "activity-top" ) );
    return TRUE;
}

GtkWidget*
torrent_inspector_new( GtkWindow * parent,
                       TrCore    * core,
                       TrTorrent * gtor )
{
    guint           tag;
    GtkWidget *     d, *n, *w, *lb;
    char            title[512];
    struct ResponseData  * rd;
    tr_torrent *    tor = tr_torrent_handle( gtor );
    const tr_info * info = tr_torrent_info( gtor );

    /* create the dialog */
    rd = g_new0(struct ResponseData, 1);
    rd->gtor = gtor;
    rd->core = core;
    g_snprintf( title, sizeof( title ), _( "%s Properties" ), info->name );
    d = gtk_dialog_new_with_buttons( title, parent, 0,
                                     GTK_STOCK_CLOSE, GTK_RESPONSE_CLOSE,
                                     NULL );
    gtk_window_set_role( GTK_WINDOW( d ), "tr-info" );
    g_signal_connect( d, "response", G_CALLBACK( response_cb ), rd );
    gtk_dialog_set_has_separator( GTK_DIALOG( d ), FALSE );
    gtk_container_set_border_width( GTK_CONTAINER( d ), GUI_PAD );
    g_object_weak_ref( G_OBJECT( gtor ), torrent_destroyed, d );


    /* add the notebook */
    n = gtk_notebook_new( );
    gtk_container_set_border_width( GTK_CONTAINER( n ), GUI_PAD );

    w = activity_page_new( gtor );
    lb = gtk_label_new( _( "Activity" ) );
    gtk_notebook_append_page( GTK_NOTEBOOK( n ), w, lb );
    g_object_set_data( G_OBJECT( d ), "activity-top", w );

    w = peer_page_new( gtor );
    lb = gtk_label_new( _( "Peers" ) );
    gtk_notebook_append_page( GTK_NOTEBOOK( n ),  w, lb );
    g_object_set_data( G_OBJECT( d ), "peers-top", w );

    w = tracker_page_new( gtor );
    lb = gtk_label_new( _( "Tracker" ) );
    gtk_notebook_append_page( GTK_NOTEBOOK( n ), w, lb );
    g_object_set_data( G_OBJECT( d ), "tracker-top", w );

    w = info_page_new( tor );
    lb = gtk_label_new( _( "Information" ) );
    gtk_notebook_append_page( GTK_NOTEBOOK( n ), w, lb );

    w = file_list_new( gtor );
    gtk_container_set_border_width( GTK_CONTAINER( w ), GUI_PAD_BIG );
    lb = gtk_label_new( _( "Files" ) );
    gtk_notebook_append_page( GTK_NOTEBOOK( n ), w, lb );
    g_object_set_data( G_OBJECT( d ), "files-top", w );

    w = options_page_new( rd );
    lb = gtk_label_new( _( "Options" ) );
    gtk_notebook_append_page( GTK_NOTEBOOK( n ), w, lb );

    gtk_box_pack_start( GTK_BOX( GTK_DIALOG( d )->vbox ), n, TRUE, TRUE, 0 );

    tag = gtr_timeout_add_seconds( UPDATE_INTERVAL_SECONDS, periodic_refresh, d );
    g_object_set_data_full( G_OBJECT( d ), "tag", GUINT_TO_POINTER( tag ), remove_tag );
    periodic_refresh( d );
    gtk_widget_show_all( GTK_DIALOG( d )->vbox );
    return d;
}

