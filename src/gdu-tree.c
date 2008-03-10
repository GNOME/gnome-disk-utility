/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*- */
/* gdu-tree.c
 *
 * Copyright (C) 2007 David Zeuthen
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
 * USA
 */

#include <config.h>
#include <glib/gi18n.h>

#include "gdu-main.h"
#include "gdu-tree.h"

enum
{
        ICON_COLUMN,
        TITLE_COLUMN,
        DEVICE_OBJ_COLUMN,
        SORTNAME_COLUMN,
        N_COLUMNS
};

static gint
sort_iter_compare_func (GtkTreeModel *model,
                        GtkTreeIter  *a,
                        GtkTreeIter  *b,
                        gpointer      userdata)
{
        char *s1;
        char *s2;
        int result;

        gtk_tree_model_get (model, a, SORTNAME_COLUMN, &s1, -1);
        gtk_tree_model_get (model, b, SORTNAME_COLUMN, &s2, -1);
        if (s1 == NULL || s2 == NULL)
                result = 0;
        else
                result = g_ascii_strcasecmp (s1, s2);
        g_free (s2);
        g_free (s1);

        return result;
}

typedef struct {
        const char *udi;
        GduDevice *device;
        gboolean found;
        GtkTreeIter iter;
} FIBDData;

static gboolean
find_iter_by_device_foreach (GtkTreeModel *model,
                             GtkTreePath *path,
                             GtkTreeIter *iter,
                             gpointer data)
{
        gboolean ret;
        GduDevice *device = NULL;
        FIBDData *fibd_data = (FIBDData *) data;

        ret = FALSE;
        gtk_tree_model_get (model, iter, DEVICE_OBJ_COLUMN, &device, -1);
        if (device == fibd_data->device) {
                fibd_data->found = TRUE;
                fibd_data->iter = *iter;
                ret = TRUE;
        }
        if (device != NULL)
                g_object_unref (device);

        return ret;
}


static gboolean
find_iter_by_device (GtkTreeStore *store, GduDevice *device, GtkTreeIter *iter)
{
        FIBDData fibd_data;
        gboolean ret;

        fibd_data.device = device;
        fibd_data.found = FALSE;
        gtk_tree_model_foreach (GTK_TREE_MODEL (store), find_iter_by_device_foreach, &fibd_data);
        if (fibd_data.found) {
                if (iter != NULL)
                        *iter = fibd_data.iter;
                ret = TRUE;
        } else {
                ret = FALSE;
        }

        return ret;
}

static void
add_device_to_tree (GtkTreeView *tree_view, GduDevice *device, GtkTreeIter *iter_out)
{
        GtkTreeIter  iter;
        GtkTreeIter  iter2;
        GtkTreeIter *parent_iter;
        GdkPixbuf   *pixbuf;
        const char  *object_path;
        char        *name;
        char        *icon_name;
        GtkTreeStore *store;
        GduDevice *parent_device;

        store = GTK_TREE_STORE (gtk_tree_view_get_model (tree_view));

        /* check to see if device is already added */
        if (find_iter_by_device (store, device, NULL))
                return;

        /* set up parent relationship */
        parent_iter = NULL;
        parent_device = gdu_device_find_parent (device);
        if (parent_device != NULL) {
                if (find_iter_by_device (store, parent_device, &iter2)) {
                        parent_iter = &iter2;
                } else {
                        /* add parent if it's not already added */
                        add_device_to_tree (tree_view, parent_device, &iter2);
                        parent_iter = &iter2;
                }
                g_object_unref (parent_device);
        }

        object_path = gdu_device_get_object_path (device);

        name = g_strdup (object_path);
        icon_name = g_strdup ("drive-harddisk"); //gdu_info_provider_get_icon_name (device);

        /* compute the name */
        if (gdu_device_is_drive (device)) {
                const char *drive_vendor;
                const char *drive_model;
                guint64 drive_size;
                gboolean drive_is_removable;
                char *strsize;

                drive_vendor = gdu_device_drive_get_vendor (device);
                drive_model = gdu_device_drive_get_model (device);
                drive_size = gdu_device_get_size (device);
                drive_is_removable = gdu_device_is_removable (device);
                g_free (name);

                strsize = NULL;
                if (!drive_is_removable && drive_size > 0) {
                        strsize = gdu_util_get_size_for_display (drive_size, FALSE);
                }

                if (strsize != NULL) {
                        name = g_strdup_printf ("%s %s %s",
                                                strsize,
                                                drive_vendor != NULL ? drive_vendor : "",
                                                drive_model != NULL ? drive_model : "");
                } else {
                        name = g_strdup_printf ("%s %s",
                                                drive_vendor != NULL ? drive_vendor : "",
                                                drive_model != NULL ? drive_model : "");
                }
                g_free (strsize);
        }


        pixbuf = NULL;
        if (icon_name != NULL) {
                pixbuf = gtk_icon_theme_load_icon (gtk_icon_theme_get_default (),
                                                 icon_name,
                                                 24,
                                                 0,
                                                 NULL);
        }

        gtk_tree_store_append (store, &iter, parent_iter);
        gtk_tree_store_set (store, &iter,
                            ICON_COLUMN, pixbuf,
                            TITLE_COLUMN, name,
                            DEVICE_OBJ_COLUMN, device,
                            SORTNAME_COLUMN, object_path,
                            -1);

        if (iter_out != NULL)
                *iter_out = iter;

        g_free (name);
        g_free (icon_name);
        if (pixbuf != NULL)
                g_object_unref (pixbuf);

        if (parent_iter != NULL) {
                GtkTreePath *path;
                path = gtk_tree_model_get_path (GTK_TREE_MODEL (store), parent_iter);
                if (tree_view != NULL && path != NULL) {
                        gtk_tree_view_expand_row (tree_view, path, TRUE);
                        gtk_tree_path_free (path);
                }
        }
}

static void
device_tree_device_added (GduPool *pool, GduDevice *device, gpointer user_data)
{
        GtkTreeView *tree_view = GTK_TREE_VIEW (user_data);
        add_device_to_tree (tree_view, device, NULL);
}

static void
device_tree_device_removed (GduPool *pool, GduDevice *device, gpointer user_data)
{
        GtkTreeIter iter;
        GtkTreeStore *store;

        store = GTK_TREE_STORE (gtk_tree_view_get_model (GTK_TREE_VIEW (user_data)));
        if (find_iter_by_device (store, device, &iter)) {
                gtk_tree_store_remove (store, &iter);
        }
}

GtkTreeView *
gdu_tree_new (GduPool *pool)
{
        GtkCellRenderer *renderer;
        GtkTreeViewColumn *column;
        GtkTreeView *tree_view;
        GtkTreeStore *store;
        GList *devices;
        GList *l;

        store = gtk_tree_store_new (N_COLUMNS,
                                    GDK_TYPE_PIXBUF,
                                    G_TYPE_STRING,
                                    GDU_TYPE_DEVICE,
                                    G_TYPE_STRING);

        gtk_tree_sortable_set_sort_func (GTK_TREE_SORTABLE (store), SORTNAME_COLUMN, sort_iter_compare_func,
                                         NULL, NULL);
        gtk_tree_sortable_set_sort_column_id (GTK_TREE_SORTABLE (store), SORTNAME_COLUMN, GTK_SORT_ASCENDING);

        tree_view = GTK_TREE_VIEW (gtk_tree_view_new_with_model (GTK_TREE_MODEL (store)));
        /* TODO: when GTK 2.12 is available... we can do this */
        /*gtk_tree_view_set_show_expanders (GTK_TREE_VIEW (tree), FALSE);*/

        column = gtk_tree_view_column_new ();
        gtk_tree_view_column_set_title (column, "Title");
        renderer = gtk_cell_renderer_pixbuf_new ();
        gtk_tree_view_column_pack_start (column, renderer, FALSE);
        gtk_tree_view_column_set_attributes (column, renderer,
                                             "pixbuf", ICON_COLUMN,
                                             NULL);
        renderer = gtk_cell_renderer_text_new ();
        gtk_tree_view_column_pack_start (column, renderer, TRUE);
        gtk_tree_view_column_set_attributes (column, renderer,
                                             "text", TITLE_COLUMN,
                                             NULL);
        gtk_tree_view_append_column (tree_view, column);

        gtk_tree_view_set_headers_visible (tree_view, FALSE);

        devices = gdu_pool_get_devices (pool);
        for (l = devices; l != NULL; l = l->next) {
                GduDevice *device = GDU_DEVICE (l->data);
                add_device_to_tree (tree_view, device, NULL);
                g_object_unref (device);
        }
        g_list_free (devices);

        /* expand all rows after the treeview widget has been realized */
        g_signal_connect (tree_view, "realize", G_CALLBACK (gtk_tree_view_expand_all), NULL);

        /* add / remove rows when hal reports device add / remove */
        g_signal_connect (pool, "device_added", (GCallback) device_tree_device_added, tree_view);
        g_signal_connect (pool, "device_removed", (GCallback) device_tree_device_removed, tree_view);

        return tree_view;
}

GduDevice *
gdu_tree_get_selected_device (GtkTreeView *tree_view)
{
        GduDevice *device;
        GtkTreePath *path;
        GtkTreeModel *device_tree_model;

        device = NULL;

        device_tree_model = gtk_tree_view_get_model (tree_view);
        gtk_tree_view_get_cursor (tree_view, &path, NULL);
        if (path != NULL) {
                GtkTreeIter iter;

                if (gtk_tree_model_get_iter (device_tree_model, &iter, path)) {

                        gtk_tree_model_get (device_tree_model, &iter,
                                            DEVICE_OBJ_COLUMN,
                                            &device,
                                            -1);

                        if (device != NULL)
                                g_object_unref (device);
                }

                gtk_tree_path_free (path);
        }

        return device;
}

void
gdu_tree_select_device (GtkTreeView *tree_view, GduDevice *device)
{
        GtkTreePath *path;
        GtkTreeModel *tree_model;
        GtkTreeIter iter;

        if (device == NULL)
                goto out;

        tree_model = gtk_tree_view_get_model (tree_view);
        if (!find_iter_by_device (GTK_TREE_STORE (tree_model), device, &iter))
                goto out;

        path = gtk_tree_model_get_path (tree_model, &iter);
        if (path == NULL)
                goto out;

        gtk_tree_view_set_cursor (tree_view, path, NULL, FALSE);
        gtk_tree_path_free (path);
out:
        ;
}
