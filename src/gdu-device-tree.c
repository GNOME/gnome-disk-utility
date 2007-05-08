/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*- */
/* gdu-device-tree.c
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

#include <gnome-device-manager/gdm-info-provider.h>

#include "gdu-device-tree.h"

enum
{
        ICON_COLUMN,
        TITLE_COLUMN,
        DEVICE_OBJ_COLUMN,
        SORTNAME_COLUMN,
        N_COLUMNS
};

static GtkTreeIter  internal_drives_iter;
static GtkTreeIter  external_drives_iter;
static GtkTreeIter  raid_arrays_iter;

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
        GdmDevice *device;
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
        GdmDevice *device = NULL;
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
find_iter_by_device (GtkTreeStore *store, GdmDevice *device, GtkTreeIter *iter)
{
        FIBDData fibd_data;
        gboolean ret;

        fibd_data.device = device;
        fibd_data.found = FALSE;
        gtk_tree_model_foreach (GTK_TREE_MODEL (store), find_iter_by_device_foreach, &fibd_data);
        if (fibd_data.found) {
                *iter = fibd_data.iter;
                ret = TRUE;
        } else {
                ret = FALSE;
        }

        return ret;
}


#define KILOBYTE_FACTOR 1024.0
#define MEGABYTE_FACTOR (1024.0 * 1024.0)
#define GIGABYTE_FACTOR (1024.0 * 1024.0 * 1024.0)

static char *
gdu_util_get_size_for_display (guint64 size, gboolean long_string)
{
        char *str;
        gdouble displayed_size;

        if (size < MEGABYTE_FACTOR) {
                displayed_size = (double) size / KILOBYTE_FACTOR;
                if (long_string)
                        str = g_strdup_printf (_("%.1f KB (%'lld bytes)"), displayed_size, size);
                else
                        str = g_strdup_printf (_("%.1f KB"), displayed_size);
        } else if (size < GIGABYTE_FACTOR) {
                displayed_size = (double) size / MEGABYTE_FACTOR;
                if (long_string)
                        str = g_strdup_printf (_("%.1f MB (%'lld bytes)"), displayed_size, size);
                else
                        str = g_strdup_printf (_("%.1f MB"), displayed_size);
        } else {
                displayed_size = (double) size / GIGABYTE_FACTOR;
                if (long_string)
                        str = g_strdup_printf (_("%.1f GB (%'lld bytes)"), displayed_size, size);
                else
                        str = g_strdup_printf (_("%.1f GB"), displayed_size);
        }
        
        return str;
}

static void
add_device_to_tree (GtkTreeView *tree_view, GdmDevice *device, GdmDevice *parent_device)
{
        GtkTreeIter  iter;
        GtkTreeIter  iter2;
        GtkTreeIter *parent_iter;
        GdkPixbuf   *pixbuf;
        const char  *udi;
        char        *name;
        char        *icon_name;
        GtkTreeStore *store;
        gboolean is_drv;
        gboolean is_vol;

        is_drv = gdm_device_test_capability (device, "storage");
        is_vol = gdm_device_test_capability (device, "volume");
        if (!is_drv && !is_vol)
                return;

        store = GTK_TREE_STORE (gtk_tree_view_get_model (tree_view));

        udi = gdm_device_get_udi (device);
        
        icon_name = gdm_info_provider_get_icon_name (device);

        if (icon_name != NULL) {
                pixbuf = gtk_icon_theme_load_icon (gtk_icon_theme_get_default (),
                                                 icon_name,
                                                 24,
                                                 0,
                                                 NULL);
        } else {
                pixbuf = NULL;
        }

        name = gdm_info_provider_get_short_name (device);
        /* compute the name */
        if (is_drv) {
                const char *drive_vendor;
                const char *drive_model;
                guint64 drive_size;
                gboolean drive_is_removable;
                gboolean is_linux_raid;
                char *strsize;

                drive_vendor = gdm_device_get_property_string (device, "storage.vendor");
                drive_model = gdm_device_get_property_string (device, "storage.model");
                drive_size = gdm_device_get_property_uint64 (device, "storage.removable.media_size");
                drive_is_removable = gdm_device_get_property_bool (device, "storage.removable");
                is_linux_raid = gdm_device_test_capability (device, "storage.linux_raid");
                
                g_free (name);

                strsize = NULL;
                if (!drive_is_removable && drive_size > 0) {
                        strsize = gdu_util_get_size_for_display (drive_size, FALSE);
                }

                if (is_linux_raid) {
                        const char *raid_level;

                        raid_level = gdm_device_get_property_string (device, "storage.linux_raid.level");

                        if (g_ascii_strcasecmp (raid_level, "linear") == 0) {
                                name = g_strdup_printf (_("%s Linear"), strsize);
                        } else if (g_ascii_strcasecmp (raid_level, "raid0") == 0) {
                                name = g_strdup_printf (_("%s RAID-0"), strsize);
                        } else if (g_ascii_strcasecmp (raid_level, "raid1") == 0) {
                                name = g_strdup_printf (_("%s RAID-1"), strsize);
                        } else if (g_ascii_strcasecmp (raid_level, "raid5") == 0) {
                                name = g_strdup_printf (_("%s RAID-5"), strsize);
                        } else {
                                name = g_strdup_printf (_("%s %s"), strsize, raid_level);
                        }
                } else {
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
                }
                g_free (strsize);

        }

        /* find the right parent */
        parent_iter = NULL;
        if (is_drv) {
                gboolean is_linux_raid;
                gboolean is_hotpluggable;

                is_linux_raid = gdm_device_test_capability (device, "storage.linux_raid");
                is_hotpluggable = gdm_device_get_property_bool (device, "storage.hotpluggable");

                if (is_linux_raid) {
                        parent_iter = &raid_arrays_iter;
                } else if (is_hotpluggable) {
                        parent_iter = &external_drives_iter;
                } else {
                        parent_iter = &internal_drives_iter;
                }


        } else {
                if (parent_device != NULL) {
                        if (find_iter_by_device (store, parent_device, &iter2)) {
                                parent_iter = &iter2;
                        }
                }
        }

        gtk_tree_store_append (store, &iter, parent_iter);
        gtk_tree_store_set (store, &iter,
                            ICON_COLUMN, pixbuf,
                            TITLE_COLUMN, name,
                            DEVICE_OBJ_COLUMN, device,
                            SORTNAME_COLUMN, udi,
                            -1);

        g_free (name);
        g_free (icon_name);
        if (pixbuf != NULL) {
                g_object_unref (pixbuf);
        }

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
device_tree_device_added (GdmPool *pool, GdmDevice *device, gpointer user_data)
{
        GtkTreeView *tree_view = GTK_TREE_VIEW (user_data);
        add_device_to_tree (tree_view, device, gdm_pool_get_parent_device (pool, device));
}

static void
device_tree_device_removed (GdmPool *pool, GdmDevice *device, gpointer user_data)
{
        GtkTreeIter iter;
        GtkTreeStore *store;

        store = GTK_TREE_STORE (gtk_tree_view_get_model (GTK_TREE_VIEW (user_data)));
        if (find_iter_by_device (store, device, &iter)) {
                gtk_tree_store_remove (store, &iter);
        }
}

static void
pool_visitor (GdmPool *pool, GdmDevice *device, GdmDevice *parent_device, GtkTreeView *tree_view)
{
        add_device_to_tree (tree_view, device, parent_device);
}

static void
gdu_device_add_drive_classes (GtkTreeView *tree_view)
{
        int n;
        GtkTreeStore *store;
        GtkTreeIter  *iter;
        GdkPixbuf   *pixbuf;

        store = GTK_TREE_STORE (gtk_tree_view_get_model (tree_view));

        for (n = 0; n < 3; n++) {
                const char *name;
                const char *icon_name;

                switch (n) {
                default:
                case 0:
                        iter = &internal_drives_iter;
                        name = _("Internal Drives");
                        icon_name = "drive-harddisk";
                        break;
                case 1:
                        iter = &external_drives_iter;
                        name = _("External Drives");
                        icon_name = "drive-removable-media";
                        break;
                case 2:
                        iter = &raid_arrays_iter;
                        name = _("RAID Arrays");
                        icon_name = "drive-optical";
                        break;
                }

                pixbuf = gtk_icon_theme_load_icon (gtk_icon_theme_get_default (),
                                                   icon_name,
                                                   24,
                                                   0,
                                                   NULL);


                gtk_tree_store_append (store, iter, NULL);
                gtk_tree_store_set (store, iter,
                                    ICON_COLUMN, pixbuf,
                                    TITLE_COLUMN, name,
                                    DEVICE_OBJ_COLUMN, NULL,
                                    SORTNAME_COLUMN, NULL,
                                    -1);

                if (pixbuf != NULL) {
                        g_object_unref (pixbuf);
                }
        }
}

GtkTreeView *
gdu_device_tree_new (GdmPool *pool)
{
        GtkCellRenderer *renderer;
        GtkTreeViewColumn *column;
        GtkTreeView *tree_view;
        GtkTreeStore *store;

        store = gtk_tree_store_new (N_COLUMNS,
                                    GDK_TYPE_PIXBUF,
                                    G_TYPE_STRING,
                                    GDM_TYPE_DEVICE,
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

        gdu_device_add_drive_classes (tree_view);

        gdm_pool_visit (pool, (GdmPoolVisitorFunc) pool_visitor, tree_view);

        /* expand all rows after the treeview widget has been realized */
        g_signal_connect (tree_view, "realize", G_CALLBACK (gtk_tree_view_expand_all), NULL);

        /* add / remove rows when hal reports device add / remove */
        g_signal_connect (pool, "device_added", (GCallback) device_tree_device_added, tree_view);
        g_signal_connect (pool, "device_removed", (GCallback) device_tree_device_removed, tree_view);

        return tree_view;
}

GdmDevice *
gdu_device_tree_get_selected_device (GtkTreeView *tree_view)
{
        GdmDevice *device;
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
gdu_device_tree_select_device (GtkTreeView *tree_view, GdmDevice *device)
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
