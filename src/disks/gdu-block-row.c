/* gdu-block-row.c
 *
 * Copyright 2023 Mohammed Sadiq <sadiq@sadiqpk.org>
 * Copyright (C) 2008-2013 Red Hat, Inc.
 *
 * Author(s):
 *   David Zeuthen <zeuthen@gmail.com>
 *   Mohammed Sadiq <sadiq@sadiqpk.org>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#define G_LOG_DOMAIN "gdu-block-row"

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <glib/gi18n.h>
#include <udisks/udisks.h>

#include "gdu-manager.h"
#include "gdu-mount-options-dialog.h"
#include "gdu-benchmark-dialog.h"
#include "gdu-encryption-options-dialog.h"
#include "gdu-create-disk-image-dialog.h"
#include "gdu-restore-disk-image-dialog.h"
#include "gdu-format-volume-dialog.h"
#include "gdu-edit-filesystem-dialog.h"
#include "gdu-change-passphrase-dialog.h"
#include "gdu-edit-partition-dialog.h"
#include "gdu-resize-volume-dialog.h"
#include "gduutils.h"
#include "gdu-item.h"
#include "gdu-block-row.h"

/**
 * GduBlockRow:
 *
 * `GduBlockRow` represents user visible details of a `GduBlock` in GUI
 */

struct _GduBlockRow
{
  HdyExpanderRow  parent_instance;

  GtkImage       *partition_image;
  GtkLabel       *partition_depth_label;
  GtkLevelBar    *space_level_bar;

  GtkLabel       *size_label;
  GtkLabel       *device_id_label;
  GtkLabel       *uuid_label;
  GtkLabel       *partition_type_label;

  GtkButton      *format_partition_button;

  GtkButton      *edit_partition_button;
  GtkButton      *edit_filesystem_button;
  GtkButton      *change_passphrase_button;

  GtkButton      *resize_button;
  GtkButton      *check_fs_button;
  GtkButton      *repair_fs_button;
  GtkButton      *take_ownership_button;

  GtkButton      *configure_fstab_button;
  GtkButton      *configure_crypttab_button;

  GtkButton      *create_partition_image_button;
  GtkButton      *restore_partition_image_button;
  GtkButton      *benchmark_partition_button;

  UDisksClient   *client;
  GduBlock       *block;
};


G_DEFINE_TYPE (GduBlockRow, gdu_block_row, HDY_TYPE_EXPANDER_ROW)

static gpointer
block_row_get_window (GduBlockRow *self)
{
  return gtk_widget_get_ancestor (GTK_WIDGET (self), GTK_TYPE_WINDOW);
}

static gpointer
block_row_get_client (void)
{
  GduManager *manager;

  manager = gdu_manager_get_default (NULL);
  return gdu_manager_get_client (manager);
}

static void
update_block_row (GduBlockRow *self)
{
  const char *description, *partition, *uuid, *device_id;
  g_autofree char *size_str = NULL;
  const char *const *mount_points;
  GduItem *parent;
  GduFeature features;
  int depth = 0;

  g_assert (GDU_IS_BLOCK_ROW (self));

  description = gdu_item_get_description (GDU_ITEM (self->block));
  partition = gdu_item_get_partition_type (GDU_ITEM (self->block));
  uuid = gdu_block_get_uuid (self->block);
  device_id = gdu_block_get_device_id (self->block);
  size_str = gdu_block_get_size_str (self->block);
  features = gdu_item_get_features (GDU_ITEM (self->block));

  hdy_preferences_row_set_title (HDY_PREFERENCES_ROW (self), description);
  gtk_label_set_label (self->partition_type_label, partition);
  gtk_label_set_label (self->uuid_label, uuid);
  gtk_label_set_label (self->device_id_label, device_id);
  gtk_label_set_label (self->size_label, size_str);

  parent = gdu_item_get_parent (GDU_ITEM (self->block));

  while (parent != NULL)
    {
      depth++;
      parent = gdu_item_get_parent (parent);
    }

  /* Don't count the topmost parent, which is the drive */
  depth--;
  gtk_widget_set_visible (GTK_WIDGET (self->partition_depth_label), depth > 0);

  if (depth > 0)
    {
      g_autoptr(GString) partition_depth = NULL;

      partition_depth = g_string_new ("┗");

      while (--depth)
        g_string_append_len (partition_depth, "━", strlen ("━"));

      g_string_append_len (partition_depth, "╸", strlen ("╸"));
      gtk_label_set_label (self->partition_depth_label, partition_depth->str);
    }

  mount_points = gdu_block_get_mount_points (self->block);

  if (mount_points && mount_points[0])
    {
      g_autofree char *mount_point = NULL;
      guint64 size, free_space;

      gtk_widget_set_visible (GTK_WIDGET (self->space_level_bar), TRUE);

      size = gdu_item_get_size (GDU_ITEM (self->block));
      free_space = gdu_block_get_unused_size (self->block);

      gtk_level_bar_set_max_value (self->space_level_bar, size / 1000);
      gtk_level_bar_set_value (self->space_level_bar, (size - free_space) / 1000);

      /* gtk4 todo: once we move to Adwaita */
      /* todo: right now we only display the first mount point */
      /* if (g_strcmp0 (mount_points[0], "/") == 0) */
      /*   { */
      /*     /\* Translators: Use for mount point '/' simply because '/' is too small to hit as a hyperlink */
      /*      *\/ */
      /*     mount_point = g_strdup_printf ("<a href=\"file:///\">%s</a>", */
      /*                                    C_("volume-content-fs", "Filesystem Root")); */
      /*   } */
      /* else */
      /*   { */
      /*     mount_point = g_markup_printf_escaped ("<a href=\"file://%s\">%s</a>", */
      /*                                            mount_points[0], mount_points[0]); */
      /*   } */

      hdy_expander_row_set_subtitle (HDY_EXPANDER_ROW (self), mount_points[0]);
    }

#define ENABLE(_widget, _feature) gtk_widget_set_sensitive (GTK_WIDGET (_widget), features & _feature)

  ENABLE (self->format_partition_button, GDU_FEATURE_FORMAT);

  ENABLE (self->edit_partition_button, GDU_FEATURE_EDIT_PARTITION);
  ENABLE (self->edit_filesystem_button, GDU_FEATURE_EDIT_LABEL);
  ENABLE (self->change_passphrase_button, GDU_FEATURE_CHANGE_PASSPHRASE);

  ENABLE (self->resize_button, GDU_FEATURE_RESIZE_PARTITION);
  ENABLE (self->check_fs_button, GDU_FEATURE_CHECK_FILESYSTEM);
  ENABLE (self->repair_fs_button, GDU_FEATURE_REPAIR_FILESYSTEM);
  ENABLE (self->take_ownership_button, GDU_FEATURE_TAKE_OWNERSHIP);

  ENABLE (self->configure_fstab_button, GDU_FEATURE_CONFIGURE_FSTAB);
  ENABLE (self->configure_crypttab_button, GDU_FEATURE_CONFIGURE_CRYPTTAB);

  ENABLE (self->create_partition_image_button, GDU_FEATURE_CREATE_IMAGE);
  ENABLE (self->restore_partition_image_button, GDU_FEATURE_RESTORE_IMAGE);
  ENABLE (self->benchmark_partition_button, GDU_FEATURE_BENCHMARK);

#undef ENABLE
}

static void
format_partition_clicked_cb (GduBlockRow *self)
{
  UDisksObject *object;

  g_assert (GDU_IS_BLOCK_ROW (self));

  object = gdu_block_get_object (self->block);
  g_assert (object != NULL);
  gdu_create_format_show (block_row_get_client (),
                          block_row_get_window (self),
                          object, FALSE, 0, 0);
}

static void
edit_partition_clicked_cb (GduBlockRow *self)
{
  UDisksObject *object;

  object = gdu_block_get_object (self->block);
  g_assert (object != NULL);
  gdu_partition_dialog_show (block_row_get_window (self),
                             object,
                             block_row_get_client ());
}

static void
edit_filesystem_clicked_cb (GduBlockRow *self)
{
  UDisksObject *object;

  object = gdu_block_get_object (self->block);
  g_assert (object != NULL);
  gdu_filesystem_dialog_show (block_row_get_window (self),
                             object,
                             block_row_get_client ());
}

static void
change_passphrase_clicked_cb (GduBlockRow *self)
{
  UDisksObject *object;

  object = gdu_block_get_object (self->block);
  gdu_change_passphrase_dialog_show (block_row_get_window (self), object);
}

static void
resize_clicked_cb (GduBlockRow *self)
{
  UDisksObject *object;

  object = gdu_block_get_object (self->block);
  g_assert (object != NULL);
  gdu_resize_dialog_show (block_row_get_window (self),
                          object,
                          block_row_get_client ());
}

static void
on_message_dialog_response (GtkDialog *dialog,
                            int        response,
                            gpointer   user_data)
{
  gtk_window_close (GTK_WINDOW (dialog));
}

static void
fs_check_cb (GObject      *obj,
             GAsyncResult *result,
             gpointer      user_data)
{
  GduBlockRow *self = user_data;
  g_autoptr(GError) error = NULL;
  UDisksFilesystem *filesystem;
  UDisksObject *object;
  gboolean consistent;

  g_assert (GDU_IS_BLOCK_ROW (self));

  object = gdu_block_get_object (self->block);
  filesystem = udisks_object_peek_filesystem (object);

  if (!udisks_filesystem_call_check_finish (filesystem, &consistent, result, &error))
    {
      gdu_utils_show_error (block_row_get_window (self),
                            _("Error while checking filesystem"),
                            error);
    }
  else
    {
      GtkWidget *message_dialog;
      UDisksObjectInfo *info;
      UDisksBlock *block;
      const char *name;
      char *s;

      object = UDISKS_OBJECT (g_dbus_interface_get_object (G_DBUS_INTERFACE (filesystem)));
      block = udisks_object_peek_block (object);
      g_assert (block != NULL);

      info = udisks_client_get_object_info (block_row_get_client (), object);
      name = udisks_block_get_id_label (block);

      if (name == NULL || *name == '\0')
        name = udisks_block_get_id_type (block);

      message_dialog = gtk_message_dialog_new_with_markup  (block_row_get_window (self),
                                                            GTK_DIALOG_MODAL,
                                                            GTK_MESSAGE_INFO,
                                                            GTK_BUTTONS_CLOSE,
                                                            "<big><b>%s</b></big>",
                                                            consistent ? _("Filesystem intact") : _("Filesystem damaged"));
      if (consistent)
        {
          s = g_strdup_printf (_("Filesystem %s on %s is undamaged."),
                               name, udisks_object_info_get_name (info));
        }
      else
        {
          /* show as result and not error message, because it's not a malfunction of GDU */
          s = g_strdup_printf (_("Filesystem %s on %s needs repairing."),
                               name, udisks_object_info_get_name (info));
        }

      gtk_message_dialog_format_secondary_markup (GTK_MESSAGE_DIALOG (message_dialog), "%s", s);
      g_signal_connect (message_dialog, "response", G_CALLBACK (on_message_dialog_response), NULL);
      gtk_window_present (GTK_WINDOW (message_dialog));

      g_free (s);
    }
}

static void
fs_check_unmount_cb (GObject      *obj,
                     GAsyncResult *result,
                     gpointer      user_data)
{
  GduBlockRow *self = user_data;

  g_assert (GDU_IS_BLOCK_ROW (self));

  if (gdu_utils_ensure_unused_finish (block_row_get_client (), result, NULL))
    {
      UDisksFilesystem *filesystem;
      UDisksObject *object;

      object = gdu_block_get_object (self->block);
      filesystem = udisks_object_peek_filesystem (object);
      udisks_filesystem_call_check (filesystem,
                                    g_variant_new ("a{sv}", NULL),
                                    NULL,
                                    fs_check_cb,
                                    self);
    }
}

static void
on_check_message_dialog_response (GtkDialog *dialog,
                                  gint       response,
                                  gpointer   user_data)
{
  GduBlockRow *self = user_data;
  UDisksObject *object;

  g_assert (GDU_IS_BLOCK_ROW (self));
  object = gdu_block_get_object (self->block);
  g_assert (object != NULL);

  if (response == GTK_RESPONSE_OK)
    gdu_utils_ensure_unused (block_row_get_client (),
                             block_row_get_window (self),
                             object,
                             fs_check_unmount_cb,
                             NULL,
                             self);

  gtk_window_close (GTK_WINDOW (dialog));
}

static void
check_fs_cb (GduBlockRow *self)
{
  GtkWidget *message_dialog, *ok_button;

  message_dialog = gtk_message_dialog_new_with_markup  (block_row_get_window (self),
                                                        GTK_DIALOG_MODAL,
                                                        GTK_MESSAGE_WARNING,
                                                        GTK_BUTTONS_OK_CANCEL,
                                                        "<big><b>%s</b></big>",
                                                        _("Confirm Check"));

  gtk_message_dialog_format_secondary_markup (GTK_MESSAGE_DIALOG (message_dialog), "%s",
                                              _("The check may take a long time, especially if the partition contains a lot of data."));

  ok_button = gtk_dialog_get_widget_for_response (GTK_DIALOG (message_dialog), GTK_RESPONSE_OK);
  gtk_style_context_add_class (gtk_widget_get_style_context (ok_button), "suggested-action");

  g_signal_connect (message_dialog, "response", G_CALLBACK (on_check_message_dialog_response), self);
  gtk_window_present (GTK_WINDOW (message_dialog));
}

static void
response_cb (GtkDialog *dialog,
             gint       response,
             gpointer   user_data)
{
  gtk_window_close (GTK_WINDOW (dialog));
}

static void
fs_repair_cb (GObject      *obj,
              GAsyncResult *result,
              gpointer      user_data)
{
  GduBlockRow *self = user_data;
  g_autoptr(GError) error = NULL;
  UDisksFilesystem *filesystem;
  UDisksObject *object;
  gboolean success;

  g_assert (GDU_IS_BLOCK_ROW (self));

  object = gdu_block_get_object (self->block);
  filesystem = udisks_object_peek_filesystem (object);

  if (!udisks_filesystem_call_repair_finish (filesystem, &success, result, &error))
    {
      gdu_utils_show_error (block_row_get_window (self),
                            _("Error while repairing filesystem"),
                            error);
      g_error_free (error);
    }
  else
    {
      GtkWidget *message_dialog;
      UDisksObjectInfo *info;
      UDisksBlock *block;
      const char *name;
      char *s;

      object = UDISKS_OBJECT (g_dbus_interface_get_object (G_DBUS_INTERFACE (filesystem)));
      block = udisks_object_peek_block (object);
      g_assert (block != NULL);
      info = udisks_client_get_object_info (block_row_get_client (), object);
      name = udisks_block_get_id_label (block);

      if (name == NULL || *name == '\0')
        name = udisks_block_get_id_type (block);

      message_dialog = gtk_message_dialog_new_with_markup  (block_row_get_window (self),
                                                            GTK_DIALOG_MODAL,
                                                            GTK_MESSAGE_INFO,
                                                            GTK_BUTTONS_CLOSE,
                                                            "<big><b>%s</b></big>",
                                                            success ? _("Repair successful") : _("Repair failed"));
      if (success)
        {
          s = g_strdup_printf (_("Filesystem %s on %s has been repaired."),
                               name, udisks_object_info_get_name (info));
        }
      else
        {
          /* show as result and not error message, because it's not a malfunction of GDU */
          s = g_strdup_printf (_("Filesystem %s on %s could not be repaired."),
                               name, udisks_object_info_get_name (info));
        }

      gtk_message_dialog_format_secondary_markup (GTK_MESSAGE_DIALOG (message_dialog), "%s", s);
      g_signal_connect (message_dialog, "response", G_CALLBACK (response_cb), NULL);
      gtk_window_present (GTK_WINDOW (message_dialog));

      g_free (s);
    }

}

static void
fs_repair_unmount_cb (GObject      *obj,
                      GAsyncResult *result,
                      gpointer      user_data)
{
  GduBlockRow *self = user_data;
  UDisksObject *object;

  g_assert (GDU_IS_BLOCK_ROW (self));
  object = gdu_block_get_object (self->block);

  if (gdu_utils_ensure_unused_finish (block_row_get_client (), result, NULL))
    {
      UDisksFilesystem *filesystem;

      filesystem = udisks_object_peek_filesystem (object);
      g_assert (filesystem != NULL);
      udisks_filesystem_call_repair (filesystem,
                                     g_variant_new ("a{sv}", NULL),
                                     NULL,
                                     fs_repair_cb,
                                     self);
    }
}

static void
on_response (GtkDialog *dialog,
             gint       response,
             gpointer   user_data)
{
  GduBlockRow *self = user_data;
  UDisksObject *object;

  g_assert (GDU_IS_BLOCK_ROW (self));
  object = gdu_block_get_object (self->block);

  if (response == GTK_RESPONSE_OK)
    gdu_utils_ensure_unused (block_row_get_client (),
                             block_row_get_window (self),
                             object,
                             fs_repair_unmount_cb,
                             NULL,
                             self);

  gtk_window_close (GTK_WINDOW (dialog));
}

static void
repair_fs_cb (GduBlockRow *self)
{
  GtkWidget *message_dialog, *ok_button;
  UDisksObject *object;

  object = gdu_block_get_object (self->block);
  g_assert (object != NULL);

  message_dialog = gtk_message_dialog_new_with_markup (block_row_get_window(self),
                                                       GTK_DIALOG_MODAL,
                                                       GTK_MESSAGE_WARNING,
                                                       GTK_BUTTONS_OK_CANCEL,
                                                       "<big><b>%s</b></big>",
                                                       _("Confirm Repair"));

  gtk_message_dialog_format_secondary_markup (GTK_MESSAGE_DIALOG (message_dialog), "%s",
                                              _("A filesystem repair is not always possible and can cause data loss. "
                                                "Consider backing it up first in order to use forensic recovery tools "
                                                "that retrieve lost files. "
                                                "The operation may take a long time, especially if the partition contains a lot of data."));

  ok_button = gtk_dialog_get_widget_for_response (GTK_DIALOG (message_dialog), GTK_RESPONSE_OK);
  gtk_style_context_add_class (gtk_widget_get_style_context (ok_button), "destructive-action");

  g_signal_connect (message_dialog, "response", G_CALLBACK (on_response), self);
  gtk_window_present (GTK_WINDOW (message_dialog));
}

static void
fs_take_ownership_cb (GObject      *object,
                      GAsyncResult *result,
                      gpointer      user_data)
{
  GduBlockRow *self = user_data;
  g_autoptr(GError) error = NULL;

  g_assert (GDU_IS_BLOCK_ROW (self));

  if (!udisks_filesystem_call_take_ownership_finish (UDISKS_FILESYSTEM (object), result, &error))
    {
      gdu_utils_show_error (block_row_get_window (self),
                            _("Error while taking filesystem ownership"),
                            error);

      g_error_free (error);
    }
}

static void
on_recursive_checkbutton (GtkToggleButton *togglebutton,
                          gpointer         user_data)
{
  GtkWidget *ok_button = GTK_WIDGET (user_data);

  if (gtk_toggle_button_get_active (togglebutton))
    {
      gtk_style_context_remove_class (gtk_widget_get_style_context (ok_button), "suggested-action");
      gtk_style_context_add_class (gtk_widget_get_style_context (ok_button), "destructive-action");
    }
  else
    {
      gtk_style_context_remove_class (gtk_widget_get_style_context (ok_button), "destructive-action");
      gtk_style_context_add_class (gtk_widget_get_style_context (ok_button), "suggested-action");
    }
}

static void
take_ownership_cb (GduBlockRow *self)
{
  GtkWindow *window;
  GtkBuilder *builder;
  GVariantBuilder options_builder;
  GtkWidget *dialog;
  GtkWidget *recursive_checkbutton;
  GtkWidget *ok_button;
  UDisksObject *object;
  UDisksFilesystem *filesystem;

  object = gdu_block_get_object (self->block);
  g_assert (object != NULL);
  filesystem = udisks_object_peek_filesystem (object);
  window = block_row_get_window (self);

  builder = gtk_builder_new_from_resource ("/org/gnome/Disks/ui/take-ownership-dialog.ui");
  dialog = GTK_WIDGET (gtk_builder_get_object (builder, "take-ownership-dialog"));
  gtk_window_set_transient_for (GTK_WINDOW (dialog), window);

  ok_button = gtk_dialog_get_widget_for_response (GTK_DIALOG (dialog), GTK_RESPONSE_OK);
  recursive_checkbutton = GTK_WIDGET (gtk_builder_get_object (builder, "recursive-checkbutton"));
  g_signal_connect (recursive_checkbutton, "toggled", G_CALLBACK (on_recursive_checkbutton), ok_button);

  if (gtk_dialog_run (GTK_DIALOG (dialog)) == GTK_RESPONSE_OK)
    {
      g_variant_builder_init (&options_builder, G_VARIANT_TYPE_VARDICT);

      if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (recursive_checkbutton)))
        g_variant_builder_add (&options_builder, "{sv}", "recursive", g_variant_new_boolean (TRUE));

      udisks_filesystem_call_take_ownership (filesystem,
                                             g_variant_builder_end (&options_builder),
                                             NULL,
                                             fs_take_ownership_cb,
                                             window);
    }

  gtk_window_close (GTK_WINDOW (dialog));
}

static void
configure_fstab_clicked_cb (GduBlockRow *self)
{
  UDisksObject *object;

  object = gdu_block_get_object (self->block);
  g_assert (object != NULL);
  /* if (object == NULL) */
  /*   object = gdu_volume_grid_get_block_object (GDU_VOLUME_GRID (window->volume_grid)); */
  gdu_fstab_dialog_show (block_row_get_window (self),
                          object,
                          block_row_get_client ());
}

static void
configure_crypttab_clicked_cb (GduBlockRow *self)
{
  UDisksObject *object;

  object = gdu_block_get_object (self->block);
  g_assert (object != NULL);
  /* if (object == NULL) */
  /*   object = gdu_volume_grid_get_block_object (GDU_VOLUME_GRID (window->volume_grid)); */
  gdu_crypttab_dialog_show (block_row_get_window (self),
                            object,
                            block_row_get_client ());
}

static void
create_partition_image_clicked_cb (GduBlockRow *self)
{
  UDisksObject *object;

  object = gdu_block_get_object (self->block);
  g_assert (object != NULL);
  gdu_create_disk_image_dialog_show (block_row_get_window (self),
                                     object,
                                     block_row_get_client ());
}

static void
restore_partition_image_clicked_cb (GduBlockRow *self)
{
  UDisksObject *object;

  object = gdu_block_get_object (self->block);
  g_assert (object != NULL);
  gdu_restore_disk_image_dialog_show (block_row_get_window (self),
                                      object,
                                      block_row_get_client (),
                                      NULL);
}

static void
benchmark_partition_clicked_cb (GduBlockRow *self)
{
  UDisksObject *object;

  object = gdu_block_get_object (self->block);
  g_assert (object != NULL);
  gdu_benchmark_dialog_show (block_row_get_window (self),
                             object,
                             block_row_get_client ());
}

static void
gdu_block_row_finalize (GObject *object)
{
  GduBlockRow *self = (GduBlockRow *)object;

  g_clear_object (&self->block);

  G_OBJECT_CLASS (gdu_block_row_parent_class)->finalize (object);
}

static void
gdu_block_row_class_init (GduBlockRowClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->finalize = gdu_block_row_finalize;

  gtk_widget_class_set_template_from_resource (widget_class,
                                               "/org/gnome/Disks/ui/"
                                               "gdu-block-row.ui");

  gtk_widget_class_bind_template_child (widget_class, GduBlockRow, partition_image);
  gtk_widget_class_bind_template_child (widget_class, GduBlockRow, partition_depth_label);
  gtk_widget_class_bind_template_child (widget_class, GduBlockRow, space_level_bar);

  gtk_widget_class_bind_template_child (widget_class, GduBlockRow, size_label);
  gtk_widget_class_bind_template_child (widget_class, GduBlockRow, device_id_label);
  gtk_widget_class_bind_template_child (widget_class, GduBlockRow, uuid_label);
  gtk_widget_class_bind_template_child (widget_class, GduBlockRow, partition_type_label);

  gtk_widget_class_bind_template_child (widget_class, GduBlockRow, format_partition_button);

  gtk_widget_class_bind_template_child (widget_class, GduBlockRow, edit_partition_button);
  gtk_widget_class_bind_template_child (widget_class, GduBlockRow, edit_filesystem_button);
  gtk_widget_class_bind_template_child (widget_class, GduBlockRow, change_passphrase_button);

  gtk_widget_class_bind_template_child (widget_class, GduBlockRow, resize_button);
  gtk_widget_class_bind_template_child (widget_class, GduBlockRow, check_fs_button);
  gtk_widget_class_bind_template_child (widget_class, GduBlockRow, repair_fs_button);
  gtk_widget_class_bind_template_child (widget_class, GduBlockRow, take_ownership_button);

  gtk_widget_class_bind_template_child (widget_class, GduBlockRow, configure_fstab_button);
  gtk_widget_class_bind_template_child (widget_class, GduBlockRow, configure_crypttab_button);

  gtk_widget_class_bind_template_child (widget_class, GduBlockRow, create_partition_image_button);
  gtk_widget_class_bind_template_child (widget_class, GduBlockRow, restore_partition_image_button);
  gtk_widget_class_bind_template_child (widget_class, GduBlockRow, benchmark_partition_button);

  gtk_widget_class_bind_template_callback (widget_class, format_partition_clicked_cb);

  gtk_widget_class_bind_template_callback (widget_class, edit_partition_clicked_cb);
  gtk_widget_class_bind_template_callback (widget_class, edit_filesystem_clicked_cb);
  gtk_widget_class_bind_template_callback (widget_class, change_passphrase_clicked_cb);

  gtk_widget_class_bind_template_callback (widget_class, resize_clicked_cb);
  gtk_widget_class_bind_template_callback (widget_class, check_fs_cb);
  gtk_widget_class_bind_template_callback (widget_class, repair_fs_cb);
  gtk_widget_class_bind_template_callback (widget_class, take_ownership_cb);

  gtk_widget_class_bind_template_callback (widget_class, configure_fstab_clicked_cb);
  gtk_widget_class_bind_template_callback (widget_class, configure_crypttab_clicked_cb);

  gtk_widget_class_bind_template_callback (widget_class, create_partition_image_clicked_cb);
  gtk_widget_class_bind_template_callback (widget_class, restore_partition_image_clicked_cb);
  gtk_widget_class_bind_template_callback (widget_class, benchmark_partition_clicked_cb);
}

static void
gdu_block_row_init (GduBlockRow *self)
{
  gtk_widget_init_template (GTK_WIDGET (self));
}

GduBlockRow *
gdu_block_row_new (GduBlock *block)
{
  GduBlockRow *self;

  g_return_val_if_fail (GDU_IS_BLOCK (block), NULL);

  self = g_object_new (GDU_TYPE_BLOCK_ROW, NULL);
  self->block = g_object_ref (block);

  g_signal_connect_object (self->block,
                           "changed",
                           G_CALLBACK (update_block_row),
                           self, G_CONNECT_SWAPPED);
  update_block_row (self);

  return self;
}

GduBlock *
gdu_block_row_get_item (GduBlockRow *self)
{
  g_return_val_if_fail (GDU_IS_BLOCK_ROW (self), NULL);

  return self->block;
}
