/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*- */
/* gdu-private.h
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

#if defined (GNOME_DISK_UTILITY_INSIDE_GDU_H)
#error "Can't include a private header in the public header file."
#endif

#ifndef GDU_PRIVATE_H
#define GDU_PRIVATE_H

#include <glib-object.h>
#include "gdu-smart-data.h"
#include "gdu-known-filesystem.h"
#include "gdu-process.h"
#include "gdu-device.h"
#include "gdu-drive.h"
#include "gdu-activatable-drive.h"
#include "gdu-volume.h"
#include "gdu-volume-hole.h"

#define SMART_DATA_STRUCT_TYPE (dbus_g_type_get_struct ("GValueArray",   \
                                                        G_TYPE_INT,      \
                                                        G_TYPE_STRING,   \
                                                        G_TYPE_INT,      \
                                                        G_TYPE_INT,      \
                                                        G_TYPE_INT,      \
                                                        G_TYPE_INT,      \
                                                        G_TYPE_STRING,   \
                                                        G_TYPE_INVALID))

#define HISTORICAL_SMART_DATA_STRUCT_TYPE (dbus_g_type_get_struct ("GValueArray",   \
                                                                   G_TYPE_UINT64, \
                                                                   G_TYPE_DOUBLE, \
                                                                   G_TYPE_UINT64, \
                                                                   G_TYPE_STRING, \
                                                                   G_TYPE_BOOLEAN, \
                                                                   dbus_g_type_get_collection ("GPtrArray", SMART_DATA_STRUCT_TYPE), \
                                                                   G_TYPE_INVALID))

#define KNOWN_FILESYSTEMS_STRUCT_TYPE (dbus_g_type_get_struct ("GValueArray",   \
                                                               G_TYPE_STRING, \
                                                               G_TYPE_STRING, \
                                                               G_TYPE_BOOLEAN, \
                                                               G_TYPE_BOOLEAN, \
                                                               G_TYPE_BOOLEAN, \
                                                               G_TYPE_UINT, \
                                                               G_TYPE_BOOLEAN, \
                                                               G_TYPE_BOOLEAN, \
                                                               G_TYPE_BOOLEAN, \
                                                               G_TYPE_BOOLEAN, \
                                                               G_TYPE_BOOLEAN, \
                                                               G_TYPE_BOOLEAN, \
                                                               G_TYPE_BOOLEAN, \
                                                               G_TYPE_BOOLEAN, \
                                                               G_TYPE_INVALID))

#define PROCESS_STRUCT_TYPE (dbus_g_type_get_struct ("GValueArray",   \
                                                     G_TYPE_UINT,     \
                                                     G_TYPE_UINT,     \
                                                     G_TYPE_STRING,   \
                                                     G_TYPE_INVALID))

GduSmartDataAttribute *_gdu_smart_data_attribute_new   (gpointer data);
GduSmartData          *_gdu_smart_data_new_from_values (guint64     time_collected,
                                                        double      temperature,
                                                        guint64     time_powered_on,
                                                        const char *last_self_test_result,
                                                        gboolean    is_failing,
                                                        GPtrArray  *attrs);

GduSmartData          * _gdu_smart_data_new            (gpointer data);

GduKnownFilesystem    *_gdu_known_filesystem_new       (gpointer data);

GduProcess            * _gdu_process_new               (gpointer data);

void _gdu_error_fixup (GError *error);

GduDevice  *_gdu_device_new_from_object_path  (GduPool     *pool, const char  *object_path);

GduVolume   *_gdu_volume_new_from_device      (GduPool *pool, GduDevice *volume, GduPresentable *enclosing_presentable);
GduDrive    *_gdu_drive_new_from_device       (GduPool *pool, GduDevice *drive);
GduVolumeHole   *_gdu_volume_hole_new       (GduPool *pool, guint64 offset, guint64 size, GduPresentable *enclosing_presentable);
GduActivatableDrive   *_gdu_activatable_drive_new             (GduPool              *pool,
                                                               GduActivableDriveKind kind);

void                   _gdu_activatable_drive_set_device      (GduActivatableDrive  *activatable_drive,
                                                               GduDevice            *device);
gboolean               _gdu_activatable_drive_is_device_set   (GduActivatableDrive  *activatable_drive);
void                   _gdu_activatable_drive_add_slave       (GduActivatableDrive  *activatable_drive,
                                                               GduDevice            *device);
void                   _gdu_activatable_drive_remove_slave    (GduActivatableDrive  *activatable_drive,
                                                               GduDevice            *device);
gboolean               _gdu_activatable_drive_has_uuid        (GduActivatableDrive  *activatable_drive,
                                                               const char *uuid);
gboolean               _gdu_activatable_drive_device_references_slave (GduActivatableDrive  *activatable_drive,
                                                                       GduDevice *device);

void        _gdu_device_changed               (GduDevice   *device);
void        _gdu_device_job_changed           (GduDevice   *device,
                                               gboolean     job_in_progress,
                                               const char  *job_id,
                                               uid_t        job_initiated_by_uid,
                                               gboolean     job_is_cancellable,
                                               int          job_num_tasks,
                                               int          job_cur_task,
                                               const char  *job_cur_task_id,
                                               double       job_cur_task_percentage);

void        _gdu_device_removed               (GduDevice   *device);

#endif /* GDU_PRIVATE_H */
