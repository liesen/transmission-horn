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

#include <string.h>
#include <glib/gi18n.h>
#include <gtk/gtk.h>
#include <libtransmission/transmission.h>
#include "actions.h"
#include "conf.h"
#include "tr-core.h"
#include "tr-prefs.h"
#include "lock.h"
#include "logo.h"

#define UNUSED G_GNUC_UNUSED

static TrCore *         myCore = NULL;

static GtkActionGroup * myGroup = NULL;

static void
action_cb( GtkAction * a,
           gpointer    user_data )
{
    doAction ( gtk_action_get_name( a ), user_data );
}

#if !GTK_CHECK_VERSION( 2, 8, 0 )
 #define GTK_STOCK_INFO GTK_STOCK_PROPERTIES
#endif

#if !GTK_CHECK_VERSION( 2, 10, 0 )
 #define GTK_STOCK_SELECT_ALL NULL
#endif

static GtkRadioActionEntry sort_radio_entries[] =
{
    { "sort-by-activity", NULL, N_( "Sort by _Activity" ), NULL, NULL, 0 },
    { "sort-by-name",     NULL, N_( "Sort by _Name" ),     NULL, NULL, 1 },
    { "sort-by-progress", NULL, N_( "Sort by _Progress" ), NULL, NULL, 2 },
    { "sort-by-ratio",    NULL, N_( "Sort by _Ratio" ),    NULL, NULL, 3 },
    { "sort-by-state",    NULL, N_( "Sort by _State" ),    NULL, NULL, 4 },
    { "sort-by-tracker",  NULL, N_( "Sort by _Tracker" ),  NULL, NULL, 5 },
    { "sort-by-age",      NULL, N_( "Sort by A_ge" ),      NULL, NULL, 6 },
    { "sort-by-eta",      NULL, N_( "Sort by _ETA" ),      NULL, NULL, 7 },
    { "sort-by-size",     NULL, N_( "Sort by Si_ze" ),     NULL, NULL, 8 }
};

static void
sort_changed_cb( GtkAction            * action UNUSED,
                 GtkRadioAction *              current,
                 gpointer user_data            UNUSED )
{
    const char * key = PREF_KEY_SORT_MODE;
    const int    i = gtk_radio_action_get_current_value( current );
    const char * val = sort_radio_entries[i].name;

    tr_core_set_pref( myCore, key, val );
}

static GtkToggleActionEntry show_toggle_entries[] =
{
    { "toggle-main-window", NULL,
      N_( "_Main Window" ), NULL, NULL, G_CALLBACK( action_cb ), TRUE },
    { "toggle-message-log", NULL,
      N_( "Message _Log" ), NULL, NULL, G_CALLBACK( action_cb ), FALSE }
};

static void
toggle_pref_cb( GtkToggleAction *  action,
                gpointer user_data UNUSED )
{
    const char *   key = gtk_action_get_name( GTK_ACTION( action ) );
    const gboolean val = gtk_toggle_action_get_active( action );

    tr_core_set_pref_bool( myCore, key, val );
}

static GtkToggleActionEntry  pref_toggle_entries[] =
{
    { "minimal-view",          NULL,
      N_( "_Minimal View" ), "<alt>M", NULL, G_CALLBACK( toggle_pref_cb ),
      FALSE },
    { "sort-reversed",         NULL,
      N_( "_Reverse Sort Order" ), NULL, NULL, G_CALLBACK( toggle_pref_cb ),
      FALSE },
    { "show-filterbar",        NULL,
      N_( "_Filterbar" ), NULL, NULL, G_CALLBACK( toggle_pref_cb ), FALSE },
    { "show-statusbar",        NULL,
      N_( "_Statusbar" ), NULL, NULL, G_CALLBACK( toggle_pref_cb ), FALSE },
    { "show-toolbar",          NULL,
      N_( "_Toolbar" ), NULL, NULL, G_CALLBACK( toggle_pref_cb ), FALSE }
};

static GtkActionEntry        entries[] =
{
    { "torrent-menu", NULL, N_( "_Torrent" ), NULL, NULL, NULL  },
    { "view-menu", NULL, N_( "_View" ), NULL, NULL, NULL  },
    { "sort-menu", NULL, N_( "_Sort Torrents By" ), NULL, NULL, NULL },
    { "edit-menu", NULL, N_( "_Edit" ), NULL, NULL, NULL },
    { "help-menu", NULL, N_( "_Help" ), NULL, NULL, NULL },
    { "add-torrent-toolbar",  GTK_STOCK_ADD, NULL, NULL, N_( "Add a torrent" ),  G_CALLBACK( action_cb ) },
    { "add-torrent-menu", GTK_STOCK_ADD, N_( "_Add..." ), "<control>D", N_( "Add a torrent" ), G_CALLBACK( action_cb ) },
    { "start-torrent", GTK_STOCK_MEDIA_PLAY, N_( "_Start" ), "<control>S", N_( "Start torrent" ), G_CALLBACK( action_cb ) },
    { "show-stats", NULL, N_( "_Statistics" ), NULL, NULL, G_CALLBACK( action_cb ) },
    { "verify-torrent", NULL, N_( "_Verify Local Data" ), NULL, NULL, G_CALLBACK( action_cb ) },
    { "pause-torrent", GTK_STOCK_MEDIA_PAUSE, N_( "_Pause" ), "<control>P", N_( "Pause torrent" ), G_CALLBACK( action_cb ) },
    { "pause-all-torrents", GTK_STOCK_MEDIA_PAUSE, N_( "_Pause All" ), NULL, N_( "Pause all torrents" ), G_CALLBACK( action_cb ) },
    { "start-all-torrents", GTK_STOCK_MEDIA_PLAY, N_( "_Start All" ), NULL, N_( "Start all torrents" ), G_CALLBACK( action_cb ) },
    { "remove-torrent", GTK_STOCK_REMOVE, NULL, "Delete", N_( "Remove torrent" ), G_CALLBACK( action_cb ) },
    { "delete-torrent", GTK_STOCK_DELETE, N_( "_Delete Files and Remove" ), "<shift>Delete", NULL, G_CALLBACK( action_cb ) },
    { "new-torrent", GTK_STOCK_NEW, N_( "_New..." ), NULL, N_( "Create a torrent" ), G_CALLBACK( action_cb ) },
    { "quit", GTK_STOCK_QUIT, N_( "_Quit" ), NULL, NULL, G_CALLBACK( action_cb ) },
    { "select-all", GTK_STOCK_SELECT_ALL, N_( "Select _All" ), "<control>A", NULL, G_CALLBACK( action_cb ) },
    { "deselect-all", NULL, N_( "Dese_lect All" ), "<shift><control>A", NULL, G_CALLBACK( action_cb ) },
    { "edit-preferences", GTK_STOCK_PREFERENCES, NULL, NULL, NULL, G_CALLBACK( action_cb ) },
    { "show-torrent-properties", GTK_STOCK_PROPERTIES, NULL, "<alt>Return", N_( "Torrent properties" ), G_CALLBACK( action_cb ) },
    { "open-torrent-folder",  GTK_STOCK_OPEN, N_( "_Open Folder" ), NULL, NULL, G_CALLBACK( action_cb ) },
    { "show-about-dialog", GTK_STOCK_ABOUT, NULL, NULL, NULL, G_CALLBACK( action_cb ) },
    { "help", GTK_STOCK_HELP, N_( "_Contents" ), "F1", NULL, G_CALLBACK( action_cb ) },
    { "update-tracker", GTK_STOCK_NETWORK, N_( "Ask Tracker for _More Peers" ), NULL, NULL, G_CALLBACK( action_cb ) },
    { "present-main-window", NULL, NULL, NULL, NULL, G_CALLBACK( action_cb ) },
};

typedef struct
{
    const guint8*   raw;
    const char *    name;
}
BuiltinIconInfo;

static const BuiltinIconInfo my_fallback_icons[] =
{
    { tr_icon_logo, "transmission"      },
    { tr_icon_lock, "transmission-lock" }
};

static void
register_my_icons( void )
{
    int              i;
    const int        n = G_N_ELEMENTS( my_fallback_icons );
    GtkIconFactory * factory = gtk_icon_factory_new( );
    GtkIconTheme *   theme = gtk_icon_theme_get_default( );

    gtk_icon_factory_add_default( factory );

    for( i = 0; i < n; ++i )
    {
        const char * name = my_fallback_icons[i].name;

        if( !gtk_icon_theme_has_icon( theme, name ) )
        {
            int          width;
            GdkPixbuf *  p;
            GtkIconSet * icon_set;

            p =
                gdk_pixbuf_new_from_inline( -1, my_fallback_icons[i].raw,
                                            FALSE,
                                            NULL );
            width = gdk_pixbuf_get_width( p );
            icon_set = gtk_icon_set_new_from_pixbuf( p );
            gtk_icon_theme_add_builtin_icon( name, width, p );
            gtk_icon_factory_add( factory, name, icon_set );

            g_object_unref( p );
            gtk_icon_set_unref( icon_set );
        }
    }

    g_object_unref ( G_OBJECT ( factory ) );
}

static GtkUIManager * myUIManager = NULL;

void
actions_set_core( TrCore * core )
{
    myCore = core;
}

void
actions_init( GtkUIManager * ui_manager,
              gpointer       callback_user_data )
{
    int              i, n;
    int              active;
    const char *     match;
    const int        n_entries = G_N_ELEMENTS( entries );
    GtkActionGroup * action_group;

    myUIManager = ui_manager;

    register_my_icons( );

    action_group = myGroup = gtk_action_group_new( "Actions" );
    gtk_action_group_set_translation_domain( action_group, NULL );


    match = pref_string_get( PREF_KEY_SORT_MODE );
    for( i = 0, n = G_N_ELEMENTS( sort_radio_entries ), active = -1;
         active == -1 && i < n; ++i )
        if( !strcmp( sort_radio_entries[i].name, match ) )
            active = i;

    gtk_action_group_add_radio_actions( action_group,
                                        sort_radio_entries,
                                        G_N_ELEMENTS( sort_radio_entries ),
                                        active,
                                        G_CALLBACK( sort_changed_cb ),
                                        NULL );

    gtk_action_group_add_toggle_actions( action_group,
                                         show_toggle_entries,
                                         G_N_ELEMENTS( show_toggle_entries ),
                                         callback_user_data );

    for( i = 0, n = G_N_ELEMENTS( pref_toggle_entries ); i < n; ++i )
        pref_toggle_entries[i].is_active =
            pref_flag_get( pref_toggle_entries[i].name );

    gtk_action_group_add_toggle_actions( action_group,
                                         pref_toggle_entries,
                                         G_N_ELEMENTS( pref_toggle_entries ),
                                         callback_user_data );

    gtk_action_group_add_actions( action_group,
                                  entries, n_entries,
                                  callback_user_data );

    gtk_ui_manager_insert_action_group( ui_manager, action_group, 0 );
    g_object_unref ( G_OBJECT( action_group ) );
}

/****
*****
****/

static GHashTable * key_to_action = NULL;

static void
ensure_action_map_loaded( GtkUIManager * uim )
{
    GList * l;

    if( key_to_action != NULL )
        return;

    key_to_action =
        g_hash_table_new_full( g_str_hash, g_str_equal, g_free, NULL );

    for( l = gtk_ui_manager_get_action_groups( uim ); l != NULL;
         l = l->next )
    {
        GtkActionGroup * action_group = GTK_ACTION_GROUP( l->data );
        GList *          ait, *actions = gtk_action_group_list_actions(
            action_group );
        for( ait = actions; ait != NULL; ait = ait->next )
        {
            GtkAction *  action = GTK_ACTION( ait->data );
            const char * name = gtk_action_get_name( action );
            g_hash_table_insert( key_to_action, g_strdup( name ), action );
        }
        g_list_free( actions );
    }
}

static GtkAction*
get_action( const char* name )
{
    ensure_action_map_loaded( myUIManager );
    return ( GtkAction* ) g_hash_table_lookup( key_to_action, name );
}

void
action_activate( const char * name )
{
    GtkAction * action = get_action( name );

    g_assert( action != NULL );
    gtk_action_activate( action );
}

void
action_sensitize( const char * name,
                  gboolean     b )
{
    GtkAction * action = get_action( name );

    g_assert( action != NULL );
    g_object_set( action, "sensitive", b, NULL );
}

void
action_toggle( const char * name,
               gboolean     b )
{
    GtkAction * action = get_action( name );

    gtk_toggle_action_set_active( GTK_TOGGLE_ACTION( action ), b );
}

GtkWidget*
action_get_widget( const char * path )
{
    return gtk_ui_manager_get_widget( myUIManager, path );
}

