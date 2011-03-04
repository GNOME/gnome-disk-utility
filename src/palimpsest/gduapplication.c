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

  GDBusProxyManager *proxy_manager;

  GtkTreeStore *lun_tree_store;

  GtkTreeIter das_iter;
  GtkTreeIter remote_iter;
  GtkTreeIter other_iter;
};

typedef struct
{
  GtkApplicationClass parent_class;
} GduApplicationClass;

enum
{
  LUN_TREE_COLUMN_SORT_KEY,
  LUN_TREE_COLUMN_IS_HEADING,
  LUN_TREE_COLUMN_HEADING_TEXT,
  LUN_TREE_COLUMN_ICON,
  LUN_TREE_COLUMN_NAME,
  LUN_TREE_COLUMN_OBJECT_PROXY,
  LUN_TREE_N_COLUMNS
};

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

  g_object_unref (app->lun_tree_store);
  g_object_unref (app->proxy_manager);
  g_object_unref (app->builder);

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
                      LUN_TREE_COLUMN_IS_HEADING,
                      &is_heading,
                      -1);

  return !is_heading;
}

typedef struct
{
  GDBusObjectProxy *object;
  GtkTreeIter iter;
  gboolean found;
} FindIterData;

static gboolean
find_iter_for_object_proxy_cb (GtkTreeModel  *model,
                               GtkTreePath   *path,
                               GtkTreeIter   *iter,
                               gpointer       user_data)
{
  FindIterData *data = user_data;
  GDBusObjectProxy *iter_object;

  iter_object = NULL;

  gtk_tree_model_get (model,
                      iter,
                      LUN_TREE_COLUMN_OBJECT_PROXY, &iter_object,
                      -1);
  if (iter_object == NULL)
    goto out;

  if (iter_object == data->object)
    {
      data->iter = *iter;
      data->found = TRUE;
      goto out;
    }

 out:
  if (iter_object != NULL)
    g_object_unref (iter_object);
  return data->found;
}

static gboolean
find_iter_for_object_proxy (GduApplication   *app,
                            GDBusObjectProxy *object,
                            GtkTreeIter      *out_iter)
{
  FindIterData data;

  memset (&data, 0, sizeof (data));
  data.object = object;
  data.found = FALSE;
  gtk_tree_model_foreach (GTK_TREE_MODEL (app->lun_tree_store),
                          find_iter_for_object_proxy_cb,
                          &data);
  if (data.found)
    {
      if (out_iter != NULL)
        *out_iter = data.iter;
    }

  return data.found;
}

static void
add_lun (GduApplication   *app,
         GDBusObjectProxy *object)
{
  UDisksLun *lun;
  gchar *name;
  gchar *sort_key;
  GIcon *icon;
  const gchar *model;
  const gchar *vendor;

  lun = UDISKS_PEEK_LUN (object);

  model = udisks_lun_get_model (lun);
  vendor = udisks_lun_get_vendor (lun);
  if (strlen (vendor) == 0)
    name = g_strdup (model);
  else if (strlen (model) == 0)
    name = g_strdup (vendor);
  else
    name = g_strconcat (vendor, " ", model, NULL);

  icon = g_themed_icon_new ("drive-harddisk"); /* for now */
  sort_key = g_strdup (name); /* for now */

  gtk_tree_store_insert_with_values (app->lun_tree_store,
                                     NULL,
                                     &app->das_iter,
                                     0,
                                     LUN_TREE_COLUMN_ICON, icon,
                                     LUN_TREE_COLUMN_NAME, name,
                                     LUN_TREE_COLUMN_SORT_KEY, sort_key,
                                     LUN_TREE_COLUMN_OBJECT_PROXY, object,
                                     -1);
  g_object_unref (icon);
  g_free (sort_key);
  g_free (name);
}

static void
remove_lun (GduApplication   *app,
            GDBusObjectProxy *object)
{
  GtkTreeIter iter;

  if (!find_iter_for_object_proxy (app,
                                   object,
                                   &iter))
    {
      g_warning ("Unable to find iter for %s",
                 g_dbus_object_proxy_get_object_path (object));
      goto out;
    }

  gtk_tree_store_remove (app->lun_tree_store, &iter);

 out:
  ;
}

static void
on_object_proxy_added (GDBusProxyManager *manager,
                       GDBusObjectProxy  *object,
                       gpointer           user_data)
{
  GduApplication *app = GDU_APPLICATION (user_data);
  if (UDISKS_PEEK_LUN (object) != NULL)
    add_lun (app, object);
}

static void
on_object_proxy_removed (GDBusProxyManager *manager,
                         GDBusObjectProxy  *object,
                         gpointer           user_data)
{
  GduApplication *app = GDU_APPLICATION (user_data);
  if (UDISKS_PEEK_LUN (object) != NULL)
    remove_lun (app, object);
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
  gchar *s;
  GList *objects;
  GList *l;

  if (app->builder != NULL)
    return;

  error = NULL;
  app->proxy_manager = udisks_proxy_manager_new_for_bus_sync (G_BUS_TYPE_SYSTEM,
                                                              G_DBUS_PROXY_MANAGER_FLAGS_NONE,
                                                              "org.freedesktop.UDisks2",
                                                              "/org/freedesktop/UDisks2",
                                                              NULL, /* GCancellable* */
                                                              &error);
  if (app->proxy_manager == NULL)
    {
      g_error ("Error getting objects from udisks: %s", error->message);
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

  app->lun_tree_store = gtk_tree_store_new (6,
                                            G_TYPE_STRING,
                                            G_TYPE_BOOLEAN,
                                            G_TYPE_STRING,
                                            G_TYPE_ICON,
                                            G_TYPE_STRING,
                                            G_TYPE_DBUS_OBJECT_PROXY);
  G_STATIC_ASSERT (6 == LUN_TREE_N_COLUMNS);

  tree_view = GTK_TREE_VIEW (gdu_application_get_widget (app, "lunlist-treeview"));
  gtk_tree_view_set_model (tree_view, GTK_TREE_MODEL (app->lun_tree_store));
  gtk_tree_sortable_set_sort_column_id (GTK_TREE_SORTABLE (app->lun_tree_store),
                                        LUN_TREE_COLUMN_SORT_KEY,
                                        GTK_SORT_ASCENDING);

  selection = gtk_tree_view_get_selection (tree_view);
  gtk_tree_selection_set_select_function (selection, dont_select_headings, NULL, NULL);

  s = g_strdup_printf ("<small><span foreground=\"#555555\">%s</span></small>",
                       _("Direct-Attached Storage"));
  gtk_tree_store_insert_with_values (app->lun_tree_store,
                                     &app->das_iter,
                                     NULL,
                                     0,
                                     LUN_TREE_COLUMN_IS_HEADING, TRUE,
                                     LUN_TREE_COLUMN_HEADING_TEXT, s,
                                     LUN_TREE_COLUMN_SORT_KEY, "0_das",
                                     -1);
  g_free (s);
  s = g_strdup_printf ("<small><span foreground=\"#555555\">%s</span></small>",
                       _("Remote Storage"));
  gtk_tree_store_insert_with_values (app->lun_tree_store,
                                     &app->remote_iter,
                                     NULL,
                                     0,
                                     LUN_TREE_COLUMN_IS_HEADING, TRUE,
                                     LUN_TREE_COLUMN_HEADING_TEXT, s,
                                     LUN_TREE_COLUMN_SORT_KEY, "1_remote",
                                     -1);
  g_free (s);
  s = g_strdup_printf ("<small><span foreground=\"#555555\">%s</span></small>",
                       _("Other Devices"));
  gtk_tree_store_insert_with_values (app->lun_tree_store,
                                     &app->other_iter,
                                     NULL,
                                     0,
                                     LUN_TREE_COLUMN_IS_HEADING, TRUE,
                                     LUN_TREE_COLUMN_HEADING_TEXT, s,
                                     LUN_TREE_COLUMN_SORT_KEY, "2_other",
                                     -1);
  g_free (s);

  column = gtk_tree_view_column_new ();
  gtk_tree_view_append_column (tree_view, column);

  renderer = gtk_cell_renderer_text_new ();
  gtk_tree_view_column_pack_start (column, renderer, FALSE);
  gtk_tree_view_column_set_attributes (column,
                                       renderer,
                                       "markup", LUN_TREE_COLUMN_HEADING_TEXT,
                                       "visible", LUN_TREE_COLUMN_IS_HEADING,
                                       NULL);

  renderer = gtk_cell_renderer_pixbuf_new ();
  g_object_set (G_OBJECT (renderer),
                "stock-size", GTK_ICON_SIZE_DND,
                NULL);
  gtk_tree_view_column_pack_start (column, renderer, FALSE);
  gtk_tree_view_column_set_attributes (column,
                                       renderer,
                                       "gicon", LUN_TREE_COLUMN_ICON,
                                       NULL);
  renderer = gtk_cell_renderer_text_new ();
  gtk_tree_view_column_pack_start (column, renderer, FALSE);
  gtk_tree_view_column_set_attributes (column,
                                       renderer,
                                       "text", LUN_TREE_COLUMN_NAME,
                                       NULL);

  /* coldplug */
  objects = g_dbus_proxy_manager_get_all (app->proxy_manager);
  for (l = objects; l != NULL; l = l->next)
    {
      GDBusObjectProxy *object = G_DBUS_OBJECT_PROXY (l->data);
      on_object_proxy_added (app->proxy_manager, object, app);
    }
  g_list_foreach (objects, (GFunc) g_object_unref, NULL);
  g_list_free (objects);

  g_signal_connect (app->proxy_manager,
                    "object-proxy-added",
                    G_CALLBACK (on_object_proxy_added),
                    app);

  g_signal_connect (app->proxy_manager,
                    "object-proxy-removed",
                    G_CALLBACK (on_object_proxy_removed),
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

