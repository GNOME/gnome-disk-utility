/*
 * Copyright (C) 2017 Kai Lüke
 *
 * Licensed under GPL version 2 or later.
 *
 * Author: Kai Lüke <kailueke@riseup.net>
 */

#include "config.h"

#include "gdu-create-password-page.h"

#include <glib/gi18n.h>
#include <pwquality.h>

typedef enum
{
  PROP_COMPLETE,
} GduCreatePasswordPageProps;

static GParamSpec *props[PROP_COMPLETE + 1] = { NULL, };

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

G_DEFINE_FINAL_TYPE (GduCreatePasswordPage, gdu_create_password_page, ADW_TYPE_BIN);

static const gchar *
pw_error_hint (gint error)
{
  switch (error)
    {
      case PWQ_ERROR_CASE_CHANGES_ONLY:
        return C_("Password hint", "Try changing some letters and numbers.");
      case PWQ_ERROR_TOO_SIMILAR:
        return C_("Password hint", "Try changing the password a bit more.");
      case PWQ_ERROR_BAD_WORDS:
        return C_("Password hint", "Try to avoid some of the words included in the password.");
      case PWQ_ERROR_ROTATED:
        return C_("Password hint", "Try changing the password a bit more.");
      case PWQ_ERROR_CRACKLIB_CHECK:
        return C_("Password hint", "Try to avoid common words.");
      case PWQ_ERROR_PALINDROME:
        return C_("Password hint", "Try to avoid reordering existing words.");
      case PWQ_ERROR_MIN_DIGITS:
        return C_("Password hint", "Try to use more numbers.");
      case PWQ_ERROR_MIN_UPPERS:
        return C_("Password hint", "Try to use more uppercase letters.");
      case PWQ_ERROR_MIN_LOWERS:
        return C_("Password hint", "Try to use more lowercase letters.");
      case PWQ_ERROR_MIN_OTHERS:
        return C_("Password hint", "Try to use more special characters, like punctuation.");
      case PWQ_ERROR_MIN_CLASSES:
        return C_("Password hint", "Try to use a mixture of letters, numbers and punctuation.");
      case PWQ_ERROR_MAX_CONSECUTIVE:
        return C_("Password hint", "Try to avoid repeating the same character.");
      case PWQ_ERROR_MAX_CLASS_REPEAT:
        return C_("Password hint", "Try to avoid repeating the same type of character: you need to mix up letters, numbers and punctuation.");
      case PWQ_ERROR_MAX_SEQUENCE:
        return C_("Password hint", "Try to avoid sequences like 1234 or abcd.");
      case PWQ_ERROR_EMPTY_PASSWORD:
        return C_("Password hint", "Mix uppercase and lowercase and try to use a number or two.");
      default:
        return C_("Password hint", "Adding more letters, numbers and punctuation will make the password stronger.");
    }
}

static pwquality_settings_t *
get_pwq (void)
{
  static pwquality_settings_t *settings = NULL;

  if (settings == NULL)
    {
      gchar *err = NULL;
      settings = pwquality_default_settings ();
      if (pwquality_read_config (settings, NULL, (gpointer)&err) < 0)
        {
          g_error ("Failed to read pwquality configuration: %s\n", err);
        }
    }

  return settings;
}

gdouble
gdu_password_strength (const gchar  *password,
                       const gchar **hint,
                       gint         *strength_level)
{
  gint rv, level, length = 0;
  gdouble strength = 0.0;
  void *auxerror;

  rv = pwquality_check (get_pwq (),
                        password,
                        NULL, /* old_password */
                        NULL, /* username */
                        &auxerror);

  if (password != NULL)
    length = strlen (password);

  strength = CLAMP (0.01 * rv, 0.0, 1.0);
  if (rv < 0) {
    level = (length > 0) ? 1 : 0;
  } else if (strength < 0.50) {
    level = 2;
  } else if (strength < 0.75) {
    level = 3;
  } else if (strength < 0.90) {
    level = 4;
  } else {
    level = 5;
  }

  *hint = pw_error_hint (rv);

  if (strength_level)
    *strength_level = level;

  return strength;
}

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

  gdu_password_strength (password, &hint, &strength_level);

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
  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_COMPLETE]);
}

static void
gdu_create_password_page_get_property (GObject    *object,
                                       guint       property_id,
                                       GValue     *value,
                                       GParamSpec *pspec)
{
  GduCreatePasswordPage *self = GDU_CREATE_PASSWORD_PAGE (object);

  switch ((GduCreatePasswordPageProps) property_id)
    {
    case PROP_COMPLETE:
      g_value_set_boolean (value, self->complete);
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

  props[PROP_COMPLETE] = g_param_spec_boolean ("complete",
                                               NULL, NULL, FALSE,
                                               G_PARAM_READABLE |
                                               G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, G_N_ELEMENTS (props), props);
}

GduCreatePasswordPage *
gdu_create_password_page_new (void)
{
  return g_object_new (GDU_TYPE_CREATE_PASSWORD_PAGE, NULL);
}
