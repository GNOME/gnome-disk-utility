/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*- */
/* gdu-linux-md-drive.c
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

#include <config.h>
#include <string.h>
#include <glib/gi18n.h>
#include <dbus/dbus-glib.h>

#include "gdu-shared.h"
#include "gdu-private.h"
#include "gdu-util.h"
#include "gdu-pool.h"
#include "gdu-linux-md-drive.h"
#include "gdu-presentable.h"

/**
 * SECTION:gdu-linux-md-drive
 * @title: GduLinuxMdDrive
 * @short_description: Linux Software RAID drives
 *
 * The #GduLinuxMdDrive class represents drives Linux Software RAID arrays.
 *
 * An #GduLinuxMdDrive drive is added to #GduPool as soon as a
 * component device that is part of the abstraction is available.  The
 * drive can be started (gdu_drive_start()) and stopped
 * (gdu_drive_stop()) and the state of the underlying components can
 * be queried through gdu_linux_md_drive_get_slave_state()).
 *
 * See the documentation for #GduPresentable for the big picture.
 */

struct _GduLinuxMdDrivePrivate
{
        /* device may be NULL */
        GduDevice *device;

        GList *slaves;

        GduPool *pool;

        gchar *uuid;

        gchar *id;
};

static GObjectClass *parent_class = NULL;

static void gdu_linux_md_drive_presentable_iface_init (GduPresentableIface *iface);

G_DEFINE_TYPE_WITH_CODE (GduLinuxMdDrive, gdu_linux_md_drive, GDU_TYPE_DRIVE,
                         G_IMPLEMENT_INTERFACE (GDU_TYPE_PRESENTABLE,
                                                gdu_linux_md_drive_presentable_iface_init))

static void device_added (GduPool *pool, GduDevice *device, gpointer user_data);
static void device_removed (GduPool *pool, GduDevice *device, gpointer user_data);
static void device_changed (GduPool *pool, GduDevice *device, gpointer user_data);

static gboolean    gdu_linux_md_drive_is_running         (GduDrive            *drive);
static gboolean    gdu_linux_md_drive_can_start_stop     (GduDrive            *drive);
static gboolean    gdu_linux_md_drive_can_start          (GduDrive            *drive);
static gboolean    gdu_linux_md_drive_can_start_degraded (GduDrive            *drive);
static void        gdu_linux_md_drive_start              (GduDrive            *drive,
                                                          GduDriveStartFunc    callback,
                                                          gpointer             user_data);
static void        gdu_linux_md_drive_stop               (GduDrive            *drive,
                                                          GduDriveStopFunc     callback,
                                                          gpointer             user_data);

static void
gdu_linux_md_drive_finalize (GObject *object)
{
        GduLinuxMdDrive *drive = GDU_LINUX_MD_DRIVE (object);

        if (drive->priv->pool != NULL) {
                g_signal_handlers_disconnect_by_func (drive->priv->pool, device_added, drive);
                g_signal_handlers_disconnect_by_func (drive->priv->pool, device_removed, drive);
                g_signal_handlers_disconnect_by_func (drive->priv->pool, device_changed, drive);
                g_object_unref (drive->priv->pool);
        }

        if (drive->priv->device != NULL) {
                g_object_unref (drive->priv->device);
        }

        g_list_foreach (drive->priv->slaves, (GFunc) g_object_unref, NULL);
        g_list_free (drive->priv->slaves);

        g_free (drive->priv->id);

        if (G_OBJECT_CLASS (parent_class)->finalize)
                (* G_OBJECT_CLASS (parent_class)->finalize) (G_OBJECT (drive));
}

static void
gdu_linux_md_drive_class_init (GduLinuxMdDriveClass *klass)
{
        GObjectClass *gobject_class = (GObjectClass *) klass;
        GduDriveClass *drive_class = (GduDriveClass *) klass;

        parent_class = g_type_class_peek_parent (klass);

        gobject_class->finalize = gdu_linux_md_drive_finalize;

        g_type_class_add_private (klass, sizeof (GduLinuxMdDrivePrivate));

        drive_class->is_running         = gdu_linux_md_drive_is_running;
        drive_class->can_start_stop     = gdu_linux_md_drive_can_start_stop;
        drive_class->can_start          = gdu_linux_md_drive_can_start;
        drive_class->can_start_degraded = gdu_linux_md_drive_can_start_degraded;
        drive_class->start              = gdu_linux_md_drive_start;
        drive_class->stop               = gdu_linux_md_drive_stop;
}

static void
gdu_linux_md_drive_init (GduLinuxMdDrive *drive)
{
        drive->priv = G_TYPE_INSTANCE_GET_PRIVATE (drive, GDU_TYPE_LINUX_MD_DRIVE, GduLinuxMdDrivePrivate);
}

static void
prime_devices (GduLinuxMdDrive *drive)
{
        GList *l;
        GList *devices;

        devices = gdu_pool_get_devices (drive->priv->pool);

        for (l = devices; l != NULL; l = l->next) {
                GduDevice *device = GDU_DEVICE (l->data);

                if (gdu_device_is_linux_md (device) &&
                    g_strcmp0 (gdu_device_linux_md_get_uuid (device), drive->priv->uuid) == 0) {
                        drive->priv->device = g_object_ref (device);
                }

                if (gdu_device_is_linux_md_component (device) &&
                    g_strcmp0 (gdu_device_linux_md_component_get_uuid (device), drive->priv->uuid) == 0) {
                        drive->priv->slaves = g_list_prepend (drive->priv->slaves, g_object_ref (device));
                }

        }

        g_list_foreach (devices, (GFunc) g_object_unref, NULL);
        g_list_free (devices);
}

static void
emit_changed (GduLinuxMdDrive *drive)
{
        //g_debug ("emitting changed for uuid '%s'", drive->priv->uuid);
        g_signal_emit_by_name (drive, "changed");
        g_signal_emit_by_name (drive->priv->pool, "presentable-changed", drive);
}

static void
device_added (GduPool *pool, GduDevice *device, gpointer user_data)
{
        GduLinuxMdDrive *drive = GDU_LINUX_MD_DRIVE (user_data);

        //g_debug ("MD: in device_added %s '%s'", gdu_device_get_object_path (device), gdu_device_linux_md_get_uuid (device));

        if (gdu_device_is_linux_md (device) &&
            g_strcmp0 (gdu_device_linux_md_get_uuid (device), drive->priv->uuid) == 0) {
                if (drive->priv->device != NULL) {
                        g_warning ("Already have md device %s", gdu_device_get_device_file (device));
                        g_object_unref (drive->priv->device);
                }
                drive->priv->device = g_object_ref (device);
                emit_changed (drive);
        }

        if (gdu_device_is_linux_md_component (device) &&
            g_strcmp0 (gdu_device_linux_md_component_get_uuid (device), drive->priv->uuid) == 0) {
                GList *l;

                for (l = drive->priv->slaves; l != NULL; l = l->next) {
                        if (g_strcmp0 (gdu_device_get_device_file (GDU_DEVICE (l->data)),
                                       gdu_device_get_device_file (device)) == 0) {
                                g_warning ("Already have md slave %s", gdu_device_get_device_file (device));
                                g_object_unref (GDU_DEVICE (l->data));
                                drive->priv->slaves = g_list_delete_link (drive->priv->slaves, l);
                        }
                }

                drive->priv->slaves = g_list_prepend (drive->priv->slaves, g_object_ref (device));
                emit_changed (drive);
        }
}

static void
device_removed (GduPool *pool, GduDevice *device, gpointer user_data)
{
        GduLinuxMdDrive *drive = GDU_LINUX_MD_DRIVE (user_data);

        //g_debug ("MD: in device_removed %s", gdu_device_get_object_path (device));

        if (device == drive->priv->device) {
                g_object_unref (device);
                drive->priv->device = NULL;
                emit_changed (drive);
        }

        if (g_list_find (drive->priv->slaves, device) != NULL) {
                g_object_unref (device);
                drive->priv->slaves = g_list_remove (drive->priv->slaves, device);
                emit_changed (drive);
        }
}

static void
device_changed (GduPool *pool, GduDevice *device, gpointer user_data)
{
        GduLinuxMdDrive *drive = GDU_LINUX_MD_DRIVE (user_data);

        //g_debug ("MD: in device_changed %s", gdu_device_get_object_path (device));

        if (device == drive->priv->device || g_list_find (drive->priv->slaves, device) != NULL) {
                emit_changed (drive);
        }
}

GduLinuxMdDrive *
_gdu_linux_md_drive_new (GduPool      *pool,
                         const gchar  *uuid)
{
        GduLinuxMdDrive *drive;

        drive = GDU_LINUX_MD_DRIVE (g_object_new (GDU_TYPE_LINUX_MD_DRIVE, NULL));
        drive->priv->pool = g_object_ref (pool);
        drive->priv->uuid = g_strdup (uuid);
        drive->priv->id = g_strdup_printf ("linux_md_%s", uuid);

        g_signal_connect (drive->priv->pool, "device-added", G_CALLBACK (device_added), drive);
        g_signal_connect (drive->priv->pool, "device-removed", G_CALLBACK (device_removed), drive);
        g_signal_connect (drive->priv->pool, "device-changed", G_CALLBACK (device_changed), drive);

        prime_devices (drive);

        return drive;
}

gboolean
_gdu_linux_md_drive_has_uuid (GduLinuxMdDrive  *drive,
                              const gchar      *uuid)
{
        return g_strcmp0 (drive->priv->uuid, uuid) == 0;
}

/**
 * gdu_linux_md_drive_has_slave:
 * @drive: A #GduLinuxMdDrive.
 * @device: A #GduDevice.
 *
 * Checks if @device is a component of @drive.
 *
 * Returns: #TRUE only if @slave is a component of @drive.
 **/
gboolean
gdu_linux_md_drive_has_slave    (GduLinuxMdDrive  *drive,
                                 GduDevice            *device)
{
        return g_list_find (drive->priv->slaves, device) != NULL;
}

/**
 * gdu_linux_md_drive_get_slaves:
 * @drive: A #GduLinuxMdDrive.
 *
 * Gets all slaves of @drive.
 *
 * Returns: A #GList of #GduDevice objects. Caller must free this list (and call g_object_unref() on all elements).
 **/
GList *
gdu_linux_md_drive_get_slaves (GduLinuxMdDrive *drive)
{
        GList *ret;
        ret = g_list_copy (drive->priv->slaves);
        g_list_foreach (ret, (GFunc) g_object_ref, NULL);
        return ret;
}

/**
 * gdu_linux_md_drive_get_first_slave:
 * @drive: A #GduLinuxMdDrive.
 *
 * Gets the first slave of @drive.
 *
 * Returns: A #GduDevice or #NULL if there are no slaves. Caller must free this object with g_object_unref().
 **/
GduDevice *
gdu_linux_md_drive_get_first_slave (GduLinuxMdDrive *drive)
{
        if (drive->priv->slaves == NULL)
                return NULL;
        else
                return g_object_ref (G_OBJECT (drive->priv->slaves->data));
}

/**
 * gdu_linux_md_drive_get_num_slaves:
 * @drive: A #GduLinuxMdDrive.
 *
 * Gets the total number of slaves of @drive.
 *
 * Returns: The number of slaves of @drive.
 **/
int
gdu_linux_md_drive_get_num_slaves (GduLinuxMdDrive *drive)
{
        return g_list_length (drive->priv->slaves);
}

/**
 * gdu_linux_md_drive_get_num_ready_slaves:
 * @drive: A #GduLinuxMdDrive.
 *
 * Gets the number of fresh/ready (See
 * #GDU_LINUX_MD_DRIVE_SLAVE_STATE_READY) slaves of
 * @drive.
 *
 * Returns: The number of fresh/ready slaves of @drive.
 **/
int
gdu_linux_md_drive_get_num_ready_slaves (GduLinuxMdDrive *drive)
{
        GList *l;
        GduDevice *slave;
        int num_ready_slaves;
        GduLinuxMdDriveSlaveState slave_state;

        num_ready_slaves = 0;
        for (l = drive->priv->slaves; l != NULL; l = l->next) {
                slave = GDU_DEVICE (l->data);
                slave_state = gdu_linux_md_drive_get_slave_state (drive, slave);
                if (slave_state == GDU_LINUX_MD_DRIVE_SLAVE_STATE_READY) {
                        num_ready_slaves++;
                }
        }

        return num_ready_slaves;
}


/**
 * gdu_linux_md_drive_get_slave_state:
 * @drive: A #GduLinuxMdDrive.
 * @slave: A #GduDevice.
 *
 * Gets the state of @slave of @drive.
 *
 * Returns: A value from #GduLinuxMdDriveSlaveState for @slave.
 **/
GduLinuxMdDriveSlaveState
gdu_linux_md_drive_get_slave_state (GduLinuxMdDrive  *drive,
                                    GduDevice        *slave)
{
        GList *l;
        guint64 max_event_number;
        gboolean one_of_us;
        GduLinuxMdDriveSlaveState ret;

        ret = -1;

        /* array is running */
        if (drive->priv->device != NULL) {
                int n;
                char **array_slaves;
                char **slaves_state;

                array_slaves = gdu_device_linux_md_get_slaves (drive->priv->device);
                slaves_state = gdu_device_linux_md_get_slaves_state (drive->priv->device);
                for (n = 0; array_slaves[n] != NULL; n++) {
                        if (strcmp (gdu_device_get_object_path (slave), array_slaves[n]) == 0) {
                                const char *state = slaves_state[n];

                                if (strcmp (state, "in_sync") == 0) {
                                        ret = GDU_LINUX_MD_DRIVE_SLAVE_STATE_RUNNING;
                                } else if (strcmp (state, "sync_in_progress") == 0) {
                                        ret = GDU_LINUX_MD_DRIVE_SLAVE_STATE_RUNNING_SYNCING;
                                } else if (strcmp (state, "spare") == 0) {
                                        ret = GDU_LINUX_MD_DRIVE_SLAVE_STATE_RUNNING_HOT_SPARE;
                                } else {
                                        g_warning ("unknown state '%s' for '%s", state, array_slaves[n]);
                                        ret = GDU_LINUX_MD_DRIVE_SLAVE_STATE_RUNNING;
                                }
                                goto out;
                        }
                }

                ret = GDU_LINUX_MD_DRIVE_SLAVE_STATE_NOT_FRESH;
                goto out;
        }

        /* array is not running */

        one_of_us = FALSE;

        /* first find the biggest event number */
        max_event_number = 0;
        for (l = drive->priv->slaves; l != NULL; l = l->next) {
                GduDevice *d = GDU_DEVICE (l->data);
                guint64 event_number;

                if (!gdu_device_is_linux_md_component (d)) {
                        g_warning ("slave is not linux md component!");
                        break;
                }

                event_number = gdu_device_linux_md_component_get_events (d);
                if (event_number > max_event_number)
                        max_event_number = event_number;

                if (d == slave)
                        one_of_us = TRUE;
        }

        if (!one_of_us) {
                g_warning ("given device is not a slave of the activatable drive");
                goto out;
        }

        /* if our event number equals the max then it's all good */
        if (max_event_number == gdu_device_linux_md_component_get_events (slave)) {
                ret = GDU_LINUX_MD_DRIVE_SLAVE_STATE_READY;
        } else {
                /* otherwise we're stale */
                ret = GDU_LINUX_MD_DRIVE_SLAVE_STATE_NOT_FRESH;
        }

out:
        return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

/* GduPresentable methods */

static const gchar *
gdu_linux_md_drive_get_id (GduPresentable *presentable)
{
        GduLinuxMdDrive *drive = GDU_LINUX_MD_DRIVE (presentable);

        return drive->priv->id;
}

static GduDevice *
gdu_linux_md_drive_get_device (GduPresentable *presentable)
{
        GduLinuxMdDrive *drive = GDU_LINUX_MD_DRIVE (presentable);
        if (drive->priv->device == NULL)
                return NULL;
        else
                return g_object_ref (drive->priv->device);
}

static GduPresentable *
gdu_linux_md_drive_get_enclosing_presentable (GduPresentable *presentable)
{
        return NULL;
}

static char *
gdu_linux_md_drive_get_name (GduPresentable *presentable)
{
        GduLinuxMdDrive *drive = GDU_LINUX_MD_DRIVE (presentable);
        GduDevice *device;
        char *ret;
        char *level_str;
        char *size_str;
        guint64 component_size;
        guint64 raid_size;
        int num_slaves;
        int num_raid_devices;
        const char *level;
        const char *name;

        ret = NULL;

        if (drive->priv->slaves == NULL)
                goto out;

        device = GDU_DEVICE (drive->priv->slaves->data);

        level = gdu_device_linux_md_component_get_level (device);
        name = gdu_device_linux_md_component_get_name (device);
        num_raid_devices = gdu_device_linux_md_component_get_num_raid_devices (device);
        num_slaves = g_list_length (drive->priv->slaves);
        component_size = gdu_device_get_size (device);

        raid_size = gdu_presentable_get_size (GDU_PRESENTABLE (drive));

        level_str = gdu_linux_md_get_raid_level_for_display (level);

        if (raid_size == 0)
                size_str = NULL;
        else
                size_str = gdu_util_get_size_for_display (raid_size, FALSE);

        if (name == NULL || strlen (name) == 0) {
                if (size_str != NULL)
                        ret = g_strdup_printf (_("%s %s Drive"), size_str, level_str);
                else
                        ret = g_strdup_printf (_("%s Drive"), level_str);
        } else {
                if (size_str != NULL)
                        ret = g_strdup_printf (_("%s %s (%s)"), size_str, name, level_str);
                else
                        ret = g_strdup_printf (_("%s (%s)"), name, level_str);
        }

        g_free (level_str);
        g_free (size_str);

out:
        return ret;
}

static GIcon *
gdu_linux_md_drive_get_icon (GduPresentable *presentable)
{
        return g_themed_icon_new_with_default_fallbacks ("gdu-raid-array");
}

static guint64
gdu_linux_md_drive_get_offset (GduPresentable *presentable)
{
        return 0;
}

static guint64
gdu_linux_md_drive_get_size (GduPresentable *presentable)
{
        GduLinuxMdDrive *drive = GDU_LINUX_MD_DRIVE (presentable);
        GduDevice *device;
        guint64 ret;
        const char *level;
        int num_raid_devices;
        int n;
        guint component_size;
        GList *l;

        ret = 0;

        if (drive->priv->device != NULL) {
                ret = gdu_device_get_size (drive->priv->device);
                goto out;
        }

        if (drive->priv->slaves == NULL) {
                g_warning ("%s: no device and no slaves", __FUNCTION__);
                goto out;
        }

        device = GDU_DEVICE (drive->priv->slaves->data);

        level = gdu_device_linux_md_component_get_level (device);
        num_raid_devices = gdu_device_linux_md_component_get_num_raid_devices (device);
        component_size = gdu_device_get_size (device);

        if (strcmp (level, "raid0") == 0) {
                /* stripes in linux can have different sizes */

                if ((int) g_list_length (drive->priv->slaves) == num_raid_devices) {
                        n = 0;
                        for (l = drive->priv->slaves; l != NULL; l = l->next) {
                                GduDevice *sd = GDU_DEVICE (l->data);
                                GduLinuxMdDriveSlaveState slave_state;

                                slave_state = gdu_linux_md_drive_get_slave_state (drive, sd);
                                if (slave_state == GDU_LINUX_MD_DRIVE_SLAVE_STATE_READY) {
                                        ret += gdu_device_get_size (sd);;
                                        n++;
                                }
                        }
                        if (n != num_raid_devices) {
                                ret = 0;
                        }
                }

        } else if (strcmp (level, "raid1") == 0) {
                ret = component_size;
        } else if (strcmp (level, "raid4") == 0) {
                ret = component_size * (num_raid_devices - 1) / num_raid_devices;
        } else if (strcmp (level, "raid5") == 0) {
                ret = component_size * (num_raid_devices - 1) / num_raid_devices;
        } else if (strcmp (level, "raid6") == 0) {
                ret = component_size * (num_raid_devices - 2) / num_raid_devices;
        } else if (strcmp (level, "raid10") == 0) {
                /* TODO: need to figure out out to compute this */
        } else if (strcmp (level, "linear") == 0) {

                if ((int) g_list_length (drive->priv->slaves) == num_raid_devices) {
                        n = 0;
                        for (l = drive->priv->slaves; l != NULL; l = l->next) {
                                GduDevice *sd = GDU_DEVICE (l->data);
                                GduLinuxMdDriveSlaveState slave_state;

                                slave_state = gdu_linux_md_drive_get_slave_state (drive, sd);
                                if (slave_state == GDU_LINUX_MD_DRIVE_SLAVE_STATE_READY) {
                                        ret += gdu_device_get_size (sd);;
                                        n++;
                                }
                        }
                        if (n != num_raid_devices) {
                                ret = 0;
                        }
                }

        } else {
                g_warning ("%s: unknown level '%s'", __FUNCTION__, level);
        }

out:
        return ret;
}

static GduPool *
gdu_linux_md_drive_get_pool (GduPresentable *presentable)
{
        GduLinuxMdDrive *drive = GDU_LINUX_MD_DRIVE (presentable);
        return g_object_ref (drive->priv->pool);
}

static gboolean
gdu_linux_md_drive_is_allocated (GduPresentable *presentable)
{
        return TRUE;
}

static gboolean
gdu_linux_md_drive_is_recognized (GduPresentable *presentable)
{
        /* TODO: maybe we need to return FALSE sometimes */
        return TRUE;
}

static void
gdu_linux_md_drive_presentable_iface_init (GduPresentableIface *iface)
{
        iface->get_id = gdu_linux_md_drive_get_id;
        iface->get_device = gdu_linux_md_drive_get_device;
        iface->get_enclosing_presentable = gdu_linux_md_drive_get_enclosing_presentable;
        iface->get_name = gdu_linux_md_drive_get_name;
        iface->get_icon = gdu_linux_md_drive_get_icon;
        iface->get_offset = gdu_linux_md_drive_get_offset;
        iface->get_size = gdu_linux_md_drive_get_size;
        iface->get_pool = gdu_linux_md_drive_get_pool;
        iface->is_allocated = gdu_linux_md_drive_is_allocated;
        iface->is_recognized = gdu_linux_md_drive_is_recognized;
}

/* ---------------------------------------------------------------------------------------------------- */

/* GduDrive virtual method overrides */

static gboolean
gdu_linux_md_drive_is_running (GduDrive *_drive)
{
        GduLinuxMdDrive *drive = GDU_LINUX_MD_DRIVE (_drive);
        //g_debug ("is running %p", drive->priv->device);
        return drive->priv->device != NULL;
}

static gboolean
gdu_linux_md_drive_can_start_stop (GduDrive *_drive)
{
        return TRUE;
}

static gboolean
gdu_linux_md_drive_can_start (GduDrive *_drive)
{
        GduLinuxMdDrive *drive = GDU_LINUX_MD_DRIVE (_drive);
        int num_slaves;
        int num_ready_slaves;
        int num_raid_devices;
        GduDevice *slave;
        GduLinuxMdDriveSlaveState slave_state;
        gboolean can_activate;
        GList *l;

        can_activate = FALSE;

        /* can't activated what's already activated */
        if (drive->priv->device != NULL)
                goto out;

        num_raid_devices = -1;
        num_slaves = 0;
        num_ready_slaves = 0;

        /* count the number of slaves in the READY state */
        for (l = drive->priv->slaves; l != NULL; l = l->next) {
                slave = GDU_DEVICE (l->data);

                num_slaves++;

                slave_state = gdu_linux_md_drive_get_slave_state (drive, slave);
                if (slave_state == GDU_LINUX_MD_DRIVE_SLAVE_STATE_READY) {
                        num_raid_devices = gdu_device_linux_md_component_get_num_raid_devices (slave);
                        num_ready_slaves++;
                }
        }

        /* we can activate only if all slaves are in the READY */
        if (num_ready_slaves == num_raid_devices) {
                can_activate = TRUE;
        }

out:
        return can_activate;
}

static gboolean
gdu_linux_md_drive_can_start_degraded (GduDrive *_drive)
{
        GduLinuxMdDrive *drive = GDU_LINUX_MD_DRIVE (_drive);
        GduDevice *device;
        gboolean can_activate_degraded;
        int num_ready_slaves;
        int num_raid_devices;
        const char *raid_level;

        device = NULL;

        can_activate_degraded = FALSE;

        /* can't activated what's already activated */
        if (drive->priv->device != NULL)
                goto out;

        /* we might even be able to activate in non-degraded mode */
        if (gdu_linux_md_drive_can_start (_drive))
                goto out;

        device = gdu_linux_md_drive_get_first_slave (drive);
        if (device == NULL)
                goto out;

        num_ready_slaves = gdu_linux_md_drive_get_num_ready_slaves (drive);
        num_raid_devices = gdu_device_linux_md_component_get_num_raid_devices (device);
        raid_level = gdu_device_linux_md_component_get_level (device);

        /* this depends on the raid level... */
        if (strcmp (raid_level, "raid1") == 0) {
                if (num_ready_slaves >= 1) {
                        can_activate_degraded = TRUE;
                }
        } else if (strcmp (raid_level, "raid4") == 0) {
                if (num_ready_slaves >= num_raid_devices - 1) {
                        can_activate_degraded = TRUE;
                }
        } else if (strcmp (raid_level, "raid5") == 0) {
                if (num_ready_slaves >= num_raid_devices - 1) {
                        can_activate_degraded = TRUE;
                }
        } else if (strcmp (raid_level, "raid6") == 0) {
                if (num_ready_slaves >= num_raid_devices - 2) {
                        can_activate_degraded = TRUE;
                }
        } else if (strcmp (raid_level, "raid10") == 0) {
                /* TODO: This is not necessarily correct; it depends on which
                 *       slaves have failed... Right now we err on the side
                 *       of saying the array can be activated even when sometimes
                 *       it can't
                 */
                if (num_ready_slaves >= num_raid_devices / 2) {
                        can_activate_degraded = TRUE;
                }
        }


out:
        if (device != NULL)
                g_object_unref (device);
        return can_activate_degraded;
}

typedef struct
{
        GduLinuxMdDrive *drive;
        GduDriveStartFunc callback;
        gpointer user_data;
} ActivationData;

static ActivationData *
activation_data_new (GduLinuxMdDrive *drive,
                     GduDriveStartFunc callback,
                     gpointer user_data)
{
        ActivationData *ad;
        ad = g_new0 (ActivationData, 1);
        ad->drive = g_object_ref (drive);
        ad->callback = callback;
        ad->user_data = user_data;
        return ad;
}

static void
activation_data_free (ActivationData *ad)
{
        g_object_unref (ad->drive);
        g_free (ad);
}

static void
activation_completed (GduPool  *pool,
                      char     *assembled_array_object_path,
                      GError   *error,
                      gpointer  user_data)
{
        ActivationData *ad = user_data;
        ad->callback (GDU_DRIVE (ad->drive), assembled_array_object_path, error, ad->user_data);
        activation_data_free (ad);
}

static void
gdu_linux_md_drive_start (GduDrive            *_drive,
                          GduDriveStartFunc    callback,
                          gpointer             user_data)
{
        GduLinuxMdDrive *drive = GDU_LINUX_MD_DRIVE (_drive);
        GPtrArray *components;
        GList *l;
        GList *slaves;

        g_return_if_fail (drive->priv->device == NULL);

        components = g_ptr_array_new ();
        slaves = gdu_linux_md_drive_get_slaves (drive);
        for (l = slaves; l != NULL; l = l->next) {
                GduDevice *d = l->data;
                /* no need to dup; we keep a ref on d for the lifetime of components */
                g_ptr_array_add (components, (gpointer) gdu_device_get_object_path (d));
        }

        gdu_pool_op_linux_md_start (drive->priv->pool,
                                    components,
                                    activation_completed,
                                    activation_data_new (drive, callback, user_data));

        g_ptr_array_free (components, TRUE);
        g_list_foreach (slaves, (GFunc) g_object_unref, NULL);
        g_list_free (slaves);
}

typedef struct
{
        GduLinuxMdDrive *drive;
        GduDriveStopFunc callback;
        gpointer user_data;
} DeactivationData;


static DeactivationData *
deactivation_data_new (GduLinuxMdDrive *drive,
                       GduDriveStopFunc callback,
                       gpointer user_data)
{
        DeactivationData *dad;
        dad = g_new0 (DeactivationData, 1);
        dad->drive = g_object_ref (drive);
        dad->callback = callback;
        dad->user_data = user_data;
        return dad;
}

static void
deactivation_data_free (DeactivationData *dad)
{
        g_object_unref (dad->drive);
        g_free (dad);
}

static void
deactivation_completed (GduDevice *device,
                        GError    *error,
                        gpointer   user_data)
{
        DeactivationData *dad = user_data;
        dad->callback (GDU_DRIVE (dad->drive), error, dad->user_data);
        deactivation_data_free (dad);
}


static void
gdu_linux_md_drive_stop (GduDrive            *_drive,
                         GduDriveStopFunc     callback,
                         gpointer             user_data)
{
        GduLinuxMdDrive *drive = GDU_LINUX_MD_DRIVE (_drive);

        g_return_if_fail (drive->priv->device != NULL);

        gdu_device_op_linux_md_stop (drive->priv->device,
                                     deactivation_completed,
                                     deactivation_data_new (drive, callback, user_data));
}
