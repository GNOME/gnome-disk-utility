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

#if !defined (GNOME_DISK_UTILITY_INSIDE_GDU_H) && !defined (GDU_COMPILATION)
#error "Only <gdu/gdu.h> can be included directly, this file may disappear or change contents."
#endif

#ifndef GDU_ACTIVATABLE_DRIVE_H
#define GDU_ACTIVATABLE_DRIVE_H

#include <gdu/gdu-device.h>
#include <gdu/gdu-drive.h>
#include <gdu/gdu-presentable.h>
#include <gdu/gdu-callbacks.h>

#define GDU_TYPE_ACTIVATABLE_DRIVE             (gdu_activatable_drive_get_type ())
#define GDU_ACTIVATABLE_DRIVE(obj)             (G_TYPE_CHECK_INSTANCE_CAST ((obj), GDU_TYPE_ACTIVATABLE_DRIVE, GduActivatableDrive))
#define GDU_ACTIVATABLE_DRIVE_CLASS(obj)       (G_TYPE_CHECK_CLASS_CAST ((obj), GDU_ACTIVATABLE_DRIVE,  GduActivatableDriveClass))
#define GDU_IS_ACTIVATABLE_DRIVE(obj)          (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GDU_TYPE_ACTIVATABLE_DRIVE))
#define GDU_IS_ACTIVATABLE_DRIVE_CLASS(obj)    (G_TYPE_CHECK_CLASS_TYPE ((obj), GDU_TYPE_ACTIVATABLE_DRIVE))
#define GDU_ACTIVATABLE_DRIVE_GET_CLASS        (G_TYPE_INSTANCE_GET_CLASS ((obj), GDU_TYPE_ACTIVATABLE_DRIVE, GduActivatableDriveClass))

typedef struct _GduActivatableDriveClass       GduActivatableDriveClass;

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

/**
 * GduActivableDriveKind:
 * @GDU_ACTIVATABLE_DRIVE_KIND_LINUX_MD: Linux md Software RAID
 *
 * The type of activatable drive.
 */
typedef enum {
        GDU_ACTIVATABLE_DRIVE_KIND_LINUX_MD,
} GduActivableDriveKind;

/**
 * GduActivableDriveSlaveState:
 * @GDU_ACTIVATABLE_DRIVE_SLAVE_STATE_RUNNING: The drive is activated and the
 * slave is part of it.
 * @GDU_ACTIVATABLE_DRIVE_SLAVE_STATE_RUNNING_SYNCING: The drive is activated
 * and the slave is part of the array but is in the process of being synced
 * into the array (e.g. it was recently added).
 * @GDU_ACTIVATABLE_DRIVE_SLAVE_STATE_RUNNING_HOT_SPARE: The drive is activated and
   the slave is a hot spare ready to kick in if a drive fails.
 * @GDU_ACTIVATABLE_DRIVE_SLAVE_STATE_READY: The drive is not activated and the
 * slave, compared to other slaves, is a valid member of the array.
 * @GDU_ACTIVATABLE_DRIVE_SLAVE_STATE_NOT_FRESH: Either the drive is not
 * activated but the slave, compared to other slaves, is not valid since
 * other slaves have higher event numbers / more recent usage dates / etc.
 * Otherwise, if the drive is activated, slaves with this type have valid
 * meta-data (e.g. uuid) but is not part of the array (typically happens
 * if the wire connecting the slave was temporarily disconnected).
 *
 * State for slaves of an activatable drive.
 **/
typedef enum {
        GDU_ACTIVATABLE_DRIVE_SLAVE_STATE_RUNNING,
        GDU_ACTIVATABLE_DRIVE_SLAVE_STATE_RUNNING_SYNCING,
        GDU_ACTIVATABLE_DRIVE_SLAVE_STATE_RUNNING_HOT_SPARE,
        GDU_ACTIVATABLE_DRIVE_SLAVE_STATE_READY,
        GDU_ACTIVATABLE_DRIVE_SLAVE_STATE_NOT_FRESH,
} GduActivableDriveSlaveState;

GType                  gdu_activatable_drive_get_type        (void);

gboolean               gdu_activatable_drive_has_slave    (GduActivatableDrive  *activatable_drive,
                                                           GduDevice            *device);
GList                 *gdu_activatable_drive_get_slaves      (GduActivatableDrive  *activatable_drive);
GduDevice             *gdu_activatable_drive_get_first_slave (GduActivatableDrive  *activatable_drive);
int                    gdu_activatable_drive_get_num_slaves  (GduActivatableDrive *activatable_drive);
int                    gdu_activatable_drive_get_num_ready_slaves (GduActivatableDrive *activatable_drive);
GduActivableDriveKind  gdu_activatable_drive_get_kind        (GduActivatableDrive  *activatable_drive);

gboolean               gdu_activatable_drive_is_activated          (GduActivatableDrive  *activatable_drive);
gboolean               gdu_activatable_drive_can_activate          (GduActivatableDrive  *activatable_drive);
gboolean               gdu_activatable_drive_can_activate_degraded (GduActivatableDrive  *activatable_drive);
void                   gdu_activatable_drive_activate              (GduActivatableDrive  *activatable_drive,
                                                                    GduActivatableDriveActivationFunc callback,
                                                                    gpointer                          user_data);
void                   gdu_activatable_drive_deactivate            (GduActivatableDrive    *activatable_drive,
                                                                    GduActivatableDriveDeactivationFunc callback,
                                                                    gpointer                            user_data);

GduActivableDriveSlaveState gdu_activatable_drive_get_slave_state (GduActivatableDrive  *activatable_drive,
                                                                   GduDevice            *slave);

#endif /* GDU_ACTIVATABLE_DRIVE_H */
