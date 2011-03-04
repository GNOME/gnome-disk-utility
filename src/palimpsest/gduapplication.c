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
#include <glib/gi18n.h>

#include "gduapplication.h"

struct _GduApplication
{
  GtkApplication parent_instance;
  GtkBuilder *builder;
  GtkWindow *window;
};

typedef struct
{
  GtkApplicationClass parent_class;
} GduApplicationClass;

G_DEFINE_TYPE (GduApplication, gdu_application, GTK_TYPE_APPLICATION);

static void
gdu_application_init (GduApplication *app)
{
  GError *error;

  app->builder = gtk_builder_new ();

  error = NULL;
  if (gtk_builder_add_from_file (app->builder,
                                 "../../data/ui/palimpsest.ui",
                                 &error) == 0)
    {
      g_error ("Error loading palimpsest.ui: %s", error->message);
      g_error_free (error);
    }
}

static void
gdu_application_finalize (GObject *object)
{
  GduApplication *app = GDU_APPLICATION (object);

  g_object_unref (app->builder);

  G_OBJECT_CLASS (gdu_application_parent_class)->finalize (object);
}

static void
gdu_application_activate (GApplication *_app)
{
  GduApplication *app = GDU_APPLICATION (_app);

  app->window = GTK_WINDOW (gtk_builder_get_object (app->builder, "palimpsest-window"));
  gtk_window_set_application (app->window, GTK_APPLICATION (app));
  gtk_widget_show_all (GTK_WIDGET (app->window));
}

static void
gdu_application_class_init (GduApplicationClass *klass)
{
  GObjectClass *gobject_class;
  GApplicationClass *application_class;

  gobject_class = G_OBJECT_CLASS (klass);
  gobject_class->finalize = gdu_application_finalize;

  application_class = G_APPLICATION_CLASS (klass);
  application_class->activate = gdu_application_activate;
}

GApplication *
gdu_application_new (void)
{
  gtk_init (NULL, NULL);
  return G_APPLICATION (g_object_new (GDU_TYPE_APPLICATION,
                                      "application-id", "org.gnome.DiskUtility",
                                      "flags", G_APPLICATION_FLAGS_NONE,
                                      NULL));
}

GtkWidget *
gdu_application_get_widget (GduApplication *app,
                            const gchar    *name)
{
  g_return_val_if_fail (GDU_IS_APPLICATION (app), NULL);
  g_return_val_if_fail (name != NULL, NULL);
  return GTK_WIDGET (gtk_builder_get_object (app->builder, name));
}

