/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*-
 *
 * Copyright (C) 2008-2011 Red Hat, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General
 * Public License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place, Suite 330,
 * Boston, MA 02111-1307, USA.
 *
 * Author: David Zeuthen <davidz@redhat.com>
 */

#include "config.h"
#include <glib/gi18n-lib.h>

#include <math.h>
#include <gdk/gdkkeysyms.h>
#include <gdk/gdkx.h>
#include <stdlib.h>

#include "gdupasswordstrengthwidget.h"

typedef struct _GduPasswordStrengthWidgetClass GduPasswordStrengthWidgetClass;

struct _GduPasswordStrengthWidget
{
  GtkHBox parent;

  GtkWidget *progress_bar;
  GtkWidget *notebook;

  gchar *password;
};

struct _GduPasswordStrengthWidgetClass
{
  GtkHBoxClass parent_class;
};

enum
{
  PROP_0,
  PROP_PASSWORD,
};

G_DEFINE_TYPE (GduPasswordStrengthWidget, gdu_password_strength_widget, GTK_TYPE_HBOX)

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

/* TODO: probably do something more sophisticated here */

/* This code is based on the Master Password dialog in Firefox
 * (pref-masterpass.js)
 * Original code triple-licensed under the MPL, GPL, and LGPL
 * so is license-compatible with this file
 */
static gdouble
compute_password_strength (const gchar *password)
{
  gint length;
  gint upper, lower, digit, misc;
  gint i;
  gdouble strength;

  length = strlen (password);
  upper = 0;
  lower = 0;
  digit = 0;
  misc = 0;

  for (i = 0; i < length ; i++)
    {
      if (g_ascii_isdigit (password[i]))
        digit++;
      else if (g_ascii_islower (password[i]))
        lower++;
      else if (g_ascii_isupper (password[i]))
        upper++;
      else
        misc++;
    }

  if (length > 5)
    length = 5;

  if (digit > 3)
    digit = 3;

  if (upper > 3)
    upper = 3;

  if (misc > 3)
    misc = 3;

  strength = ((length * 0.1) - 0.2) +
    (digit * 0.1) +
    (misc * 0.15) +
    (upper * 0.1);

  strength = CLAMP (strength, 0.0, 1.0);

  return strength;
}

/* ---------------------------------------------------------------------------------------------------- */

static const gchar *strengths[4] =
{
  N_("Weak"),
  N_("Fair"),
  N_("Good"),
  N_("Strong"),
};

#define NUM_STRENGTH_LABELS 4

static void
update (GduPasswordStrengthWidget *widget)
{
  gdouble strength;
  gint tab_num;

  if (widget->password != NULL)
    strength = compute_password_strength (widget->password);
  else
    strength = 0.0;

  g_warn_if_fail (strength >= 0.0 && strength <= 1.0);

  tab_num = (gint) floor (NUM_STRENGTH_LABELS * strength);
  if (tab_num < 0)
    tab_num = 0;
  if (tab_num > NUM_STRENGTH_LABELS - 1)
    tab_num = NUM_STRENGTH_LABELS - 1;
  gtk_notebook_set_current_page (GTK_NOTEBOOK (widget->notebook), tab_num);
  gtk_progress_bar_set_fraction (GTK_PROGRESS_BAR (widget->progress_bar), strength);
}


static void
gdu_password_strength_widget_constructed (GObject *object)
{
  GduPasswordStrengthWidget *widget = GDU_PASSWORD_STRENGTH_WIDGET (object);
  guint n;

  gtk_box_set_spacing (GTK_BOX (widget), 6);

  widget->progress_bar = gtk_progress_bar_new ();
  gtk_box_pack_start (GTK_BOX (widget), widget->progress_bar, TRUE, TRUE, 0);

  widget->notebook = gtk_notebook_new ();
  gtk_notebook_set_show_tabs (GTK_NOTEBOOK (widget->notebook), FALSE);
  gtk_box_pack_start (GTK_BOX (widget), widget->notebook, FALSE, TRUE, 0);

  for (n = 0; n < NUM_STRENGTH_LABELS; n++)
    {
      GtkWidget *label;
      gchar *s;
      label = gtk_label_new (NULL);
      gtk_misc_set_alignment (GTK_MISC (label), 0, 0.5);
      s = g_strdup_printf ("<small>%s</small>", gettext (strengths[n]));
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
