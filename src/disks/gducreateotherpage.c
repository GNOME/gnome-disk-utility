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

static const char *other_fs[][2] = {
  {"xfs", N_("XFS — Linux Filesystem")},
  {"swap", N_("Linux Swap Partition")},
  {"btrfs", N_("Btrfs — Copy-on-write Linux Filesystem, for snapshots")},
  {"f2fs", N_("F2FS — Flash Storage Linux Filesystem")},
  {"exfat", N_("exFAT — Flash Storage Windows Filesystem, used on SDXC cards")},
  {"udf", N_("UDF — Universal Disk Format, for removable devices on many systems")},
  {"empty", N_("No Filesystem")},
};

struct _GduCreateOtherPage
{
  GtkBox parent_instance;
};

typedef struct _GduCreateOtherPagePrivate GduCreateOtherPagePrivate;

struct _GduCreateOtherPagePrivate
{
  GtkBox *other_fs_box;
  GtkCheckButton *other_encrypt_checkbutton;
  GtkRadioButton *group_radio_button;
  /* The first item from the radio button that's sensitive */
  GtkRadioButton *first_sensitive_button;

  UDisksClient *client;
  const char   *selected_fs_type;
};

enum
{
  PROP_0,
  PROP_COMPLETE
};

G_DEFINE_TYPE_WITH_PRIVATE (GduCreateOtherPage, gdu_create_other_page, GTK_TYPE_BOX);

static void
on_other_fs_selected (GduCreateOtherPage *self,
                      GtkToggleButton    *button)
{
  GduCreateOtherPagePrivate *priv;
  guint fs_index;

  priv = gdu_create_other_page_get_instance_private (self);

  fs_index = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (button), "index"));
  priv->selected_fs_type = other_fs[fs_index][0];
}

static void
can_format_cb (UDisksManager *manager,
               GAsyncResult  *res,
               gpointer       user_data)
{
  GduCreateOtherPage *self;
  GduCreateOtherPagePrivate *priv;
  GtkToggleButton *toggle_button = user_data;
  g_autoptr(GVariant) out_available = NULL;
  g_autofree char *util = NULL;
  gboolean available = FALSE;

  self = g_object_get_data (G_OBJECT (toggle_button), "parent");
  priv = gdu_create_other_page_get_instance_private (self);

  if (!udisks_manager_call_can_format_finish (manager, &out_available, res, NULL))
    available = FALSE;
  else
    g_variant_get (out_available, "(bs)", &available, &util);

  if (!available)
    {
      g_autofree char *tooltip = NULL;

      gtk_widget_set_sensitive (GTK_WIDGET (toggle_button), FALSE);
      tooltip = g_strdup_printf (_("The utility %s is missing."), util);
      gtk_widget_set_tooltip_text (GTK_WIDGET (toggle_button), tooltip);
    }

  /* Select the first sensitive radio button */
  if (available && !priv->first_sensitive_button)
    {
      gtk_toggle_button_set_active (toggle_button, TRUE);
      priv->first_sensitive_button = GTK_RADIO_BUTTON (toggle_button);
    }
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
  gtk_widget_class_bind_template_child_private (GTK_WIDGET_CLASS (class), GduCreateOtherPage, group_radio_button);

  gobject_class = G_OBJECT_CLASS (class);
  gobject_class->get_property = gdu_create_other_page_get_property;
  g_object_class_install_property (gobject_class, PROP_COMPLETE,
                                   g_param_spec_boolean ("complete", NULL, NULL,
                                                         TRUE,
                                                         G_PARAM_READABLE |
                                                         G_PARAM_STATIC_STRINGS));
}

static void
gdu_create_other_page_init (GduCreateOtherPage *self)
{
  gtk_widget_init_template (GTK_WIDGET (self));
}

const gchar *
gdu_create_other_page_get_fs (GduCreateOtherPage *page)
{
  GduCreateOtherPagePrivate *priv;

  priv = gdu_create_other_page_get_instance_private (page);

  return priv->selected_fs_type;
}

gboolean
gdu_create_other_page_is_encrypted (GduCreateOtherPage *page)
{
  GduCreateOtherPagePrivate *priv;

  priv = gdu_create_other_page_get_instance_private (page);

  return gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (priv->other_encrypt_checkbutton));
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

  for (guint i = 0; i < G_N_ELEMENTS (other_fs); i++)
    {
      GtkWidget *radio;
      const char *id;

      id = other_fs[i][0];

      radio = gtk_radio_button_new_with_label_from_widget (priv->group_radio_button, other_fs[i][1]);
      g_object_set_data (G_OBJECT (radio), "index", GINT_TO_POINTER (i));
      g_object_set_data (G_OBJECT (radio), "parent", page);
      gtk_widget_show (radio);

      gtk_box_pack_start (GTK_BOX (priv->other_fs_box), radio, TRUE, TRUE, 0);
      g_signal_connect_object (radio, "toggled",
                               G_CALLBACK (on_other_fs_selected),
                               page, G_CONNECT_SWAPPED);

      udisks_manager_call_can_format (udisks_client_get_manager (priv->client), id,
                                      NULL, (GAsyncReadyCallback)can_format_cb, radio);
    }

  return page;
}
