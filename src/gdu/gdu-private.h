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

#if defined (__GDU_INSIDE_GDU_H)
#error "Can't include a private header in the public header file."
#endif

#ifndef __GDU_PRIVATE_H
#define __GDU_PRIVATE_H

#include "gdu-types.h"

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


GduLinuxMdDrive   *_gdu_linux_md_drive_new             (GduPool              *pool,
                                                        const gchar          *uuid);

gboolean _gdu_linux_md_drive_has_uuid (GduLinuxMdDrive  *drive,
                                       const gchar      *uuid);


gboolean    _gdu_device_changed               (GduDevice   *device);
void        _gdu_device_job_changed           (GduDevice   *device,
                                               gboolean     job_in_progress,
                                               const char  *job_id,
                                               uid_t        job_initiated_by_uid,
                                               gboolean     job_is_cancellable,
                                               int          job_num_tasks,
                                               int          job_cur_task,
                                               const char  *job_cur_task_id,
                                               double       job_cur_task_percentage);

void _gdu_volume_rewrite_enclosing_presentable (GduVolume *volume);
void _gdu_volume_hole_rewrite_enclosing_presentable (GduVolumeHole *volume_hole);

#endif /* __GDU_PRIVATE_H */
