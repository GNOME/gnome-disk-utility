/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*-
 *
 * Copyright (C) 2008-2013 Red Hat, Inc.
 *
 * Licensed under GPL version 2 or later.
 *
 * Author: David Zeuthen <zeuthen@gmail.com>
 */

#include "config.h"
#include <glib/gi18n.h>

#include <math.h>
#include <gdk/gdkkeysyms.h>
#include <gdk/gdkx.h>
#include <stdlib.h>

#include <pwquality.h>

#include "gdupasswordstrengthwidget.h"

typedef struct _GduPasswordStrengthWidgetClass GduPasswordStrengthWidgetClass;

struct _GduPasswordStrengthWidget
{
  GtkBox parent;

  GtkWidget *level_bar;
  GtkWidget *notebook;

  gchar *password;
};

struct _GduPasswordStrengthWidgetClass
{
  GtkBoxClass parent_class;
};

enum
{
  PROP_0,
  PROP_PASSWORD,
};

G_DEFINE_TYPE (GduPasswordStrengthWidget, gdu_password_strength_widget, GTK_TYPE_BOX)

static void
gdu_password_strength_widget_finalize (GObject *object)
{
  GduPasswordStrengthWidget *widget = GDU_PASSWORD_STRENGTH_WIDGET (object);

  g_free (widget->password);

  G_OBJECT_CLASS (gdu_password_strength_widget_parent_class)->finalize (object);
}

static void
gdu_password_strength_widget_get_property (GObject    *object,
                                           guint       property_id,
                                           GValue     *value,
                                           GParamSpec *pspec)
{
  GduPasswordStrengthWidget *widget = GDU_PASSWORD_STRENGTH_WIDGET (object);

  switch (property_id)
    {
    case PROP_PASSWORD:
      g_value_set_string (value, widget->password);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
    }
}

static void
gdu_password_strength_widget_set_property (GObject      *object,
                                           guint         property_id,
                                           const GValue *value,
                                           GParamSpec   *pspec)
{
  GduPasswordStrengthWidget *widget = GDU_PASSWORD_STRENGTH_WIDGET (object);

  switch (property_id)
    {
    case PROP_PASSWORD:
      widget->password = g_value_dup_string (value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
    }
}

/* ---------------------------------------------------------------------------------------------------- */

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

enum {
  HINT_WEAK,
  HINT_FAIR,
  HINT_GOOD,
  HINT_STRONG,
  HINT_LAST
};

static const gchar *hint_labels[HINT_LAST] = {
  NC_("Password strength", "Weak"),
  NC_("Password strength", "Fair"),
  NC_("Password strength", "Good"),
  NC_("Password strength", "Strong"),
};

static gdouble
compute_password_strength (const gchar  *passphrase,
                           gint         *out_hint)
{
  gint rv;
  gdouble strength = 0.0;
  void *auxerror;
  gint hint;

  rv = pwquality_check (get_pwq (),
                        passphrase,
                        NULL, /* old_password */
                        NULL, /* username */
                        &auxerror);

  /* we ignore things like MIN_LENGTH and NOT_GOOD_ENOUGH errors because
   * this isn't about user accounts
   */
  strength = CLAMP (0.01 * rv, 0.0, 1.0);

  if (strength < 0.50)
    hint = HINT_WEAK;
  else if (strength < 0.75)
    hint = HINT_FAIR;
  else if (strength < 0.90)
    hint = HINT_GOOD;
  else
    hint = HINT_STRONG;

  if (out_hint != NULL)
    *out_hint = hint;
  return strength;
}

/* ---------------------------------------------------------------------------------------------------- */

static void
update (GduPasswordStrengthWidget *widget)
{
  gdouble strength = 0.0;
  gint tab_num = 0;

  if (widget->password != NULL)
    strength = compute_password_strength (widget->password, &tab_num);

  g_warn_if_fail (strength >= 0.0 && strength <= 1.0);
  g_warn_if_fail (tab_num >= 0 && tab_num < HINT_LAST);

  gtk_notebook_set_current_page (GTK_NOTEBOOK (widget->notebook), tab_num);
  gtk_level_bar_set_value (GTK_LEVEL_BAR (widget->level_bar), strength);
}


static void
gdu_password_strength_widget_constructed (GObject *object)
{
  GduPasswordStrengthWidget *widget = GDU_PASSWORD_STRENGTH_WIDGET (object);
  guint n;

  gtk_box_set_spacing (GTK_BOX (widget), 6);

  widget->level_bar = gtk_level_bar_new ();
  gtk_box_pack_start (GTK_BOX (widget), widget->level_bar, TRUE, TRUE, 0);

  widget->notebook = gtk_notebook_new ();
  gtk_notebook_set_show_tabs (GTK_NOTEBOOK (widget->notebook), FALSE);
  gtk_box_pack_start (GTK_BOX (widget), widget->notebook, FALSE, TRUE, 0);

  for (n = 0; n < G_N_ELEMENTS (hint_labels); n++)
    {
      GtkWidget *label;
      gchar *s;
      label = gtk_label_new (NULL);
      gtk_misc_set_alignment (GTK_MISC (label), 0, 0.5);
      s = g_strdup_printf ("<small>%s</small>",
                           g_dpgettext2 (NULL, "Password strength", hint_labels[n]));
      gtk_label_set_markup (GTK_LABEL (label), s);
      g_free (s);
      gtk_notebook_append_page (GTK_NOTEBOOK (widget->notebook), label, NULL);
    }

  gtk_widget_show_all (GTK_WIDGET (widget));

  update (widget);

  if (G_OBJECT_CLASS (gdu_password_strength_widget_parent_class)->constructed != NULL)
    G_OBJECT_CLASS (gdu_password_strength_widget_parent_class)->constructed (object);
}

static void
gdu_password_strength_widget_class_init (GduPasswordStrengthWidgetClass *klass)
{
  GObjectClass *gobject_class;

  gobject_class = G_OBJECT_CLASS (klass);
  gobject_class->get_property = gdu_password_strength_widget_get_property;
  gobject_class->set_property = gdu_password_strength_widget_set_property;
  gobject_class->finalize     = gdu_password_strength_widget_finalize;
  gobject_class->constructed  = gdu_password_strength_widget_constructed;

  g_object_class_install_property (gobject_class, PROP_PASSWORD,
                                   g_param_spec_string ("password", NULL, NULL,
                                                        NULL,
                                                        G_PARAM_READABLE |
                                                        G_PARAM_STATIC_STRINGS));
}

static void
gdu_password_strength_widget_init (GduPasswordStrengthWidget *widget)
{
}

GtkWidget *
gdu_password_strength_widget_new (void)
{
  return GTK_WIDGET (g_object_new (GDU_TYPE_PASSWORD_STRENGTH_WIDGET,
                                   NULL));
}

void
gdu_password_strength_widget_set_password (GduPasswordStrengthWidget *widget,
                                           const gchar               *password)
{
  g_return_if_fail (GDU_IS_PASSWORD_STRENGTH_WIDGET (widget));
  g_free (widget->password);
  widget->password = g_strdup (password);
  update (widget);
}
