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
  GtkStack                  *main_stack;
  GtkListBox                *drives_listbox;
  GduDriveView              *drive_view;

  GtkFileDialog             *loop_file_chooser;
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
  if (!row)
    gtk_stack_set_visible_child_name (self->main_stack, "empty_page");
  else
    gtk_stack_set_visible_child_name (self->main_stack, "drive_page");

  if (row)
    gdu_drive_view_set_drive (self->drive_view, gdu_drive_row_get_drive (row));
}

static void
loop_open_cb (GObject      *object,
              GAsyncResult *result,
              gpointer      user_data)
{
  g_autoptr(GduWindow) self = user_data;
  g_autoptr(GError) error = NULL;

  gdu_manager_open_loop_finish (self->manager, result, &error);
  
  if (error)
    {
      gdu_utils_show_error (GTK_WINDOW (self),
                            _("Error attaching disk image"),
                            error);
    }
}


static void
file_dialog_open_cb (GObject      *source_object,
                     GAsyncResult *res,
                     gpointer      user_data)
{
  GduWindow *self = GDU_WINDOW (user_data);
  GtkFileDialog *file_dialog = GTK_FILE_DIALOG (source_object);
  GFile *file = NULL;

  g_assert (GDU_IS_WINDOW (self));


  file = gtk_file_dialog_open_finish (file_dialog, res, NULL);
  
  if (!file)
    return;

  gdu_manager_open_loop_async (self->manager,
                               file,
                               TRUE,
                               loop_open_cb,
                               g_object_ref (self));
}

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
  gtk_widget_class_bind_template_child (widget_class, GduWindow, main_stack);
  gtk_widget_class_bind_template_child (widget_class, GduWindow, drives_listbox);
  gtk_widget_class_bind_template_child (widget_class, GduWindow, drive_view);

  gtk_widget_class_bind_template_child (widget_class, GduWindow, readonly_check_button);

  gtk_widget_class_bind_template_callback (widget_class, drive_list_row_selection_changed_cb);
}

static void
gdu_window_init (GduWindow *self)
{
  gtk_widget_init_template (GTK_WIDGET (self));
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
  GtkFileDialog *dialog;
  g_return_if_fail (GDU_IS_WINDOW (self));

  dialog = gtk_file_dialog_new ();
  gtk_file_dialog_set_title (dialog, _("Select a Disk Image to Attach"));
  gtk_file_dialog_set_modal (dialog, TRUE);

  gdu_utils_configure_file_dialog_for_disk_images (dialog,
                                                   TRUE,   /* set file types */
                                                   FALSE); /* allow_compressed */

  gtk_file_dialog_open (dialog,
                        GTK_WINDOW (self),
                        NULL,
                        file_dialog_open_cb,
                        self);
}
