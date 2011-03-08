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
#include "gdutreemodel.h"

struct _GduApplication
{
  GtkApplication parent_instance;

  GtkBuilder *builder;
  GtkWindow *window;
  gboolean running_from_source_tree;

  UDisksClient *client;

  GduTreeModel *model;
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
    goto out;

  g_object_unref (app->model);

 out:
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

static gboolean
dont_select_headings (GtkTreeSelection *selection,
                      GtkTreeModel     *model,
                      GtkTreePath      *path,
                      gboolean          selected,
                      gpointer          data)
{
  GtkTreeIter iter;
  gboolean is_heading;

  gtk_tree_model_get_iter (model,
                           &iter,
                           path);
  gtk_tree_model_get (model,
                      &iter,
                      GDU_TREE_MODEL_COLUMN_IS_HEADING,
                      &is_heading,
                      -1);

  return !is_heading;
}

static void
on_row_inserted (GtkTreeModel *tree_model,
                 GtkTreePath  *path,
                 GtkTreeIter  *iter,
                 gpointer      user_data)
{
  GduApplication *app = GDU_APPLICATION (user_data);
  gtk_tree_view_expand_all (GTK_TREE_VIEW (gdu_application_get_widget (app, "lunlist-treeview")));
}

static void
gdu_application_activate (GApplication *_app)
{
  GduApplication *app = GDU_APPLICATION (_app);
  GError *error;
  GtkNotebook *notebook;
  GtkTreeView *tree_view;
  GtkTreeViewColumn *column;
  GtkCellRenderer *renderer;
  GtkTreeSelection *selection;
  const gchar *path;

  if (app->builder != NULL)
    return;

  error = NULL;
  app->client = udisks_client_new_sync (NULL, /* GCancellable* */
                                        &error);
  if (app->client == NULL)
    {
      g_error ("Error getting udisks client: %s", error->message);
      g_error_free (error);
    }

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

  app->window = GTK_WINDOW (gdu_application_get_widget (app, "palimpsest-window"));
  gtk_application_add_window (GTK_APPLICATION (app), app->window);
  gtk_widget_show_all (GTK_WIDGET (app->window));

  notebook = GTK_NOTEBOOK (gdu_application_get_widget (app, "palimpsest-notebook"));
  gtk_notebook_set_show_tabs (notebook, FALSE);
  gtk_notebook_set_show_border (notebook, FALSE);

  GtkStyleContext *context;
  context = gtk_widget_get_style_context (gdu_application_get_widget (app, "lunlist-scrolledwindow"));
  gtk_style_context_set_junction_sides (context, GTK_JUNCTION_BOTTOM);
  context = gtk_widget_get_style_context (gdu_application_get_widget (app, "lunlist-add-remove-toolbar"));
  gtk_style_context_set_junction_sides (context, GTK_JUNCTION_TOP);

  app->model = gdu_tree_model_new (app->client);

  tree_view = GTK_TREE_VIEW (gdu_application_get_widget (app, "lunlist-treeview"));
  gtk_tree_view_set_model (tree_view, GTK_TREE_MODEL (app->model));
  gtk_tree_sortable_set_sort_column_id (GTK_TREE_SORTABLE (app->model),
                                        GDU_TREE_MODEL_COLUMN_SORT_KEY,
                                        GTK_SORT_ASCENDING);

  selection = gtk_tree_view_get_selection (tree_view);
  gtk_tree_selection_set_select_function (selection, dont_select_headings, NULL, NULL);

  column = gtk_tree_view_column_new ();
  gtk_tree_view_append_column (tree_view, column);

  renderer = gtk_cell_renderer_text_new ();
  gtk_tree_view_column_pack_start (column, renderer, FALSE);
  gtk_tree_view_column_set_attributes (column,
                                       renderer,
                                       "markup", GDU_TREE_MODEL_COLUMN_HEADING_TEXT,
                                       "visible", GDU_TREE_MODEL_COLUMN_IS_HEADING,
                                       NULL);

  renderer = gtk_cell_renderer_pixbuf_new ();
  g_object_set (G_OBJECT (renderer),
                "stock-size", GTK_ICON_SIZE_DND,
                NULL);
  gtk_tree_view_column_pack_start (column, renderer, FALSE);
  gtk_tree_view_column_set_attributes (column,
                                       renderer,
                                       "gicon", GDU_TREE_MODEL_COLUMN_ICON,
                                       NULL);
  renderer = gtk_cell_renderer_text_new ();
  gtk_tree_view_column_pack_start (column, renderer, FALSE);
  gtk_tree_view_column_set_attributes (column,
                                       renderer,
                                       "text", GDU_TREE_MODEL_COLUMN_NAME,
                                       NULL);

  /* expand on insertion - hmm, I wonder if there's an easier way to do this */
  g_signal_connect (app->model,
                    "row-inserted",
                    G_CALLBACK (on_row_inserted),
                    app);
  gtk_tree_view_expand_all (tree_view);
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
