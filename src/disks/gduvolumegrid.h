/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*-
 *
 * Copyright (C) 2008-2013 Red Hat, Inc.
 *
 * Licensed under GPL version 2 or later.
 *
 * Author: David Zeuthen <zeuthen@gmail.com>
 */

#ifndef __GDU_VOLUME_GRID_H__
#define __GDU_VOLUME_GRID_H__

#include <gtk/gtk.h>
#include "gdutypes.h"

G_BEGIN_DECLS

#define GDU_TYPE_VOLUME_GRID         gdu_volume_grid_get_type()
#define GDU_VOLUME_GRID(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), GDU_TYPE_VOLUME_GRID, GduVolumeGrid))
#define GDU_IS_VOLUME_GRID(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), GDU_TYPE_VOLUME_GRID))

GType                     gdu_volume_grid_get_type              (void) G_GNUC_CONST;
GtkWidget*                gdu_volume_grid_new                   (GduApplication      *application);
void                      gdu_volume_grid_set_block_object      (GduVolumeGrid       *grid,
                                                                 UDisksObject        *block_device);
UDisksObject             *gdu_volume_grid_get_block_object      (GduVolumeGrid      *grid);

void                      gdu_volume_grid_set_no_media_string   (GduVolumeGrid       *grid,
                                                                 const gchar         *str);
const gchar              *gdu_volume_grid_get_no_media_string   (GduVolumeGrid      *grid);

gboolean                  gdu_volume_grid_includes_object       (GduVolumeGrid       *grid,
                                                                 UDisksObject        *object);
gboolean                  gdu_volume_grid_select_object         (GduVolumeGrid       *grid,
                                                                 UDisksObject        *block_object);

GduVolumeGridElementType  gdu_volume_grid_get_selected_type     (GduVolumeGrid       *grid);
UDisksObject             *gdu_volume_grid_get_selected_device   (GduVolumeGrid       *grid);
guint64                   gdu_volume_grid_get_selected_offset   (GduVolumeGrid       *grid);
guint64                   gdu_volume_grid_get_selected_size     (GduVolumeGrid       *grid);

G_END_DECLS

#endif /* __GDU_VOLUME_GRID_H__ */
