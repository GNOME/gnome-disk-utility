/*
 * Copyright (C) 2017 Kai Lüke
 *
 * Licensed under GPL version 2 or later.
 *
 * Author: Kai Lüke <kailueke@riseup.net>
 */

#include "config.h"

#include <glib/gi18n.h>

#include "gducreatepasswordpage.h"
#include "gdupasswordstrengthwidget.h"

struct _GduCreatePasswordPage
{
  GtkGrid parent_instance;
};

typedef struct _GduCreatePasswordPagePrivate GduCreatePasswordPagePrivate;

struct _GduCreatePasswordPagePrivate
{
  GtkEntry *password_entry;
  GtkEntry *confirm_password_entry;
  GtkCheckButton *show_password_checkbutton;
  GtkBox *password_strength_box;
  GtkWidget *password_strengh_widget;

  gboolean complete;
};

enum
{
  PROP_0,
  PROP_COMPLETE
};

G_DEFINE_TYPE_WITH_PRIVATE (GduCreatePasswordPage, gdu_create_password_page, GTK_TYPE_GRID);

static void
gdu_create_password_page_init (GduCreatePasswordPage *page)
{
  gtk_widget_init_template (GTK_WIDGET (page));
}

static void
gdu_create_password_page_get_property (GObject    *object,
                                       guint       property_id,
                                       GValue     *value,
                                       GParamSpec *pspec)
{
  GduCreatePasswordPage *page = GDU_CREATE_PASSWORD_PAGE (object);
  GduCreatePasswordPagePrivate *priv;

  priv = gdu_create_password_page_get_instance_private (page);

  switch (property_id)
    {
    case PROP_COMPLETE:
      g_value_set_boolean (value, priv->complete);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
    }
}

static void
gdu_create_password_page_class_init (GduCreatePasswordPageClass *class)
{
  GObjectClass *gobject_class;

  gtk_widget_class_set_template_from_resource (GTK_WIDGET_CLASS (class),
                                               "/org/gnome/Disks/ui/create-password-page.ui");
  gtk_widget_class_bind_template_child_private (GTK_WIDGET_CLASS (class), GduCreatePasswordPage, password_strength_box);
  gtk_widget_class_bind_template_child_private (GTK_WIDGET_CLASS (class), GduCreatePasswordPage, password_entry);
  gtk_widget_class_bind_template_child_private (GTK_WIDGET_CLASS (class), GduCreatePasswordPage, confirm_password_entry);
  gtk_widget_class_bind_template_child_private (GTK_WIDGET_CLASS (class), GduCreatePasswordPage, show_password_checkbutton);

  gobject_class = G_OBJECT_CLASS (class);
  gobject_class->get_property = gdu_create_password_page_get_property;
  g_object_class_install_property (gobject_class, PROP_COMPLETE,
                                   g_param_spec_boolean ("complete", NULL, NULL,
                                                         FALSE,
                                                         G_PARAM_READABLE |
                                                         G_PARAM_STATIC_STRINGS));
}

const gchar *
gdu_create_password_page_get_password (GduCreatePasswordPage *page)
{
  GduCreatePasswordPagePrivate *priv;

  priv = gdu_create_password_page_get_instance_private (page);

  return gtk_entry_get_text (priv->password_entry);
}

static void
on_password_changed (GObject *object, GParamSpec *pspec, gpointer user_data)
{
  GduCreatePasswordPage *page = GDU_CREATE_PASSWORD_PAGE (user_data);
  GduCreatePasswordPagePrivate *priv;
  gboolean can_proceed = FALSE;

  priv = gdu_create_password_page_get_instance_private (page);

  gtk_entry_set_icon_from_icon_name (priv->confirm_password_entry, GTK_ENTRY_ICON_SECONDARY, NULL);
  gtk_entry_set_icon_tooltip_text (priv->confirm_password_entry, GTK_ENTRY_ICON_SECONDARY, NULL);

  if (gtk_entry_get_text_length (priv->password_entry) > 0)
    {
      if (g_strcmp0 (gtk_entry_get_text (priv->password_entry),
                     gtk_entry_get_text (priv->confirm_password_entry)) == 0)
        {
          can_proceed = TRUE;
        }
      else if (gtk_entry_get_text_length (priv->confirm_password_entry) > 0)
        {
          gtk_entry_set_icon_from_icon_name (priv->confirm_password_entry, GTK_ENTRY_ICON_SECONDARY, "dialog-warning-symbolic");
          gtk_entry_set_icon_tooltip_text (priv->confirm_password_entry, GTK_ENTRY_ICON_SECONDARY, _("The passwords do not match"));
        }
    }

  gdu_password_strength_widget_set_password (GDU_PASSWORD_STRENGTH_WIDGET (priv->password_strengh_widget),
                                             gtk_entry_get_text (priv->password_entry));

  priv->complete = can_proceed;
  g_object_notify (G_OBJECT (page), "complete");
}

GduCreatePasswordPage *
gdu_create_password_page_new (void)
{
  GduCreatePasswordPage *page;
  GduCreatePasswordPagePrivate *priv;

  page = g_object_new (GDU_TYPE_CREATE_PASSWORD_PAGE, NULL);
  priv = gdu_create_password_page_get_instance_private (page);
  g_signal_connect (priv->password_entry, "notify::text", G_CALLBACK (on_password_changed), page);
  g_signal_connect (priv->confirm_password_entry, "notify::text", G_CALLBACK (on_password_changed), page);
  g_object_bind_property (priv->show_password_checkbutton, "active", priv->password_entry, "visibility", G_BINDING_SYNC_CREATE);
  g_object_bind_property (priv->show_password_checkbutton, "active", priv->confirm_password_entry, "visibility", G_BINDING_SYNC_CREATE);
  priv->password_strengh_widget = gdu_password_strength_widget_new ();
  gtk_box_pack_start (priv->password_strength_box, priv->password_strengh_widget, TRUE, TRUE, 0);

  return page;
}
