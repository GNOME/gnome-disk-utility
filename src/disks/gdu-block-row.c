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
#include "gdu-format-volume-dialog.h"
#include "gdu-edit-filesystem-dialog.h"
#include "gdu-change-passphrase-dialog.h"
#include "gdu-edit-partition-dialog.h"
#include "gdu-resize-volume-dialog.h"
#include "gduutils.h"
#include "gdu-item.h"
#include "gdu-block-row.h"
#include "gdu-drive.h"
#include "gdu-unlock-dialog.h"
#include "gdu-rust.h"

/**
 * GduBlockRow:
 *
 * `GduBlockRow` represents user visible details of a `GduBlock` in GUI
 */

struct _GduBlockRow
{
  AdwExpanderRow  parent_instance;

  GtkWidget      *partition_image;
  GtkWidget      *partition_depth_label;
  GtkWidget      *job_indicator_spinner;
  GtkWidget      *space_level_bar;
  GtkWidget      *create_partition_button;
  GtkWidget      *block_menu_button;
  GMenuModel     *volume_actions_submenu;

  GtkWidget      *size_row;
  GtkWidget      *device_id_row;
  GtkWidget      *uuid_row;
  GtkWidget      *partition_type_row;

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
gdu_block_row_update_label (GduBlockRow *self)
{
  const char *label;

  label = gdu_item_get_description (GDU_ITEM (self->block));

  adw_preferences_row_set_title (ADW_PREFERENCES_ROW (self), label);
}

static void
gdu_block_row_update_information (GduBlockRow *self)
{
  const char *partition, *uuid, *device_id;
  g_autofree char *size_str = NULL;

  partition = gdu_item_get_partition_type (GDU_ITEM (self->block));
  uuid = gdu_block_get_uuid (self->block);
  device_id = gdu_block_get_device_id (self->block);
  size_str = gdu_block_get_size_str (self->block);

  adw_action_row_set_subtitle (ADW_ACTION_ROW (self->partition_type_row),
                               partition != NULL ? partition : "—");
  adw_action_row_set_subtitle (ADW_ACTION_ROW (self->uuid_row), uuid);
  adw_action_row_set_subtitle (ADW_ACTION_ROW (self->device_id_row), device_id);
  adw_action_row_set_subtitle (ADW_ACTION_ROW (self->size_row), size_str);
}

static void
gdu_block_row_update_features (GduBlockRow *self)
{
  GduFeature features;
  g_autoptr (GVariant) menu_item_attribute = NULL;
  g_autofree char *menu_attribute_label = NULL;
  features = gdu_item_get_features (GDU_ITEM (self->block));
  if (!features)
    {
      /* Hide the block menu button if there are no features
      * This usually happens when the block is a free space
      * or the parent is mounted as read only.
      */
      gtk_widget_set_visible (self->block_menu_button, FALSE);
    }
  else if (features & GDU_FEATURE_CREATE_PARTITION)
    {
      gtk_widget_set_visible (self->create_partition_button, TRUE);
    }

  menu_item_attribute = g_menu_model_get_item_attribute_value (self->volume_actions_submenu,
                                                               0, "action", g_variant_type_new ("s"));

  if ((g_strcmp0 (g_variant_get_string (menu_item_attribute, NULL), "row.unmount") == 0)
  || (g_strcmp0 (g_variant_get_string (menu_item_attribute, NULL), "row.mount") == 0)
  || (g_strcmp0 (g_variant_get_string (menu_item_attribute, NULL), "row.lock") == 0)
  || (g_strcmp0 (g_variant_get_string (menu_item_attribute, NULL), "row.unlock") == 0))
    g_menu_remove (G_MENU (self->volume_actions_submenu), 0);

  if (features & GDU_FEATURE_CAN_MOUNT)
    g_menu_prepend (G_MENU (self->volume_actions_submenu), _("Mount"), "row.mount");
  else if (features & GDU_FEATURE_CAN_UNMOUNT)
    g_menu_prepend (G_MENU (self->volume_actions_submenu), _("Unmount"), "row.unmount");
  else if (features & GDU_FEATURE_CAN_LOCK)
    g_menu_prepend (G_MENU (self->volume_actions_submenu), _("Lock"), "row.lock");
  else if (features & GDU_FEATURE_CAN_UNLOCK)
    g_menu_prepend (G_MENU (self->volume_actions_submenu), _("Unlock"), "row.unlock");

  #define ENABLE(_action, _feature) gtk_widget_action_set_enabled (GTK_WIDGET (self), _action, (features & _feature) != 0)
  ENABLE ("row.create_partition", GDU_FEATURE_CREATE_PARTITION);
  ENABLE ("row.mount", GDU_FEATURE_CAN_MOUNT);
  ENABLE ("row.unmount", GDU_FEATURE_CAN_UNMOUNT);
  ENABLE ("row.lock", GDU_FEATURE_CAN_LOCK);
  ENABLE ("row.unlock", GDU_FEATURE_CAN_UNLOCK);
  ENABLE ("row.resize", GDU_FEATURE_RESIZE_PARTITION);
  ENABLE ("row.edit_partition", GDU_FEATURE_EDIT_PARTITION);
  ENABLE ("row.edit_filesystem", GDU_FEATURE_EDIT_LABEL);
  ENABLE ("row.change_passphrase", GDU_FEATURE_CHANGE_PASSPHRASE);
  ENABLE ("row.configure_fstab", GDU_FEATURE_CONFIGURE_FSTAB);
  ENABLE ("row.configure_crypttab", GDU_FEATURE_CONFIGURE_CRYPTTAB);
  ENABLE ("row.take_ownership", GDU_FEATURE_TAKE_OWNERSHIP);
  ENABLE ("row.format_partition", GDU_FEATURE_FORMAT);
  ENABLE ("row.delete", GDU_FEATURE_DELETE_PARTITION);
  ENABLE ("row.check_fs", GDU_FEATURE_CHECK_FILESYSTEM);
  ENABLE ("row.repair_fs", GDU_FEATURE_REPAIR_FILESYSTEM);
  ENABLE ("row.benchmark_partition", GDU_FEATURE_BENCHMARK);
  ENABLE ("row.create_partition_image", GDU_FEATURE_CREATE_IMAGE);
  ENABLE ("row.restore_partition_image", GDU_FEATURE_RESTORE_IMAGE);

  #undef ENABLE
}

static void
gdu_block_row_update_depth_label (GduBlockRow *self)
{
  GduItem *parent;
  int depth = 0;

  parent = gdu_item_get_parent (GDU_ITEM (self->block));
  while ((parent = gdu_item_get_parent (parent)) != NULL)
    {
      depth++;
    }


  if (depth > 0)
    {
      g_autoptr(GString) depth_str = NULL;

      depth_str = g_string_new ("┗");

      while (--depth)
        g_string_append_len (depth_str, "━", strlen ("━"));

      g_string_append_len (depth_str, "╸", strlen ("╸"));
      gtk_label_set_label (GTK_LABEL (self->partition_depth_label), depth_str->str);
      gtk_widget_set_visible (self->partition_depth_label, TRUE);
    }
}

static void
gdu_block_row_update_mount_point_label (GduBlockRow *self)
{
  guint64 size, free_space;
  const char *const *mount_points;
  gboolean is_mounted;

  mount_points = gdu_block_get_mount_points (self->block);
  is_mounted = mount_points != NULL &&  *mount_points != NULL;

  gtk_widget_set_visible (self->space_level_bar, is_mounted);

  if (!is_mounted)
    {
      adw_expander_row_set_subtitle (ADW_EXPANDER_ROW (self), ("—"));
      return;
    }

  size = gdu_item_get_size (GDU_ITEM (self->block));
  free_space = gdu_block_get_unused_size (self->block);

  gtk_level_bar_set_max_value (GTK_LEVEL_BAR (self->space_level_bar), size / 1000);
  gtk_level_bar_set_value (GTK_LEVEL_BAR (self->space_level_bar), (size - free_space) / 1000);

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

static void
gdu_block_row_update (GduBlockRow *self)
{
  g_assert (GDU_IS_BLOCK_ROW (self));

  gdu_block_row_update_label (self);
  gdu_block_row_update_information (self);
  gdu_block_row_update_depth_label (self);
  gdu_block_row_update_mount_point_label (self);
  gdu_block_row_update_features (self);
}

static void
format_partition_cb (GtkWidget  *widget,
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
edit_partition_cb (GtkWidget  *widget,
                           const char *action_name,
                           GVariant   *parameter)
{
  GduBlockRow *self = GDU_BLOCK_ROW (widget);
  UDisksObject *object;

  object = gdu_block_get_object (self->block);
  g_assert (object != NULL);
  gdu_edit_partition_dialog_show (block_row_get_window (self),
                             object,
                             block_row_get_client ());
}

static void
edit_filesystem_cb (GtkWidget  *widget,
                    const char *action_name,
                    GVariant   *parameter)
{
  GduBlockRow *self = GDU_BLOCK_ROW (widget);
  UDisksObject *object;

  object = gdu_block_get_object (self->block);
  g_assert (object != NULL);

  gdu_edit_filesystem_dialog_show (block_row_get_window (self),
                                   self->block);
}

static void
create_partition_cb (GtkWidget  *widget,
                     const char *action_name,
                     GVariant   *parameter)
{
  GduBlockRow *self = GDU_BLOCK_ROW (widget);
  GduItem *drive = NULL;
  UDisksObject *object = NULL;

  drive = gdu_item_get_parent (GDU_ITEM (self->block));
  while (!GDU_IS_DRIVE (drive))
    {
      g_assert (drive);
      drive = gdu_item_get_parent (drive);
    }

  object = gdu_drive_get_object (GDU_DRIVE (drive));
  g_assert (object != NULL);

  gdu_create_format_show (block_row_get_client (),
                          block_row_get_window (self),
                          object,
                          TRUE,
                          gdu_block_get_offset (self->block),
                          gdu_item_get_size (GDU_ITEM (self->block)));
}

static void
unmount_cb (GtkWidget  *widget,
            const char *action_name,
            GVariant   *parameter)
{
  GduBlockRow *self = GDU_BLOCK_ROW (widget);
  UDisksObject *object;

  object = gdu_block_get_object (self->block);

  gdu_utils_ensure_unused (block_row_get_client (),
                           block_row_get_window (self),
                           object,
                           NULL,
                           NULL,
                           NULL);
}

static void
mount_complete_cb (GObject *source_object,
          GAsyncResult     *res,
          gpointer          user_data)
{
  GduBlockRow *self = GDU_BLOCK_ROW (user_data);
  UDisksFilesystem *filesystem = UDISKS_FILESYSTEM (source_object);
  g_autoptr(GError) error = NULL;

  if (!udisks_filesystem_call_mount_finish (filesystem,
                                            NULL, /* out_mount_path */
                                            res,
                                            &error))
    {
      gdu_utils_show_error (block_row_get_window (self),
                            _("Error mounting filesystem"),
                            error);
    }
}


static void
mount_cb (GtkWidget  *widget,
          const char *action_name,
          GVariant   *parameter)
{
  GduBlockRow *self = GDU_BLOCK_ROW (widget);
  UDisksObject *object;
  UDisksFilesystem *filesystem;

  object = gdu_block_get_object (self->block);
  filesystem = udisks_object_peek_filesystem (object);

  udisks_filesystem_call_mount (filesystem,
                                g_variant_new ("a{sv}", NULL), /* options */
                                NULL, /* cancellable */
                                (GAsyncReadyCallback) mount_complete_cb,
                                self);
}

static void
unlock_cb (GtkWidget  *widget,
           const char *action_name,
           GVariant   *parameter)
{
  GduBlockRow *self = GDU_BLOCK_ROW (widget);
  UDisksObject *object;

  object = gdu_block_get_object (self->block);

  gdu_unlock_dialog_show (block_row_get_window (self),
                          object);
}


static void
partition_delete_cb (GObject         *object,
                     GAsyncResult    *res,
                     gpointer         user_data)
{
  g_autoptr(GduBlockRow) self = user_data;
  UDisksPartition *partition = UDISKS_PARTITION (object);
  g_autoptr(GError) error = NULL;

  if (!udisks_partition_call_delete_finish (partition,
                                            res,
                                            &error))
    {
      gdu_utils_show_error (block_row_get_window (self),
                            _("Error deleting partition"),
                            error);
    }
}

static void
delete_ensure_unused_cb (GObject      *obj,
                         GAsyncResult *result,
                         gpointer      user_data)
{
  g_autoptr(GduBlockRow) self = user_data;
  UDisksObject *object;

  object = gdu_block_get_object (self->block);
  if (gdu_utils_ensure_unused_finish (block_row_get_client (), result, NULL))
    {
      UDisksPartition *partition;
      partition = udisks_object_peek_partition (object);
      udisks_partition_call_delete (partition,
                                    g_variant_new ("a{sv}", NULL), /* options */
                                    NULL, /* cancellable */
                                    partition_delete_cb,
                                    g_object_ref (self));
    }
}

static void
delete_response_cb (GObject      *source_object,
                    GAsyncResult *response,
                    gpointer      user_data)
{
  GduBlockRow *self = GDU_BLOCK_ROW (user_data);
  AdwAlertDialog *dialog = ADW_ALERT_DIALOG (source_object);
  UDisksObject *object;

  object = gdu_block_get_object (self->block);
  g_assert (object != NULL);

  if (g_strcmp0 (adw_alert_dialog_choose_finish(dialog, response), "cancel") == 0)
    return;

  gdu_utils_ensure_unused (block_row_get_client (),
                           block_row_get_window (self),
                           object,
                           delete_ensure_unused_cb,
                           NULL, /* GCancellable */
                           g_object_ref (self));
}

static void
delete_cb (GtkWidget  *widget,
           const char *action_name,
           GVariant   *parameter)
{
  GduBlockRow *self = GDU_BLOCK_ROW (widget);
  UDisksObject *object;
  g_autoptr(GList) objects;
  ConfirmationDialogData *data;
  GtkWidget *affected_devices_widget;

  object = gdu_block_get_object (self->block);
  objects = g_list_append (NULL, object);

  affected_devices_widget = gdu_util_create_widget_from_objects (block_row_get_client (),
                                                                 objects);

  data = g_new0 (ConfirmationDialogData, 1);
  data->message = _("Delete Partition?");
  data->description = _("All data on the partition will be lost");
  data->response_verb = _("Delete");
  data->response_appearance = ADW_RESPONSE_DESTRUCTIVE;
  data->callback = delete_response_cb;
  data->user_data = g_object_ref (self);

  gdu_utils_show_confirmation (block_row_get_window (self),
                               data,
                               affected_devices_widget);
}

static void
change_passphrase_cb (GtkWidget  *widget,
                              const char *action_name,
                              GVariant   *parameter)
{
  GduBlockRow *self = GDU_BLOCK_ROW (widget);
  UDisksObject *object;
  UDisksClient *client;

  object = gdu_block_get_object (self->block);
  client = block_row_get_client ();
  gdu_change_passphrase_dialog_show (block_row_get_window (self), object, client);
}

static void
resize_cb (GtkWidget  *widget,
                   const char *action_name,
                   GVariant   *parameter)
{
  GduBlockRow *self = GDU_BLOCK_ROW (widget);
  UDisksObject *object;
  g_autofree gchar *missing_util = NULL;
  const gchar *fs_type;
  g_autoptr(GError) error = NULL;

  g_assert (GDU_IS_BLOCK (self->block));

  fs_type = gdu_block_get_fs_type (self->block);
  gdu_utils_can_resize (self->client, fs_type, FALSE, NULL, &missing_util);

  if (g_strcmp0 (missing_util, "") > 0) {
    error = g_error_new (udisks_error_quark(), 
                         0,
                         _("The utility “%s” is required to resize a %s partition."), 
                         missing_util, fs_type);

    gdu_utils_show_error (block_row_get_window (self),
                          _("Error while resizing filesystem"),
                          error);

    return;
  }

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
  UDisksBlock *block;
  UDisksObject *object;
  UDisksObjectInfo *info;
  UDisksFilesystem *filesystem;
  gboolean consistent;
  const char *name;

  g_assert (GDU_IS_BLOCK_ROW (self));

  object = gdu_block_get_object (self->block);
  filesystem = udisks_object_peek_filesystem (object);

  if (!udisks_filesystem_call_check_finish (filesystem, &consistent, result, &error))
      return gdu_utils_show_error (block_row_get_window (self),
                                   _("Error while checking filesystem"),
                                   error);

  object = UDISKS_OBJECT (g_dbus_interface_get_object (G_DBUS_INTERFACE (filesystem)));
  block = udisks_object_peek_block (object);
  g_assert (block != NULL);

  info = udisks_client_get_object_info (block_row_get_client (), object);
  name = udisks_block_get_id_label (block);

  if (name == NULL || *name == '\0')
    name = udisks_block_get_id_type (block);

  gdu_utils_show_message (consistent ? _ ("Filesystem intact") : _ ("Filesystem damaged"),
                          g_strdup_printf (consistent
                           ? _ ("Filesystem %s on %s is undamaged.")
                           : _ ("Filesystem %s on %s needs repairing."),
                           name, udisks_object_info_get_name (info)),
                          block_row_get_window (self));

  g_object_unref (self);
}

static void
fs_check_unmount_cb (GObject      *obj,
                     GAsyncResult *result,
                     gpointer      user_data)
{
  GduBlockRow *self = user_data;
  UDisksObject *object;
  UDisksFilesystem *filesystem;

  g_assert (GDU_IS_BLOCK_ROW (self));

  if (!gdu_utils_ensure_unused_finish (block_row_get_client (), result, NULL))
    {
      g_object_unref (self);
      return;
    }

  object = gdu_block_get_object (self->block);
  filesystem = udisks_object_peek_filesystem (object);
  udisks_filesystem_call_check (filesystem,
                                g_variant_new ("a{sv}", NULL),
                                NULL,
                                fs_check_cb,
                                self);
}

static void
on_check_message_dialog_response (GObject          *obj,
                                  GAsyncResult     *response,
                                  gpointer          user_data)
{
  GduBlockRow *self = user_data;
  AdwAlertDialog *dialog = ADW_ALERT_DIALOG (obj);
  UDisksObject *object;

  g_assert (GDU_IS_BLOCK_ROW (self));

  if (g_strcmp0 (adw_alert_dialog_choose_finish(dialog, response), "cancel") == 0)
    return;

  object = gdu_block_get_object (self->block);
  g_assert (object != NULL);

  gdu_utils_ensure_unused (block_row_get_client (),
                           block_row_get_window (self),
                           object,
                           fs_check_unmount_cb,
                           NULL,
                           g_object_ref (self));
}

static void
check_fs_cb (GtkWidget  *widget,
             const char *action_name,
             GVariant   *parameter)
{
  GduBlockRow *self = GDU_BLOCK_ROW (widget);
  ConfirmationDialogData *data;

  data = g_new0 (ConfirmationDialogData, 1);
  data->message = _("Confirm Check?");
  data->description = _("The check may take a long time, especially if the partition contains a lot of data.");
  data->response_verb = _("Check");
  data->response_appearance = ADW_RESPONSE_SUGGESTED;
  data->callback = on_check_message_dialog_response;
  data->user_data = self;

  gdu_utils_show_confirmation (block_row_get_window (self),
                               data, NULL);
}

static void
fs_repair_cb (GObject      *obj,
              GAsyncResult *result,
              gpointer      user_data)
{
  GduBlockRow *self = user_data;
  g_autoptr(GError) error = NULL;
  UDisksBlock *block;
  UDisksObject *object;
  UDisksObjectInfo *info;
  UDisksFilesystem *filesystem;
  const char *name;
  gboolean success;

  g_assert (GDU_IS_BLOCK_ROW (self));

  object = gdu_block_get_object (self->block);
  filesystem = udisks_object_peek_filesystem (object);

  if (!udisks_filesystem_call_repair_finish (filesystem, &success, result, &error))
    {
      gdu_utils_show_error (block_row_get_window (self),
                            _("Error while repairing filesystem"),
                            error);
      g_object_unref (self);
      return;
    }


  object = UDISKS_OBJECT (g_dbus_interface_get_object (G_DBUS_INTERFACE (filesystem)));
  block = udisks_object_peek_block (object);
  g_assert (block != NULL);
  info = udisks_client_get_object_info (block_row_get_client (), object);
  name = udisks_block_get_id_label (block);

  if (name == NULL || *name == '\0')
    name = udisks_block_get_id_type (block);

  gdu_utils_show_message (success ? _("Repair successful") : _("Repair failed"),
                          g_strdup_printf (success
                           ? _("Filesystem %s on %s has been repaired.")
                           : _("Filesystem %s on %s could not be repaired."),
                           name, udisks_object_info_get_name (info)),
                          block_row_get_window (self));

  g_object_unref (self);
}

static void
fs_repair_unmount_cb (GObject      *obj,
                      GAsyncResult *result,
                      gpointer      user_data)
{
  GduBlockRow *self = user_data;
  UDisksObject *object;
  UDisksFilesystem *filesystem;

  g_assert (GDU_IS_BLOCK_ROW (self));
  object = gdu_block_get_object (self->block);

  if (!gdu_utils_ensure_unused_finish (block_row_get_client (), result, NULL))
    {
      g_object_unref (self);
      return;
    }

  filesystem = udisks_object_peek_filesystem (object);
  g_assert (filesystem != NULL);
  udisks_filesystem_call_repair (filesystem,
                                 g_variant_new ("a{sv}", NULL),
                                 NULL,
                                 fs_repair_cb,
                                 self);
}

static void
on_repair_message_dialog_response (GObject        *source_object,
                                   GAsyncResult   *response,
                                   gpointer        user_data)
{
  GduBlockRow *self = GDU_BLOCK_ROW (user_data);
  AdwAlertDialog *dialog = ADW_ALERT_DIALOG (source_object);
  UDisksObject *object;

  g_assert (GDU_IS_BLOCK_ROW (self));

  if (g_strcmp0 (adw_alert_dialog_choose_finish(dialog, response), "cancel") == 0)
    return;

  object = gdu_block_get_object (self->block);
  g_assert (object != NULL);

  gdu_utils_ensure_unused (block_row_get_client (),
                           block_row_get_window (self),
                           object,
                           fs_repair_unmount_cb,
                           NULL,
                           g_object_ref (self));
}

static void
repair_fs_cb (GtkWidget  *widget,
              const char *action_name,
              GVariant   *parameter)
{
  GduBlockRow *self = GDU_BLOCK_ROW (widget);
  ConfirmationDialogData *data;

  data = g_new0 (ConfirmationDialogData, 1);
  data->message = _("Repair Filesystem?");
  data->description = _("A filesystem repair is not always possible and can "
                        "cause data loss. Consider backing it up first in order "
                        "to use forensic recovery tools that retrieve lost files. "
                        "The operation may take a long time, especially if the partition contains a lot of data.");
  data->response_verb = _("Repair");
  data->response_appearance = ADW_RESPONSE_DESTRUCTIVE;
  data->callback = on_repair_message_dialog_response;
  data->user_data = self;

  gdu_utils_show_confirmation (block_row_get_window (self),
                               data, NULL);
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
    }
}

void
on_recursive_switch_cb (GObject     *source_object,
                        GParamSpec  *pspec,
                        gpointer     user_data)
{
  AdwAlertDialog *dialog = ADW_ALERT_DIALOG (source_object);
  gboolean active;

  g_assert (GTK_IS_SWITCH (user_data));
  g_assert (ADW_IS_ALERT_DIALOG (dialog));

  active = gtk_switch_get_active (GTK_SWITCH(user_data));

  adw_alert_dialog_set_response_appearance (dialog,
                                            "confirm",
                                            active ? ADW_RESPONSE_DESTRUCTIVE : ADW_RESPONSE_SUGGESTED);

  adw_alert_dialog_set_default_response (dialog,
                                         active ? "cancel" : "confirm");
}

typedef struct
{
  GduBlockRow *self;
  GtkWidget   *recursive;
} TakeOwnershipDialogData;

static void
on_take_ownership_dialog_response_cb (GObject       *source_object,
                                      GAsyncResult  *response,
                                      gpointer       user_data)
{
  GduBlockRow *self;
  AdwAlertDialog *dialog;
  GtkSwitch *recursive_switch;
  UDisksObject *object;
  UDisksFilesystem *filesystem;
  GVariantBuilder options_builder;
  TakeOwnershipDialogData *data = user_data;

  self = data->self;
  recursive_switch = GTK_SWITCH (data->recursive);
  dialog = ADW_ALERT_DIALOG (source_object);

  g_return_if_fail (GDU_IS_BLOCK_ROW (self));
  g_return_if_fail (ADW_IS_ALERT_DIALOG (dialog));

  if (g_strcmp0 (adw_alert_dialog_choose_finish(dialog, response), "cancel") == 0)
    return;

  object = gdu_block_get_object (self->block);
  g_assert (object != NULL);
  filesystem = udisks_object_peek_filesystem (object);

  g_variant_builder_init (&options_builder, G_VARIANT_TYPE_VARDICT);

  if (gtk_switch_get_active (GTK_SWITCH (recursive_switch)))
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
  GtkBuilder *builder;
  AdwAlertDialog *dialog;
  GtkWidget *recursive_switch;
  TakeOwnershipDialogData *data;

  builder = gtk_builder_new_from_resource ("/org/gnome/DiskUtility/ui/"
                                           "gdu-take-ownership-dialog.ui");
  dialog = ADW_ALERT_DIALOG (gtk_builder_get_object (builder, "ownership_dialog"));
  recursive_switch = GTK_WIDGET (gtk_builder_get_object (builder, "recursive_switch"));

  adw_alert_dialog_format_body (dialog,
                                _("Make your user and group the owner of ”%s”"),
                                gdu_item_get_description (GDU_ITEM (self->block)));

  data = g_new0 (TakeOwnershipDialogData, 1);
  data->self = self;
  data->recursive = recursive_switch;

  adw_alert_dialog_choose (dialog,
                           block_row_get_window (self),
                           NULL, /* GCancellable */
                           on_take_ownership_dialog_response_cb,
                           data);
}

static void
configure_fstab_cb (GtkWidget  *widget,
                            const char *action_name,
                            GVariant   *parameter)
{
  GduBlockRow *self = GDU_BLOCK_ROW (widget);
  UDisksObject *object;

  object = gdu_block_get_object (self->block);
  g_assert (object != NULL);

  gdu_mount_options_dialog_show (block_row_get_window (self),
                          object,
                          block_row_get_client ());
}

static void
configure_crypttab_cb (GtkWidget  *widget,
                               const char *action_name,
                               GVariant   *parameter)
{
  GduBlockRow *self = GDU_BLOCK_ROW (widget);
  UDisksObject *object;

  object = gdu_block_get_object (self->block);
  g_assert (object != NULL);

  gdu_encryption_options_dialog_show (block_row_get_window (self),
                                      block_row_get_client (),
                                      object);
}

static void
create_partition_image_cb (GtkWidget  *widget,
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
restore_partition_image_cb (GtkWidget  *widget,
                                    const char *action_name,
                                    GVariant   *parameter)
{
  GduBlockRow *self = GDU_BLOCK_ROW (widget);
  UDisksObject *object;
  const gchar *object_path;

  object = gdu_block_get_object (self->block);
  g_assert (object != NULL);
  object_path = g_dbus_object_get_object_path (G_DBUS_OBJECT (object));
  gdu_rs_restore_disk_image_dialog_show (block_row_get_window (self),
                                      object_path,
                                      NULL);
}

static void
benchmark_partition_cb (GtkWidget  *widget,
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

  static struct {
    const char *name;
    void (*cb) (GtkWidget *widget, const char *action_name, GVariant *parameter);
  } actions[] = {
    { "row.create_partition", create_partition_cb },
    { "row.mount", mount_cb },
    { "row.unmount", unmount_cb },
    { "row.lock", unmount_cb },
    { "row.unlock", unlock_cb },
    { "row.resize", resize_cb },
    { "row.edit_partition", edit_partition_cb },
    { "row.edit_filesystem", edit_filesystem_cb },
    { "row.change_passphrase", change_passphrase_cb },
    { "row.configure_fstab", configure_fstab_cb },
    { "row.configure_crypttab", configure_crypttab_cb },
    { "row.take_ownership", take_ownership_cb },
    { "row.format_partition", format_partition_cb },
    { "row.delete", delete_cb },
    { "row.check_fs", check_fs_cb },
    { "row.repair_fs", repair_fs_cb },
    { "row.benchmark_partition", benchmark_partition_cb },
    { "row.create_partition_image", create_partition_image_cb },
    { "row.restore_partition_image", restore_partition_image_cb },
  };

  object_class->finalize = gdu_block_row_finalize;

  gtk_widget_class_set_template_from_resource (widget_class,
                                               "/org/gnome/DiskUtility/ui/"
                                               "gdu-block-row.ui");

  gtk_widget_class_bind_template_child (widget_class, GduBlockRow, partition_image);
  gtk_widget_class_bind_template_child (widget_class, GduBlockRow, partition_depth_label);
  gtk_widget_class_bind_template_child (widget_class, GduBlockRow, job_indicator_spinner);
  gtk_widget_class_bind_template_child (widget_class, GduBlockRow, space_level_bar);
  gtk_widget_class_bind_template_child (widget_class, GduBlockRow, create_partition_button);
  gtk_widget_class_bind_template_child (widget_class, GduBlockRow, block_menu_button);
  gtk_widget_class_bind_template_child (widget_class, GduBlockRow, volume_actions_submenu);

  gtk_widget_class_bind_template_child (widget_class, GduBlockRow, size_row);
  gtk_widget_class_bind_template_child (widget_class, GduBlockRow, device_id_row);
  gtk_widget_class_bind_template_child (widget_class, GduBlockRow, uuid_row);
  gtk_widget_class_bind_template_child (widget_class, GduBlockRow, partition_type_row);

  for (guint i = 0; i < G_N_ELEMENTS (actions); i++)
    {
      gtk_widget_class_install_action (widget_class, actions[i].name, NULL, actions[i].cb);
    }
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

  gtk_widget_add_css_class (GTK_WIDGET (self), g_object_get_data (G_OBJECT (block), "color"));

  g_signal_connect_object (self->block,
                           "changed",
                           G_CALLBACK (gdu_block_row_update),
                           self, G_CONNECT_SWAPPED);
  gdu_block_row_update (self);

  return self;
}
