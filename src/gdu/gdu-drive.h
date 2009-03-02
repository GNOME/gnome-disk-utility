/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*- */
/* gdu-drive.h
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

#if !defined (__GDU_INSIDE_GDU_H) && !defined (GDU_COMPILATION)
#error "Only <gdu/gdu.h> can be included directly, this file may disappear or change contents."
#endif

#ifndef __GDU_DRIVE_H
#define __GDU_DRIVE_H

#include <gdu/gdu-types.h>

G_BEGIN_DECLS

#define GDU_TYPE_DRIVE         (gdu_drive_get_type ())
#define GDU_DRIVE(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), GDU_TYPE_DRIVE, GduDrive))
#define GDU_DRIVE_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST ((k), GDU_DRIVE,  GduDriveClass))
#define GDU_IS_DRIVE(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), GDU_TYPE_DRIVE))
#define GDU_IS_DRIVE_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), GDU_TYPE_DRIVE))
#define GDU_DRIVE_GET_CLASS(k) (G_TYPE_INSTANCE_GET_CLASS ((k), GDU_TYPE_DRIVE, GduDriveClass))

typedef struct _GduDriveClass    GduDriveClass;
typedef struct _GduDrivePrivate  GduDrivePrivate;

struct _GduDrive
{
        GObject parent;

        /*< private >*/
        GduDrivePrivate *priv;
};

struct _GduDriveClass
{
        GObjectClass parent_class;

        /*< public >*/
        /* VTable */
        gboolean    (*is_active)             (GduDrive              *drive);
        gboolean    (*is_activatable)        (GduDrive              *drive);
        gboolean    (*can_deactivate)        (GduDrive              *drive);
        gboolean    (*can_activate)          (GduDrive              *drive,
                                              gboolean              *out_degraded);
        void        (*activate)              (GduDrive              *drive,
                                              GduDriveActivateFunc   callback,
                                              gpointer               user_data);
        void        (*deactivate)            (GduDrive              *drive,
                                              GduDriveDeactivateFunc callback,
                                              gpointer               user_data);

};

GType       gdu_drive_get_type           (void);

gboolean    gdu_drive_is_active             (GduDrive              *drive);
gboolean    gdu_drive_is_activatable        (GduDrive              *drive);
gboolean    gdu_drive_can_deactivate        (GduDrive              *drive);
gboolean    gdu_drive_can_activate          (GduDrive              *drive,
                                             gboolean              *out_degraded);
void        gdu_drive_activate              (GduDrive              *drive,
                                             GduDriveActivateFunc   callback,
                                             gpointer               user_data);
void        gdu_drive_deactivate            (GduDrive              *drive,
                                             GduDriveDeactivateFunc callback,
                                             gpointer               user_data);

G_END_DECLS

#endif /* __GDU_DRIVE_H */
