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

#ifndef __GDU_VOLUME_GRID_H__
#define __GDU_VOLUME_GRID_H__

#include <gtk/gtk.h>
#include "gdutypes.h"

G_BEGIN_DECLS

#define GDU_TYPE_VOLUME_GRID         gdu_volume_grid_get_type()
#define GDU_VOLUME_GRID(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), GDU_TYPE_VOLUME_GRID, GduVolumeGrid))
#define GDU_IS_VOLUME_GRID(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), GDU_TYPE_VOLUME_GRID))

GType                     gdu_volume_grid_get_type            (void) G_GNUC_CONST;
GtkWidget*                gdu_volume_grid_new                 (UDisksClient        *client);
void                      gdu_volume_grid_set_block_device    (GduVolumeGrid       *grid,
                                                               GDBusObjectProxy    *block_device);
GduVolumeGridElementType  gdu_volume_grid_get_selected_type   (GduVolumeGrid       *grid);
GDBusObjectProxy         *gdu_volume_grid_get_selected_device (GduVolumeGrid       *grid);
guint64                   gdu_volume_grid_get_selected_offset (GduVolumeGrid       *grid);
guint64                   gdu_volume_grid_get_selected_size   (GduVolumeGrid       *grid);

G_END_DECLS

#endif /* __GDU_VOLUME_GRID_H__ */
