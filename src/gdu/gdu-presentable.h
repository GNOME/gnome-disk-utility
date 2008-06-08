/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*- */
/* gdu-presentable.h
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

#include <gdu/gdu-device.h>

#ifndef GDU_PRESENTABLE_H
#define GDU_PRESENTABLE_H

#define GDU_TYPE_PRESENTABLE            (gdu_presentable_get_type ())
#define GDU_PRESENTABLE(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), GDU_TYPE_PRESENTABLE, GduPresentable))
#define GDU_IS_PRESENTABLE(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GDU_TYPE_PRESENTABLE))
#define GDU_PRESENTABLE_GET_IFACE(obj)  (G_TYPE_INSTANCE_GET_INTERFACE ((obj), GDU_TYPE_PRESENTABLE, GduPresentableIface))

typedef struct _GduPresentable GduPresentable;


/**
 * GduPresentableIface:
 * @g_iface: The parent interface.
 * @changed: Signal emitted when the presentable is changed.
 * @removed: Signal emitted when the presentable is removed. Recipients should release all references to the object.
 * @job_changed: Signal emitted when the job state on the underlying #GduDevice changes.
 * @get_device: Returns the underlying #GduDevice.
 * @get_enclosing_presentable: Returns the #GduPresentable that is the parent or #NULL if there is no parent.
 * @get_name: Returns a name for the presentable suitable for presentation in an user interface.
 * @get_icon_name: Returns a name for the icon suitable for display in an user interface.
 * @get_offset: Returns where the data represented by the presentable starts on the underlying main block device.
 * @get_size: Returns the size of the presentable or zero if not allocated.
 * @get_pool: Returns the #GduPool object that the presentable was obtained from.
 * @is_allocated: Returns whether the presentable is allocated or whether it represents free space.
 * @is_recognized: Returns whether the contents of the presentable are recognized (e.g. well-known file system type).
 *
 * Interface for #GduPresentable implementations.
 */
typedef struct _GduPresentableIface    GduPresentableIface;

struct _GduPresentableIface
{
        GTypeInterface g_iface;

        /* signals */
        void (*changed)     (GduPresentable *presentable);
        void (*removed)     (GduPresentable *presentable);
        void (*job_changed) (GduPresentable *presentable);

        /* virtual table */
        GduDevice *      (*get_device)                (GduPresentable *presentable);
        GduPresentable * (*get_enclosing_presentable) (GduPresentable *presentable);
        char *           (*get_name)                  (GduPresentable *presentable);
        char *           (*get_icon_name)             (GduPresentable *presentable);
        guint64          (*get_offset)                (GduPresentable *presentable);
        guint64          (*get_size)                  (GduPresentable *presentable);
        GduPool *        (*get_pool)                  (GduPresentable *presentable);
        gboolean         (*is_allocated)              (GduPresentable *presentable);
        gboolean         (*is_recognized)             (GduPresentable *presentable);
};

GType           gdu_presentable_get_type                  (void) G_GNUC_CONST;
GduDevice      *gdu_presentable_get_device                (GduPresentable *presentable);
GduPresentable *gdu_presentable_get_enclosing_presentable (GduPresentable *presentable);
char           *gdu_presentable_get_name                  (GduPresentable *presentable);
char           *gdu_presentable_get_icon_name             (GduPresentable *presentable);
guint64         gdu_presentable_get_offset                (GduPresentable *presentable);
guint64         gdu_presentable_get_size                  (GduPresentable *presentable);
GduPool        *gdu_presentable_get_pool                  (GduPresentable *presentable);
gboolean        gdu_presentable_is_allocated              (GduPresentable *presentable);
gboolean        gdu_presentable_is_recognized             (GduPresentable *presentable);

GduPresentable *gdu_presentable_get_toplevel              (GduPresentable *presentable);

#endif /* GDU_PRESENTABLE_H */
