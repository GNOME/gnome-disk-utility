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

#include <handy.h>
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
  GtkBox         parent_instance;

  GtkStack      *main_stack;
  HdyStatusPage *empty_page;

  GtkBox        *drive_page;
  GtkImage      *drive_image;
  GtkLabel      *drive_name_label;
  GtkLabel      *drive_path_label;

  GtkLabel      *drive_model_label;
  GtkLabel      *drive_serial_label;
  GtkLabel      *drive_part_type_label;
  GtkLabel      *drive_size_label;

  GtkListBox    *drive_parts_listbox;

  GtkButton     *format_disk_button;
  GtkButton     *create_disk_image_button;
  GtkButton     *restore_disk_image_button;

  GtkButton     *benchmark_disk_button;
  GtkButton     *smart_disk_button;
  GtkButton     *drive_settings_button;

  GtkButton     *standby_button;
  GtkButton     *wakeup_button;
  GtkButton     *poweroff_button;

  GduDrive      *drive;
};


G_DEFINE_TYPE (GduDriveView, gdu_drive_view, GTK_TYPE_BOX)

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
  gtk_list_box_bind_model (self->drive_parts_listbox,
                           partitions,
                           (GtkListBoxCreateWidgetFunc)gdu_block_row_new,
                           NULL, NULL);

  features = gdu_item_get_features (GDU_ITEM (self->drive));

#define ENABLE(_widget, _feature) gtk_widget_set_sensitive (GTK_WIDGET (_widget), features & _feature)

  ENABLE (self->format_disk_button, GDU_FEATURE_FORMAT);
  ENABLE (self->create_disk_image_button, GDU_FEATURE_CREATE_IMAGE);
  ENABLE (self->restore_disk_image_button, GDU_FEATURE_RESTORE_IMAGE);

  ENABLE (self->benchmark_disk_button, GDU_FEATURE_BENCHMARK);
  ENABLE (self->smart_disk_button, GDU_FEATURE_SMART);
  ENABLE (self->drive_settings_button, GDU_FEATURE_SETTINGS);

  ENABLE (self->standby_button, GDU_FEATURE_STANDBY);
  ENABLE (self->wakeup_button, GDU_FEATURE_WAKEUP);
  ENABLE (self->poweroff_button, GDU_FEATURE_POWEROFF);

#undef ENABLE
}

static void
format_disk_clicked_cb (GduDriveView *self)
{
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
create_disk_image_clicked_cb (GduDriveView *self)
{
  UDisksObject *object;
  GduManager *manager;

  g_assert (GDU_IS_DRIVE_VIEW (self));

  object = gdu_drive_get_object_for_format (self->drive);
  manager = gdu_manager_get_default (NULL);
  g_warning ("create disk :%p", object);
  g_assert (object != NULL);

  g_assert (object != NULL);
  gdu_create_disk_image_dialog_show (drive_view_get_window (self),
                                     object,
                                     gdu_manager_get_client (manager));
}

static void
restore_disk_image_clicked_cb (GduDriveView *self)
{
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
benchmark_disk_clicked_cb (GduDriveView *self)
{
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
smart_disk_clicked_cb (GduDriveView *self)
{
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
drive_settings_clicked_cb (GduDriveView *self)
{
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
standby_drive_clicked_cb (GduDriveView *self)
{
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
wakeup_drive_clicked_cb (GduDriveView *self)
{
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
poweroff_drive_clicked_cb (GduDriveView *self)
{
  GtkWindow *window;
  GList *siblings;

  g_assert (GDU_IS_DRIVE_VIEW (self));

  window = drive_view_get_window (self);
  siblings = gdu_drive_get_siblings (self->drive);

  if (siblings != NULL)
    {
      g_autoptr(GList) objects = NULL;
      UDisksClient *client;
      const char *heading;
      const char *message;

      client = drive_view_get_client ();
      objects = g_list_append (NULL, gdu_drive_get_object (self->drive));
      objects = g_list_concat (objects, siblings);

      /* Translators: Heading for powering off a device with multiple drives */
      heading = _("Are you sure you want to power off the drives?");
      /* Translators: Message for powering off a device with multiple drives */
      message = _("This operation will prepare the system for the following drives to be powered down and removed.");

      if (!gdu_utils_show_confirmation (window, heading, message,
                                        _("_Power Off"),
                                        NULL, NULL,
                                        client, objects, FALSE))
        {
          return;
        }
    }

  gdu_drive_power_off_async (self->drive,
                             window,
                             NULL,
                             drive_view_power_off_cb,
                             g_object_ref (self));
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

  gtk_widget_class_bind_template_child (widget_class, GduDriveView, main_stack);
  gtk_widget_class_bind_template_child (widget_class, GduDriveView, empty_page);

  gtk_widget_class_bind_template_child (widget_class, GduDriveView, drive_page);
  gtk_widget_class_bind_template_child (widget_class, GduDriveView, drive_image);
  gtk_widget_class_bind_template_child (widget_class, GduDriveView, drive_name_label);
  gtk_widget_class_bind_template_child (widget_class, GduDriveView, drive_path_label);

  gtk_widget_class_bind_template_child (widget_class, GduDriveView, drive_model_label);
  gtk_widget_class_bind_template_child (widget_class, GduDriveView, drive_serial_label);
  gtk_widget_class_bind_template_child (widget_class, GduDriveView, drive_part_type_label);
  gtk_widget_class_bind_template_child (widget_class, GduDriveView, drive_size_label);

  gtk_widget_class_bind_template_child (widget_class, GduDriveView, drive_parts_listbox);

  gtk_widget_class_bind_template_child (widget_class, GduDriveView, format_disk_button);
  gtk_widget_class_bind_template_child (widget_class, GduDriveView, create_disk_image_button);
  gtk_widget_class_bind_template_child (widget_class, GduDriveView, restore_disk_image_button);

  gtk_widget_class_bind_template_child (widget_class, GduDriveView, benchmark_disk_button);
  gtk_widget_class_bind_template_child (widget_class, GduDriveView, smart_disk_button);
  gtk_widget_class_bind_template_child (widget_class, GduDriveView, drive_settings_button);

  gtk_widget_class_bind_template_child (widget_class, GduDriveView, standby_button);
  gtk_widget_class_bind_template_child (widget_class, GduDriveView, wakeup_button);
  gtk_widget_class_bind_template_child (widget_class, GduDriveView, poweroff_button);

  gtk_widget_class_bind_template_callback (widget_class, format_disk_clicked_cb);
  gtk_widget_class_bind_template_callback (widget_class, create_disk_image_clicked_cb);
  gtk_widget_class_bind_template_callback (widget_class, restore_disk_image_clicked_cb);

  gtk_widget_class_bind_template_callback (widget_class, benchmark_disk_clicked_cb);
  gtk_widget_class_bind_template_callback (widget_class, smart_disk_clicked_cb);
  gtk_widget_class_bind_template_callback (widget_class, drive_settings_clicked_cb);

  gtk_widget_class_bind_template_callback (widget_class, standby_drive_clicked_cb);
  gtk_widget_class_bind_template_callback (widget_class, wakeup_drive_clicked_cb);
  gtk_widget_class_bind_template_callback (widget_class, poweroff_drive_clicked_cb);
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
  GtkWidget *child;

  g_return_if_fail (GDU_IS_DRIVE_VIEW (self));
  g_return_if_fail (!drive || GDU_IS_DRIVE (drive));

  if (self->drive == drive)
    return;

  if (drive)
    child = GTK_WIDGET (self->drive_page);
  else
    child = GTK_WIDGET (self->empty_page);

  gtk_stack_set_visible_child (self->main_stack, child);

  g_set_object (&self->drive, drive);

  if (drive)
    update_drive_view (self);
}
