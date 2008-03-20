/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*- */
/* gdu-presentable-tree.h
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


#ifndef GNOME_DISK_UTILITY_TREE_H
#define GNOME_DISK_UTILITY_TREE_H

#include <gtk/gtk.h>

#include "gdu-pool.h"

GtkTreeView      *gdu_tree_new                      (GduPool     *pool);
GduPresentable   *gdu_tree_get_selected_presentable (GtkTreeView *tree_view);
void              gdu_tree_select_presentable       (GtkTreeView *tree_view, GduPresentable *presentable);
void              gdu_tree_select_first_presentable (GtkTreeView *tree_view);

#endif /* GNOME_DISK_UTILITY_TREE_H */
