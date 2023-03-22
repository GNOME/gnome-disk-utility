/*
 * Author: Mohammed Sadiq <www.sadiqpk.org>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later OR CC0-1.0
 */

#pragma once

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define GTK_CONTAINER GTK_WIDGET

void        gtk_widget_destroy            (GtkWidget       *widget);
int         gtk_dialog_run                (GtkDialog       *dialog);
int         gtk_native_dialog_run         (GtkNativeDialog *dialog);
const char *gtk_entry_get_text            (gpointer         entry);
void        gtk_entry_set_text            (gpointer         entry,
                                           const char      *text);
gboolean    gtk_window_has_toplevel_focus (GtkWindow       *window);
void        gtk_container_add             (gpointer         container,
                                           GtkWidget       *widget);
void        gtk_container_remove          (gpointer         container,
                                           GtkWidget       *widget);
char       *gtk_file_chooser_get_filename (GtkFileChooser  *chooser);
gboolean    gtk_file_chooser_set_filename (GtkFileChooser  *chooser,
                                           const char      *path);
gboolean    gtk_show_uri_on_window        (GtkWindow       *parent,
                                           const char      *uri,
                                           guint32          timestamp,
                                           GError         **error);
GList      *gtk_container_get_children    (gpointer         container);
void        gtk_widget_show_all           (GtkWidget       *widget);

G_END_DECLS
