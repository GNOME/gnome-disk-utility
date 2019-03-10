/*
 * Copyright (C) 2017 Kai Lüke
 *
 * Licensed under GPL version 2 or later.
 *
 * Author: Kai Lüke <kailueke@riseup.net>
 */

#include "config.h"

#include <glib/gi18n.h>

#include "gducreateotherpage.h"

struct _GduCreateOtherPage
{
  GtkBox parent_instance;
};

typedef struct _GduCreateOtherPagePrivate GduCreateOtherPagePrivate;

struct _GduCreateOtherPagePrivate
{
  GtkBox *other_fs_box;
  GtkCheckButton *other_encrypt_checkbutton;

  UDisksClient *client;
  const gchar *other_fs_type;
  GtkRadioButton *prev_other_fs_radio;
  gint current_index;
};

enum
{
  PROP_0,
  PROP_COMPLETE
};

G_DEFINE_TYPE_WITH_PRIVATE (GduCreateOtherPage, gdu_create_other_page, GTK_TYPE_BOX);

static void
gdu_create_other_page_init (GduCreateOtherPage *page)
{
  gtk_widget_init_template (GTK_WIDGET (page));
}

static void
gdu_create_other_page_get_property (GObject    *object,
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
gdu_create_other_page_class_init (GduCreateOtherPageClass *class)
{
  GObjectClass *gobject_class;

  gtk_widget_class_set_template_from_resource (GTK_WIDGET_CLASS (class),
                                               "/org/gnome/Disks/ui/create-other-page.ui");
  gtk_widget_class_bind_template_child_private (GTK_WIDGET_CLASS (class), GduCreateOtherPage, other_fs_box);
  gtk_widget_class_bind_template_child_private (GTK_WIDGET_CLASS (class), GduCreateOtherPage, other_encrypt_checkbutton);

  gobject_class = G_OBJECT_CLASS (class);
  gobject_class->get_property = gdu_create_other_page_get_property;
  g_object_class_install_property (gobject_class, PROP_COMPLETE,
                                   g_param_spec_boolean ("complete", NULL, NULL,
                                                         TRUE,
                                                         G_PARAM_READABLE |
                                                         G_PARAM_STATIC_STRINGS));
}

static void
row_adder (GduCreateOtherPage *page, gboolean tested, gboolean available, gchar *util);

const gchar *
gdu_create_other_page_get_fs (GduCreateOtherPage *page)
{
  GduCreateOtherPagePrivate *priv;

  priv = gdu_create_other_page_get_instance_private (page);

  return priv->other_fs_type;
}

gboolean
gdu_create_other_page_is_encrypted (GduCreateOtherPage *page)
{
  GduCreateOtherPagePrivate *priv;

  priv = gdu_create_other_page_get_instance_private (page);

  return gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (priv->other_encrypt_checkbutton));
}

static const gchar *other_fs[] = {
  "xfs", "swap", "btrfs", "f2fs", "exfat", "udf", "empty", NULL
};

static const gchar *
get_fs_description (const gchar *fs_type)
{
  if (g_strcmp0 (fs_type, "xfs") == 0)
    return _("XFS — Linux Filesystem");
  else if (g_strcmp0 (fs_type, "swap") == 0)
    return _("Linux Swap Partition");
  else if (g_strcmp0 (fs_type, "btrfs") == 0)
    return _("Btrfs — Copy-on-write Linux Filesystem, for snapshots");
  else if (g_strcmp0 (fs_type, "f2fs") == 0)
    return _("F2FS — Flash Storage Linux Filesystem");
  else if (g_strcmp0 (fs_type, "exfat") == 0)
    return _("exFAT — Flash Storage Windows Filesystem, used on SDXC cards");
  else if (g_strcmp0 (fs_type, "udf") == 0)
    return _("UDF — Universal Disk Format, for removable devices on many systems");
  else if (g_strcmp0 (fs_type, "empty") == 0)
    return _("No Filesystem");
  else
    return fs_type;
}

static void
on_other_fs_selected (GtkToggleButton *object, GduCreateOtherPage *page)
{
  GduCreateOtherPagePrivate *priv;

  priv = gdu_create_other_page_get_instance_private (page);

  priv->other_fs_type = g_object_get_data (G_OBJECT (object), "id");
}

static void
can_format_cb (UDisksManager *manager,
               GAsyncResult  *res,
               gpointer       user_data)
{
  GVariant *out_available;
  GError *error = NULL;
  gboolean available = FALSE;
  gchar *util = NULL;

  if (!udisks_manager_call_can_format_finish (manager, &out_available, res, &error))
    {
      available = FALSE;
      g_clear_error (&error);
    }
  else
    {
      g_variant_get (out_available, "(bs)", &available, &util);
      g_variant_unref (out_available);
    }

  row_adder (user_data, TRUE, available, util);
  g_free (util);
}

static void
row_adder (GduCreateOtherPage *page, gboolean tested, gboolean available, gchar *missing_util)
{
  GSList *group = NULL;
  GduCreateOtherPagePrivate *priv;
  const gchar *id;

  priv = gdu_create_other_page_get_instance_private (page);
  id = other_fs[priv->current_index];

  if (id == NULL)
    {
      gtk_widget_show_all (GTK_WIDGET (priv->other_fs_box));
      return;
    }

   if (!tested)
    {
      udisks_manager_call_can_format (udisks_client_get_manager (priv->client), id,
                                      NULL, (GAsyncReadyCallback) can_format_cb, page);
      return;
    }

  if (priv->prev_other_fs_radio != NULL)
    group = gtk_radio_button_get_group (priv->prev_other_fs_radio);

  priv->prev_other_fs_radio = GTK_RADIO_BUTTON (gtk_radio_button_new_with_label (group, get_fs_description (id)));
  gtk_box_pack_start (GTK_BOX (priv->other_fs_box), GTK_WIDGET (priv->prev_other_fs_radio), TRUE, TRUE, 0);
  g_signal_connect (priv->prev_other_fs_radio, "toggled", G_CALLBACK (on_other_fs_selected), page);
  g_object_set_data_full (G_OBJECT (priv->prev_other_fs_radio), "id", g_strdup (id), g_free);

  if (!available)
    {
      gchar *s;

      gtk_widget_set_sensitive (GTK_WIDGET (priv->prev_other_fs_radio), FALSE);
      s = g_strdup_printf (_("The utility %s is missing."), missing_util);
      gtk_widget_set_tooltip_text (GTK_WIDGET (priv->prev_other_fs_radio), s);

      g_free (s);
    }

  if (priv->other_fs_type == NULL && available)
    {
      priv->other_fs_type = id;
      gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (priv->prev_other_fs_radio), TRUE);
    }

  priv->current_index++;
  row_adder (page, FALSE, TRUE, NULL);
}

static void
on_fs_type_changed (GtkToggleButton *object, gpointer user_data)
{
  GduCreateOtherPage *page = GDU_CREATE_OTHER_PAGE (user_data);

  g_object_notify (G_OBJECT (page), "complete");
}


GduCreateOtherPage *
gdu_create_other_page_new (UDisksClient *client)
{
  GduCreateOtherPage *page;
  GduCreateOtherPagePrivate *priv;

  page = g_object_new (GDU_TYPE_CREATE_OTHER_PAGE, NULL);
  priv = gdu_create_other_page_get_instance_private (page);
  priv->client = client;
  g_signal_connect (priv->other_encrypt_checkbutton, "toggled", G_CALLBACK (on_fs_type_changed), page);

  /* custom format page content is empty on loading */
  priv->other_fs_type = NULL;
  priv->current_index = 0;
  row_adder (page, FALSE, TRUE, NULL);

  return page;
}
