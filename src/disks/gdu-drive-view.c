/* gdu-drive-view.c
 *
 * Copyright 2023 Mohammed Sadiq <sadiq@sadiqpk.org>
 *
 * Author(s):
 *   Mohammed Sadiq <sadiq@sadiqpk.org>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#define G_LOG_DOMAIN "gdu-drive-view"

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <adwaita.h>
#include <udisks/udisks.h>
#include <glib/gi18n.h>

#include "gdu-block-row.h"
#include "gdu-item.h"
#include "gdu-manager.h"
#include "gduatasmartdialog.h"
#include "gdu-benchmark-dialog.h"
#include "gdu-create-disk-image-dialog.h"
#include "gdudisksettingsdialog.h"
#include "gdu-format-disk-dialog.h"
#include "gdu-restore-disk-image-dialog.h"
#include "gdu-drive-view.h"

struct _GduDriveView
{
  AdwBin         parent_instance;

  GtkImage      *drive_image;
  GtkLabel      *drive_name_label;
  GtkLabel      *drive_path_label;

  GtkLabel      *drive_model_label;
  GtkLabel      *drive_serial_label;
  GtkLabel      *drive_part_type_label;
  GtkLabel      *drive_size_label;

  GtkListBox    *drive_partitions_listbox;

  GduDrive      *drive;
};


G_DEFINE_TYPE (GduDriveView, gdu_drive_view, ADW_TYPE_BIN)

static gpointer
drive_view_get_window (GduDriveView *self)
{
  return gtk_widget_get_ancestor (GTK_WIDGET (self), GTK_TYPE_WINDOW);
}

static gpointer
drive_view_get_client (void)
{
  GduManager *manager;

  manager = gdu_manager_get_default (NULL);
  return gdu_manager_get_client (manager);
}

static void
update_drive_view (GduDriveView *self)
{
  const char *description, *name, *model, *serial, *partition;
  g_autofree char *size_str = NULL;
  GListModel *partitions;
  GIcon *icon;
  GduFeature features;
  guint64 size;

  g_assert (GDU_IS_DRIVE_VIEW (self));

  description = gdu_item_get_description (GDU_ITEM (self->drive));
  name = gdu_drive_get_name (self->drive);
  icon = gdu_item_get_icon (GDU_ITEM (self->drive));

  gtk_label_set_label (self->drive_name_label, description);
  gtk_label_set_label (self->drive_path_label, name);
  g_object_set (self->drive_image, "gicon", icon, NULL);

  partition = gdu_item_get_partition_type (GDU_ITEM (self->drive));
  serial = gdu_drive_get_serial (self->drive);
  model = gdu_drive_get_model (self->drive);
  size = gdu_item_get_size (GDU_ITEM (self->drive));
  size_str = g_format_size_full (size, G_FORMAT_SIZE_LONG_FORMAT);

  gtk_label_set_label (self->drive_part_type_label, partition);
  gtk_label_set_label (self->drive_model_label, model);
  gtk_label_set_label (self->drive_serial_label, serial);
  gtk_label_set_label (self->drive_size_label, size ? size_str : "—");

  partitions = gdu_item_get_partitions (GDU_ITEM (self->drive));
  gtk_list_box_bind_model (self->drive_partitions_listbox,
                           partitions,
                           (GtkListBoxCreateWidgetFunc)gdu_block_row_new,
                           NULL, NULL);


  features = gdu_item_get_features (GDU_ITEM (self->drive));
  #define ENABLE(_action, _feature) gtk_widget_action_set_enabled (GTK_WIDGET (self), _action, (features & _feature) != 0) 
  ENABLE ("view.format", GDU_FEATURE_FORMAT);
  ENABLE ("view.create-image", GDU_FEATURE_CREATE_IMAGE);
  ENABLE ("view.restore-image", GDU_FEATURE_RESTORE_IMAGE);
  ENABLE ("view.benchmark", GDU_FEATURE_BENCHMARK);
  ENABLE ("view.smart", GDU_FEATURE_SMART);
  ENABLE ("view.settings", GDU_FEATURE_SETTINGS);
  ENABLE ("view.standby", GDU_FEATURE_STANDBY);
  ENABLE ("view.wakeup", GDU_FEATURE_WAKEUP);
  ENABLE ("view.poweroff", GDU_FEATURE_POWEROFF);

  #undef ENABLE
}

static void
format_disk_clicked_cb (GtkWidget  *widget,
                        const char *action_name,
                        GVariant   *parameter)
{
  GduDriveView *self = GDU_DRIVE_VIEW (widget);
  UDisksObject *object;
  GduManager *manager;

  g_assert (GDU_IS_DRIVE_VIEW (self));

  object = gdu_drive_get_object_for_format (self->drive);
  manager = gdu_manager_get_default (NULL);
  g_warning ("format :%p", object);
  g_assert (object != NULL);
  gdu_format_disk_dialog_show (drive_view_get_window (self),
                               object,
                               gdu_manager_get_client (manager));
}

static void
create_disk_image_clicked_cb (GtkWidget  *widget,
                              const char *action_name,
                              GVariant   *parameter)
{
  GduDriveView *self = GDU_DRIVE_VIEW (widget);
  UDisksObject *object;
  GduManager *manager;

  g_assert (GDU_IS_DRIVE_VIEW (self));

  object = gdu_drive_get_object_for_format (self->drive);
  manager = gdu_manager_get_default (NULL);
  g_assert (object != NULL);

  gdu_create_disk_image_dialog_show (drive_view_get_window (self),
                                     object,
                                     gdu_manager_get_client (manager));
}

static void
restore_disk_image_clicked_cb (GtkWidget  *widget,
                               const char *action_name,
                               GVariant   *parameter)
{
  GduDriveView *self = GDU_DRIVE_VIEW (widget);
  UDisksObject *object;
  GduManager *manager;

  g_assert (GDU_IS_DRIVE_VIEW (self));

  object = gdu_drive_get_object_for_format (self->drive);
  manager = gdu_manager_get_default (NULL);
  g_warning ("restore disk :%p", object);
  g_assert (object != NULL);

  g_assert (object != NULL);
  gdu_restore_disk_image_dialog_show (drive_view_get_window (self),
                                      object,
                                      gdu_manager_get_client (manager),
                                      NULL);
}

static void
benchmark_disk_clicked_cb (GtkWidget  *widget,
                           const char *action_name,
                           GVariant   *parameter)
{
  GduDriveView *self = GDU_DRIVE_VIEW (widget);
  UDisksObject *object;
  GduManager *manager;

  g_assert (GDU_IS_DRIVE_VIEW (self));

  object = gdu_drive_get_object (self->drive);
  manager = gdu_manager_get_default (NULL);
  g_warning ("benchmark disk :%p", object);
  g_assert (object != NULL);

  g_assert (object != NULL);
  gdu_benchmark_dialog_show (drive_view_get_window (self),
                             object,
                             gdu_manager_get_client (manager));
}

static void
smart_disk_clicked_cb (GtkWidget  *widget,
                       const char *action_name,
                       GVariant   *parameter)
{
  GduDriveView *self = GDU_DRIVE_VIEW (widget);
  UDisksObject *object;
  GduManager *manager;

  g_assert (GDU_IS_DRIVE_VIEW (self));

  object = gdu_drive_get_object (self->drive);
  manager = gdu_manager_get_default (NULL);
  g_assert (object != NULL);

  gdu_disk_settings_dialog_show (drive_view_get_window (self),
                                 object,
                                 gdu_manager_get_client (manager));
}

static void
drive_settings_clicked_cb (GtkWidget  *widget,
                           const char *action_name,
                           GVariant   *parameter)
{
  GduDriveView *self = GDU_DRIVE_VIEW (widget);
  UDisksObject *object;
  GduManager *manager;

  g_assert (GDU_IS_DRIVE_VIEW (self));

  object = gdu_drive_get_object (self->drive);
  manager = gdu_manager_get_default (NULL);
  g_assert (object != NULL);

  gdu_ata_smart_dialog_show (drive_view_get_window (self),
                             object,
                             gdu_manager_get_client (manager));
}

static void
drive_view_standby_cb (GObject      *object,
                       GAsyncResult *result,
                       gpointer      user_data)
{
  g_autoptr(GduDriveView) self = user_data;
  g_autoptr(GError) error = NULL;

  g_assert (GDU_IS_DRIVE_VIEW (self));

  if (!gdu_drive_standby_finish (self->drive, result, &error))
    {
      gdu_utils_show_error (drive_view_get_window (self),
                            _("An error occurred when trying to "
                              "put the drive into standby mode"),
                            error);
    }
}

static void
standby_drive_clicked_cb (GtkWidget  *widget,
                          const char *action_name,
                          GVariant   *parameter)
{
  GduDriveView *self = GDU_DRIVE_VIEW (widget);
  g_assert (GDU_IS_DRIVE_VIEW (self));

  gdu_drive_standby_async (self->drive, NULL,
                           drive_view_standby_cb,
                           g_object_ref (self));
}

static void
drive_view_wakeup_cb (GObject      *object,
                      GAsyncResult *result,
                      gpointer      user_data)
{
  g_autoptr(GduDriveView) self = user_data;
  g_autoptr(GError) error = NULL;

  g_assert (GDU_IS_DRIVE_VIEW (self));

  if (!gdu_drive_wakeup_finish (self->drive, result, &error))
    {
      gdu_utils_show_error (drive_view_get_window (self),
                            _("An error occurred when trying to "
                              "wake up the drive from standby mode"),
                            error);
    }
}

static void
wakeup_drive_clicked_cb (GtkWidget  *widget,
                         const char *action_name,
                         GVariant   *parameter)
{
  GduDriveView *self = GDU_DRIVE_VIEW (widget);
  g_assert (GDU_IS_DRIVE_VIEW (self));

  gdu_drive_wakeup_async (self->drive, NULL,
                          drive_view_wakeup_cb,
                          g_object_ref (self));
}

static void
drive_view_power_off_cb (GObject      *object,
                         GAsyncResult *result,
                         gpointer      user_data)
{
  g_autoptr(GduDriveView) self = user_data;
  g_autoptr(GError) error = NULL;

  g_assert (GDU_IS_DRIVE_VIEW (self));

  if (!gdu_drive_power_off_finish (self->drive, result, &error))
    {
      gdu_utils_show_error (drive_view_get_window (self),
                            _("Error powering off drive"),
                            error);
    }
}


static void
poweroff_confirmation_response_cb (GObject      *object,
                                   GAsyncResult *response,
                                   gpointer      user_data)
{
  GduDriveView *self = GDU_DRIVE_VIEW (user_data);
  AdwMessageDialog *dialog = ADW_MESSAGE_DIALOG (object);

  if (g_strcmp0 (adw_message_dialog_choose_finish(dialog, response), "cancel") == 0)
    return;

  gdu_drive_power_off_async (self->drive,
                             drive_view_get_window (self),
                             NULL,
                             drive_view_power_off_cb,
                             g_object_ref (self));
}

static void
poweroff_drive_clicked_cb (GtkWidget  *widget,
                           const char *action_name,
                           GVariant   *parameter)
{
  GduDriveView *self = GDU_DRIVE_VIEW (widget);
  GList *siblings;
  UDisksClient *client;
  g_autoptr(GList) objects = NULL;

  g_assert (GDU_IS_DRIVE_VIEW (self));

  siblings = gdu_drive_get_siblings (self->drive);

  if (siblings == NULL)
  {
    gdu_drive_power_off_async (self->drive,
                               drive_view_get_window (self),
                               NULL,
                               drive_view_power_off_cb,
                               g_object_ref (self));
    return;  
  }

  client = drive_view_get_client ();
  objects = g_list_append (NULL, gdu_drive_get_object (self->drive));
  objects = g_list_concat (objects, siblings);

  gdu_utils_show_confirmation (drive_view_get_window (self),
                               /* Translators: Heading for powering off a device with multiple drives */
                               _("Are you sure you want to power off the drives?"),
                               /* Translators: Message for powering off a device with multiple drives */
                               _("This operation will prepare the system for the following drives to be powered down and removed."),
                              _("_Power Off"),
                              NULL,
                              NULL,
                              client,
                              objects,
                              poweroff_confirmation_response_cb,
                              self,
                              ADW_RESPONSE_DEFAULT);
}

static void
gdu_drive_view_finalize (GObject *object)
{
  GduDriveView *self = (GduDriveView *)object;

  g_clear_object (&self->drive);

  G_OBJECT_CLASS (gdu_drive_view_parent_class)->finalize (object);
}

static void
gdu_drive_view_class_init (GduDriveViewClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->finalize = gdu_drive_view_finalize;

  gtk_widget_class_set_template_from_resource (widget_class,
                                               "/org/gnome/DiskUtility/ui/"
                                               "gdu-drive-view.ui");

  gtk_widget_class_bind_template_child (widget_class, GduDriveView, drive_image);
  gtk_widget_class_bind_template_child (widget_class, GduDriveView, drive_name_label);
  gtk_widget_class_bind_template_child (widget_class, GduDriveView, drive_path_label);

  gtk_widget_class_bind_template_child (widget_class, GduDriveView, drive_model_label);
  gtk_widget_class_bind_template_child (widget_class, GduDriveView, drive_serial_label);
  gtk_widget_class_bind_template_child (widget_class, GduDriveView, drive_part_type_label);
  gtk_widget_class_bind_template_child (widget_class, GduDriveView, drive_size_label);

  gtk_widget_class_bind_template_child (widget_class, GduDriveView, drive_partitions_listbox);

  gtk_widget_class_install_action (widget_class, "view.format", NULL, format_disk_clicked_cb);
  gtk_widget_class_install_action (widget_class, "view.create-image", NULL, create_disk_image_clicked_cb);
  gtk_widget_class_install_action (widget_class, "view.restore-image", NULL, restore_disk_image_clicked_cb);

  gtk_widget_class_install_action (widget_class, "view.benchmark", NULL, benchmark_disk_clicked_cb);
  gtk_widget_class_install_action (widget_class, "view.smart", NULL, smart_disk_clicked_cb);
  gtk_widget_class_install_action (widget_class, "view.settings", NULL, drive_settings_clicked_cb);

  gtk_widget_class_install_action (widget_class, "view.standby", NULL, standby_drive_clicked_cb);
  gtk_widget_class_install_action (widget_class, "view.wakeup", NULL, wakeup_drive_clicked_cb);
  gtk_widget_class_install_action (widget_class, "view.poweroff", NULL, poweroff_drive_clicked_cb);
}

static void
gdu_drive_view_init (GduDriveView *self)
{
  gtk_widget_init_template (GTK_WIDGET (self));
}

void
gdu_drive_view_set_drive (GduDriveView *self,
                          GduDrive     *drive)
{
  g_return_if_fail (GDU_IS_DRIVE_VIEW (self));
  g_return_if_fail (!drive || GDU_IS_DRIVE (drive));

  if (self->drive == drive)
    return;

  g_set_object (&self->drive, drive);

  if (drive)
    update_drive_view (self);
}
