/* gdu-window.c
 *
 * Copyright (C) 2008-2013 Red Hat, Inc.
 * Copyright 2023 Mohammed Sadiq <sadiq@sadiqpk.org>
 *
 * Licensed under GPL version 2 or later.
 *
 * Author(s):
 *   David Zeuthen <zeuthen@gmail.com>
 *   Mohammed Sadiq <sadiq@sadiqpk.org>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#define G_LOG_DOMAIN "gdu-window"

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <glib/gi18n.h>

#include "gduutils.h"
#include "gdu-new-disk-image-dialog.h"
#include "gdu-drive-row.h"
#include "gdu-drive-view.h"
#include "gdu-window.h"

struct _GduWindow
{
  AdwApplicationWindow       parent_instance;

  AdwOverlaySplitView       *split_view;
  GtkListBox                *drives_listbox;
  GduDriveView              *drive_view;

  // GtkFileChooserDialog      *loop_file_chooser;
  GtkCheckButton            *readonly_check_button;

  GduManager                *manager;
};


G_DEFINE_TYPE (GduWindow, gdu_window, ADW_TYPE_APPLICATION_WINDOW)

static void
drive_list_row_selection_changed_cb (GduWindow *self)
{
  GduDriveRow *row;

  g_assert (GDU_IS_WINDOW (self));

  row = (gpointer)gtk_list_box_get_selected_row (self->drives_listbox);
  gdu_drive_view_set_drive (self->drive_view, row ? gdu_drive_row_get_drive (row) : NULL);

  if (row)
    hdy_leaflet_navigate (self->main_leaflet, HDY_NAVIGATION_DIRECTION_FORWARD);
  else
    hdy_leaflet_navigate (self->main_leaflet, HDY_NAVIGATION_DIRECTION_BACK);
}

static void
loop_open_cb (GObject      *object,
              GAsyncResult *result,
              gpointer      user_data)
{
  g_autoptr(GduWindow) self = user_data;
  g_autoptr(GError) error = NULL;

  if (gdu_manager_open_loop_finish (self->manager, result, &error))
    {
      g_autoptr(GFile) folder = NULL;

      /* now that we know the user picked a folder, update file chooser settings */
      /* gtk4 todo: Update to GtkFileDialog
      folder = gtk_file_chooser_get_current_folder_file (GTK_FILE_CHOOSER (self->loop_file_chooser));
      gdu_utils_file_chooser_for_disk_images_set_default_folder (folder);
      */
    }
  else if (error)
    {
      gdu_utils_show_error (GTK_WINDOW (self),
                            _("Error attaching disk image"),
                            error);
    }
}

/*
static void
loop_file_chooser_response_cb (GduWindow *self,
                               int         response)
{
  g_autofree char *file_name = NULL;
  gboolean read_only;

  g_assert (GDU_IS_WINDOW (self));

  gtk_widget_set_visible (GTK_WIDGET (self->loop_file_chooser), FALSE);

  if (response != GTK_RESPONSE_ACCEPT)
    return;

  // file_name = gtk_file_chooser_get_filename (GTK_FILE_CHOOSER (self->loop_file_chooser)); gtk4 todo
  read_only = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (self->readonly_check_button));

  gdu_manager_open_loop_async (self->manager, file_name, read_only,
                               loop_open_cb,
                               g_object_ref (self));
}
*/

static void
gdu_window_finalize (GObject *object)
{
  GduWindow *self = (GduWindow *)object;

  g_clear_object (&self->manager);

  G_OBJECT_CLASS (gdu_window_parent_class)->finalize (object);
}

static void
gdu_window_class_init (GduWindowClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->finalize = gdu_window_finalize;

  gtk_widget_class_set_template_from_resource (widget_class,
                                               "/org/gnome/DiskUtility/ui/"
                                               "gdu-window.ui");

  gtk_widget_class_bind_template_child (widget_class, GduWindow, split_view);
  gtk_widget_class_bind_template_child (widget_class, GduWindow, drives_listbox);
  gtk_widget_class_bind_template_child (widget_class, GduWindow, drive_view);

  // gtk_widget_class_bind_template_child (widget_class, GduWindow, loop_file_chooser);
  gtk_widget_class_bind_template_child (widget_class, GduWindow, readonly_check_button);

  gtk_widget_class_bind_template_callback (widget_class, drive_list_row_selection_changed_cb);
  // gtk_widget_class_bind_template_callback (widget_class, loop_file_chooser_response_cb);
}

static void
gdu_window_init (GduWindow *self)
{
  gtk_widget_init_template (GTK_WIDGET (self));
  // gtk4 todo
  // gdu_utils_configure_file_chooser_for_disk_images (GTK_FILE_CHOOSER (self->loop_file_chooser),
  //                                                   TRUE,   /* set file types */
  //                                                   FALSE); /* allow_compressed */
}

GduWindow *
gdu_window_new (GApplication *application,
                 GduManager   *manager)
{
  GduWindow *self;
  GListModel *drives;

  g_return_val_if_fail (G_IS_APPLICATION (application), NULL);
  g_return_val_if_fail (GDU_IS_MANAGER (manager), NULL);

  self = g_object_new (GDU_TYPE_WINDOW, "application", application, NULL);

  self->manager = g_object_ref (manager);
  drives = gdu_manager_get_drives (manager);

  gtk_list_box_bind_model(self->drives_listbox,
                          drives,
                          (GtkListBoxCreateWidgetFunc)gdu_drive_row_new,
                          NULL, NULL);

  return self;
}

void
gdu_window_show_attach_disk_image (GduWindow *self)
{
  g_return_if_fail (GDU_IS_WINDOW (self));

/*
  gtk_window_present (GTK_WINDOW (self->loop_file_chooser));
*/
}

void
gdu_window_show_new_disk_image (GduWindow *self)
{
  GtkWindow *dialog;

  g_return_if_fail (GDU_IS_WINDOW (self));

  dialog = g_object_new (GDU_TYPE_NEW_DISK_IMAGE_DIALOG, NULL);
  gtk_window_set_transient_for (dialog, GTK_WINDOW (self));

  gtk_window_present (dialog);
}
