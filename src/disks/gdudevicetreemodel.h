/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*-
 *
 * Copyright (C) 2008-2013 Red Hat, Inc.
 *
 * Licensed under GPL version 2 or later.
 *
 * Author: David Zeuthen <zeuthen@gmail.com>
 */

#ifndef __GDU_DEVICE_TREE_MODEL_H__
#define __GDU_DEVICE_TREE_MODEL_H__

#include <gtk/gtk.h>
#include "gdutypes.h"

G_BEGIN_DECLS

#define GDU_TYPE_DEVICE_TREE_MODEL         (gdu_device_tree_model_get_type ())
#define GDU_DEVICE_TREE_MODEL(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), GDU_TYPE_DEVICE_TREE_MODEL, GduDeviceTreeModel))
#define GDU_IS_DEVICE_TREE_MODEL(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), GDU_TYPE_DEVICE_TREE_MODEL))

enum
{
  GDU_DEVICE_TREE_MODEL_COLUMN_SORT_KEY,
  GDU_DEVICE_TREE_MODEL_COLUMN_IS_HEADING,
  GDU_DEVICE_TREE_MODEL_COLUMN_HEADING_TEXT,
  GDU_DEVICE_TREE_MODEL_COLUMN_ICON,
  GDU_DEVICE_TREE_MODEL_COLUMN_NAME,
  GDU_DEVICE_TREE_MODEL_COLUMN_OBJECT,
  GDU_DEVICE_TREE_MODEL_COLUMN_BLOCK,
  GDU_DEVICE_TREE_MODEL_COLUMN_WARNING,
  GDU_DEVICE_TREE_MODEL_COLUMN_PULSE,
  GDU_DEVICE_TREE_MODEL_COLUMN_JOBS_RUNNING,
  GDU_DEVICE_TREE_MODEL_COLUMN_POWER_STATE_FLAGS,
  GDU_DEVICE_TREE_MODEL_COLUMN_SIZE,
  GDU_DEVICE_TREE_MODEL_COLUMN_SELECTED,
  GDU_DEVICE_TREE_MODEL_N_COLUMNS
};

GType               gdu_device_tree_model_get_type            (void) G_GNUC_CONST;
GduDeviceTreeModel *gdu_device_tree_model_new                 (GduApplication     *application);
GduApplication     *gdu_device_tree_model_get_application     (GduDeviceTreeModel *model);
gboolean            gdu_device_tree_model_get_iter_for_object (GduDeviceTreeModel *model,
                                                               UDisksObject       *object,
                                                               GtkTreeIter        *iter);

void                gdu_device_tree_model_clear_selected      (GduDeviceTreeModel *model);
void                gdu_device_tree_model_toggle_selected     (GduDeviceTreeModel *model,
                                                               GtkTreeIter        *iter);
GList              *gdu_device_tree_model_get_selected        (GduDeviceTreeModel *model);
GList              *gdu_device_tree_model_get_selected_blocks (GduDeviceTreeModel *model);


G_END_DECLS

#endif /* __GDU_DEVICE_TREE_MODEL_H__ */
