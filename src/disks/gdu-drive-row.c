/* gdu-drive-row.c
 *
 * Copyright 2023 Mohammed Sadiq <sadiq@sadiqpk.org>
 *
 * Author(s):
 *   Mohammed Sadiq <sadiq@sadiqpk.org>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#define G_LOG_DOMAIN "gdu-drive-row"

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include "gdu-drive-row.h"

struct _GduDriveRow
{
  AdwActionRow  parent_instance;

  GtkImage     *drive_image;
  GduDrive     *drive;
};


G_DEFINE_TYPE (GduDriveRow, gdu_drive_row, ADW_TYPE_ACTION_ROW)

static void
update_drive_row (GduDriveRow *self)
{
  const char *description, *name;
  GIcon *icon;

  g_assert (GDU_IS_DRIVE_ROW (self));

  description = gdu_item_get_description (GDU_ITEM (self->drive));
  name = gdu_drive_get_name (self->drive);
  icon = gdu_item_get_icon (GDU_ITEM (self->drive));

  adw_preferences_row_set_title (ADW_PREFERENCES_ROW (self), description);
  adw_action_row_set_subtitle (ADW_ACTION_ROW (self), name);
  gtk_image_set_from_gicon (self->drive_image, icon);
}

static void
gdu_drive_row_finalize (GObject *object)
{
  GduDriveRow *self = (GduDriveRow *)object;

  g_clear_object (&self->drive);

  G_OBJECT_CLASS (gdu_drive_row_parent_class)->finalize (object);
}

static void
gdu_drive_row_class_init (GduDriveRowClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->finalize = gdu_drive_row_finalize;

  gtk_widget_class_set_template_from_resource (widget_class,
                                               "/org/gnome/DiskUtility/ui/"
                                               "gdu-drive-row.ui");

  gtk_widget_class_bind_template_child (widget_class, GduDriveRow, drive_image);
}

static void
gdu_drive_row_init (GduDriveRow *self)
{
  gtk_widget_init_template (GTK_WIDGET (self));
}

GduDriveRow *
gdu_drive_row_new (GduDrive *drive)
{
  GduDriveRow *self;

  g_return_val_if_fail (GDU_IS_DRIVE (drive), NULL);

  self = g_object_new (GDU_TYPE_DRIVE_ROW, NULL);
  self->drive = g_object_ref (drive);

  g_signal_connect_object (self->drive,
                           "changed",
                           G_CALLBACK (update_drive_row),
                           self, G_CONNECT_SWAPPED);
  update_drive_row (self);

  return self;
}

GduDrive *
gdu_drive_row_get_drive (GduDriveRow *self)
{
  g_return_val_if_fail (GDU_IS_DRIVE_ROW (self), NULL);

  return self->drive;
}
