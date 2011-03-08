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

#ifndef __GDU_TREE_MODEL_H__
#define __GDU_TREE_MODEL_H__

#include <gtk/gtk.h>
#include "gdutypes.h"

G_BEGIN_DECLS

#define GDU_TYPE_TREE_MODEL         (gdu_tree_model_get_type ())
#define GDU_TREE_MODEL(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), GDU_TYPE_TREE_MODEL, GduTreeModel))
#define GDU_IS_TREE_MODEL(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), GDU_TYPE_TREE_MODEL))

enum
{
  GDU_TREE_MODEL_COLUMN_SORT_KEY,
  GDU_TREE_MODEL_COLUMN_IS_HEADING,
  GDU_TREE_MODEL_COLUMN_HEADING_TEXT,
  GDU_TREE_MODEL_COLUMN_ICON,
  GDU_TREE_MODEL_COLUMN_NAME,
  GDU_TREE_MODEL_COLUMN_OBJECT_PROXY,
  GDU_TREE_MODEL_N_COLUMNS
};

GType         gdu_tree_model_get_type   (void) G_GNUC_CONST;
GduTreeModel *gdu_tree_model_new        (UDisksClient *client);
UDisksClient *gdu_tree_model_get_client (GduTreeModel *model);


G_END_DECLS

#endif /* __GDU_TREE_MODEL_H__ */
