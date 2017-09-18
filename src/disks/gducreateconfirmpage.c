/*
 * Copyright (C) 2017 Kai Lüke
 *
 * Licensed under GPL version 2 or later.
 *
 * Author: Kai Lüke <kailueke@riseup.net>
 */

#include "config.h"

#include <glib/gi18n.h>

#include "gducreateconfirmpage.h"

struct _GduCreateConfirmPage
{
  GtkGrid parent_instance;
};

typedef struct _GduCreateConfirmPagePrivate GduCreateConfirmPagePrivate;

struct _GduCreateConfirmPagePrivate
{
  GtkLabel *device_name_label;
  GtkLabel *volume_name_label;
  GtkLabel *used_label;
  GtkLabel *used_amount_label;
  GtkLabel *location_path_label;

  UDisksClient *client;
  UDisksObject *object;
  UDisksBlock *block;
};

enum
{
  PROP_0,
  PROP_COMPLETE
};

G_DEFINE_TYPE_WITH_PRIVATE (GduCreateConfirmPage, gdu_create_confirm_page, GTK_TYPE_GRID);

static void
gdu_create_confirm_page_init (GduCreateConfirmPage *page)
{
  gtk_widget_init_template (GTK_WIDGET (page));
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
gdu_create_confirm_page_class_init (GduCreateConfirmPageClass *class)
{
  GObjectClass *gobject_class;

  gtk_widget_class_set_template_from_resource (GTK_WIDGET_CLASS (class),
                                               "/org/gnome/Disks/ui/create-confirm-page.ui");
  /* confirm page with information on current device usage */
  gtk_widget_class_bind_template_child_private (GTK_WIDGET_CLASS (class), GduCreateConfirmPage, device_name_label);
  gtk_widget_class_bind_template_child_private (GTK_WIDGET_CLASS (class), GduCreateConfirmPage, volume_name_label);
  gtk_widget_class_bind_template_child_private (GTK_WIDGET_CLASS (class), GduCreateConfirmPage, used_amount_label);
  gtk_widget_class_bind_template_child_private (GTK_WIDGET_CLASS (class), GduCreateConfirmPage, used_label);
  gtk_widget_class_bind_template_child_private (GTK_WIDGET_CLASS (class), GduCreateConfirmPage, location_path_label);

  gobject_class = G_OBJECT_CLASS (class);
  gobject_class->get_property = gdu_create_confirm_page_get_property;
  g_object_class_install_property (gobject_class, PROP_COMPLETE,
                                   g_param_spec_boolean ("complete", NULL, NULL,
                                                         TRUE,
                                                         G_PARAM_READABLE |
                                                         G_PARAM_STATIC_STRINGS));
}

void
gdu_create_confirm_page_fill_confirmation (GduCreateConfirmPage *page)
{
  GduCreateConfirmPagePrivate *priv;
  UDisksObjectInfo *info;
  gint64 unused_space = -1;
  gint64 size;
  const gchar *s;
  gchar *s1;
  gchar *s2;

  priv = gdu_create_confirm_page_get_instance_private (page);
  /* gather data on current device usage for the confirmation page */
  info = udisks_client_get_object_info (priv->client, priv->object);
  unused_space = gdu_utils_get_unused_for_block (priv->client, priv->block);
  /* Translators: In most cases this should not need translation unless the
   *              separation character '—' is not appropriate. The strings come
   *              from UDisks, first is description, second the name:
   *              "Partition 1 of 32 GB Flash Disk — /dev/sdb1".
   */
  s1 = g_strdup_printf (_("%s — %s"), udisks_object_info_get_description (info),
                                      udisks_object_info_get_name (info));
  gtk_label_set_text (priv->device_name_label, s1);
  g_free (s1);

  s = udisks_block_get_id_label (priv->block);
  if (s != NULL && strlen(s) > 0)
    gtk_label_set_text (priv->volume_name_label, s);
  else
    gtk_label_set_text (priv->volume_name_label, udisks_block_get_id_type (priv->block));
  size = udisks_block_get_size (priv->block);
  if (unused_space > 0)
    {
      gtk_widget_show (GTK_WIDGET (priv->used_label));
      gtk_widget_show (GTK_WIDGET (priv->used_amount_label));
      s1 = udisks_client_get_size_for_display (priv->client, size - unused_space, FALSE, FALSE);
      /* Translators: Disk usage in the format '3 GB (7%)', unit string comes from UDisks.
       */
      s2 = g_strdup_printf (_("%s (%.1f%%)"), s1, 100.0 * (size - unused_space) / size);
      gtk_label_set_text (priv->used_amount_label, s2);
      g_free (s1);
      g_free (s2);
    }
  else
    {
      gtk_widget_hide (GTK_WIDGET (priv->used_label));
      gtk_widget_hide (GTK_WIDGET (priv->used_amount_label));
    }
  gtk_label_set_text (priv->location_path_label, udisks_block_get_preferred_device (priv->block));

  g_object_unref (info);
}

GduCreateConfirmPage *
gdu_create_confirm_page_new (UDisksClient *client, UDisksObject *object, UDisksBlock *block)
{
  GduCreateConfirmPage *page;
  GduCreateConfirmPagePrivate *priv;

  page = g_object_new (GDU_TYPE_CREATE_CONFIRM_PAGE, NULL);
  priv = gdu_create_confirm_page_get_instance_private (page);
  priv->client = client;
  priv->object = object;
  priv->block = block;

  return page;
}
