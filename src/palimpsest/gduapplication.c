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
  gboolean running_from_source_tree;
};

typedef struct
{
  GtkApplicationClass parent_class;
} GduApplicationClass;

G_DEFINE_TYPE (GduApplication, gdu_application, GTK_TYPE_APPLICATION);

static void
gdu_application_init (GduApplication *app)
{
}

static void
gdu_application_finalize (GObject *object)
{
  GduApplication *app = GDU_APPLICATION (object);

  if (app->builder != NULL)
    g_object_unref (app->builder);

  G_OBJECT_CLASS (gdu_application_parent_class)->finalize (object);
}

static gboolean
gdu_application_local_command_line (GApplication    *_app,
                                    gchar         ***arguments,
                                    int             *exit_status)
{
  GduApplication *app = GDU_APPLICATION (_app);

  /* figure out if running from source tree */
  if (g_strcmp0 ((*arguments)[0], "./palimpsest") == 0)
    app->running_from_source_tree = TRUE;

  /* chain up */
  return G_APPLICATION_CLASS (gdu_application_parent_class)->local_command_line (_app,
                                                                                 arguments,
                                                                                 exit_status);
}

static void
gdu_application_activate (GApplication *_app)
{
  GduApplication *app = GDU_APPLICATION (_app);
  GError *error;
  const gchar *path;

  if (app->builder != NULL)
    return;

  app->builder = gtk_builder_new ();
  error = NULL;
  path = app->running_from_source_tree ? "../../data/ui/palimpsest.ui" :
                                         PACKAGE_DATA_DIR "/gnome-disk-utility/palimpsest.ui";
  if (gtk_builder_add_from_file (app->builder,
                                 path,
                                 &error) == 0)
    {
      g_error ("Error loading %s: %s", path, error->message);
      g_error_free (error);
    }

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
  application_class->local_command_line = gdu_application_local_command_line;
  application_class->activate           = gdu_application_activate;
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

