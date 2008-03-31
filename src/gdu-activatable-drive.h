/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*- */
/* gdu-activatable-drive.h
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

#ifndef GDU_ACTIVATABLE_DRIVE_H
#define GDU_ACTIVATABLE_DRIVE_H

#include "gdu-device.h"
#include "gdu-drive.h"
#include "gdu-presentable.h"

#define GDU_TYPE_ACTIVATABLE_DRIVE             (gdu_activatable_drive_get_type ())
#define GDU_ACTIVATABLE_DRIVE(obj)             (G_TYPE_CHECK_INSTANCE_CAST ((obj), GDU_TYPE_ACTIVATABLE_DRIVE, GduActivatableDrive))
#define GDU_ACTIVATABLE_DRIVE_CLASS(obj)       (G_TYPE_CHECK_CLASS_CAST ((obj), GDU_ACTIVATABLE_DRIVE,  GduActivatableDriveClass))
#define GDU_IS_ACTIVATABLE_DRIVE(obj)          (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GDU_TYPE_ACTIVATABLE_DRIVE))
#define GDU_IS_ACTIVATABLE_DRIVE_CLASS(obj)    (G_TYPE_CHECK_CLASS_TYPE ((obj), GDU_TYPE_ACTIVATABLE_DRIVE))
#define GDU_ACTIVATABLE_DRIVE_GET_CLASS        (G_TYPE_INSTANCE_GET_CLASS ((obj), GDU_TYPE_ACTIVATABLE_DRIVE, GduActivatableDriveClass))

typedef struct _GduActivatableDriveClass       GduActivatableDriveClass;
typedef struct _GduActivatableDrive            GduActivatableDrive;

struct _GduActivatableDrivePrivate;
typedef struct _GduActivatableDrivePrivate     GduActivatableDrivePrivate;

struct _GduActivatableDrive
{
        GduDrive parent;

        /* private */
        GduActivatableDrivePrivate *priv;
};

struct _GduActivatableDriveClass
{
        GduDriveClass parent_class;
};

typedef enum {
        GDU_ACTIVATABLE_DRIVE_KIND_LINUX_MD,
} GduActivableDriveKind;

GType                  gdu_activatable_drive_get_type        (void);
GduActivatableDrive   *gdu_activatable_drive_new             (GduPool              *pool,
                                                              GduActivableDriveKind kind,
                                                              const char           *uuid);
void                   gdu_activatable_drive_set_device      (GduActivatableDrive  *activatable_drive,
                                                              GduDevice            *device);
void                   gdu_activatable_drive_add_slave       (GduActivatableDrive  *activatable_drive,
                                                              GduDevice            *device);
void                   gdu_activatable_drive_remove_slave    (GduActivatableDrive  *activatable_drive,
                                                              GduDevice            *device);
GList                 *gdu_activatable_drive_get_slaves      (GduActivatableDrive  *activatable_drive);
GduDevice             *gdu_activatable_drive_get_first_slave (GduActivatableDrive  *activatable_drive);
int                    gdu_activatable_drive_get_num_slaves  (GduActivatableDrive *activatable_drive);
GduActivableDriveKind  gdu_activatable_drive_get_kind        (GduActivatableDrive  *activatable_drive);


#endif /* GDU_ACTIVATABLE_DRIVE_H */
