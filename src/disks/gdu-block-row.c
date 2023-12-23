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
  AdwExpanderRow  parent_instance;

  GtkImage       *partition_image;
  GtkLabel       *partition_depth_label;
  GtkLevelBar    *space_level_bar;

  GtkLabel       *size_label;
  GtkLabel       *device_id_label;
  GtkLabel       *uuid_label;
  GtkLabel       *partition_type_label;

  UDisksClient   *client;
  GduBlock       *block;
};


G_DEFINE_TYPE (GduBlockRow, gdu_block_row, ADW_TYPE_EXPANDER_ROW)

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

  adw_preferences_row_set_title (ADW_PREFERENCES_ROW (self), description);
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

      adw_expander_row_set_subtitle (ADW_EXPANDER_ROW (self), mount_points[0]);
    }

  #define ENABLE(_action, _feature) gtk_widget_action_set_enabled (GTK_WIDGET (self), _action, (features & _feature) != 0)
  ENABLE ("row.change_passphrase", GDU_FEATURE_CHANGE_PASSPHRASE);
  ENABLE ("row.resize", GDU_FEATURE_RESIZE_PARTITION);
  ENABLE ("row.edit_partition", GDU_FEATURE_EDIT_PARTITION);
  ENABLE ("row.edit_filesystem", GDU_FEATURE_EDIT_LABEL);
  ENABLE ("row.take_ownership", GDU_FEATURE_TAKE_OWNERSHIP);
  ENABLE ("row.format_partition", GDU_FEATURE_FORMAT);
  ENABLE ("row.configure_fstab", GDU_FEATURE_CONFIGURE_FSTAB);
  ENABLE ("row.configure_crypttab", GDU_FEATURE_CONFIGURE_CRYPTTAB);
  ENABLE ("row.check_fs", GDU_FEATURE_CHECK_FILESYSTEM);
  ENABLE ("row.repair_fs", GDU_FEATURE_REPAIR_FILESYSTEM);
  ENABLE ("row.benchmark_partition", GDU_FEATURE_BENCHMARK);
  ENABLE ("row.create_partition_image", GDU_FEATURE_CREATE_IMAGE);
  ENABLE ("row.restore_partition_image", GDU_FEATURE_RESTORE_IMAGE);

  #undef ENABLE
}

static void
format_partition_clicked_cb (GtkWidget  *widget,
                             const char *action_name,
                             GVariant   *parameter)
{
  GduBlockRow *self = GDU_BLOCK_ROW (widget);
  UDisksObject *object;

  g_assert (GDU_IS_BLOCK_ROW (self));

  object = gdu_block_get_object (self->block);
  g_assert (object != NULL);
  gdu_create_format_show (block_row_get_client (),
                          block_row_get_window (self),
                          object, FALSE, 0, 0);
}

static void
edit_partition_clicked_cb (GtkWidget  *widget,
                           const char *action_name,
                           GVariant   *parameter)
{
  GduBlockRow *self = GDU_BLOCK_ROW (widget);
  UDisksObject *object;

  object = gdu_block_get_object (self->block);
  g_assert (object != NULL);
  gdu_partition_dialog_show (block_row_get_window (self),
                             object,
                             block_row_get_client ());
}

static void
edit_filesystem_clicked_cb (GtkWidget  *widget,
                            const char *action_name,
                            GVariant   *parameter)
{
  GduBlockRow *self = GDU_BLOCK_ROW (widget);
  UDisksObject *object;

  object = gdu_block_get_object (self->block);
  g_assert (object != NULL);
  gdu_filesystem_dialog_show (block_row_get_window (self),
                             object,
                             block_row_get_client ());
}

static void
change_passphrase_clicked_cb (GtkWidget  *widget,
                              const char *action_name,
                              GVariant   *parameter)
{
  GduBlockRow *self = GDU_BLOCK_ROW (widget);
  UDisksObject *object;

  object = gdu_block_get_object (self->block);
  gdu_change_passphrase_dialog_show (block_row_get_window (self), object);
}

static void
resize_clicked_cb (GtkWidget  *widget,
                   const char *action_name,
                   GVariant   *parameter)
{
  GduBlockRow *self = GDU_BLOCK_ROW (widget);
  UDisksObject *object;

  object = gdu_block_get_object (self->block);
  g_assert (object != NULL);
  gdu_resize_dialog_show (block_row_get_window (self),
                          object,
                          block_row_get_client ());
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
      GtkWidget *dialog;
      UDisksObjectInfo *info;
      UDisksBlock *block;
      const char *name;

      object = UDISKS_OBJECT (g_dbus_interface_get_object (G_DBUS_INTERFACE (filesystem)));
      block = udisks_object_peek_block (object);
      g_assert (block != NULL);

      info = udisks_client_get_object_info (block_row_get_client (), object);
      name = udisks_block_get_id_label (block);

      if (name == NULL || *name == '\0')
        name = udisks_block_get_id_type (block);

      dialog = adw_message_dialog_new (block_row_get_window (self),
                                       consistent ? _("Filesystem intact") : _("Filesystem damaged"),
                                      NULL);

      adw_message_dialog_format_body_markup (ADW_MESSAGE_DIALOG (dialog),
                                             consistent ? _("Filesystem %s on %s is undamaged.")
                                              : _("Filesystem %s on %s needs repairing."),
                                              name, udisks_object_info_get_name (info));
      
      adw_message_dialog_add_response (ADW_MESSAGE_DIALOG (dialog),
                                       "close",
                                       _("Close"));

      adw_message_dialog_set_close_response (ADW_MESSAGE_DIALOG (dialog),
                                             "close");
      
      gtk_window_present (GTK_WINDOW (dialog));
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
on_check_message_dialog_response (GduBlockRow      *self,
                                  gchar            *response,
                                  AdwMessageDialog *dialog)
{
  UDisksObject *object;

  g_assert (GDU_IS_BLOCK_ROW (self));
  object = gdu_block_get_object (self->block);
  g_assert (object != NULL);

  gtk_window_close (GTK_WINDOW (dialog));
  if (g_strcmp0 (response, "cancel") == 0)
    return;

  gdu_utils_ensure_unused (block_row_get_client (),
                           block_row_get_window (self),
                           object,
                           fs_check_unmount_cb,
                           NULL,
                           self);
}

static void
check_fs_cb (GtkWidget  *widget,
             const char *action_name,
             GVariant   *parameter)
{
  GduBlockRow *self = GDU_BLOCK_ROW (widget);
  GtkWidget *dialog;

  dialog = adw_message_dialog_new (block_row_get_window (self),
                                           _("Confirm Check"),
                                          _("The check may take a long time, especially if the partition contains a lot of data."));

  adw_message_dialog_add_responses (ADW_MESSAGE_DIALOG (dialog),
                                    "cancel",  _("_Cancel"),
                                    "confirm", _("_Ok"),
                                    NULL);

  adw_message_dialog_set_response_appearance (ADW_MESSAGE_DIALOG (dialog),
                                              "confirm", 
                                              ADW_RESPONSE_SUGGESTED);
  
  adw_message_dialog_set_default_response (ADW_MESSAGE_DIALOG (dialog),
                                           "confirm");

  adw_message_dialog_set_close_response (ADW_MESSAGE_DIALOG (dialog),
                                         "cancel");

  g_signal_connect_swapped (dialog,
                            "response",
                            G_CALLBACK (on_check_message_dialog_response),
                            self);
  
  gtk_window_present (GTK_WINDOW (dialog));
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
    }
  else
    {
      GtkWidget *dialog;
      UDisksObjectInfo *info;
      UDisksBlock *block;
      const char *name;

      object = UDISKS_OBJECT (g_dbus_interface_get_object (G_DBUS_INTERFACE (filesystem)));
      block = udisks_object_peek_block (object);
      g_assert (block != NULL);
      info = udisks_client_get_object_info (block_row_get_client (), object);
      name = udisks_block_get_id_label (block);

      if (name == NULL || *name == '\0')
        name = udisks_block_get_id_type (block);

      dialog = adw_message_dialog_new (block_row_get_window (self),
                                       success ? _("Repair successful") : _("Repair failed"),
                                       NULL);

      adw_message_dialog_format_body_markup (ADW_MESSAGE_DIALOG (dialog),
                                            success ? _("Filesystem %s on %s has been repaired.") 
                                            : _("Filesystem %s on %s could not be repaired."),
                                            name, udisks_object_info_get_name (info));
      
      adw_message_dialog_add_response (ADW_MESSAGE_DIALOG (dialog),
                                       "close",
                                       _("Close"));

      adw_message_dialog_set_close_response (ADW_MESSAGE_DIALOG (dialog),
                                             "close");
      
      gtk_window_present (GTK_WINDOW (dialog));
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
on_repair_message_dialog_response (GduBlockRow      *self,
                                   gchar            *response,
                                   AdwMessageDialog *dialog)
{
  UDisksObject *object;

  g_assert (GDU_IS_BLOCK_ROW (self));
  object = gdu_block_get_object (self->block);

  if (g_strcmp0 (response, "cancel") == 0)
    return;

  gdu_utils_ensure_unused (block_row_get_client (),
                           block_row_get_window (self),
                           object,
                           fs_repair_unmount_cb,
                           NULL,
                           self);
}

static void
repair_fs_cb (GtkWidget  *widget,
              const char *action_name,
              GVariant   *parameter)
{
  GduBlockRow *self = GDU_BLOCK_ROW (widget);
  GtkWidget *dialog;
  UDisksObject *object;

  object = gdu_block_get_object (self->block);
  g_assert (object != NULL);

  dialog = adw_message_dialog_new (block_row_get_window (self),
                                   _("Confirm Repair"),
                                   _("A filesystem repair is not always possible and can cause data loss. "
                                     "Consider backing it up first in order to use forensic recovery tools "
                                     "that retrieve lost files. "
                                     "The operation may take a long time, especially if the partition contains a lot of data."));

  adw_message_dialog_add_responses (ADW_MESSAGE_DIALOG (dialog),
                                    "cancel",  _("_Cancel"),
                                    "confirm", _("_Ok"),
                                    NULL);

  adw_message_dialog_set_response_appearance (ADW_MESSAGE_DIALOG (dialog),
                                              "confirm", 
                                              ADW_RESPONSE_DESTRUCTIVE);

  adw_message_dialog_set_default_response (ADW_MESSAGE_DIALOG (dialog),
                                            "cancel");

  adw_message_dialog_set_close_response (ADW_MESSAGE_DIALOG (dialog),
                                          "cancel");

  g_signal_connect_swapped (dialog,
                            "response",
                            G_CALLBACK (on_repair_message_dialog_response),
                            self);
  gtk_window_present (GTK_WINDOW (dialog));
}

static void
fs_take_ownership_cb (GObject      *object,
                      GAsyncResult *result,
                      gpointer      user_data)
{
  GduBlockRow *self = GDU_BLOCK_ROW (user_data);
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
on_recursive_checkbutton (GtkCheckButton  *togglebutton,
                          gpointer         user_data)
{
  AdwMessageDialog *dialog = ADW_MESSAGE_DIALOG (user_data);
  gboolean active;

  active = gtk_check_button_get_active (togglebutton);
  
  adw_message_dialog_set_response_appearance (dialog,
                                              "confirm", 
                                              active ?
                                              ADW_RESPONSE_DESTRUCTIVE :
                                              ADW_RESPONSE_SUGGESTED);
  adw_message_dialog_set_default_response (dialog,
                                           active ?
                                           "cancel" :
                                           "confirm");
}

typedef struct
{
  GduBlockRow    *self;
  GtkCheckButton *recursive_checkbutton;
} TakeOwnershipDialogData;

static void
on_take_ownership_dialog_response_cb (GObject       *source_object,
                                      GAsyncResult  *response,
                                      gpointer       user_data)
{
  AdwMessageDialog *dialog = ADW_MESSAGE_DIALOG (source_object);
  TakeOwnershipDialogData *data = user_data;
  GduBlockRow *self;
  GtkCheckButton *recursive_checkbutton;
  UDisksObject *object;
  UDisksFilesystem *filesystem;
  GVariantBuilder options_builder;

  if (g_strcmp0 (adw_message_dialog_choose_finish(dialog, response), "cancel") == 0)
    return;

  self = data->self;
  recursive_checkbutton = data->recursive_checkbutton;
  object = gdu_block_get_object (self->block);
  g_assert (object != NULL);
  filesystem = udisks_object_peek_filesystem (object);
  
  g_variant_builder_init (&options_builder, G_VARIANT_TYPE_VARDICT);

  if (gtk_check_button_get_active (recursive_checkbutton))
    g_variant_builder_add (&options_builder, "{sv}", "recursive", g_variant_new_boolean (TRUE));

  udisks_filesystem_call_take_ownership (filesystem,
                                        g_variant_builder_end (&options_builder),
                                        NULL,
                                        fs_take_ownership_cb,
                                        self);
}

static void
take_ownership_cb (GtkWidget  *widget,
                   const char *action_name,
                   GVariant   *parameter)
{
  GduBlockRow *self = GDU_BLOCK_ROW (widget);
  GtkWidget *dialog;
  TakeOwnershipDialogData *data;
  GtkWidget *recursive_checkbutton;
  
  dialog = adw_message_dialog_new (block_row_get_window (self),
                                   _("Confirm Taking Ownership"),
                                   _("Changes ownership of the filesystem to your user and group."
                                     "The recursive mode does also change the ownership of all "
                                     "subdirectories and files, this can lead to destructive "
                                     "results when the filesystem contains a directory structure "
                                     "where ownership should belong to different users (e.g., a "
                                     "system backup or a filesystem that is accessed by multiple users)."));

  adw_message_dialog_add_responses (ADW_MESSAGE_DIALOG (dialog),
                                    "cancel",  _("_Cancel"),
                                    "confirm", _("_Ok"),
                                    NULL);

  adw_message_dialog_set_close_response (ADW_MESSAGE_DIALOG (dialog),
                                         "cancel");

  adw_message_dialog_set_response_appearance (ADW_MESSAGE_DIALOG (dialog),
                                              "confirm", 
                                              ADW_RESPONSE_SUGGESTED);

  adw_message_dialog_set_default_response (ADW_MESSAGE_DIALOG (dialog),
                                           "confirm");

  recursive_checkbutton = gtk_check_button_new_with_label (_("Enable _recursive mode"));
  gtk_check_button_set_use_underline (GTK_CHECK_BUTTON (recursive_checkbutton), TRUE);
  g_signal_connect (recursive_checkbutton,
                    "toggled",
                    G_CALLBACK (on_recursive_checkbutton),
                    dialog);

  adw_message_dialog_set_extra_child (ADW_MESSAGE_DIALOG (dialog), recursive_checkbutton);

  data = g_new0 (TakeOwnershipDialogData, 1);
  data->self = self;
  data->recursive_checkbutton = GTK_CHECK_BUTTON (recursive_checkbutton);

  adw_message_dialog_choose (ADW_MESSAGE_DIALOG (dialog),
                             NULL,
                             on_take_ownership_dialog_response_cb,
                             data);
}

static void
configure_fstab_clicked_cb (GtkWidget  *widget,
                            const char *action_name,
                            GVariant   *parameter)
{
  GduBlockRow *self = GDU_BLOCK_ROW (widget);
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
configure_crypttab_clicked_cb (GtkWidget  *widget,
                               const char *action_name,
                               GVariant   *parameter)
{
  GduBlockRow *self = GDU_BLOCK_ROW (widget);
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
create_partition_image_clicked_cb (GtkWidget  *widget,
                                   const char *action_name,
                                   GVariant   *parameter)
{
  GduBlockRow *self = GDU_BLOCK_ROW (widget);
  UDisksObject *object;

  object = gdu_block_get_object (self->block);
  g_assert (object != NULL);
  gdu_create_disk_image_dialog_show (block_row_get_window (self),
                                     object,
                                     block_row_get_client ());
}

static void
restore_partition_image_clicked_cb (GtkWidget  *widget,
                                    const char *action_name,
                                    GVariant   *parameter)
{
  GduBlockRow *self = GDU_BLOCK_ROW (widget);
  UDisksObject *object;

  object = gdu_block_get_object (self->block);
  g_assert (object != NULL);
  gdu_restore_disk_image_dialog_show (block_row_get_window (self),
                                      object,
                                      block_row_get_client (),
                                      NULL);
}

static void
benchmark_partition_clicked_cb (GtkWidget  *widget,
                                const char *action_name,
                                GVariant   *parameter)
{
  GduBlockRow *self = GDU_BLOCK_ROW (widget);
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
                                               "/org/gnome/DiskUtility/ui/"
                                               "gdu-block-row.ui");

  gtk_widget_class_bind_template_child (widget_class, GduBlockRow, partition_image);
  gtk_widget_class_bind_template_child (widget_class, GduBlockRow, partition_depth_label);
  gtk_widget_class_bind_template_child (widget_class, GduBlockRow, space_level_bar);

  gtk_widget_class_bind_template_child (widget_class, GduBlockRow, size_label);
  gtk_widget_class_bind_template_child (widget_class, GduBlockRow, device_id_label);
  gtk_widget_class_bind_template_child (widget_class, GduBlockRow, uuid_label);
  gtk_widget_class_bind_template_child (widget_class, GduBlockRow, partition_type_label);

  gtk_widget_class_install_action (widget_class, "row.change_passphrase", NULL, change_passphrase_clicked_cb);
  gtk_widget_class_install_action (widget_class, "row.resize", NULL, resize_clicked_cb);
  gtk_widget_class_install_action (widget_class, "row.edit_partition", NULL, edit_partition_clicked_cb);
  gtk_widget_class_install_action (widget_class, "row.edit_filesystem", NULL, edit_filesystem_clicked_cb);
  gtk_widget_class_install_action (widget_class, "row.take_ownership", NULL, take_ownership_cb);
  gtk_widget_class_install_action (widget_class, "row.format_partition", NULL, format_partition_clicked_cb);
  gtk_widget_class_install_action (widget_class, "row.configure_fstab", NULL, configure_fstab_clicked_cb);
  gtk_widget_class_install_action (widget_class, "row.configure_crypttab", NULL, configure_crypttab_clicked_cb);

  gtk_widget_class_install_action (widget_class, "row.check_fs", NULL, check_fs_cb);
  gtk_widget_class_install_action (widget_class, "row.repair_fs", NULL, repair_fs_cb);
  gtk_widget_class_install_action (widget_class, "row.benchmark_partition", NULL, benchmark_partition_clicked_cb);
  
  gtk_widget_class_install_action (widget_class, "row.create_partition_image", NULL, create_partition_image_clicked_cb);
  gtk_widget_class_install_action (widget_class, "row.restore_partition_image", NULL, restore_partition_image_clicked_cb);
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
