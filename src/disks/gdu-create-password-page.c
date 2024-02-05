/*
 * Copyright (C) 2017 Kai Lüke
 *
 * Licensed under GPL version 2 or later.
 *
 * Author: Kai Lüke <kailueke@riseup.net>
 */

#include "config.h"

#include <glib/gi18n.h>

#include "gdu-create-password-page.h"

enum
{
  PROP_0,
  PROP_COMPLETE
};

struct _GduCreatePasswordPage
{
  AdwBin     parent_instance;

  GtkWidget *password_entry;
  GtkWidget *confirm_password_entry;
  GtkWidget *strength_indicator;
  GtkWidget *confirm_password_label;
  GtkWidget *strength_hint_label;

  gboolean complete;
};

G_DEFINE_TYPE (GduCreatePasswordPage, gdu_create_password_page, ADW_TYPE_BIN);

const gchar *
gdu_create_password_page_get_password (GduCreatePasswordPage *self)
{
  return gtk_editable_get_text (GTK_EDITABLE (self->password_entry));
}

static void
update_password_strength (GduCreatePasswordPage *self)
{
  gint strength_level;
  const gchar *hint;
  const gchar *password;
  const gchar *verify;

  password = gtk_editable_get_text (GTK_EDITABLE (self->password_entry));

  pw_strength (password, &hint, &strength_level);

  gtk_level_bar_set_value (GTK_LEVEL_BAR (self->strength_indicator), strength_level);
  gtk_label_set_label (GTK_LABEL (self->strength_hint_label), hint);

  if (strength_level > 0)
    {
      gtk_widget_remove_css_class (self->password_entry, "error");
    }
  else
    {
      gtk_widget_add_css_class (self->password_entry, "error");
    }

  verify = gtk_editable_get_text (GTK_EDITABLE (self->confirm_password_entry));
  if (strlen (verify) == 0)
    {
      gtk_widget_set_sensitive (self->confirm_password_entry, strength_level > 0);
    }
}

static void
on_password_changed (GduCreatePasswordPage *self)
{
  gboolean can_proceed = FALSE;
  const gchar *password = NULL;
  const gchar *verify = NULL;

  password = gtk_editable_get_text (GTK_EDITABLE (self->password_entry));
  verify = gtk_editable_get_text (GTK_EDITABLE (self->confirm_password_entry));

  gtk_widget_add_css_class (self->password_entry, "error");

  if (strlen (password) > 0)
    {
      if (g_strcmp0 (password, verify) == 0)
        {
          gtk_widget_remove_css_class (self->confirm_password_entry, "error");
          gtk_widget_set_visible (self->confirm_password_label, FALSE);
          can_proceed = TRUE;
        }
      else if (strlen (verify) > 0)
        {
          gtk_widget_add_css_class (self->confirm_password_entry, "error");
          gtk_widget_set_visible (self->confirm_password_label, TRUE);
        }
    }
  update_password_strength (self);

  self->complete = can_proceed;
  g_object_notify (G_OBJECT (self), "complete");
}

static void
gdu_create_password_page_get_property (GObject    *object,
                                       guint       property_id,
                                       GValue     *value,
                                       GParamSpec *pspec)
{
  GduCreatePasswordPage *self = GDU_CREATE_PASSWORD_PAGE (object);

  switch (property_id)
    {
    case PROP_COMPLETE:
      g_value_set_boolean (value, self->complete);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
    }
}

static void
gdu_create_password_page_init (GduCreatePasswordPage *self)
{
  gtk_widget_init_template (GTK_WIDGET (self));
}

static void
gdu_create_password_page_class_init (GduCreatePasswordPageClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->get_property = gdu_create_password_page_get_property;

  gtk_widget_class_set_template_from_resource (widget_class,
                                               "/org/gnome/DiskUtility/ui/"
                                               "gdu-create-password-page.ui");

  gtk_widget_class_bind_template_child (widget_class, GduCreatePasswordPage, password_entry);
  gtk_widget_class_bind_template_child (widget_class, GduCreatePasswordPage, confirm_password_entry);
  gtk_widget_class_bind_template_child (widget_class, GduCreatePasswordPage, strength_indicator);
  gtk_widget_class_bind_template_child (widget_class, GduCreatePasswordPage, confirm_password_label);
  gtk_widget_class_bind_template_child (widget_class, GduCreatePasswordPage, strength_hint_label);

  gtk_widget_class_bind_template_callback (widget_class, on_password_changed);

  g_object_class_install_property (object_class, PROP_COMPLETE,
                                   g_param_spec_boolean ("complete",
                                                         NULL, NULL, FALSE,
                                                         G_PARAM_READABLE |
                                                         G_PARAM_STATIC_STRINGS));
}

GduCreatePasswordPage *
gdu_create_password_page_new (void)
{
  return g_object_new (GDU_TYPE_CREATE_PASSWORD_PAGE, NULL);
}
