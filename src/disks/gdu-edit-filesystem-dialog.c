/*
 * Copyright (C) 2008-2013 Red Hat, Inc.
 * Copyright (C) 2022 Purism SPC
 *
 * Licensed under GPL version 2 or later.
 *
 * Author(s):
 *   David Zeuthen <zeuthen@gmail.com>
 *   Mohammed Sadiq <sadiq@sadiqpk.org>
 */

#include "config.h"

#include <glib/gi18n.h>

#include "gduutils.h"
#include "gdu-block.h"
#include "gdu-edit-filesystem-dialog.h"

struct _GduEditFilesystemDialog
{
  AdwWindow          parent_instance;

  GtkWidget          *change_button;
  GtkWidget          *warning_banner;
  GtkWidget          *fs_label_row;

  GduBlock           *drive_block;
};

G_DEFINE_TYPE (GduEditFilesystemDialog, gdu_edit_filesystem_dialog, ADW_TYPE_WINDOW)

static gpointer
gdu_edit_filesystem_dialog_get_window (GduEditFilesystemDialog *self)
{
  return gtk_widget_get_ancestor (GTK_WIDGET (self), GTK_TYPE_WINDOW);
}

static void
change_filesystem_label_cb (GObject      *object,
                            GAsyncResult *result,
                            gpointer      user_data)
{
  g_autoptr(GduEditFilesystemDialog) self = user_data;
  g_autoptr(GError) error = NULL;

  g_assert (GDU_IS_EDIT_FILESYSTEM_DIALOG (self));

  if (!gdu_block_set_fs_label_finish (self->drive_block, result, &error))
    {
      gdu_utils_show_error (gdu_edit_filesystem_dialog_get_window (self),
                            _("Error setting label"),
                            error);
    }

  gtk_window_close (GTK_WINDOW (self));
}

static void
on_change_button_clicked (GduEditFilesystemDialog *self)
{
  const char *label;

  label = gtk_editable_get_text (GTK_EDITABLE (self->fs_label_row));

  gdu_block_set_fs_label_async (self->drive_block,
                                label,
                                change_filesystem_label_cb,
                                g_object_ref (self));
}

static void
on_fs_label_row_changed_cb (GduEditFilesystemDialog *self)
{
  GString *s;
  guint max_len;
  const char *fs_type;

  g_assert (GDU_IS_EDIT_FILESYSTEM_DIALOG (self));

  fs_type = gdu_block_get_fs_type (self->drive_block);
  max_len = gdu_utils_get_max_label_length (fs_type);
  s = g_string_new (gtk_editable_get_text (GTK_EDITABLE (self->fs_label_row)));

  g_string_truncate (s, max_len);
  gtk_editable_set_text (GTK_EDITABLE (self->fs_label_row),
                         g_string_free_and_steal (s));

  if (g_strcmp0 (gtk_editable_get_text (GTK_EDITABLE (self->fs_label_row)),
                 gdu_block_get_fs_label (self->drive_block)) == 0)
    {
      gtk_widget_add_css_class (GTK_WIDGET (self->fs_label_row), "error");
      gtk_widget_set_sensitive (GTK_WIDGET (self->change_button), FALSE);
      /* gtk4 todo: Notify the user that the label is the same as the current one */
    }
  else
    {
      gtk_widget_remove_css_class (GTK_WIDGET (self->fs_label_row), "error");
      gtk_widget_set_sensitive (GTK_WIDGET (self->change_button), TRUE);
    }
}

static void
gdu_edit_filesystem_dialog_finalize (GObject *object)
{
  GduEditFilesystemDialog *self = (GduEditFilesystemDialog *)object;

  g_clear_object (&self->drive_block);

  G_OBJECT_CLASS (gdu_edit_filesystem_dialog_parent_class)->finalize (object);
}

static void
gdu_edit_filesystem_dialog_class_init (GduEditFilesystemDialogClass *klass)
{
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = gdu_edit_filesystem_dialog_finalize;

  gtk_widget_class_set_template_from_resource (widget_class,
                                               "/org/gnome/DiskUtility/ui/"
                                               "gdu-edit-filesystem-dialog.ui");

  gtk_widget_class_bind_template_child (widget_class, GduEditFilesystemDialog, change_button);
  gtk_widget_class_bind_template_child (widget_class, GduEditFilesystemDialog, warning_banner);
  gtk_widget_class_bind_template_child (widget_class, GduEditFilesystemDialog, fs_label_row);

  gtk_widget_class_bind_template_callback (widget_class, on_change_button_clicked);
  gtk_widget_class_bind_template_callback (widget_class, on_fs_label_row_changed_cb);
}

static void
gdu_edit_filesystem_dialog_init (GduEditFilesystemDialog *self)
{
  gtk_widget_init_template (GTK_WIDGET (self));
}

void
gdu_edit_filesystem_dialog_show (GtkWindow    *parent_window,
                                 UDisksClient *client,
                                 UDisksObject *object)
{
  GduEditFilesystemDialog *self;
  const char *label;

  g_return_if_fail (UDISKS_IS_CLIENT (client));
  g_return_if_fail (UDISKS_IS_OBJECT (object));

  self = g_object_new (GDU_TYPE_EDIT_FILESYSTEM_DIALOG,
                       "transient-for", parent_window,
                       NULL);

  self->drive_block = gdu_block_new (client, object, NULL);

  label = gdu_block_get_fs_label (self->drive_block);
  gtk_editable_set_text (GTK_EDITABLE (self->fs_label_row), label);

  adw_banner_set_revealed (ADW_BANNER (self->warning_banner),
                           gdu_block_needs_unmount (self->drive_block));

  gtk_window_present (GTK_WINDOW (self));
}
