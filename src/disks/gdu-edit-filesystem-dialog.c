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
#include "gdu-application.h"
#include "gdu-edit-filesystem-dialog.h"
#include "gduvolumegrid.h"

struct _GduFilesystemDialog
{
  GtkDialog          parent_instance;

  GtkLabel          *warning_label;
  GtkEntry          *filesystem_label_entry;

  GtkWindow         *parent_window;
  GduBlock *drive_block;
};

G_DEFINE_TYPE (GduFilesystemDialog, gdu_filesystem_dialog, GTK_TYPE_DIALOG)

static void
change_filesystem_label_cb (GObject      *object,
                            GAsyncResult *result,
                            gpointer      user_data)
{
  g_autoptr(GduFilesystemDialog) self = user_data;
  g_autoptr(GError) error = NULL;

  g_assert (GDU_IS_FILESYSTEM_DIALOG (self));

  if (!gdu_block_set_fs_label_finish (self->drive_block, result, &error))
    {
      gdu_utils_show_error (self->parent_window,
                            _("Error setting label"),
                            error);
    }
}

static void
filesystem_dialog_response_cb (GduFilesystemDialog *self,
                               int                  response_id)
{
  const char *label;

  if (response_id != GTK_RESPONSE_OK)
    {
      gtk_widget_hide (GTK_WIDGET (self));
      gtk_widget_destroy (GTK_WIDGET (self));
      return;
    }

  label = gtk_entry_get_text (self->filesystem_label_entry);

  gdu_block_set_fs_label_async (self->drive_block,
                                          label,
                                          change_filesystem_label_cb,
                                          g_object_ref (self));
}

static void
filesystem_label_entry_changed_cb (GduFilesystemDialog *self,
                                   GtkEntry            *entry)
{
  gboolean sensitive = FALSE;

  g_assert (GDU_IS_FILESYSTEM_DIALOG (self));

  gtk_entry_set_icon_from_icon_name (entry,
                                     GTK_ENTRY_ICON_SECONDARY,
                                     NULL);
  gtk_entry_set_icon_tooltip_text (entry,
                                   GTK_ENTRY_ICON_SECONDARY,
                                   NULL);

  if (g_strcmp0 (gtk_entry_get_text (entry),
                 gdu_block_get_fs_label (self->drive_block)) != 0)
    {
      sensitive = TRUE;
    }
  else
    {
      gtk_entry_set_icon_from_icon_name (entry,
                                         GTK_ENTRY_ICON_SECONDARY,
                                         "dialog-warning-symbolic");
      gtk_entry_set_icon_tooltip_text (entry,
                                       GTK_ENTRY_ICON_SECONDARY,
                                       _("The label matches the existing label"));
    }

  gtk_dialog_set_response_sensitive (GTK_DIALOG (self),
                                     GTK_RESPONSE_OK,
                                     sensitive);
}

static void
gdu_filesystem_dialog_finalize (GObject *object)
{
  GduFilesystemDialog *self = (GduFilesystemDialog *)object;

  g_clear_object (&self->drive_block);

  G_OBJECT_CLASS (gdu_filesystem_dialog_parent_class)->finalize (object);
}

static void
gdu_filesystem_dialog_class_init (GduFilesystemDialogClass *klass)
{
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = gdu_filesystem_dialog_finalize;

  gtk_widget_class_set_template_from_resource (widget_class,
                                               "/org/gnome/Disks/"
                                               "ui/gdu-edit-filesystem-dialog.ui");

  gtk_widget_class_bind_template_child (widget_class, GduFilesystemDialog, warning_label);
  gtk_widget_class_bind_template_child (widget_class, GduFilesystemDialog, filesystem_label_entry);

  gtk_widget_class_bind_template_callback (widget_class, filesystem_dialog_response_cb);
  gtk_widget_class_bind_template_callback (widget_class, filesystem_label_entry_changed_cb);
}

static void
gdu_filesystem_dialog_init (GduFilesystemDialog *self)
{
  gtk_widget_init_template (GTK_WIDGET (self));
}

static GduFilesystemDialog *
gdu_filesystem_dialog_new (void)
{
  return g_object_new (GDU_TYPE_FILESYSTEM_DIALOG, NULL);
}

static void
gdu_filesystem_dialog_set_drive (GduFilesystemDialog *self,
                                 GduBlock            *drive_partition)
{
  const char *fs_type, *label;

  fs_type = gdu_block_get_fs_type (drive_partition);
  label = gdu_block_get_fs_label (drive_partition);

  gtk_entry_set_text (GTK_ENTRY (self->filesystem_label_entry), label);
  gtk_editable_select_region (GTK_EDITABLE (self->filesystem_label_entry), 0, -1);
  gtk_entry_set_max_length (self->filesystem_label_entry,
                            gdu_utils_get_max_label_length (fs_type));

  gtk_widget_set_visible (GTK_WIDGET (self->warning_label),
                          gdu_block_needs_unmount (drive_partition));
}

void
gdu_filesystem_dialog_show (GtkWindow    *parent_window,
                            UDisksObject *object,
                            UDisksClient *client)
{
  GduFilesystemDialog *self;

  self = gdu_filesystem_dialog_new ();
  self->drive_block = gdu_block_new (client, object, NULL);
  gdu_filesystem_dialog_set_drive (self, self->drive_block);
  self->parent_window = parent_window;

  gtk_window_set_transient_for (GTK_WINDOW (self), parent_window);
  gtk_dialog_set_default_response (GTK_DIALOG (self), GTK_RESPONSE_OK);
  gtk_window_present (GTK_WINDOW (self));
}
