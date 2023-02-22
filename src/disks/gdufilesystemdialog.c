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
#include "gduapplication.h"
#include "gduwindow.h"
#include "gdufilesystemdialog.h"
#include "gduvolumegrid.h"

struct _GduFilesystemDialog
{
  GtkDialog         parent_instance;

  GtkLabel         *warning_label;
  GtkEntry         *filesystem_label_entry;

  GduWindow        *window;
  UDisksObject     *disk_object;
  UDisksFilesystem *filesystem;

  char             *old_label;
  gboolean          needs_unmount;
};

G_DEFINE_TYPE (GduFilesystemDialog, gdu_filesystem_dialog, GTK_TYPE_DIALOG)

static void
change_filesystem_label_cb (UDisksFilesystem  *filesystem,
                            GAsyncResult      *res,
                            gpointer           user_data)
{
  GduFilesystemDialog *self;
  g_autoptr(GTask) task = user_data;
  g_autoptr(GError) error = NULL;

  g_assert (G_IS_TASK (task));

  self = g_task_get_source_object (task);
  g_assert (GDU_IS_FILESYSTEM_DIALOG (self));

  if (!udisks_filesystem_call_set_label_finish (filesystem,
                                                res,
                                                &error))
    {
      gdu_utils_show_error (GTK_WINDOW (self->window),
                            _("Error setting label"),
                            error);
    }
}

static void
ensure_unused_cb (UDisksObject *object,
                  GAsyncResult *res,
                  gpointer      user_data)
{
  GduFilesystemDialog *self;
  g_autoptr(GTask) task = user_data;
  const char *label;

  g_assert (G_IS_TASK (task));

  self = g_task_get_source_object (task);
  label = g_task_get_task_data (task);
  g_assert (GDU_IS_FILESYSTEM_DIALOG (self));

  if (gdu_window_ensure_unused_finish (self->window, res, NULL))
    {
      udisks_filesystem_call_set_label (self->filesystem,
                                        label,
                                        g_variant_new ("a{sv}", NULL), /* options */
                                        NULL, /* cancellable */
                                        (GAsyncReadyCallback) change_filesystem_label_cb,
                                        g_steal_pointer (&task));
    }
}

static void
filesystem_dialog_response_cb (GduFilesystemDialog *self,
                               int                  response_id)
{
  g_autoptr(GTask) task = NULL;
  const char *label;

  if (response_id != GTK_RESPONSE_OK)
    goto end;

  label = gtk_entry_get_text (self->filesystem_label_entry);

  task = g_task_new (self, NULL, NULL, NULL);
  g_task_set_task_data (task, g_strdup (label), g_free);

  if (self->needs_unmount)
    {
      gdu_window_ensure_unused (self->window,
                                self->disk_object,
                                (GAsyncReadyCallback) ensure_unused_cb,
                                NULL, /* cancellable */
                                g_steal_pointer (&task));
    }
  else
    {
      udisks_filesystem_call_set_label (self->filesystem,
                                        label,
                                        g_variant_new ("a{sv}", NULL), /* options */
                                        NULL, /* cancellable */
                                        (GAsyncReadyCallback) change_filesystem_label_cb,
                                        g_steal_pointer (&task));
    }

 end:
  gtk_widget_hide (GTK_WIDGET (self));
  gtk_widget_destroy (GTK_WIDGET (self));
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

  if (g_strcmp0 (gtk_entry_get_text (entry), self->old_label) != 0)
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

  g_clear_pointer (&self->old_label, g_free);
  g_clear_object (&self->disk_object);
  g_clear_object (&self->filesystem);

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
                                               "ui/edit-filesystem-dialog.ui");

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
gdu_filesystem_dialog_set_udisks (GduFilesystemDialog *self,
                                  UDisksObject        *object)
{
  const char *const *mount_points;
  UDisksBlock *block;
  const char *fs_type;

  self->disk_object = g_object_ref (object);
  self->filesystem = udisks_object_get_filesystem (object);
  block = udisks_object_peek_block (object);
  g_assert (self->filesystem != NULL);
  g_assert (block != NULL);

  mount_points = udisks_filesystem_get_mount_points (self->filesystem);
  fs_type = udisks_block_get_id_type (block);
  self->old_label = udisks_block_dup_id_label (block);

  gtk_entry_set_text (GTK_ENTRY (self->filesystem_label_entry), self->old_label);
  gtk_editable_select_region (GTK_EDITABLE (self->filesystem_label_entry), 0, -1);
  gtk_entry_set_max_length (self->filesystem_label_entry,
                            gdu_utils_get_max_label_length (fs_type));

  /* Needs to unmount if there's at least one mount point ... */
  if (mount_points && *mount_points)
    self->needs_unmount = TRUE;

  /* ... and if they are not ext2, ext3, nor ext4 */
  if (self->needs_unmount &&
      (g_strcmp0 (fs_type, "ext2") == 0 ||
       g_strcmp0 (fs_type, "ext3") == 0 ||
       g_strcmp0 (fs_type, "ext4") == 0))
    self->needs_unmount = FALSE;

  gtk_widget_set_visible (GTK_WIDGET (self->warning_label), self->needs_unmount);
}

void
gdu_filesystem_dialog_show (GduWindow    *window,
                            UDisksObject *object)
{
  GduFilesystemDialog *self;

  self = gdu_filesystem_dialog_new ();
  gdu_filesystem_dialog_set_udisks (self, object);
  self->window = window;

  gtk_window_set_transient_for (GTK_WINDOW (self), GTK_WINDOW (window));
  gtk_dialog_set_default_response (GTK_DIALOG (self), GTK_RESPONSE_OK);
  gtk_window_present (GTK_WINDOW (self));
}
