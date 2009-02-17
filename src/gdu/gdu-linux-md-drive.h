/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*- */
/* gdu-linux-md-drive.h
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

#ifndef GDU_LINUX_MD_DRIVE_H
#define GDU_LINUX_MD_DRIVE_H

#include <gdu/gdu-device.h>
#include <gdu/gdu-drive.h>
#include <gdu/gdu-presentable.h>
#include <gdu/gdu-callbacks.h>

#define GDU_TYPE_LINUX_MD_DRIVE             (gdu_linux_md_drive_get_type ())
#define GDU_LINUX_MD_DRIVE(obj)             (G_TYPE_CHECK_INSTANCE_CAST ((obj), GDU_TYPE_LINUX_MD_DRIVE, GduLinuxMdDrive))
#define GDU_LINUX_MD_DRIVE_CLASS(obj)       (G_TYPE_CHECK_CLASS_CAST ((obj), GDU_LINUX_MD_DRIVE,  GduLinuxMdDriveClass))
#define GDU_IS_LINUX_MD_DRIVE(obj)          (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GDU_TYPE_LINUX_MD_DRIVE))
#define GDU_IS_LINUX_MD_DRIVE_CLASS(obj)    (G_TYPE_CHECK_CLASS_TYPE ((obj), GDU_TYPE_LINUX_MD_DRIVE))
#define GDU_LINUX_MD_DRIVE_GET_CLASS(obj)   (G_TYPE_INSTANCE_GET_CLASS ((obj), GDU_TYPE_LINUX_MD_DRIVE, GduLinuxMdDriveClass))

typedef struct _GduLinuxMdDriveClass       GduLinuxMdDriveClass;

struct _GduLinuxMdDrive
{
        GduDrive parent;

        /*< private >*/
        GduLinuxMdDrivePrivate *priv;
};

struct _GduLinuxMdDriveClass
{
        GduDriveClass parent_class;
};

/**
 * GduLinuxMdDriveSlaveState:
 * @GDU_LINUX_MD_DRIVE_SLAVE_STATE_RUNNING: The drive is activated and the
 * slave is part of it.
 * @GDU_LINUX_MD_DRIVE_SLAVE_STATE_RUNNING_SYNCING: The drive is activated
 * and the slave is part of the array but is in the process of being synced
 * into the array (e.g. it was recently added).
 * @GDU_LINUX_MD_DRIVE_SLAVE_STATE_RUNNING_HOT_SPARE: The drive is activated and
   the slave is a hot spare ready to kick in if a drive fails.
 * @GDU_LINUX_MD_DRIVE_SLAVE_STATE_READY: The drive is not activated and the
 * slave, compared to other slaves, is a valid member of the array.
 * @GDU_LINUX_MD_DRIVE_SLAVE_STATE_NOT_FRESH: Either the drive is not
 * activated but the slave, compared to other slaves, is not valid since
 * other slaves have higher event numbers / more recent usage dates / etc.
 * Otherwise, if the drive is activated, slaves with this type have valid
 * meta-data (e.g. uuid) but is not part of the array (typically happens
 * if the wire connecting the slave was temporarily disconnected).
 *
 * State for slaves of an Linux MD software raid drive.
 **/
typedef enum {
        GDU_LINUX_MD_DRIVE_SLAVE_STATE_RUNNING,
        GDU_LINUX_MD_DRIVE_SLAVE_STATE_RUNNING_SYNCING,
        GDU_LINUX_MD_DRIVE_SLAVE_STATE_RUNNING_HOT_SPARE,
        GDU_LINUX_MD_DRIVE_SLAVE_STATE_READY,
        GDU_LINUX_MD_DRIVE_SLAVE_STATE_NOT_FRESH,
} GduLinuxMdDriveSlaveState;

GType                      gdu_linux_md_drive_get_type             (void);
const gchar               *gdu_linux_md_drive_get_uuid             (GduLinuxMdDrive  *drive);
gboolean                   gdu_linux_md_drive_has_slave            (GduLinuxMdDrive  *drive,
                                                                    GduDevice        *device);
GList                     *gdu_linux_md_drive_get_slaves           (GduLinuxMdDrive  *drive);
GduDevice                 *gdu_linux_md_drive_get_first_slave      (GduLinuxMdDrive  *drive);
int                        gdu_linux_md_drive_get_num_slaves       (GduLinuxMdDrive  *drive);
int                        gdu_linux_md_drive_get_num_ready_slaves (GduLinuxMdDrive  *drive);
GduLinuxMdDriveSlaveState  gdu_linux_md_drive_get_slave_state      (GduLinuxMdDrive  *drive,
                                                                    GduDevice        *slave);

#endif /* GDU_LINUX_MD_DRIVE_H */
