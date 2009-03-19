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

#ifndef TR_GTK_ADD_DIALOG_H
#define TR_GTK_ADD_DIALOG_H

#include <gtk/gtk.h>
#include "tr-core.h"

/* This dialog assumes ownership of the ctor */
GtkWidget* addSingleTorrentDialog( GtkWindow * parent,
                                   TrCore *    core,
                                   tr_ctor *   ctor );

GtkWidget* addDialog( GtkWindow * parent,
                      TrCore *    core );

#endif /* TR_GTK_ADD_DIALOG */
