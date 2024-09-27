/* gdu-drive-header.c
 *
 * Copyright 2024 Christopher Davis <christopherdavis@gnome.org>
 *
 * Author(s)
 *   Christopher Davis <christopherdavis@gnome.org>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <adwaita.h>

#include "gdu-drive-header.h"
#include "glib.h"
#include "gtk/gtk.h"

struct _GduDriveHeader
{
  AdwBin parent_instance;

  AdwMultiLayoutView *layout_view;

  GtkImage      *drive_image;

  char *drive_name;
  char *drive_path;
};

G_DEFINE_FINAL_TYPE (GduDriveHeader, gdu_drive_header, ADW_TYPE_BIN)

enum {
  PROP_0,
  PROP_ICON,
  PROP_DRIVE_NAME,
  PROP_DRIVE_PATH,
  N_PROPS
};

static GParamSpec *properties [N_PROPS];


static GtkAlign
get_alignment_from_layout_name (GduDriveHeader     *self,
                                const char         *layout_name)
{
  if (g_strcmp0 (layout_name, "horizontal") == 0)
    return GTK_ALIGN_START;
  else
    return GTK_ALIGN_FILL;
}

static void
gdu_drive_header_set_property (GObject      *object,
                               guint         prop_id,
                               const GValue *value,
                               GParamSpec   *pspec)
{
  GduDriveHeader *self = GDU_DRIVE_HEADER (object);

  switch (prop_id)
    {
    case PROP_ICON:
      gdu_drive_header_set_icon (self, G_ICON (g_value_get_object (value)));
      break;
    case PROP_DRIVE_NAME:
      gdu_drive_header_set_drive_name (self, g_value_get_string (value));
      break;
    case PROP_DRIVE_PATH:
      gdu_drive_header_set_drive_path (self, g_value_get_string (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
gdu_drive_header_get_property (GObject    *object,
                               guint       prop_id,
                               GValue     *value,
                               GParamSpec *pspec)
{
  GduDriveHeader *self = GDU_DRIVE_HEADER (object);

  switch (prop_id)
    {
    case PROP_ICON:
      g_value_set_object (value, gtk_image_get_gicon (self->drive_image));
      break;
    case PROP_DRIVE_NAME:
      g_value_set_string (value, self->drive_name);
      break;
    case PROP_DRIVE_PATH:
      g_value_set_string (value, self->drive_path);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
gdu_drive_header_class_init (GduDriveHeaderClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->set_property = gdu_drive_header_set_property;
  object_class->get_property = gdu_drive_header_get_property;

  gtk_widget_class_set_template_from_resource (widget_class,
                                               "/org/gnome/DiskUtility/ui/"
                                               "gdu-drive-header.ui");

  gtk_widget_class_bind_template_child (widget_class, GduDriveHeader, layout_view);
  gtk_widget_class_bind_template_child (widget_class, GduDriveHeader, drive_image);

  gtk_widget_class_bind_template_callback (widget_class, get_alignment_from_layout_name);

  properties[PROP_ICON] =
    g_param_spec_object ("icon", NULL, NULL,
                         G_TYPE_ICON,
                         (G_PARAM_READWRITE |
                          G_PARAM_EXPLICIT_NOTIFY |
                          G_PARAM_STATIC_STRINGS));
  properties[PROP_DRIVE_NAME] =
    g_param_spec_string ("drive-name", NULL, NULL, "",
                         (G_PARAM_READWRITE |
                          G_PARAM_EXPLICIT_NOTIFY |
                          G_PARAM_STATIC_STRINGS));
  properties[PROP_DRIVE_PATH] =
    g_param_spec_string ("drive-path", NULL, NULL, "",
                         (G_PARAM_READWRITE |
                          G_PARAM_EXPLICIT_NOTIFY |
                          G_PARAM_STATIC_STRINGS));
  
  g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
gdu_drive_header_init (GduDriveHeader *self)
{
  gtk_widget_init_template (GTK_WIDGET (self));
}

void
gdu_drive_header_set_layout_name (GduDriveHeader *self,
                                  const char     *name)
{
  adw_multi_layout_view_set_layout_name (self->layout_view, name);
}


void
gdu_drive_header_set_icon (GduDriveHeader *self,
                           GIcon          *icon)
{
  g_assert (GDU_IS_DRIVE_HEADER (self));
  g_assert (G_IS_ICON (icon));

  gtk_image_set_from_gicon (self->drive_image, icon);
  g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_ICON]);
}

void
gdu_drive_header_set_drive_name (GduDriveHeader *self,
                                 const char     *name)
{
  g_assert (GDU_IS_DRIVE_HEADER (self));

  if (g_set_str (&self->drive_name, name))
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_DRIVE_NAME]);
}

void
gdu_drive_header_set_drive_path (GduDriveHeader *self,
                                 const char     *path)
{
  g_assert (GDU_IS_DRIVE_HEADER (self));

  if (g_set_str (&self->drive_path, path))
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_DRIVE_PATH]);
}