/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*- */
/* gdu-tree.c
 *
 * Copyright (C) 2007 David Zeuthen
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 */

#include <config.h>
#include <glib/gi18n.h>
#include <string.h>

#include <gdu-gtk/gdu-gtk.h>

#include "gdu-tree.h"


struct _GduDeviceTreePrivate
{
        GduPresentable *presentable;
        GduPool *pool;
};

static GObjectClass *parent_class = NULL;

G_DEFINE_TYPE (GduDeviceTree, gdu_device_tree, GTK_TYPE_TREE_VIEW)

static void device_tree_presentable_added (GduPool *pool, GduPresentable *presentable, gpointer user_data);
static void device_tree_presentable_removed (GduPool *pool, GduPresentable *presentable, gpointer user_data);
static void device_tree_presentable_changed (GduPool *pool, GduPresentable *presentable, gpointer user_data);
static void add_presentable_to_tree (GduDeviceTree *device_tree, GduPresentable *presentable, GtkTreeIter *iter_out);

static gint sort_iter_compare_func (GtkTreeModel *model, GtkTreeIter  *a, GtkTreeIter  *b, gpointer userdata);

enum {
        PROP_0,
        PROP_POOL,
};

enum
{
        ICON_COLUMN,
        TITLE_COLUMN,
        PRESENTABLE_OBJ_COLUMN,
        SORTNAME_COLUMN,
        N_COLUMNS
};

static void
update_pool (GduDeviceTree *device_tree)
{
        GList *presentables;
        GList *l;

        presentables = gdu_pool_get_presentables (device_tree->priv->pool);
        for (l = presentables; l != NULL; l = l->next) {
                GduPresentable *presentable = GDU_PRESENTABLE (l->data);
                add_presentable_to_tree (device_tree, presentable, NULL);
                g_object_unref (presentable);
        }
        g_list_free (presentables);

        /* add/remove/change rows when the pool reports presentable add/remove/change */
        g_signal_connect (device_tree->priv->pool, "presentable-added",
                          (GCallback) device_tree_presentable_added, device_tree);
        g_signal_connect (device_tree->priv->pool, "presentable-removed",
                          (GCallback) device_tree_presentable_removed, device_tree);
        g_signal_connect (device_tree->priv->pool, "presentable-changed",
                          (GCallback) device_tree_presentable_changed, device_tree);
}

static void
gdu_device_tree_set_property (GObject      *object,
                              guint         prop_id,
                              const GValue *value,
                              GParamSpec   *pspec)
{
        GduDeviceTree *device_tree = GDU_DEVICE_TREE (object);
        gpointer obj;

        switch (prop_id) {
        case PROP_POOL:
                if (device_tree->priv->pool != NULL)
                        g_object_unref (device_tree->priv->pool);
                obj = g_value_get_object (value);
                device_tree->priv->pool = (obj == NULL ? NULL : g_object_ref (obj));
                update_pool (device_tree);
                break;

        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static void
gdu_device_tree_get_property (GObject     *object,
                                    guint        prop_id,
                                    GValue      *value,
                                    GParamSpec  *pspec)
{
        GduDeviceTree *device_tree = GDU_DEVICE_TREE (object);

        switch (prop_id) {
        case PROP_POOL:
                g_value_set_object (value, device_tree->priv->pool);
                break;

        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
    }
}

static void
gdu_device_tree_finalize (GduDeviceTree *device_tree)
{
        g_signal_handlers_disconnect_by_func (device_tree->priv->pool, device_tree_presentable_added, device_tree);
        g_signal_handlers_disconnect_by_func (device_tree->priv->pool, device_tree_presentable_removed, device_tree);
        g_signal_handlers_disconnect_by_func (device_tree->priv->pool, device_tree_presentable_changed, device_tree);

        if (device_tree->priv->pool != NULL)
                g_object_unref (device_tree->priv->pool);

        if (G_OBJECT_CLASS (parent_class)->finalize)
                (* G_OBJECT_CLASS (parent_class)->finalize) (G_OBJECT (device_tree));
}

static void
gdu_device_tree_class_init (GduDeviceTreeClass *klass)
{
        GObjectClass *obj_class = (GObjectClass *) klass;

        parent_class = g_type_class_peek_parent (klass);

        obj_class->finalize = (GObjectFinalizeFunc) gdu_device_tree_finalize;
        obj_class->set_property = gdu_device_tree_set_property;
        obj_class->get_property = gdu_device_tree_get_property;

        g_type_class_add_private (klass, sizeof (GduDeviceTreePrivate));

        /**
         * GduDeviceTree:pool:
         *
         * The #GduPool instance we are getting information from
         */
        g_object_class_install_property (obj_class,
                                         PROP_POOL,
                                         g_param_spec_object ("pool",
                                                              NULL,
                                                              NULL,
                                                              GDU_TYPE_POOL,
                                                              G_PARAM_WRITABLE |
                                                              G_PARAM_READABLE |
                                                              G_PARAM_CONSTRUCT_ONLY));
}

static void
gdu_device_tree_init (GduDeviceTree *device_tree)
{
        GtkCellRenderer *renderer;
        GtkTreeViewColumn *column;
        GtkTreeStore *store;

        device_tree->priv = G_TYPE_INSTANCE_GET_PRIVATE (device_tree, GDU_TYPE_DEVICE_TREE, GduDeviceTreePrivate);

        store = gtk_tree_store_new (N_COLUMNS,
                                    GDK_TYPE_PIXBUF,
                                    G_TYPE_STRING,
                                    GDU_TYPE_PRESENTABLE,
                                    G_TYPE_STRING);

        gtk_tree_sortable_set_sort_func (GTK_TREE_SORTABLE (store), SORTNAME_COLUMN, sort_iter_compare_func,
                                         NULL, NULL);
        gtk_tree_sortable_set_sort_column_id (GTK_TREE_SORTABLE (store), SORTNAME_COLUMN, GTK_SORT_ASCENDING);

        gtk_tree_view_set_model (GTK_TREE_VIEW (device_tree), GTK_TREE_MODEL (store));
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
        gtk_tree_view_append_column (GTK_TREE_VIEW (device_tree), column);

        gtk_tree_view_set_headers_visible (GTK_TREE_VIEW (device_tree), FALSE);

        gtk_tree_view_set_show_expanders (GTK_TREE_VIEW (device_tree), FALSE);
        gtk_tree_view_set_level_indentation (GTK_TREE_VIEW (device_tree), 16);

}

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
        GduPresentable *presentable;
        gboolean found;
        GtkTreeIter iter;
} FIBDData;

static gboolean
find_iter_by_presentable_foreach (GtkTreeModel *model,
                                  GtkTreePath *path,
                                  GtkTreeIter *iter,
                                  gpointer data)
{
        gboolean ret;
        GduPresentable *presentable = NULL;
        FIBDData *fibd_data = (FIBDData *) data;

        ret = FALSE;
        gtk_tree_model_get (model, iter, PRESENTABLE_OBJ_COLUMN, &presentable, -1);
        if (presentable == fibd_data->presentable) {
                fibd_data->found = TRUE;
                fibd_data->iter = *iter;
                ret = TRUE;
        }
        if (presentable != NULL)
                g_object_unref (presentable);

        return ret;
}


static gboolean
find_iter_by_presentable (GtkTreeStore *store, GduPresentable *presentable, GtkTreeIter *iter)
{
        FIBDData fibd_data;
        gboolean ret;

        fibd_data.presentable = presentable;
        fibd_data.found = FALSE;
        gtk_tree_model_foreach (GTK_TREE_MODEL (store), find_iter_by_presentable_foreach, &fibd_data);
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
device_tree_presentable_changed (GduPool *pool, GduPresentable *presentable, gpointer user_data)
{
        GtkTreeView *tree_view = GTK_TREE_VIEW (user_data);
        char *name;
        GtkTreeStore *store;
        GtkTreeIter iter;
        GdkPixbuf *pixbuf;
        GduDevice *device;

        store = GTK_TREE_STORE (gtk_tree_view_get_model (tree_view));

        /* update name and icon */
        if (find_iter_by_presentable (store, presentable, &iter)) {

                name = gdu_presentable_get_name (presentable);
                device = gdu_presentable_get_device (presentable);

                pixbuf = gdu_util_get_pixbuf_for_presentable (presentable, GTK_ICON_SIZE_MENU);

                gtk_tree_store_set (store,
                                    &iter,
                                    ICON_COLUMN, pixbuf,
                                    TITLE_COLUMN, name,
                                    -1);

                g_free (name);
                if (pixbuf != NULL)
                        g_object_unref (pixbuf);
                if (device != NULL)
                        g_object_unref (device);
        }
}

static void
add_presentable_to_tree (GduDeviceTree *device_tree, GduPresentable *presentable, GtkTreeIter *iter_out)
{
        GtkTreeIter  iter;
        GtkTreeIter  iter2;
        GtkTreeIter *parent_iter;
        GdkPixbuf   *pixbuf;
        char        *name;
        GtkTreeStore *store;
        GduDevice *device;
        GduPresentable *enclosing_presentable;
        const char  *object_path;
        char *sortname;

        device = NULL;

        store = GTK_TREE_STORE (gtk_tree_view_get_model (GTK_TREE_VIEW (device_tree)));

        /* check to see if presentable is already added */
        if (find_iter_by_presentable (store, presentable, NULL))
                goto out;

        /* set up parent relationship */
        parent_iter = NULL;
        enclosing_presentable = gdu_presentable_get_enclosing_presentable (presentable);
        if (enclosing_presentable != NULL) {
                if (find_iter_by_presentable (store, enclosing_presentable, &iter2)) {
                        parent_iter = &iter2;
                } else {
                        /* add parent if it's not already added */
                        /*g_debug ("we have no parent for %s (%p)", gdu_presentable_get_id (enclosing_presentable), enclosing_presentable);*/
                        add_presentable_to_tree (device_tree, enclosing_presentable, &iter2);
                        parent_iter = &iter2;
                }
                g_object_unref (enclosing_presentable);
        }

        device = gdu_presentable_get_device (presentable);
        if (device != NULL)
                object_path = gdu_device_get_object_path (device);
        else
                object_path = "";

        /* compute the name */
        name = gdu_presentable_get_name (presentable);
        pixbuf = gdu_util_get_pixbuf_for_presentable (presentable, GTK_ICON_SIZE_MENU);

        /* sort by offset so we get partitions in the right order */
        sortname = g_strdup_printf ("%016" G_GINT64_FORMAT "_%s", gdu_presentable_get_offset (presentable), object_path);

        /*g_debug ("adding %s (%p)", gdu_presentable_get_id (presentable), presentable);*/

        gtk_tree_store_append (store, &iter, parent_iter);
        gtk_tree_store_set (store, &iter,
                            ICON_COLUMN, pixbuf,
                            TITLE_COLUMN, name,
                            PRESENTABLE_OBJ_COLUMN, presentable,
                            SORTNAME_COLUMN, sortname,
                            -1);
        g_free (sortname);

        if (iter_out != NULL)
                *iter_out = iter;

        g_free (name);
        if (pixbuf != NULL)
                g_object_unref (pixbuf);

        if (parent_iter != NULL) {
                GtkTreePath *path;
                path = gtk_tree_model_get_path (GTK_TREE_MODEL (store), parent_iter);
                if (path != NULL) {
                        gtk_tree_view_expand_row (GTK_TREE_VIEW (device_tree), path, TRUE);
                        gtk_tree_path_free (path);
                }
        }

out:
        if (device != NULL)
                g_object_unref (device);
}

static void
device_tree_presentable_added (GduPool *pool, GduPresentable *presentable, gpointer user_data)
{
        GduDeviceTree *device_tree = GDU_DEVICE_TREE (user_data);
        add_presentable_to_tree (device_tree, presentable, NULL);
}

static void
device_tree_presentable_removed (GduPool *pool, GduPresentable *presentable, gpointer user_data)
{
        GduDeviceTree *device_tree = GDU_DEVICE_TREE (user_data);
        GtkTreeIter iter;
        GtkTreeStore *store;

        store = GTK_TREE_STORE (gtk_tree_view_get_model (GTK_TREE_VIEW (device_tree)));
        if (find_iter_by_presentable (store, presentable, &iter)) {
                gtk_tree_store_remove (store, &iter);
        }
}

GtkWidget *
gdu_device_tree_new (GduPool *pool)
{
        return GTK_WIDGET (g_object_new (GDU_TYPE_DEVICE_TREE, "pool", pool, NULL));
}

GduPresentable *
gdu_device_tree_get_selected_presentable (GtkTreeView *tree_view)
{
        GduPresentable *presentable;
        GtkTreePath *path;
        GtkTreeModel *presentable_tree_model;

        presentable = NULL;

        presentable_tree_model = gtk_tree_view_get_model (tree_view);
        gtk_tree_view_get_cursor (tree_view, &path, NULL);
        if (path != NULL) {
                GtkTreeIter iter;

                if (gtk_tree_model_get_iter (presentable_tree_model, &iter, path)) {

                        gtk_tree_model_get (presentable_tree_model, &iter,
                                            PRESENTABLE_OBJ_COLUMN,
                                            &presentable,
                                            -1);

                        if (presentable != NULL)
                                g_object_unref (presentable);
                }

                gtk_tree_path_free (path);
        }

        return presentable;
}

void
gdu_device_tree_select_presentable (GtkTreeView *tree_view, GduPresentable *presentable)
{
        GtkTreePath *path;
        GtkTreeModel *tree_model;
        GtkTreeIter iter;

        if (presentable == NULL)
                goto out;

        tree_model = gtk_tree_view_get_model (tree_view);
        if (!find_iter_by_presentable (GTK_TREE_STORE (tree_model), presentable, &iter))
                goto out;

        path = gtk_tree_model_get_path (tree_model, &iter);
        if (path == NULL)
                goto out;

        gtk_tree_view_set_cursor (tree_view, path, NULL, FALSE);
        gtk_tree_path_free (path);
out:
        ;
}

void
gdu_device_tree_select_first_presentable (GtkTreeView *tree_view)
{
        GtkTreePath *path;
        GtkTreeModel *tree_model;
        GtkTreeIter iter;

        tree_model = gtk_tree_view_get_model (tree_view);

        if (gtk_tree_model_get_iter_first (tree_model, &iter)) {
                path = gtk_tree_model_get_path (tree_model, &iter);
                if (path == NULL)
                        goto out;

                gtk_tree_view_set_cursor (tree_view, path, NULL, FALSE);
                gtk_tree_path_free (path);
        }
out:
        ;
}
