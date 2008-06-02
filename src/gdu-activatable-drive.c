/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*- */
/* gdu-activatable-drive.c
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

#include "gdu-util.h"
#include "gdu-pool.h"
#include "gdu-activatable-drive.h"
#include "gdu-presentable.h"

struct _GduActivatableDrivePrivate
{
        /* device may be NULL */
        GduDevice *device;

        GduActivableDriveKind kind;

        GduPool *pool;

        GList   *slaves;
};

static GObjectClass *parent_class = NULL;

static void gdu_activatable_drive_presentable_iface_init (GduPresentableIface *iface);
G_DEFINE_TYPE_WITH_CODE (GduActivatableDrive, gdu_activatable_drive, GDU_TYPE_DRIVE,
                         G_IMPLEMENT_INTERFACE (GDU_TYPE_PRESENTABLE,
                                                gdu_activatable_drive_presentable_iface_init))

static void device_removed (GduDevice *device, gpointer user_data);
static void device_job_changed (GduDevice *device, gpointer user_data);
static void device_changed (GduDevice *device, gpointer user_data);

static void
gdu_activatable_drive_finalize (GduActivatableDrive *activatable_drive)
{
        GList *l;

        if (activatable_drive->priv->device != NULL) {
                g_signal_handlers_disconnect_by_func (activatable_drive->priv->device, device_changed,
                                                      activatable_drive);
                g_signal_handlers_disconnect_by_func (activatable_drive->priv->device, device_job_changed,
                                                      activatable_drive);
                g_signal_handlers_disconnect_by_func (activatable_drive->priv->device, device_removed,
                                                      activatable_drive);
                g_object_unref (activatable_drive->priv->device);
        }

        if (activatable_drive->priv->pool != NULL) {
                g_object_unref (activatable_drive->priv->pool);
        }

        for (l = activatable_drive->priv->slaves; l != NULL; l = l->next) {
                GduDevice *device = GDU_DEVICE (l->data);
                g_signal_handlers_disconnect_by_func (device, device_changed, activatable_drive);
                g_signal_handlers_disconnect_by_func (device, device_job_changed, activatable_drive);
                g_object_unref (device);
        }
        g_list_free (activatable_drive->priv->slaves);

        if (G_OBJECT_CLASS (parent_class)->finalize)
                (* G_OBJECT_CLASS (parent_class)->finalize) (G_OBJECT (activatable_drive));
}

static void
gdu_activatable_drive_class_init (GduActivatableDriveClass *klass)
{
        GObjectClass *obj_class = (GObjectClass *) klass;

        parent_class = g_type_class_peek_parent (klass);

        obj_class->finalize = (GObjectFinalizeFunc) gdu_activatable_drive_finalize;

}

static void
gdu_activatable_drive_init (GduActivatableDrive *activatable_drive)
{
        activatable_drive->priv = g_new0 (GduActivatableDrivePrivate, 1);
}

static void
device_changed (GduDevice *device, gpointer user_data)
{
        GduActivatableDrive *activatable_drive = GDU_ACTIVATABLE_DRIVE (user_data);
        g_signal_emit_by_name (activatable_drive, "changed");
        g_signal_emit_by_name (activatable_drive->priv->pool, "presentable-changed", activatable_drive);
}

static void
device_job_changed (GduDevice *device, gpointer user_data)
{
        GduActivatableDrive *activatable_drive = GDU_ACTIVATABLE_DRIVE (user_data);
        g_signal_emit_by_name (activatable_drive, "job-changed");
        g_signal_emit_by_name (activatable_drive->priv->pool, "presentable-job-changed", activatable_drive);
}

static void
device_removed (GduDevice *device, gpointer user_data)
{
        GduActivatableDrive *activatable_drive = GDU_ACTIVATABLE_DRIVE (user_data);
        g_signal_emit_by_name (activatable_drive, "removed");
}

GduActivatableDrive *
gdu_activatable_drive_new (GduPool               *pool,
                           GduActivableDriveKind  kind)
{
        GduActivatableDrive *activatable_drive;

        activatable_drive = GDU_ACTIVATABLE_DRIVE (g_object_new (GDU_TYPE_ACTIVATABLE_DRIVE, NULL));
        activatable_drive->priv->pool = g_object_ref (pool);
        activatable_drive->priv->kind = kind;

        return activatable_drive;
}

gboolean
gdu_activatable_drive_has_uuid (GduActivatableDrive  *activatable_drive,
                                const char *uuid)
{
        gboolean ret;
        GduDevice *first_slave;

        ret = FALSE;

        if (activatable_drive->priv->slaves == NULL)
                goto out;

        first_slave = GDU_DEVICE (activatable_drive->priv->slaves->data);
        if (strcmp (gdu_device_linux_md_component_get_uuid (first_slave), uuid) == 0)
                ret = TRUE;

out:
        return ret;
}

gboolean
gdu_activatable_drive_device_references_slave (GduActivatableDrive  *activatable_drive,
                                               GduDevice *device)
{
        int n;
        gboolean ret;
        char **slaves;

        ret = FALSE;

        if (activatable_drive->priv->device == NULL)
                goto out;

        slaves = gdu_device_linux_md_get_slaves (activatable_drive->priv->device);
        for (n = 0; slaves[n] != NULL; n++) {
                if (strcmp (slaves[n], gdu_device_get_object_path (device)) == 0) {
                        ret = TRUE;
                        goto out;
                }
        }

out:
        return ret;
}


GduActivableDriveKind
gdu_activatable_drive_get_kind (GduActivatableDrive *activatable_drive)
{
        return activatable_drive->priv->kind;
}

static GduDevice *
gdu_activatable_drive_get_device (GduPresentable *presentable)
{
        GduActivatableDrive *activatable_drive = GDU_ACTIVATABLE_DRIVE (presentable);
        if (activatable_drive->priv->device == NULL)
                return NULL;
        else
                return g_object_ref (activatable_drive->priv->device);
}

static GduPresentable *
gdu_activatable_drive_get_enclosing_presentable (GduPresentable *presentable)
{
        return NULL;
}

static char *
gdu_activatable_drive_get_name (GduPresentable *presentable)
{
        GduActivatableDrive *activatable_drive = GDU_ACTIVATABLE_DRIVE (presentable);
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

        if (activatable_drive->priv->slaves == NULL)
                goto out;

        device = GDU_DEVICE (activatable_drive->priv->slaves->data);

        switch (activatable_drive->priv->kind) {
        default:
                g_warning ("no naming for kind=%d", activatable_drive->priv->kind);
                break;

        case GDU_ACTIVATABLE_DRIVE_KIND_LINUX_MD:
                level = gdu_device_linux_md_component_get_level (device);
                name = gdu_device_linux_md_component_get_name (device);
                num_raid_devices = gdu_device_linux_md_component_get_num_raid_devices (device);
                num_slaves = g_list_length (activatable_drive->priv->slaves);
                component_size = gdu_device_get_size (device);

                raid_size = gdu_presentable_get_size (GDU_PRESENTABLE (activatable_drive));

                if (strcmp (level, "raid0") == 0) {
                        level_str = g_strdup (_("RAID-0"));
                } else if (strcmp (level, "raid1") == 0) {
                        level_str = g_strdup (_("RAID-1"));
                } else if (strcmp (level, "raid4") == 0) {
                        level_str = g_strdup (_("RAID-4"));
                } else if (strcmp (level, "raid5") == 0) {
                        level_str = g_strdup (_("RAID-5"));
                } else if (strcmp (level, "raid6") == 0) {
                        level_str = g_strdup (_("RAID-6"));
                } else if (strcmp (level, "raid10") == 0) {
                        level_str = g_strdup (_("RAID-10"));
                } else if (strcmp (level, "linear") == 0) {
                        level_str = g_strdup (_("Linear"));
                } else {
                        level_str = g_strdup (level);
                }

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

                break;
        }

out:
        return ret;
}

static char *
gdu_activatable_drive_get_icon_name (GduPresentable *presentable)
{
        GduActivatableDrive *activatable_drive = GDU_ACTIVATABLE_DRIVE (presentable);
        char *ret;

        ret = NULL;

        switch (activatable_drive->priv->kind) {
        default:
                g_warning ("no icon for kind=%d", activatable_drive->priv->kind);
                /* explicit fallthrough */

        case GDU_ACTIVATABLE_DRIVE_KIND_LINUX_MD:
                /* TODO: get this into the naming spec */
                ret = g_strdup ("gdu-raid-array");
                break;
        }
        return ret;
}

static guint64
gdu_activatable_drive_get_offset (GduPresentable *presentable)
{
        return 0;
}

static guint64
gdu_activatable_drive_get_size (GduPresentable *presentable)
{
        GduActivatableDrive *activatable_drive = GDU_ACTIVATABLE_DRIVE (presentable);
        GduDevice *device;
        guint64 ret;
        const char *level;
        int num_raid_devices;
        int n;
        guint component_size;
        GList *l;

        ret = 0;

        if (activatable_drive->priv->device != NULL) {
                ret = gdu_device_get_size (activatable_drive->priv->device);
        } else {
                if (activatable_drive->priv->slaves == NULL) {
                        g_warning ("%s: no device and no slaves", __FUNCTION__);
                        goto out;
                }

                device = GDU_DEVICE (activatable_drive->priv->slaves->data);

                level = gdu_device_linux_md_component_get_level (device);
                num_raid_devices = gdu_device_linux_md_component_get_num_raid_devices (device);
                component_size = gdu_device_get_size (device);

                if (strcmp (level, "raid0") == 0) {
                        /* stripes in linux can have different sizes */

                        if ((int) g_list_length (activatable_drive->priv->slaves) == num_raid_devices) {
                                n = 0;
                                for (l = activatable_drive->priv->slaves; l != NULL; l = l->next) {
                                        GduDevice *sd = GDU_DEVICE (l->data);
                                        GduActivableDriveSlaveState slave_state;

                                        slave_state = gdu_activatable_drive_get_slave_state (activatable_drive, sd);
                                        if (slave_state == GDU_ACTIVATABLE_DRIVE_SLAVE_STATE_READY) {
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

                        if ((int) g_list_length (activatable_drive->priv->slaves) == num_raid_devices) {
                                n = 0;
                                for (l = activatable_drive->priv->slaves; l != NULL; l = l->next) {
                                        GduDevice *sd = GDU_DEVICE (l->data);
                                        GduActivableDriveSlaveState slave_state;

                                        slave_state = gdu_activatable_drive_get_slave_state (activatable_drive, sd);
                                        if (slave_state == GDU_ACTIVATABLE_DRIVE_SLAVE_STATE_READY) {
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
        }

out:
        return ret;
}

static GduPool *
gdu_activatable_drive_get_pool (GduPresentable *presentable)
{
        GduActivatableDrive *activatable_drive = GDU_ACTIVATABLE_DRIVE (presentable);
        return g_object_ref (activatable_drive->priv->pool);
}

static gboolean
gdu_activatable_drive_is_allocated (GduPresentable *presentable)
{
        return TRUE;
}

static gboolean
gdu_activatable_drive_is_recognized (GduPresentable *presentable)
{
        /* TODO: maybe we need to return FALSE sometimes */
        return TRUE;
}

gboolean
gdu_activatable_drive_is_device_set (GduActivatableDrive  *activatable_drive)
{
        return activatable_drive->priv->device != NULL;
}

void
gdu_activatable_drive_set_device (GduActivatableDrive *activatable_drive, GduDevice *device)
{
        if (activatable_drive->priv->device != NULL) {
                g_signal_handlers_disconnect_by_func (activatable_drive->priv->device, device_changed,
                                                      activatable_drive);
                g_signal_handlers_disconnect_by_func (activatable_drive->priv->device, device_job_changed,
                                                      activatable_drive);
                g_signal_handlers_disconnect_by_func (activatable_drive->priv->device, device_removed,
                                                      activatable_drive);
                g_object_unref (activatable_drive->priv->device);
        }

        activatable_drive->priv->device = device != NULL ? g_object_ref (device) : NULL;

        if (device != NULL) {
                g_signal_connect (device, "changed", (GCallback) device_changed, activatable_drive);
                g_signal_connect (device, "job-changed", (GCallback) device_job_changed, activatable_drive);
                g_signal_connect (device, "removed", (GCallback) device_removed, activatable_drive);
        }

        g_signal_emit_by_name (activatable_drive, "changed");
}

gboolean
gdu_activatable_drive_has_slave    (GduActivatableDrive  *activatable_drive,
                                    GduDevice            *device)
{
        return g_list_find (activatable_drive->priv->slaves, device) != NULL;
}

void
gdu_activatable_drive_add_slave (GduActivatableDrive *activatable_drive,
                                 GduDevice           *device)
{
        activatable_drive->priv->slaves = g_list_append (activatable_drive->priv->slaves, device);
        g_signal_emit_by_name (activatable_drive, "changed");
        g_signal_emit_by_name (activatable_drive->priv->pool, "presentable-changed", activatable_drive);

        /* We're also interested in 'changed' events from the slave; forward them onto
         * ourselves.
         *
         * There's no need to watch for removed; the owner (GduPool typically) is responsible for
         * removing slaves when they disappear.
         */
        g_signal_connect (device, "changed", (GCallback) device_changed, activatable_drive);
        g_signal_connect (device, "job-changed", (GCallback) device_job_changed, activatable_drive);
}

void
gdu_activatable_drive_remove_slave (GduActivatableDrive *activatable_drive,
                                    GduDevice           *device)
{
        activatable_drive->priv->slaves = g_list_remove (activatable_drive->priv->slaves, device);
        g_signal_emit_by_name (activatable_drive, "changed");
        g_signal_emit_by_name (activatable_drive->priv->pool, "presentable-changed", activatable_drive);

        g_signal_handlers_disconnect_by_func (device, device_changed, activatable_drive);
        g_signal_handlers_disconnect_by_func (device, device_job_changed, activatable_drive);
}

GList *
gdu_activatable_drive_get_slaves (GduActivatableDrive *activatable_drive)
{
        GList *ret;
        ret = g_list_copy (activatable_drive->priv->slaves);
        g_list_foreach (ret, (GFunc) g_object_ref, NULL);
        return ret;
}

GduDevice *
gdu_activatable_drive_get_first_slave (GduActivatableDrive *activatable_drive)
{
        if (activatable_drive->priv->slaves == NULL)
                return NULL;
        else
                return g_object_ref (G_OBJECT (activatable_drive->priv->slaves->data));
}

int
gdu_activatable_drive_get_num_slaves (GduActivatableDrive *activatable_drive)
{
        return g_list_length (activatable_drive->priv->slaves);
}

int
gdu_activatable_drive_get_num_ready_slaves (GduActivatableDrive *activatable_drive)
{
        GList *l;
        GduDevice *slave;
        int num_ready_slaves;
        GduActivableDriveSlaveState slave_state;

        num_ready_slaves = 0;
        for (l = activatable_drive->priv->slaves; l != NULL; l = l->next) {
                slave = GDU_DEVICE (l->data);
                slave_state = gdu_activatable_drive_get_slave_state (activatable_drive, slave);
                if (slave_state == GDU_ACTIVATABLE_DRIVE_SLAVE_STATE_READY) {
                        num_ready_slaves++;
                }
        }

        return num_ready_slaves;
}

static void
gdu_activatable_drive_presentable_iface_init (GduPresentableIface *iface)
{
        iface->get_device = gdu_activatable_drive_get_device;
        iface->get_enclosing_presentable = gdu_activatable_drive_get_enclosing_presentable;
        iface->get_name = gdu_activatable_drive_get_name;
        iface->get_icon_name = gdu_activatable_drive_get_icon_name;
        iface->get_offset = gdu_activatable_drive_get_offset;
        iface->get_size = gdu_activatable_drive_get_size;
        iface->get_pool = gdu_activatable_drive_get_pool;
        iface->is_allocated = gdu_activatable_drive_is_allocated;
        iface->is_recognized = gdu_activatable_drive_is_recognized;
}

gboolean
gdu_activatable_drive_is_activated (GduActivatableDrive  *activatable_drive)
{
        return activatable_drive->priv->device != NULL;
}

/**
 * gdu_activatable_drive_can_activate:
 * @activatable_drive: A #GduActivatableDrive.
 *
 * Check if @activatable_drive can be activated in non-degraded
 * mode. See also gdu_activatable_drive_can_activate_degraded().
 *
 * Returns: #TRUE only if it can be activated. If @activatable_drive
 * is already activated this function returns #FALSE.
 **/
gboolean
gdu_activatable_drive_can_activate (GduActivatableDrive  *activatable_drive)
{
        int num_slaves;
        int num_ready_slaves;
        int num_raid_devices;
        GduDevice *slave;
        GduActivableDriveSlaveState slave_state;
        gboolean can_activate;
        GList *l;

        can_activate = FALSE;

        /* can't activated what's already activated */
        if (activatable_drive->priv->device != NULL)
                goto out;

        switch (activatable_drive->priv->kind) {
        case GDU_ACTIVATABLE_DRIVE_KIND_LINUX_MD:

                num_raid_devices = -1;
                num_slaves = 0;
                num_ready_slaves = 0;

                /* count the number of slaves in the READY state */
                for (l = activatable_drive->priv->slaves; l != NULL; l = l->next) {
                        slave = GDU_DEVICE (l->data);

                        num_slaves++;

                        slave_state = gdu_activatable_drive_get_slave_state (activatable_drive, slave);
                        if (slave_state == GDU_ACTIVATABLE_DRIVE_SLAVE_STATE_READY) {
                                num_raid_devices = gdu_device_linux_md_component_get_num_raid_devices (slave);
                                num_ready_slaves++;
                        }
                }

                /* we can activate only if all slaves are in the READY */
                if (num_ready_slaves == num_raid_devices) {
                        can_activate = TRUE;
                }
                break;

        default:
                g_warning ("unknown kind %d", activatable_drive->priv->kind);
                break;
        }
out:
        return can_activate;
}

/**
 * gdu_activatable_drive_can_activate_degraded:
 * @activatable_drive: A #GduActivatableDrive.
 *
 * Check if @activatable_drive can be activated in degraded mode. See
 * also gdu_activatable_drive_can_activate().
 *
 * Returns: #TRUE only if it can be activated in degraded mode. If
 * @activatable_drive is already activated or can be activated in
 * non-degraded mode, this function returns #FALSE.
 **/
gboolean
gdu_activatable_drive_can_activate_degraded (GduActivatableDrive  *activatable_drive)
{
        GduDevice *device;
        gboolean can_activate_degraded;

        can_activate_degraded = FALSE;

        /* can't activated what's already activated */
        if (activatable_drive->priv->device != NULL)
                goto out;

        /* we might even be able to activate in non-degraded mode */
        if (gdu_activatable_drive_can_activate (activatable_drive))
                goto out;

        switch (activatable_drive->priv->kind) {
        case GDU_ACTIVATABLE_DRIVE_KIND_LINUX_MD:
                device = gdu_activatable_drive_get_first_slave (activatable_drive);
                if (device != NULL) {
                        int num_ready_slaves;
                        int num_raid_devices;
                        const char *raid_level;

                        num_ready_slaves = gdu_activatable_drive_get_num_ready_slaves (activatable_drive);
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
                        g_object_unref (device);
                }
                break;

        default:
                g_warning ("unknown kind %d", activatable_drive->priv->kind);
                break;
        }

out:
        return can_activate_degraded;
}

typedef struct
{
        GduActivatableDrive *activatable_drive;
        GduActivatableDriveActivationFunc callback;
        gpointer user_data;
} ActivationData;

static ActivationData *
activation_data_new (GduActivatableDrive *activatable_drive,
                     GduActivatableDriveActivationFunc callback,
                     gpointer user_data)
{
        ActivationData *ad;
        ad = g_new0 (ActivationData, 1);
        ad->activatable_drive = g_object_ref (activatable_drive);
        ad->callback = callback;
        ad->user_data = user_data;
        return ad;
}

static void
activation_data_free (ActivationData *ad)
{
        g_object_unref (ad->activatable_drive);
        g_free (ad);
}

static void
activation_completed (GduPool  *pool,
                      char     *assembled_array_object_path,
                      GError   *error,
                      gpointer  user_data)
{
        ActivationData *ad = user_data;
        ad->callback (ad->activatable_drive, assembled_array_object_path, error, ad->user_data);
        activation_data_free (ad);
}

void
gdu_activatable_drive_activate (GduActivatableDrive  *activatable_drive,
                                GduActivatableDriveActivationFunc callback,
                                gpointer user_data)
{
        GPtrArray *components;
        GList *l;
        GList *slaves;

        g_return_if_fail (activatable_drive->priv->kind == GDU_ACTIVATABLE_DRIVE_KIND_LINUX_MD);
        g_return_if_fail (activatable_drive->priv->device == NULL);

        components = g_ptr_array_new ();
        slaves = gdu_activatable_drive_get_slaves (activatable_drive);
        for (l = slaves; l != NULL; l = l->next) {
                GduDevice *d = l->data;
                /* no need to dup; we keep a ref on d for the lifetime of components */
                g_ptr_array_add (components, (gpointer) gdu_device_get_object_path (d));
        }

        gdu_pool_op_linux_md_start (activatable_drive->priv->pool,
                                    components,
                                    activation_completed,
                                    activation_data_new (activatable_drive, callback, user_data));

        g_ptr_array_free (components, TRUE);
        g_list_foreach (slaves, (GFunc) g_object_unref, NULL);
        g_list_free (slaves);
}

typedef struct
{
        GduActivatableDrive *activatable_drive;
        GduActivatableDriveDeactivationFunc callback;
        gpointer user_data;
} DeactivationData;

static DeactivationData *
deactivation_data_new (GduActivatableDrive *activatable_drive,
                       GduActivatableDriveDeactivationFunc callback,
                       gpointer user_data)
{
        DeactivationData *dad;
        dad = g_new0 (DeactivationData, 1);
        dad->activatable_drive = g_object_ref (activatable_drive);
        dad->callback = callback;
        dad->user_data = user_data;
        return dad;
}

static void
deactivation_data_free (DeactivationData *dad)
{
        g_object_unref (dad->activatable_drive);
        g_free (dad);
}

static void
deactivation_completed (GduDevice *device,
                        GError    *error,
                        gpointer   user_data)
{
        DeactivationData *dad = user_data;
        dad->callback (dad->activatable_drive, error, dad->user_data);
        deactivation_data_free (dad);
}

void
gdu_activatable_drive_deactivate (GduActivatableDrive *activatable_drive,
                                  GduActivatableDriveDeactivationFunc callback,
                                  gpointer                            user_data)
{
        g_return_if_fail (activatable_drive->priv->kind == GDU_ACTIVATABLE_DRIVE_KIND_LINUX_MD);
        g_return_if_fail (activatable_drive->priv->device != NULL);

        gdu_device_op_linux_md_stop (activatable_drive->priv->device,
                                     deactivation_completed,
                                     deactivation_data_new (activatable_drive, callback, user_data));
}

GduActivableDriveSlaveState
gdu_activatable_drive_get_slave_state (GduActivatableDrive  *activatable_drive,
                                       GduDevice            *slave)
{
        GList *l;
        guint64 max_event_number;
        gboolean one_of_us;
        GduActivableDriveSlaveState ret;

        g_return_val_if_fail (activatable_drive->priv->kind == GDU_ACTIVATABLE_DRIVE_KIND_LINUX_MD, -1);

        ret = -1;

        /* array is running */
        if (activatable_drive->priv->device != NULL) {
                int n;
                char **array_slaves;
                char **slaves_state;

                array_slaves = gdu_device_linux_md_get_slaves (activatable_drive->priv->device);
                slaves_state = gdu_device_linux_md_get_slaves_state (activatable_drive->priv->device);
                for (n = 0; array_slaves[n] != NULL; n++) {
                        if (strcmp (gdu_device_get_object_path (slave), array_slaves[n]) == 0) {
                                const char *state = slaves_state[n];

                                if (strcmp (state, "in_sync") == 0) {
                                        ret = GDU_ACTIVATABLE_DRIVE_SLAVE_STATE_RUNNING;
                                } else if (strcmp (state, "sync_in_progress") == 0) {
                                        ret = GDU_ACTIVATABLE_DRIVE_SLAVE_STATE_RUNNING_SYNCING;
                                } else if (strcmp (state, "spare") == 0) {
                                        ret = GDU_ACTIVATABLE_DRIVE_SLAVE_STATE_RUNNING_HOT_SPARE;
                                } else {
                                        g_warning ("unknown state '%s' for '%s", state, array_slaves[n]);
                                        ret = GDU_ACTIVATABLE_DRIVE_SLAVE_STATE_RUNNING;
                                }
                                goto out;
                        }
                }

                ret = GDU_ACTIVATABLE_DRIVE_SLAVE_STATE_NOT_FRESH;
                goto out;
        }

        /* array is not running */

        one_of_us = FALSE;

        /* first find the biggest event number */
        max_event_number = 0;
        for (l = activatable_drive->priv->slaves; l != NULL; l = l->next) {
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
                ret = GDU_ACTIVATABLE_DRIVE_SLAVE_STATE_READY;
        } else {
                /* otherwise we're stale */
                ret = GDU_ACTIVATABLE_DRIVE_SLAVE_STATE_NOT_FRESH;
        }

out:
        return ret;
}
