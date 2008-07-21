/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*- */
/* gdu-presentable-tree.h
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


#ifndef GNOME_DISK_UTILITY_TREE_H
#define GNOME_DISK_UTILITY_TREE_H

#include <gtk/gtk.h>

#include <gdu/gdu.h>

#define GDU_TYPE_DEVICE_TREE             (gdu_device_tree_get_type ())
#define GDU_DEVICE_TREE(obj)             (G_TYPE_CHECK_INSTANCE_CAST ((obj), GDU_TYPE_DEVICE_TREE, GduDeviceTree))
#define GDU_DEVICE_TREE_CLASS(obj)       (G_TYPE_CHECK_CLASS_CAST ((obj), GDU_DEVICE_TREE,  GduDeviceTreeClass))
#define GDU_IS_DEVICE_TREE(obj)          (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GDU_TYPE_DEVICE_TREE))
#define GDU_IS_DEVICE_TREE_CLASS(obj)    (G_TYPE_CHECK_CLASS_TYPE ((obj), GDU_TYPE_DEVICE_TREE))
#define GDU_DEVICE_TREE_GET_CLASS        (G_TYPE_INSTANCE_GET_CLASS ((obj), GDU_TYPE_DEVICE_TREE, GduDeviceTreeClass))

typedef struct _GduDeviceTreeClass       GduDeviceTreeClass;
typedef struct _GduDeviceTree            GduDeviceTree;

struct _GduDeviceTreePrivate;
typedef struct _GduDeviceTreePrivate     GduDeviceTreePrivate;

struct _GduDeviceTree
{
        GtkTreeView parent;

        /* private */
        GduDeviceTreePrivate *priv;
};

struct _GduDeviceTreeClass
{
        GtkTreeViewClass parent_class;
};


GType             gdu_device_tree_get_type                 (void);
GtkWidget        *gdu_device_tree_new                      (GduPool     *pool);
GduPresentable   *gdu_device_tree_get_selected_presentable (GtkTreeView *tree_view);
void              gdu_device_tree_select_presentable       (GtkTreeView *tree_view, GduPresentable *presentable);
void              gdu_device_tree_select_first_presentable (GtkTreeView *tree_view);

#endif /* GNOME_DISK_UTILITY_TREE_H */
