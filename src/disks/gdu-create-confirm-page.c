/*
 * Copyright (C) 2017 Kai Lüke
 *
 * Licensed under GPL version 2 or later.
 *
 * Author: Kai Lüke <kailueke@riseup.net>
 */

#include "config.h"

#include <glib/gi18n.h>

#include "gdu-create-confirm-page.h"

enum
{
  PROP_0,
  PROP_COMPLETE
};

struct _GduCreateConfirmPage
{
  AdwBin         parent_instance;

  GtkWidget     *device_row;
  GtkWidget     *volume_row;
  GtkWidget     *usage_row;
  GtkWidget     *location_row;

  UDisksClient  *client;
  UDisksObject  *object;
  UDisksBlock   *block;
};

G_DEFINE_TYPE (GduCreateConfirmPage, gdu_create_confirm_page, ADW_TYPE_BIN);

static void
gdu_create_confirm_page_set_device_name (GduCreateConfirmPage *self)
{
  g_autoptr(UDisksObjectInfo) info;
  g_autofree char *s = NULL;

  info = udisks_client_get_object_info (self->client, self->object);

  /* Translators: In most cases this should not need translation unless the
   *              separation character '—' is not appropriate. The strings come
   *              from UDisks, first is description, second the name:
   *              "Partition 1 of 32 GB Flash Disk — /dev/sdb1".
   */
  s = g_strdup_printf (_("%s — %s"),
                       udisks_object_info_get_description (info),
                       udisks_object_info_get_name (info));

  adw_action_row_set_subtitle (ADW_ACTION_ROW (self->device_row), s);
}

static void
gdu_create_confirm_page_set_volume_label (GduCreateConfirmPage *self)
{
  const gchar *s = NULL;

  s = udisks_block_get_id_label (self->block);
  if (s == NULL || strlen (s) == 0)
    {
      s = udisks_block_get_id_type (self->block);
    }

  adw_action_row_set_subtitle (ADW_ACTION_ROW (self->volume_row), s);
}

static void
gdu_create_confirm_page_set_location (GduCreateConfirmPage *self)
{
  adw_action_row_set_subtitle (ADW_ACTION_ROW (self->location_row),
                               udisks_block_get_preferred_device (self->block));
}

static void
gdu_create_confirm_page_set_usage (GduCreateConfirmPage *self)
{
  gint64 unused_space = -1;
  gint64 size;
  g_autofree char *s1 = NULL;
  g_autofree char *s2 = NULL;

  unused_space = gdu_utils_get_unused_for_block (self->client, self->block);

  size = udisks_block_get_size (self->block);
  if (unused_space > 0)
    {
      gtk_widget_set_visible (GTK_WIDGET (self->usage_row), TRUE);
      s1 = udisks_client_get_size_for_display (self->client,
                                               size - unused_space,
                                               FALSE, FALSE);
      /* Translators: Disk usage in the format '3 GB (7%)', unit string comes
       * from UDisks.
       */
      s2 = g_strdup_printf (_ ("%s (%.1f%%)"), s1, 100.0 * (size - unused_space) / size);
      adw_action_row_set_subtitle (ADW_ACTION_ROW (self->usage_row), s2);
    }
}

static void
gdu_create_confirm_page_get_property (GObject    *object,
                                      guint       property_id,
                                      GValue     *value,
                                      GParamSpec *pspec)
{
  switch (property_id)
    {
    case PROP_COMPLETE:
      g_value_set_boolean (value, TRUE);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
    }
}

static void
gdu_create_confirm_page_init (GduCreateConfirmPage *self)
{
  gtk_widget_init_template (GTK_WIDGET (self));
}

static void
gdu_create_confirm_page_class_init (GduCreateConfirmPageClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->get_property = gdu_create_confirm_page_get_property;

  gtk_widget_class_set_template_from_resource (widget_class,
                                               "/org/gnome/DiskUtility/ui/"
                                               "gdu-create-confirm-page.ui");

  /* confirm page with information on current device usage */
  gtk_widget_class_bind_template_child (widget_class, GduCreateConfirmPage, device_row);
  gtk_widget_class_bind_template_child (widget_class, GduCreateConfirmPage, volume_row);
  gtk_widget_class_bind_template_child (widget_class, GduCreateConfirmPage, usage_row);
  gtk_widget_class_bind_template_child (widget_class, GduCreateConfirmPage, location_row);

  g_object_class_install_property (object_class, PROP_COMPLETE,
                                   g_param_spec_boolean ("complete",
                                                         NULL, NULL, TRUE,
                                                         G_PARAM_READABLE |
                                                         G_PARAM_STATIC_STRINGS));
}

GduCreateConfirmPage *
gdu_create_confirm_page_new (UDisksClient *client,
                             UDisksObject *object,
                             UDisksBlock  *block)
{
  GduCreateConfirmPage *self;

  self = g_object_new (GDU_TYPE_CREATE_CONFIRM_PAGE, NULL);
  self->client = client;
  self->object = object;
  self->block = block;

  gdu_create_confirm_page_set_device_name (self);
  gdu_create_confirm_page_set_volume_label (self);
  gdu_create_confirm_page_set_usage (self);
  gdu_create_confirm_page_set_location (self);

  return self;
}
