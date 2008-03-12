/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*- */
/* gdu-device.h
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

#ifndef GDU_DEVICE_H
#define GDU_DEVICE_H

#include <glib-object.h>

#define GDU_TYPE_DEVICE             (gdu_device_get_type ())
#define GDU_DEVICE(obj)             (G_TYPE_CHECK_INSTANCE_CAST ((obj), GDU_TYPE_DEVICE, GduDevice))
#define GDU_DEVICE_CLASS(obj)       (G_TYPE_CHECK_CLASS_CAST ((obj), GDU_DEVICE,  GduDeviceClass))
#define GDU_IS_DEVICE(obj)          (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GDU_TYPE_DEVICE))
#define GDU_IS_DEVICE_CLASS(obj)    (G_TYPE_CHECK_CLASS_TYPE ((obj), GDU_TYPE_DEVICE))
#define GDU_DEVICE_GET_CLASS        (G_TYPE_INSTANCE_GET_CLASS ((obj), GDU_TYPE_DEVICE, GduDeviceClass))

typedef struct _GduDeviceClass       GduDeviceClass;
typedef struct _GduDevice            GduDevice;

struct _GduDevicePrivate;
typedef struct _GduDevicePrivate     GduDevicePrivate;

struct _GduPool;;
typedef struct _GduPool GduPool;

struct _GduDevice
{
        GObject parent;

        /* private */
        GduDevicePrivate *priv;
};

struct _GduDeviceClass
{
        GObjectClass parent_class;

        /* signals */
        void (*changed) (GduDevice *device);
        void (*removed) (GduDevice *device);
};

GType       gdu_device_get_type              (void);
GduDevice  *gdu_device_new_from_object_path  (GduPool     *pool, const char  *object_path);
const char *gdu_device_get_object_path       (GduDevice   *device);
GduDevice  *gdu_device_find_parent           (GduDevice   *device);

void        gdu_device_changed               (GduDevice   *device);
void        gdu_device_removed               (GduDevice   *device);

const char *gdu_device_get_device_file (GduDevice *device);
guint64 gdu_device_get_size (GduDevice *device);
guint64 gdu_device_get_block_size (GduDevice *device);
gboolean gdu_device_is_removable (GduDevice *device);
gboolean gdu_device_is_media_available (GduDevice *device);
gboolean gdu_device_is_partition (GduDevice *device);
gboolean gdu_device_is_partition_table (GduDevice *device);
gboolean gdu_device_is_drive (GduDevice *device);

const char *gdu_device_id_get_usage (GduDevice *device);
const char *gdu_device_id_get_type (GduDevice *device);
const char *gdu_device_id_get_version (GduDevice *device);
const char *gdu_device_id_get_label (GduDevice *device);
const char *gdu_device_id_get_uuid (GduDevice *device);

const char *gdu_device_partition_get_slave (GduDevice *device);
const char *gdu_device_partition_get_scheme (GduDevice *device);
const char *gdu_device_partition_get_type (GduDevice *device);
const char *gdu_device_partition_get_label (GduDevice *device);
const char *gdu_device_partition_get_uuid (GduDevice *device);
char **gdu_device_partition_get_flags (GduDevice *device);
int gdu_device_partition_get_number (GduDevice *device);
guint64 gdu_device_partition_get_offset (GduDevice *device);
guint64 gdu_device_partition_get_size (GduDevice *device);

const char *gdu_device_partition_table_get_scheme (GduDevice *device);
int         gdu_device_partition_table_get_count (GduDevice *device);
int         gdu_device_partition_table_get_max_number (GduDevice *device);
GArray     *gdu_device_partition_table_get_offsets (GduDevice *device);
GArray     *gdu_device_partition_table_get_sizes (GduDevice *device);

const char *gdu_device_drive_get_vendor (GduDevice *device);
const char *gdu_device_drive_get_model (GduDevice *device);
const char *gdu_device_drive_get_revision (GduDevice *device);
const char *gdu_device_drive_get_serial (GduDevice *device);

#endif /* GDU_DEVICE_H */
