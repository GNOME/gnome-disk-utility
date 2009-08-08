/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*- */
/* gdu-gtk-enums.h
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

#if !defined (__GDU_GTK_INSIDE_GDU_GTK_H) && !defined (GDU_GTK_COMPILATION)
#error "Only <gdu-gtk/gdu-gtk.h> can be included directly, this file may disappear or change contents."
#endif

#ifndef GDU_GTK_ENUMS_H
#define GDU_GTK_ENUMS_H

#include <glib-object.h>

/**
 * GduPoolTreeModelColumn:
 * @GDU_POOL_TREE_MODEL_COLUMN_ICON: The #GIcon for the presentable.
 * @GDU_POOL_TREE_MODEL_COLUMN_NAME: Human readable name of the presentable, e.g. "80 GB Solid-state Disk" or
 * "Fedora (Rawhide)".
 * @GDU_POOL_TREE_MODEL_COLUMN_VPD_NAME: Name for the presentable derived from Vital Product Data,
 * e.g. "ATA INTEL SSDSA2MH080G1GC".
 * @GDU_POOL_TREE_MODEL_COLUMN_DESCRIPTION: Human readable description of the presentable, e.g. "MBR Partition Table"
 * or "32GB Linux ext3".
 * @GDU_POOL_TREE_MODEL_COLUMN_PRESENTABLE: The #GduPresentable object.
 * @GDU_POOL_TREE_MODEL_COLUMN_VISIBLE: Whether the item is visible.
 * @GDU_POOL_TREE_MODEL_COLUMN_TOGGLED: Whether the item is toggled.
 * @GDU_POOL_TREE_MODEL_COLUMN_CAN_BE_TOGGLED: Whether the item can be toggled.
 *
 * Columns used in #GduPoolTreeModel.
 */
typedef enum {
        GDU_POOL_TREE_MODEL_COLUMN_ICON,
        GDU_POOL_TREE_MODEL_COLUMN_NAME,
        GDU_POOL_TREE_MODEL_COLUMN_VPD_NAME,
        GDU_POOL_TREE_MODEL_COLUMN_DESCRIPTION,
        GDU_POOL_TREE_MODEL_COLUMN_PRESENTABLE,
        GDU_POOL_TREE_MODEL_COLUMN_VISIBLE,
        GDU_POOL_TREE_MODEL_COLUMN_TOGGLED,
        GDU_POOL_TREE_MODEL_COLUMN_CAN_BE_TOGGLED,
} GduPoolTreeModelColumn;

typedef enum {
        GDU_POOL_TREE_VIEW_FLAGS_NONE        = 0,
        GDU_POOL_TREE_VIEW_FLAGS_SHOW_TOGGLE = (1<<0),
} GduPoolTreeViewFlags;

/**
 * GduPoolTreeModelFlags:
 * @GDU_POOL_TREE_MODEL_FLAGS_NONE: No flags set.
 * @GDU_POOL_TREE_MODEL_FLAGS_NO_VOLUMES: Don't include presentables representing volumes or holes.
 * @GDU_POOL_TREE_MODEL_FLAGS_NO_UNALLOCATABLE_DRIVES: Don't include drives that cannot be allocated to e.g. a RAID array.
 *
 * Flags used when creating a #GduPoolTreeModel.
 */
typedef enum {
        GDU_POOL_TREE_MODEL_FLAGS_NONE                      = 0,
        GDU_POOL_TREE_MODEL_FLAGS_NO_VOLUMES                = (1<<0),
        GDU_POOL_TREE_MODEL_FLAGS_NO_UNALLOCATABLE_DRIVES   = (1<<2),
} GduPoolTreeModelFlags;

#endif /* GDU_GTK_ENUMS_H */
