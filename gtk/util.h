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

#ifndef TG_UTIL_H
#define TG_UTIL_H

#include <sys/types.h>
#include <stdarg.h>

/* macro to shut up "unused parameter" warnings */
#ifndef UNUSED
 #define UNUSED G_GNUC_UNUSED
#endif

/* return number of items in array */
#define ALEN( a ) ( (signed)G_N_ELEMENTS( a ) )

/* used for a callback function with a data parameter */
typedef void ( *callbackfunc_t )( void* );

/* return a human-readable string for the size given in bytes. */
char*       tr_strlsize( char *   buf,
                         guint64  size,
                         size_t   buflen );

/* return a human-readable string for the transfer rate given in bytes. */
char*       tr_strlspeed( char * buf,
                          double KiBps,
                          size_t buflen );

/* return a human-readable string for the given ratio. */
char*       tr_strlratio( char * buf,
                          double ratio,
                          size_t buflen );

/* return a human-readable string for the time given in seconds. */
char*       tr_strltime( char * buf,
                         int    secs,
                         size_t buflen );

char*       gtr_localtime( time_t time );

/* create a directory and any missing parent directories */
int         mkdir_p( const char *name,
                     mode_t      mode );

/* create a copy of a GSList of strings, this dups the actual strings too */
GSList *    dupstrlist( GSList * list );

/* joins a GSList of strings into one string using an optional separator */
char *      joinstrlist( GSList *list,
                         char *  sep );

/* free a GSList of strings */
void        freestrlist( GSList *list );

/* decodes a string that has been urlencoded */
char *      decode_uri( const char * uri );

/* return a list of cleaned-up paths, with invalid directories removed */
GSList *    checkfilenames( int     argc,
                            char ** argv );

void        gtr_open_file( const char * path );

gboolean    gtr_dbus_add_torrent( const char * filename );

gboolean    gtr_dbus_present_window( void );

char*       gtr_get_help_url( void );

#ifdef GTK_MAJOR_VERSION

GtkWidget * gtr_button_new_from_stock( const char * stock,
                                       const char * mnemonic );

guint       gtr_timeout_add_seconds( guint       interval,
                                     GSourceFunc function,
                                     gpointer    data );

void        addTorrentErrorDialog( GtkWidget *  window_or_child,
                                   int          err,
                                   const char * filename );

/* create an error dialog, if wind is NULL or mapped then show dialog now,
   otherwise show it when wind becomes mapped */
void        errmsg( GtkWindow *  wind,
                    const char * format,
                    ... ) G_GNUC_PRINTF( 2, 3 );

/* create an error dialog but do not gtk_widget_show() it,
   calls func( data ) when the dialog is closed */
GtkWidget * errmsg_full( GtkWindow *    wind,
                         callbackfunc_t func,
                         void *         data,
                         const char *   format,
                         ... ) G_GNUC_PRINTF( 4, 5 );

/* pop up the context menu if a user right-clicks.
   if the row they right-click on isn't selected, select it. */
gboolean    on_tree_view_button_pressed( GtkWidget *      view,
                                         GdkEventButton * event,
                                         gpointer         unused );

/* if the click didn't specify a row, clear the selection */
gboolean    on_tree_view_button_released( GtkWidget *      view,
                                          GdkEventButton * event,
                                          gpointer         unused );



gpointer    tr_object_ref_sink( gpointer object );

int         tr_file_trash_or_remove( const char * filename );

#endif /* GTK_MAJOR_VERSION */

#endif /* TG_UTIL_H */
