/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*- */
/* gdu-pool.h
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

#if !defined (GNOME_DISK_UTILITY_INSIDE_GDU_H) && !defined (GDU_COMPILATION)
#error "Only <gdu/gdu.h> can be included directly, this file may disappear or change contents."
#endif

#if !defined(GDU_POOL_H)
#define GDU_POOL_H

#include <gdu/gdu-shared.h>
#include <gdu/gdu-presentable.h>
#include <gdu/gdu-known-filesystem.h>

#define GDU_TYPE_POOL             (gdu_pool_get_type ())
#define GDU_POOL(obj)             (G_TYPE_CHECK_INSTANCE_CAST ((obj), GDU_TYPE_POOL, GduPool))
#define GDU_POOL_CLASS(obj)       (G_TYPE_CHECK_CLASS_CAST ((obj), GDU_POOL,  GduPoolClass))
#define GDU_IS_POOL(obj)          (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GDU_TYPE_POOL))
#define GDU_IS_POOL_CLASS(obj)    (G_TYPE_CHECK_CLASS_TYPE ((obj), GDU_TYPE_POOL))
#define GDU_POOL_GET_CLASS        (G_TYPE_INSTANCE_GET_CLASS ((obj), GDU_TYPE_POOL, GduPoolClass))

typedef struct _GduPoolClass       GduPoolClass;

struct _GduPoolPrivate;
typedef struct _GduPoolPrivate     GduPoolPrivate;

struct _GduPool
{
        GObject parent;

        /* private */
        GduPoolPrivate *priv;
};

struct _GduPoolClass
{
        GObjectClass parent_class;

        /* signals */
        void (*device_added) (GduPool *pool, GduDevice *device);
        void (*device_removed) (GduPool *pool, GduDevice *device);
        void (*device_changed) (GduPool *pool, GduDevice *device);
        void (*device_job_changed) (GduPool *pool, GduDevice *device);
        void (*presentable_added) (GduPool *pool, GduPresentable *presentable);
        void (*presentable_removed) (GduPool *pool, GduPresentable *presentable);
        void (*presentable_changed) (GduPool *pool, GduPresentable *presentable);
        void (*presentable_job_changed) (GduPool *pool, GduPresentable *presentable);
};

GType       gdu_pool_get_type           (void);
GduPool    *gdu_pool_new                (void);

char       *gdu_pool_get_daemon_version (GduPool *pool);
gboolean    gdu_pool_supports_luks_devices (GduPool *pool);
GList      *gdu_pool_get_known_filesystems (GduPool *pool);
GduKnownFilesystem *gdu_pool_get_known_filesystem_by_id (GduPool *pool, const char *id);

GduDevice  *gdu_pool_get_by_object_path (GduPool *pool, const char *object_path);
GduPresentable *gdu_pool_get_volume_by_device      (GduPool *pool, GduDevice *device);

GList      *gdu_pool_get_devices               (GduPool *pool);
GList      *gdu_pool_get_presentables          (GduPool *pool);
GList      *gdu_pool_get_enclosed_presentables (GduPool *pool, GduPresentable *presentable);

/* ---------------------------------------------------------------------------------------------------- */

typedef void (*GduPoolLinuxMdStartCompletedFunc) (GduPool    *pool,
                                                  char       *assembled_array_object_path,
                                                  GError     *error,
                                                  gpointer    user_data);

void gdu_pool_op_linux_md_start (GduPool *pool,
                                 GPtrArray *component_objpaths,
                                 GduPoolLinuxMdStartCompletedFunc callback,
                                 gpointer user_data);


#endif /* GDU_POOL_H */
