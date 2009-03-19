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

#include <locale.h>
#include <sys/param.h>
#include <signal.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

#include <gtk/gtk.h>
#include <glib/gi18n.h>
#include <glib/gstdio.h>

#include <libtransmission/transmission.h>
#include <libtransmission/rpcimpl.h>
#include <libtransmission/utils.h>
#include <libtransmission/version.h>

#include "actions.h"
#include "add-dialog.h"
#include "blocklist.h"
#include "conf.h"
#include "details.h"
#include "dialogs.h"
#include "hig.h"
#include "makemeta-ui.h"
#include "msgwin.h"
#include "notify.h"
#include "stats.h"
#include "tr-core.h"
#include "tr-icon.h"
#include "tr-prefs.h"
#include "tr-torrent.h"
#include "tr-window.h"
#include "util.h"
#include "ui.h"

#define MY_NAME "transmission"

#define REFRESH_INTERVAL_SECONDS 2

#if GTK_CHECK_VERSION( 2, 8, 0 )
 #define SHOW_LICENSE
static const char * LICENSE =
    "The Transmission binaries and most of its source code is distributed "
    "license. "
    "\n\n"
    "Some files are copyrighted by Charles Kerr and are covered by "
    "the GPL version 2.  Works owned by the Transmission project "
    "are granted a special exemption to clause 2(b) so that the bulk "
    "of its code can remain under the MIT license.  This exemption does "
    "not extend to original or derived works not owned by the "
    "Transmission project. "
    "\n\n"
    "Permission is hereby granted, free of charge, to any person obtaining "
    "a copy of this software and associated documentation files (the "
    "'Software'), to deal in the Software without restriction, including "
    "without limitation the rights to use, copy, modify, merge, publish, "
    "distribute, sublicense, and/or sell copies of the Software, and to "
    "permit persons to whom the Software is furnished to do so, subject to "
    "the following conditions: "
    "\n\n"
    "The above copyright notice and this permission notice shall be included "
    "in all copies or substantial portions of the Software. "
    "\n\n"
    "THE SOFTWARE IS PROVIDED 'AS IS', WITHOUT WARRANTY OF ANY KIND, "
    "EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF "
    "MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. "
    "IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY "
    "CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, "
    "TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE "
    "SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.";
#endif

struct cbdata
{
    unsigned int  isIconified : 1;
    unsigned int  isClosing   : 1;
    guint         timer;
    gpointer      icon;
    GtkWindow *   wind;
    TrCore *      core;
    GtkWidget *   msgwin;
    GtkWidget *   prefs;
    GSList *      errqueue;
    GSList *      dupqueue;
    GHashTable *  tor2details;
    GHashTable *  details2tor;
    GtkTreeSelection * sel;
};

#define CBDATA_PTR "callback-data-pointer"

static GtkUIManager * myUIManager = NULL;

static void           appsetup( TrWindow * wind,
                                GSList *   args,
                                struct     cbdata *,
                                gboolean   paused,
                                gboolean   minimized );

static void           winsetup( struct cbdata * cbdata,
                                TrWindow *      wind );

static void           wannaquit( void * vdata );

static void           setupdrag( GtkWidget *    widget,
                                 struct cbdata *data );

static void           gotdrag( GtkWidget *       widget,
                               GdkDragContext *  dc,
                               gint              x,
                               gint              y,
                               GtkSelectionData *sel,
                               guint             info,
                               guint             time,
                               gpointer          gdata );

static void           coreerr( TrCore *         core,
                               enum tr_core_err code,
                               const char *     msg,
                               gpointer         gdata );

static void           onAddTorrent( TrCore *,
                                    tr_ctor *,
                                    gpointer );

static void           prefschanged( TrCore *     core,
                                    const char * key,
                                    gpointer     data );

static gboolean       updatemodel( gpointer gdata );

struct counts_data
{
    int    totalCount;
    int    activeCount;
    int    inactiveCount;
};

static void
accumulateStatusForeach( GtkTreeModel *      model,
                         GtkTreePath  * path UNUSED,
                         GtkTreeIter *       iter,
                         gpointer            user_data )
{
    int                  activity = 0;
    struct counts_data * counts = user_data;

    ++counts->totalCount;

    gtk_tree_model_get( model, iter, MC_ACTIVITY, &activity, -1 );

    if( TR_STATUS_IS_ACTIVE( activity ) )
        ++counts->activeCount;
    else
        ++counts->inactiveCount;
}

static void
accumulateCanUpdateForeach( GtkTreeModel *      model,
                            GtkTreePath  * path UNUSED,
                            GtkTreeIter *       iter,
                            gpointer            accumulated_status )
{
    tr_torrent * tor;
    gtk_tree_model_get( model, iter, MC_TORRENT_RAW, &tor, -1 );
    *(int*)accumulated_status |= tr_torrentCanManualUpdate( tor );
}

static void
refreshTorrentActions( struct cbdata * data )
{
    int canUpdate;
    struct counts_data counts;
    GtkTreeSelection * s = data->sel;

    counts.activeCount = 0;
    counts.inactiveCount = 0;
    counts.totalCount = 0;
    gtk_tree_selection_selected_foreach( s, accumulateStatusForeach, &counts );
    action_sensitize( "pause-torrent", counts.activeCount != 0 );
    action_sensitize( "start-torrent", counts.inactiveCount != 0 );
    action_sensitize( "remove-torrent", counts.totalCount != 0 );
    action_sensitize( "delete-torrent", counts.totalCount != 0 );
    action_sensitize( "verify-torrent", counts.totalCount != 0 );
    action_sensitize( "open-torrent-folder", counts.totalCount == 1 );
    action_sensitize( "show-torrent-properties", counts.totalCount == 1 );

    canUpdate = 0;
    gtk_tree_selection_selected_foreach( s, accumulateCanUpdateForeach, &canUpdate );
    action_sensitize( "update-tracker", canUpdate != 0 );

    {
        GtkTreeView *  view = gtk_tree_selection_get_tree_view( s );
        GtkTreeModel * model = gtk_tree_view_get_model( view );
        const int torrentCount = gtk_tree_model_iter_n_children( model, NULL ) != 0;
        action_sensitize( "select-all", torrentCount != 0 );
        action_sensitize( "deselect-all", torrentCount != 0 );
    }

    {
        tr_session * session = tr_core_session( data->core );
        const int active = tr_sessionGetActiveTorrentCount( session );
        const int total = tr_sessionCountTorrents( session );
        action_sensitize( "pause-all-torrents", active != 0 );
        action_sensitize( "start-all-torrents", active != total );
    }
}

static void
selectionChangedCB( GtkTreeSelection * s UNUSED, gpointer data )
{
    refreshTorrentActions( data );
}

static void
onMainWindowSizeAllocated( GtkWidget *            window,
                           GtkAllocation  * alloc UNUSED,
                           gpointer         gdata UNUSED )
{
    const gboolean isMaximized = window->window
                            && ( gdk_window_get_state( window->window )
                                 & GDK_WINDOW_STATE_MAXIMIZED );

    if( !isMaximized )
    {
        int x, y, w, h;
        gtk_window_get_position( GTK_WINDOW( window ), &x, &y );
        gtk_window_get_size( GTK_WINDOW( window ), &w, &h );
        pref_int_set( PREF_KEY_MAIN_WINDOW_X, x );
        pref_int_set( PREF_KEY_MAIN_WINDOW_Y, y );
        pref_int_set( PREF_KEY_MAIN_WINDOW_WIDTH, w );
        pref_int_set( PREF_KEY_MAIN_WINDOW_HEIGHT, h );
    }
}

static sig_atomic_t global_sigcount = 0;

static void
fatalsig( int sig )
{
    /* revert to default handler after this many */
    static const int SIGCOUNT_MAX = 3;

    if( ++global_sigcount >= SIGCOUNT_MAX )
    {
        signal( sig, SIG_DFL );
        raise( sig );
    }
}

static void
setupsighandlers( void )
{
#ifdef G_OS_WIN32
    const int sigs[] = { SIGINT, SIGTERM };
#else
    const int sigs[] = { SIGHUP, SIGINT, SIGQUIT, SIGTERM };
#endif
    guint     i;

    for( i = 0; i < G_N_ELEMENTS( sigs ); ++i )
        signal( sigs[i], fatalsig );
}

static tr_rpc_callback_status
onRPCChanged( tr_session            * session UNUSED,
              tr_rpc_callback_type    type,
              struct tr_torrent     * tor,
              void                  * gdata )
{
    struct cbdata * cbdata = gdata;
    gdk_threads_enter( );

    switch( type )
    {
        case TR_RPC_TORRENT_ADDED:
            tr_core_add_torrent( cbdata->core,
                                 tr_torrent_new_preexisting( tor ) );
            break;

        case TR_RPC_TORRENT_STARTED:
            /* this should be automatic */
            break;

        case TR_RPC_TORRENT_STOPPED:
            /* this should be automatic */
            break;

        case TR_RPC_TORRENT_REMOVING:
            tr_core_torrent_destroyed( cbdata->core, tr_torrentId( tor ) );
            break;

        case TR_RPC_TORRENT_CHANGED:
        case TR_RPC_SESSION_CHANGED:
            /* nothing interesting to do here */
            break;
    }

    gdk_threads_leave( );
    return TR_RPC_OK;
}

int
main( int     argc,
      char ** argv )
{
    char *              err = NULL;
    struct cbdata *     cbdata;
    GSList *            argfiles;
    GError *            gerr;
    gboolean            didinit = FALSE;
    gboolean            didlock = FALSE;
    gboolean            showversion = FALSE;
    gboolean            startpaused = FALSE;
    gboolean            startminimized = FALSE;
    const char *        domain = MY_NAME;
    char *              configDir = NULL;
    tr_lockfile_state_t tr_state;

    GOptionEntry entries[] = {
        { "paused",     'p', 0, G_OPTION_ARG_NONE,
          &startpaused, _( "Start with all torrents paused" ), NULL },
        { "version",    '\0', 0, G_OPTION_ARG_NONE,
          &showversion, _( "Show version number and exit" ), NULL },
#ifdef STATUS_ICON_SUPPORTED
        { "minimized",  'm', 0, G_OPTION_ARG_NONE,
          &startminimized,
          _( "Start minimized in system tray" ), NULL },
#endif
        { "config-dir", 'g', 0, G_OPTION_ARG_FILENAME, &configDir,
          _( "Where to look for configuration files" ), NULL },
        { NULL, 0,   0, 0, NULL, NULL, NULL }
    };

    cbdata = g_new0( struct cbdata, 1 );
    cbdata->tor2details = g_hash_table_new( g_str_hash, g_str_equal );
    cbdata->details2tor = g_hash_table_new( g_direct_hash, g_direct_equal );

    /* bind the gettext domain */
    setlocale( LC_ALL, "" );
    bindtextdomain( domain, TRANSMISSIONLOCALEDIR );
    bind_textdomain_codeset( domain, "UTF-8" );
    textdomain( domain );
    g_set_application_name( _( "Transmission" ) );

    /* initialize gtk */
    if( !g_thread_supported( ) )
        g_thread_init( NULL );

    gerr = NULL;
    if( !gtk_init_with_args( &argc, &argv, _( "[torrent files]" ), entries,
                             (char*)domain, &gerr ) )
    {
        fprintf( stderr, "%s\n", gerr->message );
        g_clear_error( &gerr );
        return 0;
    }

    if( showversion )
    {
        fprintf( stderr, "%s %s\n", g_get_application_name( ), LONG_VERSION_STRING );
        return 0;
    }

    if( configDir == NULL )
        configDir = (char*) tr_getDefaultConfigDir( MY_NAME );

    tr_notify_init( );
    didinit = cf_init( configDir, NULL ); /* must come before actions_init */

    myUIManager = gtk_ui_manager_new ( );
    actions_init ( myUIManager, cbdata );
    gtk_ui_manager_add_ui_from_string ( myUIManager, fallback_ui_file, -1, NULL );
    gtk_ui_manager_ensure_update ( myUIManager );
    gtk_window_set_default_icon_name ( MY_NAME );

    setupsighandlers( ); /* set up handlers for fatal signals */

    /* either get a lockfile s.t. this is the one instance of
     * transmission that's running, OR if there are files to
     * be added, delegate that to the running instance via dbus */
    didlock = cf_lock( &tr_state, &err );
    argfiles = checkfilenames( argc - 1, argv + 1 );
    if( !didlock && argfiles )
    {
        GSList * l;
        gboolean delegated = FALSE;
        for( l = argfiles; l; l = l->next )
            delegated |= gtr_dbus_add_torrent( l->data );
        if( delegated )
            err = NULL;
    }
    else if( ( !didlock ) && ( tr_state == TR_LOCKFILE_ELOCK ) )
    {
        gtr_dbus_present_window( );
        err = NULL;
    }

    if( didlock && ( didinit || cf_init( configDir, &err ) ) )
    {
        const char * str;
        GtkWindow * win;
        tr_session * session;

        /* ensure the directories are created */
       if(( str = pref_string_get( PREF_KEY_DIR_WATCH )))
           mkdir_p( str, 0777 );
       if(( str = pref_string_get( TR_PREFS_KEY_DOWNLOAD_DIR )))
           mkdir_p( str, 0777 );

        /* initialize the libtransmission session */
        session = tr_sessionInit( "gtk", configDir, TRUE, pref_get_all( ) );
        cbdata->core = tr_core_new( session );

        /* create main window now to be a parent to any error dialogs */
        win = GTK_WINDOW( tr_window_new( myUIManager, cbdata->core ) );
        g_signal_connect( win, "size-allocate", G_CALLBACK( onMainWindowSizeAllocated ), cbdata );

        appsetup( win, argfiles, cbdata, startpaused, startminimized );
        tr_sessionSetRPCCallback( session, onRPCChanged, cbdata );
        gtr_blocklist_maybe_autoupdate( cbdata->core );

        gtk_main( );
    }
    else if( err )
    {
        gtk_widget_show( errmsg_full( NULL, (callbackfunc_t)gtk_main_quit,
                                      NULL, "%s", err ) );
        g_free( err );
        gtk_main( );
    }

    return 0;
}

static gboolean
updateScheduledLimits( gpointer data )
{
    tr_session *    tr = data;
    static gboolean last_state = FALSE;
    gboolean        in_sched_state = FALSE;

    if( !pref_flag_get( PREF_KEY_SCHED_LIMIT_ENABLED ) )
    {
        in_sched_state = FALSE;
    }
    else
    {
        const int  begin_time = pref_int_get( PREF_KEY_SCHED_BEGIN );
        const int  end_time = pref_int_get( PREF_KEY_SCHED_END );
        time_t     t;
        struct tm *tm;
        int        cur_time;

        time( &t );
        tm = localtime ( &t );
        cur_time = ( tm->tm_hour * 60 ) + tm->tm_min;

        if( end_time >= begin_time )
        {
            if( ( cur_time >= begin_time ) && ( cur_time <= end_time ) )
                in_sched_state = TRUE;
        }
        else
        {
            if( ( cur_time >= begin_time ) || ( cur_time <= end_time ) )
                in_sched_state = TRUE;
        }
    }

    if( last_state != in_sched_state )
    {
        if( in_sched_state )
        {
            int limit;

            tr_inf ( _( "Beginning to use scheduled bandwidth limits" ) );

            tr_sessionSetSpeedLimitEnabled( tr, TR_DOWN, TRUE );
            limit = pref_int_get( PREF_KEY_SCHED_DL_LIMIT );
            tr_sessionSetSpeedLimit( tr, TR_DOWN, limit );
            tr_sessionSetSpeedLimitEnabled( tr, TR_UP, TRUE );
            limit = pref_int_get( PREF_KEY_SCHED_UL_LIMIT );
            tr_sessionSetSpeedLimit( tr, TR_UP, limit );
        }
        else
        {
            gboolean b;
            int      limit;

            tr_inf ( _( "Ending use of scheduled bandwidth limits" ) );

            b = pref_flag_get( TR_PREFS_KEY_DSPEED_ENABLED );
            tr_sessionSetSpeedLimitEnabled( tr, TR_DOWN, b );
            limit = pref_int_get( TR_PREFS_KEY_DSPEED );
            tr_sessionSetSpeedLimit( tr, TR_DOWN, limit );
            b = pref_flag_get( TR_PREFS_KEY_USPEED_ENABLED );
            tr_sessionSetSpeedLimitEnabled( tr, TR_UP, b );
            limit = pref_int_get( TR_PREFS_KEY_USPEED );
            tr_sessionSetSpeedLimit( tr, TR_UP, limit );
        }

        last_state = in_sched_state;
    }
    else if( in_sched_state )
    {
        static int old_dl_limit = 0, old_ul_limit = 0;
        int        dl_limit = pref_int_get( PREF_KEY_SCHED_DL_LIMIT );
        int        ul_limit = pref_int_get( PREF_KEY_SCHED_UL_LIMIT );

        if( ( dl_limit != old_dl_limit ) || ( ul_limit != old_ul_limit ) )
        {
            tr_sessionSetSpeedLimitEnabled( tr, TR_DOWN, TRUE );
            tr_sessionSetSpeedLimit( tr, TR_DOWN, dl_limit );
            tr_sessionSetSpeedLimitEnabled( tr, TR_UP, TRUE );
            tr_sessionSetSpeedLimit( tr, TR_UP, ul_limit );

            old_dl_limit = dl_limit;
            old_ul_limit = ul_limit;
        }
    }

    return TRUE;
}

static void
appsetup( TrWindow *      wind,
          GSList *        torrentFiles,
          struct cbdata * cbdata,
          gboolean        forcepause,
          gboolean        isIconified )
{
    const pref_flag_t start =
        forcepause ? PREF_FLAG_FALSE : PREF_FLAG_DEFAULT;
    const pref_flag_t prompt = PREF_FLAG_DEFAULT;

    /* fill out cbdata */
    cbdata->wind         = NULL;
    cbdata->icon         = NULL;
    cbdata->msgwin       = NULL;
    cbdata->prefs        = NULL;
    cbdata->timer        = 0;
    cbdata->isClosing    = 0;
    cbdata->errqueue     = NULL;
    cbdata->dupqueue     = NULL;
    cbdata->isIconified  = isIconified;

    if( isIconified )
        pref_flag_set( PREF_KEY_SHOW_TRAY_ICON, TRUE );

    actions_set_core( cbdata->core );

    /* set up core handlers */
    g_signal_connect( cbdata->core, "error", G_CALLBACK( coreerr ), cbdata );
    g_signal_connect( cbdata->core, "add-torrent-prompt",
                      G_CALLBACK( onAddTorrent ), cbdata );
    g_signal_connect_swapped( cbdata->core, "quit",
                              G_CALLBACK( wannaquit ), cbdata );
    g_signal_connect( cbdata->core, "prefs-changed",
                      G_CALLBACK( prefschanged ), cbdata );

    /* add torrents from command-line and saved state */
    tr_core_load( cbdata->core, forcepause );
    tr_core_add_list( cbdata->core, torrentFiles, start, prompt );
    torrentFiles = NULL;
    tr_core_torrents_added( cbdata->core );

    /* set up main window */
    winsetup( cbdata, wind );

    /* set up the icon */
    prefschanged( cbdata->core, PREF_KEY_SHOW_TRAY_ICON, cbdata );

    /* start model update timer */
    cbdata->timer = gtr_timeout_add_seconds( REFRESH_INTERVAL_SECONDS, updatemodel, cbdata );
    updatemodel( cbdata );

    /* start scheduled rate timer */
    updateScheduledLimits ( tr_core_session( cbdata->core ) );
    gtr_timeout_add_seconds( 60, updateScheduledLimits, tr_core_session( cbdata->core ) );

    /* either show the window or iconify it */
    if( !isIconified )
        gtk_widget_show( GTK_WIDGET( wind ) );
    else
    {
        gtk_window_iconify( wind );
        gtk_window_set_skip_taskbar_hint( cbdata->wind,
                                          cbdata->icon != NULL );
    }
}

static void
tr_window_present( GtkWindow * window )
{
#if GTK_CHECK_VERSION( 2, 8, 0 )
    gtk_window_present_with_time( window, gtk_get_current_event_time( ) );
#else
    gtk_window_present( window );
#endif
}

static void
toggleMainWindow( struct cbdata * cbdata,
                  gboolean        doPresent )
{
    GtkWindow * window = GTK_WINDOW( cbdata->wind );
    const int   doShow = cbdata->isIconified;
    static int  x = 0;
    static int  y = 0;

    if( doShow || doPresent )
    {
        cbdata->isIconified = 0;
        gtk_window_set_skip_taskbar_hint( window, FALSE );
        gtk_window_move( window, x, y );
        gtk_widget_show( GTK_WIDGET( window ) );
        tr_window_present( window );
    }
    else
    {
        gtk_window_get_position( window, &x, &y );
        gtk_window_set_skip_taskbar_hint( window, TRUE );
        gtk_widget_hide( GTK_WIDGET( window ) );
        cbdata->isIconified = 1;
    }
}

static gboolean
winclose( GtkWidget * w    UNUSED,
          GdkEvent * event UNUSED,
          gpointer         gdata )
{
    struct cbdata * cbdata = gdata;

    if( cbdata->icon != NULL )
        action_activate ( "toggle-main-window" );
    else
        askquit( cbdata->core, cbdata->wind, wannaquit, cbdata );

    return TRUE; /* don't propagate event further */
}

static void
rowChangedCB( GtkTreeModel  * model UNUSED,
              GtkTreePath   * path,
              GtkTreeIter   * iter  UNUSED,
              gpointer        gdata )
{
    struct cbdata * data = gdata;
    if( gtk_tree_selection_path_is_selected ( data->sel, path ) )
        refreshTorrentActions( gdata );
}

static void
winsetup( struct cbdata * cbdata,
          TrWindow *      wind )
{
    GtkTreeModel *     model;
    GtkTreeSelection * sel;

    g_assert( NULL == cbdata->wind );
    cbdata->wind = GTK_WINDOW( wind );
    cbdata->sel = sel = GTK_TREE_SELECTION( tr_window_get_selection( cbdata->wind ) );

    g_signal_connect( sel, "changed", G_CALLBACK( selectionChangedCB ), cbdata );
    selectionChangedCB( sel, cbdata );
    model = tr_core_model( cbdata->core );
    g_signal_connect( model, "row-changed", G_CALLBACK( rowChangedCB ), cbdata );
    g_signal_connect( wind, "delete-event", G_CALLBACK( winclose ), cbdata );
    refreshTorrentActions( cbdata );

    setupdrag( GTK_WIDGET( wind ), cbdata );
}

static gpointer
quitThreadFunc( gpointer gdata )
{
    struct cbdata * cbdata = gdata;

    tr_core_close( cbdata->core );

    /* shutdown the gui */
    if( cbdata->prefs )
        gtk_widget_destroy( GTK_WIDGET( cbdata->prefs ) );
    if( cbdata->wind )
        gtk_widget_destroy( GTK_WIDGET( cbdata->wind ) );
    g_object_unref( cbdata->core );
    if( cbdata->icon )
        g_object_unref( cbdata->icon );
    if( cbdata->errqueue )
    {
        g_slist_foreach( cbdata->errqueue, (GFunc)g_free, NULL );
        g_slist_free( cbdata->errqueue );
    }
    if( cbdata->dupqueue )
    {
        g_slist_foreach( cbdata->dupqueue, (GFunc)g_free, NULL );
        g_slist_free( cbdata->dupqueue );
    }

    g_hash_table_destroy( cbdata->details2tor );
    g_hash_table_destroy( cbdata->tor2details );
    g_free( cbdata );

    /* exit the gtk main loop */
    gtk_main_quit( );
    return NULL;
}

static void
do_exit_cb( GtkWidget *w  UNUSED,
            gpointer data UNUSED )
{
    exit( 0 );
}

static void
wannaquit( void * vdata )
{
    GtkWidget *     r, * p, * b, * w, *c;
    struct cbdata * cbdata = vdata;

    /* stop the update timer */
    if( cbdata->timer )
    {
        g_source_remove( cbdata->timer );
        cbdata->timer = 0;
    }

    c = GTK_WIDGET( cbdata->wind );
    gtk_container_remove( GTK_CONTAINER( c ), gtk_bin_get_child( GTK_BIN( c ) ) );

    r = gtk_alignment_new( 0.5, 0.5, 0.01, 0.01 );
    gtk_container_add( GTK_CONTAINER( c ), r );

    p = gtk_table_new( 3, 2, FALSE );
    gtk_table_set_col_spacings( GTK_TABLE( p ), GUI_PAD_BIG );
    gtk_container_add( GTK_CONTAINER( r ), p );

    w = gtk_image_new_from_stock( GTK_STOCK_NETWORK, GTK_ICON_SIZE_DIALOG );
    gtk_table_attach_defaults( GTK_TABLE( p ), w, 0, 1, 0, 2 );

    w = gtk_label_new( NULL );
    gtk_label_set_markup( GTK_LABEL( w ), _( "<b>Closing Connections</b>" ) );
    gtk_misc_set_alignment( GTK_MISC( w ), 0.0, 0.5 );
    gtk_table_attach_defaults( GTK_TABLE( p ), w, 1, 2, 0, 1 );

    w = gtk_label_new( _( "Sending upload/download totals to tracker..." ) );
    gtk_misc_set_alignment( GTK_MISC( w ), 0.0, 0.5 );
    gtk_table_attach_defaults( GTK_TABLE( p ), w, 1, 2, 1, 2 );

    b = gtk_alignment_new( 0.0, 1.0, 0.01, 0.01 );
    w = gtr_button_new_from_stock( GTK_STOCK_QUIT, _( "_Quit Now" ) );
    g_signal_connect( w, "clicked", G_CALLBACK( do_exit_cb ), NULL );
    gtk_container_add( GTK_CONTAINER( b ), w );
    gtk_table_attach( GTK_TABLE(
                          p ), b, 1, 2, 2, 3, GTK_FILL, GTK_FILL, 0, 10 );

    gtk_widget_show_all( r );

    /* clear the UI */
    gtk_list_store_clear( GTK_LIST_STORE( tr_core_model( cbdata->core ) ) );

    /* shut down libT */
    g_thread_create( quitThreadFunc, vdata, TRUE, NULL );
}

static void
gotdrag( GtkWidget         * widget UNUSED,
         GdkDragContext *           dc,
         gint                x      UNUSED,
         gint                y      UNUSED,
         GtkSelectionData *         sel,
         guint               info   UNUSED,
         guint                      time,
         gpointer                   gdata )
{
    struct cbdata * data = gdata;
    GSList *        paths = NULL;
    GSList *        freeme = NULL;

#if 0
    int             i;
    char *          sele = gdk_atom_name( sel->selection );
    char *          targ = gdk_atom_name( sel->target );
    char *          type = gdk_atom_name( sel->type );

    g_message( "dropped file: sel=%s targ=%s type=%s fmt=%i len=%i",
               sele, targ, type, sel->format, sel->length );
    g_free( sele );
    g_free( targ );
    g_free( type );
    if( sel->format == 8 )
    {
        for( i = 0; i < sel->length; ++i )
            fprintf( stderr, "%02X ", sel->data[i] );
        fprintf( stderr, "\n" );
    }
#endif

    if( ( sel->format == 8 )
      && ( sel->selection == gdk_atom_intern( "XdndSelection", FALSE ) ) )
    {
        int      i;
        char *   str = g_strndup( (char*)sel->data, sel->length );
        gchar ** files = g_strsplit_set( str, "\r\n", -1 );
        for( i = 0; files && files[i]; ++i )
        {
            char * filename;
            if( !*files[i] ) /* empty filename... */
                continue;

            /* decode the filename */
            filename = decode_uri( files[i] );
            freeme = g_slist_prepend( freeme, filename );
            if( !g_utf8_validate( filename, -1, NULL ) )
                continue;

            /* walk past "file://", if present */
            if( g_str_has_prefix( filename, "file:" ) )
            {
                filename += 5;
                while( g_str_has_prefix( filename, "//" ) )
                    ++filename;
            }

            /* if the file doesn't exist, the first part
               might be a hostname ... walk past it. */
            if( !g_file_test( filename, G_FILE_TEST_EXISTS ) )
            {
                char * pch = strchr( filename + 1, '/' );
                if( pch != NULL )
                    filename = pch;
            }

            /* finally, add it to the list of torrents to try adding */
            if( g_file_test( filename, G_FILE_TEST_EXISTS ) )
                paths = g_slist_prepend( paths, g_strdup( filename ) );
        }

        /* try to add any torrents we found */
        if( paths )
        {
            paths = g_slist_reverse( paths );
            tr_core_add_list_defaults( data->core, paths );
            tr_core_torrents_added( data->core );
        }

        freestrlist( freeme );
        g_strfreev( files );
        g_free( str );
    }

    gtk_drag_finish( dc, ( NULL != paths ), FALSE, time );
}

static void
setupdrag( GtkWidget *    widget,
           struct cbdata *data )
{
    GtkTargetEntry targets[] = {
        { (char*)"STRING",          0, 0 },
        { (char*)"text/plain",      0, 0 },
        { (char*)"text/uri-list",   0, 0 },
    };

    g_signal_connect( widget, "drag_data_received", G_CALLBACK(
                          gotdrag ), data );

    gtk_drag_dest_set( widget, GTK_DEST_DEFAULT_ALL, targets,
                       ALEN( targets ), GDK_ACTION_COPY | GDK_ACTION_MOVE );
}

static void
flushAddTorrentErrors( GtkWindow *  window,
                       const char * primary,
                       GSList **    files )
{
    GString *   s = g_string_new( NULL );
    GSList *    l;
    GtkWidget * w;

    if( g_slist_length( *files ) > 1 ) {
        for( l=*files; l!=NULL; l=l->next )
            g_string_append_printf( s, "\xE2\x88\x99 %s\n", (const char*)l->data );
    } else {
        for( l=*files; l!=NULL; l=l->next )
            g_string_append_printf( s, "%s\n", (const char*)l->data );
    }
    w = gtk_message_dialog_new( window,
                                GTK_DIALOG_DESTROY_WITH_PARENT,
                                GTK_MESSAGE_ERROR,
                                GTK_BUTTONS_CLOSE,
                                "%s", primary );
    gtk_message_dialog_format_secondary_text( GTK_MESSAGE_DIALOG( w ),
                                              "%s", s->str );
    g_signal_connect_swapped( w, "response",
                              G_CALLBACK( gtk_widget_destroy ), w );
    gtk_widget_show_all( w );
    g_string_free( s, TRUE );

    g_slist_foreach( *files, (GFunc)g_free, NULL );
    g_slist_free( *files );
    *files = NULL;
}

static void
showTorrentErrors( struct cbdata * cbdata )
{
    if( cbdata->errqueue )
        flushAddTorrentErrors( GTK_WINDOW( cbdata->wind ),
                               ngettext( "Couldn't add corrupt torrent",
                                         "Couldn't add corrupt torrents",
                                         g_slist_length( cbdata->errqueue ) ),
                               &cbdata->errqueue );

    if( cbdata->dupqueue )
        flushAddTorrentErrors( GTK_WINDOW( cbdata->wind ),
                               ngettext( "Couldn't add duplicate torrent",
                                         "Couldn't add duplicate torrents",
                                         g_slist_length( cbdata->dupqueue ) ),
                               &cbdata->dupqueue );
}

static void
coreerr( TrCore * core    UNUSED,
         enum tr_core_err code,
         const char *     msg,
         gpointer         gdata )
{
    struct cbdata * c = gdata;

    switch( code )
    {
        case TR_EINVALID:
            c->errqueue =
                g_slist_append( c->errqueue, g_path_get_basename( msg ) );
            break;

        case TR_EDUPLICATE:
            c->dupqueue = g_slist_append( c->dupqueue, g_strdup( msg ) );
            break;

        case TR_CORE_ERR_NO_MORE_TORRENTS:
            showTorrentErrors( c );
            break;

        case TR_CORE_ERR_SAVE_STATE:
            errmsg( c->wind, "%s", msg );
            break;

        default:
            g_assert_not_reached( );
            break;
    }
}

#if GTK_CHECK_VERSION( 2, 8, 0 )
static void
on_main_window_focus_in( GtkWidget      * widget UNUSED,
                         GdkEventFocus  * event  UNUSED,
                         gpointer                gdata )
{
    struct cbdata * cbdata = gdata;

    gtk_window_set_urgency_hint( GTK_WINDOW( cbdata->wind ), FALSE );
}

#endif

static void
onAddTorrent( TrCore *  core,
              tr_ctor * ctor,
              gpointer  gdata )
{
    struct cbdata * cbdata = gdata;
    GtkWidget *     w = addSingleTorrentDialog( cbdata->wind, core, ctor );

#if GTK_CHECK_VERSION( 2, 8, 0 )
    g_signal_connect( w, "focus-in-event",
                      G_CALLBACK( on_main_window_focus_in ),  cbdata );
    gtk_window_set_urgency_hint( cbdata->wind, TRUE );
#endif
}

static void
prefschanged( TrCore * core UNUSED,
              const char *  key,
              gpointer      data )
{
    struct cbdata  * cbdata = data;
    tr_session     * tr     = tr_core_session( cbdata->core );

    if( !strcmp( key, TR_PREFS_KEY_ENCRYPTION ) )
    {
        const int encryption = pref_int_get( key );
        g_message( "setting encryption to %d", encryption );
        tr_sessionSetEncryption( tr, encryption );
    }
    else if( !strcmp( key, TR_PREFS_KEY_DOWNLOAD_DIR ) )
    {
        tr_sessionSetDownloadDir( tr, pref_string_get( key ) );
    }
    else if( !strcmp( key, TR_PREFS_KEY_MSGLEVEL ) )
    {
        tr_setMessageLevel( pref_int_get( key ) );
    }
    else if( !strcmp( key, TR_PREFS_KEY_PEER_PORT_RANDOM_ENABLED ) )
    {
        /* FIXME */
    }
    else if( !strcmp( key, TR_PREFS_KEY_PEER_PORT ) )
    {
        const int port = pref_int_get( key );
        tr_sessionSetPeerPort( tr, port );
    }
    else if( !strcmp( key, TR_PREFS_KEY_BLOCKLIST_ENABLED ) )
    {
        const gboolean flag = pref_flag_get( key );
        tr_blocklistSetEnabled( tr, flag );
    }
    else if( !strcmp( key, PREF_KEY_SHOW_TRAY_ICON ) )
    {
        const int show = pref_flag_get( key );
        if( show && !cbdata->icon )
            cbdata->icon = tr_icon_new( cbdata->core );
        else if( !show && cbdata->icon )
        {
            g_object_unref( cbdata->icon );
            cbdata->icon = NULL;
        }
    }
    else if( !strcmp( key, TR_PREFS_KEY_DSPEED_ENABLED ) )
    {
        const gboolean b = pref_flag_get( key );
        tr_sessionSetSpeedLimitEnabled( tr, TR_DOWN, b );
    }
    else if( !strcmp( key, TR_PREFS_KEY_DSPEED ) )
    {
        const int limit = pref_int_get( key );
        tr_sessionSetSpeedLimit( tr, TR_DOWN, limit );
    }
    else if( !strcmp( key, TR_PREFS_KEY_USPEED_ENABLED ) )
    {
        const gboolean b = pref_flag_get( key );
        tr_sessionSetSpeedLimitEnabled( tr, TR_UP, b );
    }
    else if( !strcmp( key, TR_PREFS_KEY_USPEED ) )
    {
        const int limit = pref_int_get( key );
        tr_sessionSetSpeedLimit( tr, TR_UP, limit );
    }
    else if( !strcmp( key, TR_PREFS_KEY_RATIO_ENABLED ) )
    {
        const gboolean b = pref_flag_get( key );
        tr_sessionSetRatioLimited( tr, b );
    }
    else if( !strcmp( key, TR_PREFS_KEY_RATIO ) )
    {
        const double limit = pref_double_get( key );
        tr_sessionSetRatioLimit( tr, limit );
    }
    else if( !strncmp( key, "sched-", 6 ) )
    {
        updateScheduledLimits( tr );
    }
    else if( !strcmp( key, TR_PREFS_KEY_PORT_FORWARDING ) )
    {
        tr_sessionSetPortForwardingEnabled( tr, pref_flag_get( key ) );
    }
    else if( !strcmp( key, TR_PREFS_KEY_PEX_ENABLED ) )
    {
        tr_sessionSetPexEnabled( tr, pref_flag_get( key ) );
    }
    else if( !strcmp( key, TR_PREFS_KEY_RPC_PORT ) )
    {
        tr_sessionSetRPCPort( tr, pref_int_get( key ) );
    }
    else if( !strcmp( key, TR_PREFS_KEY_RPC_ENABLED ) )
    {
        tr_sessionSetRPCEnabled( tr, pref_flag_get( key ) );
    }
    else if( !strcmp( key, TR_PREFS_KEY_RPC_WHITELIST ) )
    {
        const char * s = pref_string_get( key );
        tr_sessionSetRPCWhitelist( tr, s );
    }
    else if( !strcmp( key, TR_PREFS_KEY_RPC_WHITELIST_ENABLED ) )
    {
        tr_sessionSetRPCWhitelistEnabled( tr, pref_flag_get( key ) );
    }
    else if( !strcmp( key, TR_PREFS_KEY_RPC_USERNAME ) )
    {
        const char * s = pref_string_get( key );
        tr_sessionSetRPCUsername( tr, s );
    }
    else if( !strcmp( key, TR_PREFS_KEY_RPC_PASSWORD ) )
    {
        const char * s = pref_string_get( key );
        tr_sessionSetRPCPassword( tr, s );
    }
    else if( !strcmp( key, TR_PREFS_KEY_RPC_AUTH_REQUIRED ) )
    {
        const gboolean enabled = pref_flag_get( key );
        tr_sessionSetRPCPasswordEnabled( tr, enabled );
    }
    else if( !strcmp( key, TR_PREFS_KEY_PROXY ) )
    {
        const char * s = pref_string_get( key );
        tr_sessionSetProxy( tr, s );
    }
    else if( !strcmp( key, TR_PREFS_KEY_PROXY_TYPE ) )
    {
        const int i = pref_int_get( key );
        tr_sessionSetProxyType( tr, i );
    }
    else if( !strcmp( key, TR_PREFS_KEY_PROXY_ENABLED ) )
    {
        const gboolean enabled = pref_flag_get( key );
        tr_sessionSetProxyEnabled( tr, enabled );
    }
    else if( !strcmp( key, TR_PREFS_KEY_PROXY_AUTH_ENABLED ) )
    {
        const gboolean enabled = pref_flag_get( key );
        tr_sessionSetProxyAuthEnabled( tr, enabled );
    }
    else if( !strcmp( key, TR_PREFS_KEY_PROXY_USERNAME ) )
    {
        const char * s = pref_string_get( key );
        tr_sessionSetProxyUsername( tr, s );
    }
    else if( !strcmp( key, TR_PREFS_KEY_PROXY_PASSWORD ) )
    {
        const char * s = pref_string_get( key );
        tr_sessionSetProxyPassword( tr, s );
    }
    else if( !strcmp( key, TR_PREFS_KEY_PROXY_PORT ) )
    {
        tr_sessionSetProxyPort( tr, pref_int_get( key ) );
    }
    else if( !strcmp( key, TR_PREFS_KEY_RPC_PASSWORD ) )
    {
        const char * s = pref_string_get( key );
        tr_sessionSetProxyPassword( tr, s );
    }
}

static gboolean
updatemodel( gpointer gdata )
{
    struct cbdata *data = gdata;
    const gboolean done = data->isClosing || global_sigcount;

    if( !done )
    {
        /* update the torrent data in the model */
        tr_core_update( data->core );

        /* update the main window's statusbar and toolbar buttons */
        if( data->wind )
            tr_window_update( data->wind );

        /* update the actions */
        refreshTorrentActions( data );
    }

    return !done;
}

static void
aboutDialogActivateLink( GtkAboutDialog * dialog    UNUSED,
                         const gchar *              link_,
                         gpointer         user_data UNUSED )
{
    gtr_open_file( link_ );
}

static void
about( GtkWindow * parent )
{
    const char *authors[] =
    {
        "Charles Kerr (Backend; GTK+)",
        "Mitchell Livingston (Backend; OS X)",
        "Eric Petit (Backend; OS X)",
        "Josh Elsasser (Daemon; Backend; GTK+)",
        "Bryan Varner (BeOS)",
        NULL
    };

    const char *website_url = "http://www.transmissionbt.com/";

    gtk_about_dialog_set_url_hook( aboutDialogActivateLink, NULL, NULL );

    gtk_show_about_dialog( parent,
                           "name", g_get_application_name( ),
                           "comments",
                           _( "A fast and easy BitTorrent client" ),
                           "version", LONG_VERSION_STRING,
                           "website", website_url,
                           "website-label", website_url,
                           "copyright",
                           _( "Copyright 2005-2009 The Transmission Project" ),
                           "logo-icon-name", MY_NAME,
#ifdef SHOW_LICENSE
                           "license", LICENSE,
                           "wrap-license", TRUE,
#endif
                           "authors", authors,
                           /* Translators: translate "translator-credits" as
                              your name
                              to have it appear in the credits in the "About"
                              dialog */
                           "translator-credits", _( "translator-credits" ),
                           NULL );
}

static void
startTorrentForeach( GtkTreeModel *      model,
                     GtkTreePath  * path UNUSED,
                     GtkTreeIter *       iter,
                     gpointer       data UNUSED )
{
    tr_torrent * tor = NULL;

    gtk_tree_model_get( model, iter, MC_TORRENT_RAW, &tor, -1 );
    tr_torrentStart( tor );
}

static void
stopTorrentForeach( GtkTreeModel *      model,
                    GtkTreePath  * path UNUSED,
                    GtkTreeIter *       iter,
                    gpointer       data UNUSED )
{
    tr_torrent * tor = NULL;

    gtk_tree_model_get( model, iter, MC_TORRENT_RAW, &tor, -1 );
    tr_torrentStop( tor );
}

static void
updateTrackerForeach( GtkTreeModel *      model,
                      GtkTreePath  * path UNUSED,
                      GtkTreeIter *       iter,
                      gpointer       data UNUSED )
{
    tr_torrent * tor = NULL;

    gtk_tree_model_get( model, iter, MC_TORRENT_RAW, &tor, -1 );
    tr_torrentManualUpdate( tor );
}

static void
detailsClosed( gpointer  user_data,
               GObject * details )
{
    struct cbdata * data = user_data;
    gpointer        hashString = g_hash_table_lookup( data->details2tor,
                                                      details );

    g_hash_table_remove( data->details2tor, details );
    g_hash_table_remove( data->tor2details, hashString );
}

static void
openFolderForeach( GtkTreeModel *           model,
                   GtkTreePath  * path      UNUSED,
                   GtkTreeIter *            iter,
                   gpointer       user_data UNUSED )
{
    TrTorrent * gtor = NULL;

    gtk_tree_model_get( model, iter, MC_TORRENT, &gtor, -1 );
    tr_torrent_open_folder( gtor );
    g_object_unref( G_OBJECT( gtor ) );
}

static void
showInfoForeach( GtkTreeModel *      model,
                 GtkTreePath  * path UNUSED,
                 GtkTreeIter *       iter,
                 gpointer            user_data )
{
    const char *    hashString;
    struct cbdata * data = user_data;
    TrTorrent *     tor = NULL;
    GtkWidget *     w;

    gtk_tree_model_get( model, iter, MC_TORRENT, &tor, -1 );
    hashString = tr_torrent_info( tor )->hashString;
    w = g_hash_table_lookup( data->tor2details, hashString );
    if( w != NULL )
        gtk_window_present( GTK_WINDOW( w ) );
    else
    {
        w = torrent_inspector_new( GTK_WINDOW( data->wind ), data->core, tor );
        gtk_widget_show( w );
        g_hash_table_insert( data->tor2details, (gpointer)hashString, w );
        g_hash_table_insert( data->details2tor, w, (gpointer)hashString );
        g_object_weak_ref( G_OBJECT( w ), detailsClosed, data );
    }

    g_object_unref( G_OBJECT( tor ) );
}

static void
recheckTorrentForeach( GtkTreeModel *      model,
                       GtkTreePath  * path UNUSED,
                       GtkTreeIter *       iter,
                       gpointer       data UNUSED )
{
    TrTorrent * gtor = NULL;

    gtk_tree_model_get( model, iter, MC_TORRENT, &gtor, -1 );
    tr_torrentVerify( tr_torrent_handle( gtor ) );
    g_object_unref( G_OBJECT( gtor ) );
}

static gboolean
msgwinclosed( void )
{
    action_toggle( "toggle-message-log", FALSE );
    return FALSE;
}

static void
accumulateSelectedTorrents( GtkTreeModel *      model,
                            GtkTreePath  * path UNUSED,
                            GtkTreeIter *       iter,
                            gpointer            gdata )
{
    GSList **   data = ( GSList** ) gdata;
    TrTorrent * tor = NULL;

    gtk_tree_model_get( model, iter, MC_TORRENT, &tor, -1 );
    *data = g_slist_prepend( *data, tor );
}

static void
removeSelected( struct cbdata * data,
                gboolean        delete_files )
{
    GSList *           l = NULL;
    GtkTreeSelection * s = tr_window_get_selection( data->wind );

    gtk_tree_selection_selected_foreach( s, accumulateSelectedTorrents, &l );
    gtk_tree_selection_unselect_all( s );
    if( l )
    {
        l = g_slist_reverse( l );
        confirmRemove( data->wind, data->core, l, delete_files );
    }
}

static void
startAllTorrents( struct cbdata * data )
{
    tr_session * session = tr_core_session( data->core );
    const char * cmd = "{ \"method\": \"torrent-start\" }";
    tr_rpc_request_exec_json( session, cmd, strlen( cmd ), NULL, NULL );
}

static void
pauseAllTorrents( struct cbdata * data )
{
    tr_session * session = tr_core_session( data->core );
    const char * cmd = "{ \"method\": \"torrent-stop\" }";
    tr_rpc_request_exec_json( session, cmd, strlen( cmd ), NULL, NULL );
}

void
doAction( const char * action_name, gpointer user_data )
{
    struct cbdata * data = user_data;
    gboolean        changed = FALSE;

    if(  !strcmp( action_name, "add-torrent-menu" )
      || !strcmp( action_name, "add-torrent-toolbar" ) )
    {
        addDialog( data->wind, data->core );
    }
    else if( !strcmp( action_name, "show-stats" ) )
    {
        GtkWidget * dialog = stats_dialog_create( data->wind, data->core );
        gtk_widget_show( dialog );
    }
    else if( !strcmp( action_name, "start-torrent" ) )
    {
        GtkTreeSelection * s = tr_window_get_selection( data->wind );
        gtk_tree_selection_selected_foreach( s, startTorrentForeach, NULL );
        changed |= gtk_tree_selection_count_selected_rows( s ) != 0;
    }
    else if( !strcmp( action_name, "pause-all-torrents" ) )
    {
        pauseAllTorrents( data );
    }
    else if( !strcmp( action_name, "start-all-torrents" ) )
    {
        startAllTorrents( data );
    }
    else if( !strcmp( action_name, "pause-torrent" ) )
    {
        GtkTreeSelection * s = tr_window_get_selection( data->wind );
        gtk_tree_selection_selected_foreach( s, stopTorrentForeach, NULL );
        changed |= gtk_tree_selection_count_selected_rows( s ) != 0;
    }
    else if( !strcmp( action_name, "verify-torrent" ) )
    {
        GtkTreeSelection * s = tr_window_get_selection( data->wind );
        gtk_tree_selection_selected_foreach( s, recheckTorrentForeach, NULL );
        changed |= gtk_tree_selection_count_selected_rows( s ) != 0;
    }
    else if( !strcmp( action_name, "open-torrent-folder" ) )
    {
        GtkTreeSelection * s = tr_window_get_selection( data->wind );
        gtk_tree_selection_selected_foreach( s, openFolderForeach, data );
    }
    else if( !strcmp( action_name, "show-torrent-properties" ) )
    {
        GtkTreeSelection * s = tr_window_get_selection( data->wind );
        gtk_tree_selection_selected_foreach( s, showInfoForeach, data );
    }
    else if( !strcmp( action_name, "update-tracker" ) )
    {
        GtkTreeSelection * s = tr_window_get_selection( data->wind );
        gtk_tree_selection_selected_foreach( s, updateTrackerForeach,
                                             data->wind );
    }
    else if( !strcmp( action_name, "new-torrent" ) )
    {
        GtkWidget * w = make_meta_ui( GTK_WINDOW( data->wind ),
                                     tr_core_session( data->core ) );
        gtk_widget_show_all( w );
    }
    else if( !strcmp( action_name, "remove-torrent" ) )
    {
        removeSelected( data, FALSE );
    }
    else if( !strcmp( action_name, "delete-torrent" ) )
    {
        removeSelected( data, TRUE );
    }
    else if( !strcmp( action_name, "quit" ) )
    {
        askquit( data->core, data->wind, wannaquit, data );
    }
    else if( !strcmp( action_name, "select-all" ) )
    {
        GtkTreeSelection * s = tr_window_get_selection( data->wind );
        gtk_tree_selection_select_all( s );
    }
    else if( !strcmp( action_name, "deselect-all" ) )
    {
        GtkTreeSelection * s = tr_window_get_selection( data->wind );
        gtk_tree_selection_unselect_all( s );
    }
    else if( !strcmp( action_name, "edit-preferences" ) )
    {
        if( NULL == data->prefs )
        {
            data->prefs = tr_prefs_dialog_new( G_OBJECT( data->core ),
                                               data->wind );
            g_signal_connect( data->prefs, "destroy",
                              G_CALLBACK( gtk_widget_destroyed ), &data->prefs );
            gtk_widget_show( GTK_WIDGET( data->prefs ) );
        }
    }
    else if( !strcmp( action_name, "toggle-message-log" ) )
    {
        if( !data->msgwin )
        {
            GtkWidget * win = msgwin_new( data->core );
            g_signal_connect( win, "destroy", G_CALLBACK( msgwinclosed ),
                              NULL );
            data->msgwin = win;
        }
        else
        {
            action_toggle( "toggle-message-log", FALSE );
            gtk_widget_destroy( data->msgwin );
            data->msgwin = NULL;
        }
    }
    else if( !strcmp( action_name, "show-about-dialog" ) )
    {
        about( data->wind );
    }
    else if( !strcmp ( action_name, "help" ) )
    {
        char * url = gtr_get_help_url( );
        gtr_open_file( url );
        g_free( url );
    }
    else if( !strcmp( action_name, "toggle-main-window" ) )
    {
        toggleMainWindow( data, FALSE );
    }
    else if( !strcmp( action_name, "present-main-window" ) )
    {
        toggleMainWindow( data, TRUE );
    }
    else g_error ( "Unhandled action: %s", action_name );

    if( changed )
        updatemodel( data );
}

