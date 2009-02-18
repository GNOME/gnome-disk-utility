/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*- */
/* gdu-volume.c
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
#include <stdlib.h>

#include "gdu-private.h"
#include "gdu-util.h"
#include "gdu-pool.h"
#include "gdu-device.h"
#include "gdu-volume.h"
#include "gdu-presentable.h"
#include "gdu-linux-md-drive.h"

/**
 * SECTION:gdu-volume
 * @title: GduVolume
 * @short_description: Volumes
 *
 * The #GduVolume class is used to represent regions of a drive;
 * typically it represents partitions (for partitioned devices) or
 * the whole file system (for e.g. optical discs and floppy disks).
 *
 * See the documentation for #GduPresentable for the big picture.
 */

struct _GduVolumePrivate
{
        GduDevice *device;
        GduPool *pool;
        GduPresentable *enclosing_presentable;
        gchar *id;
};

static GObjectClass *parent_class = NULL;

static void gdu_volume_presentable_iface_init (GduPresentableIface *iface);
G_DEFINE_TYPE_WITH_CODE (GduVolume, gdu_volume, G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (GDU_TYPE_PRESENTABLE,
                                                gdu_volume_presentable_iface_init))

static void device_removed (GduDevice *device, gpointer user_data);
static void device_job_changed (GduDevice *device, gpointer user_data);
static void device_changed (GduDevice *device, gpointer user_data);

static void
gdu_volume_finalize (GduVolume *volume)
{
        //g_debug ("finalized volume '%s' %p", volume->priv->id, volume);

        if (volume->priv->device != NULL) {
                g_signal_handlers_disconnect_by_func (volume->priv->device, device_changed, volume);
                g_signal_handlers_disconnect_by_func (volume->priv->device, device_job_changed, volume);
                g_signal_handlers_disconnect_by_func (volume->priv->device, device_removed, volume);
                g_object_unref (volume->priv->device);
        }

        if (volume->priv->pool != NULL)
                g_object_unref (volume->priv->pool);

        if (volume->priv->enclosing_presentable != NULL)
                g_object_unref (volume->priv->enclosing_presentable);

        g_free (volume->priv->id);

        if (G_OBJECT_CLASS (parent_class)->finalize)
                (* G_OBJECT_CLASS (parent_class)->finalize) (G_OBJECT (volume));
}

static void
gdu_volume_class_init (GduVolumeClass *klass)
{
        GObjectClass *obj_class = (GObjectClass *) klass;

        parent_class = g_type_class_peek_parent (klass);

        obj_class->finalize = (GObjectFinalizeFunc) gdu_volume_finalize;

        g_type_class_add_private (klass, sizeof (GduVolumePrivate));
}

static void
gdu_volume_init (GduVolume *volume)
{
        volume->priv = G_TYPE_INSTANCE_GET_PRIVATE (volume, GDU_TYPE_VOLUME, GduVolumePrivate);
}

static void
device_changed (GduDevice *device, gpointer user_data)
{
        GduVolume *volume = GDU_VOLUME (user_data);
        g_signal_emit_by_name (volume, "changed");
        g_signal_emit_by_name (volume->priv->pool, "presentable-changed", volume);
}

static void
device_job_changed (GduDevice *device, gpointer user_data)
{
        GduVolume *volume = GDU_VOLUME (user_data);
        g_signal_emit_by_name (volume, "job-changed");
        g_signal_emit_by_name (volume->priv->pool, "presentable-job-changed", volume);
}

static void
device_removed (GduDevice *device, gpointer user_data)
{
        GduVolume *volume = GDU_VOLUME (user_data);
        g_signal_emit_by_name (volume, "removed");
}

GduVolume *
_gdu_volume_new_from_device (GduPool *pool, GduDevice *device, GduPresentable *enclosing_presentable)
{
        GduVolume *volume;

        volume = GDU_VOLUME (g_object_new (GDU_TYPE_VOLUME, NULL));
        volume->priv->device = g_object_ref (device);
        volume->priv->pool = g_object_ref (pool);
        volume->priv->enclosing_presentable =
                enclosing_presentable != NULL ? g_object_ref (enclosing_presentable) : NULL;
        volume->priv->id = g_strdup_printf ("volume_%s_enclosed_by_%s",
                                            gdu_device_get_device_file (volume->priv->device),
                                            enclosing_presentable != NULL ? gdu_presentable_get_id (enclosing_presentable) : "(none)");

        g_signal_connect (device, "changed", (GCallback) device_changed, volume);
        g_signal_connect (device, "job-changed", (GCallback) device_job_changed, volume);
        g_signal_connect (device, "removed", (GCallback) device_removed, volume);
        return volume;
}

static const gchar *
gdu_volume_get_id (GduPresentable *presentable)
{
        GduVolume *volume = GDU_VOLUME (presentable);
        return volume->priv->id;
}

static GduDevice *
gdu_volume_get_device (GduPresentable *presentable)
{
        GduVolume *volume = GDU_VOLUME (presentable);
        return g_object_ref (volume->priv->device);
}

static GduPresentable *
gdu_volume_get_enclosing_presentable (GduPresentable *presentable)
{
        GduVolume *volume = GDU_VOLUME (presentable);
        if (volume->priv->enclosing_presentable != NULL)
                return g_object_ref (volume->priv->enclosing_presentable);
        return NULL;
}

static char *
gdu_volume_get_name (GduPresentable *presentable)
{
        GduVolume *volume = GDU_VOLUME (presentable);
        const char *label;
        const char *usage;
        const char *type;
        char *result;
        gboolean is_extended_partition;
        char *strsize;
        guint64 size;

        result = NULL;

        label = gdu_device_id_get_label (volume->priv->device);
        if (gdu_device_is_partition (volume->priv->device))
                size = gdu_device_partition_get_size (volume->priv->device);
        else
                size = gdu_device_get_size (volume->priv->device);
        strsize = gdu_util_get_size_for_display (size, FALSE);

        /* see comment in gdu_pool_add_device_by_object_path() for how to avoid hardcoding 0x05 etc. types */
        is_extended_partition = FALSE;
        if (gdu_device_is_partition (volume->priv->device) &&
            strcmp (gdu_device_partition_get_scheme (volume->priv->device), "mbr") == 0) {
                int type;
                type = strtol (gdu_device_partition_get_type (volume->priv->device), NULL, 0);
                if (type == 0x05 || type == 0x0f || type == 0x85)
                        is_extended_partition = TRUE;
        }

        usage = gdu_device_id_get_usage (volume->priv->device);
        type = gdu_device_id_get_type (volume->priv->device);

        if (is_extended_partition) {
                result = g_strdup_printf (_("%s Extended"), strsize);
        } else if ((usage != NULL && strcmp (usage, "filesystem") == 0) &&
                   (label != NULL && strlen (label) > 0)) {
                result = g_strdup (label);
        } else if (usage != NULL) {
                if (strcmp (usage, "crypto") == 0) {
                        result = g_strdup_printf (_("%s Encrypted"), strsize);
                } else if (strcmp (usage, "filesystem") == 0) {
                        char *fsname;
                        fsname = gdu_util_get_fstype_for_display (gdu_device_id_get_type (volume->priv->device),
                                                                  gdu_device_id_get_version (volume->priv->device),
                                                                  FALSE);
                        result = g_strdup_printf (_("%s %s File System"), strsize, fsname);
                        g_free (fsname);
                } else if (strcmp (usage, "partitiontable") == 0) {
                        result = g_strdup_printf (_("%s Partition Table"), strsize);
                } else if (strcmp (usage, "raid") == 0) {
                        if (strcmp (type, "LVM2_member") == 0) {
                                result = g_strdup_printf (_("%s LVM2 Physical Volume"), strsize);
                        } else {
                                const gchar *array_name;
                                const gchar *level;
                                gchar *level_str;

                                array_name = gdu_device_linux_md_component_get_name (volume->priv->device);
                                level = gdu_device_linux_md_component_get_level (volume->priv->device);

                                if (level != NULL && strlen (level) > 0)
                                        level_str = gdu_linux_md_get_raid_level_for_display (level);
                                else
                                        level_str = g_strdup (_("RAID"));

                                if (array_name != NULL && strlen (array_name) > 0) {
                                        /* RAID component; the label is the array name */
                                        result = g_strdup_printf (_("%s %s (%s)"), strsize, level_str, array_name);
                                } else {
                                        result = g_strdup_printf (_("%s %s"), strsize, level_str);
                                }

                                g_free (level_str);
                        }
                } else if (strcmp (usage, "other") == 0) {
                        if (strcmp (type, "swap") == 0) {
                                result = g_strdup_printf (_("%s Swap Space"), strsize);
                        } else {
                                result = g_strdup_printf (_("%s Data"), strsize);
                        }
                } else if (strcmp (usage, "") == 0) {
                        result = g_strdup_printf (_("%s Unrecognized"), strsize);
                }
        } else {
                if (gdu_device_is_partition (volume->priv->device)) {
                        result = g_strdup_printf (_("%s Partition"), strsize);
                } else {
                        result = g_strdup_printf (_("%s Partition"), strsize);
                }
        }

        if (result == NULL)
                result = g_strdup_printf (_("%s Unrecognized"), strsize);

        g_free (strsize);

        return result;
}

static GIcon *
gdu_volume_get_icon (GduPresentable *presentable)
{
        GduVolume *volume = GDU_VOLUME (presentable);
        GduPresentable *p;
        GduDevice *d;
        const char *usage;
        const char *connection_interface;
        const char *name;
        const char *drive_media;
        gboolean is_removable;
        GIcon *icon;

        p = NULL;
        d = NULL;
        name = NULL;
        is_removable = FALSE;

        usage = gdu_device_id_get_usage (volume->priv->device);

        p = gdu_presentable_get_toplevel (presentable);
        if (p == NULL)
                goto out;

        if (GDU_IS_LINUX_MD_DRIVE (p)) {
                name = "gdu-raid-array";
                goto out;
        }

#if 0
        /* unless it's a file system or LUKS encrypted volume, use the gdu-unmountable icon */
        if (g_strcmp0 (usage, "filesystem") != 0 && g_strcmp0 (usage, "crypto") != 0) {
                name = "gdu-unmountable";
                goto out;
        }
#endif

        d = gdu_presentable_get_device (p);
        if (d == NULL)
                goto out;

        if (!gdu_device_is_drive (d))
                goto out;

        connection_interface = gdu_device_drive_get_connection_interface (d);
        if (connection_interface == NULL)
                goto out;

        is_removable = gdu_device_is_removable (d);

        drive_media = gdu_device_drive_get_media (d);

        /* first try the media */
        if (drive_media != NULL) {
                if (strcmp (drive_media, "flash_cf") == 0) {
                        name = "media-flash-cf";
                } else if (strcmp (drive_media, "flash_ms") == 0) {
                        name = "media-flash-ms";
                } else if (strcmp (drive_media, "flash_sm") == 0) {
                        name = "media-flash-sm";
                } else if (strcmp (drive_media, "flash_sd") == 0) {
                        name = "media-flash-sd";
                } else if (strcmp (drive_media, "flash_sdhc") == 0) {
                        /* TODO: get icon name for sdhc */
                        name = "media-flash-sd";
                } else if (strcmp (drive_media, "flash_mmc") == 0) {
                        /* TODO: get icon for mmc */
                        name = "media-flash-sd";
                } else if (g_str_has_prefix (drive_media, "flash")) {
                        name = "media-flash";
                } else if (g_str_has_prefix (drive_media, "optical")) {
                        /* TODO: handle rest of optical-* */
                        name = "media-optical";
                }
        }

        /* else fall back to connection interface */
        if (name == NULL && connection_interface != NULL) {
                if (g_str_has_prefix (connection_interface, "ata")) {
                        if (is_removable)
                                name = "drive-removable-media-ata";
                        else
                                name = "drive-harddisk-ata";
                } else if (g_str_has_prefix (connection_interface, "scsi")) {
                        if (is_removable)
                                name = "drive-removable-media-scsi";
                        else
                                name = "drive-harddisk-scsi";
                } else if (strcmp (connection_interface, "usb") == 0) {
                        if (is_removable)
                                name = "drive-removable-media-usb";
                        else
                                name = "drive-harddisk-usb";
                } else if (strcmp (connection_interface, "firewire") == 0) {
                        if (is_removable)
                                name = "drive-removable-media-ieee1394";
                        else
                                name = "drive-harddisk-ieee1394";
                }
        }

out:
        if (p != NULL)
                g_object_unref (p);
        if (d != NULL)
                g_object_unref (d);

        /* ultimate fallback */
        if (name == NULL) {
                if (is_removable)
                        name = "drive-removable-media";
                else
                        name = "drive-harddisk";
        }

        icon = g_themed_icon_new_with_default_fallbacks (name);

        if (usage != NULL && strcmp (usage, "crypto") == 0) {
                GEmblem *emblem;
                GIcon *padlock;
                GIcon *emblemed_icon;

                padlock = g_themed_icon_new ("gdu-encrypted-lock");
                emblem = g_emblem_new_with_origin (padlock, G_EMBLEM_ORIGIN_DEVICE);

                emblemed_icon = g_emblemed_icon_new (icon, emblem);
                g_object_unref (icon);
                icon = emblemed_icon;

                g_object_unref (padlock);
                g_object_unref (emblem);

        }

        if (gdu_device_is_luks_cleartext (volume->priv->device)) {
                GEmblem *emblem;
                GIcon *padlock;
                GIcon *emblemed_icon;

                padlock = g_themed_icon_new ("gdu-encrypted-unlock");
                emblem = g_emblem_new_with_origin (padlock, G_EMBLEM_ORIGIN_DEVICE);

                emblemed_icon = g_emblemed_icon_new (icon, emblem);
                g_object_unref (icon);
                icon = emblemed_icon;

                g_object_unref (padlock);
                g_object_unref (emblem);
        }

        return icon;
}

static guint64
gdu_volume_get_offset (GduPresentable *presentable)
{
        GduVolume *volume = GDU_VOLUME (presentable);
        if (gdu_device_is_partition (volume->priv->device))
                return gdu_device_partition_get_offset (volume->priv->device);
        return 0;
}

static guint64
gdu_volume_get_size (GduPresentable *presentable)
{
        GduVolume *volume = GDU_VOLUME (presentable);
        if (gdu_device_is_partition (volume->priv->device))
                return gdu_device_partition_get_size (volume->priv->device);
        return gdu_device_get_size (volume->priv->device);
}

static GduPool *
gdu_volume_get_pool (GduPresentable *presentable)
{
        GduVolume *volume = GDU_VOLUME (presentable);
        return gdu_device_get_pool (volume->priv->device);
}


static gboolean
gdu_volume_is_allocated (GduPresentable *presentable)
{
        return TRUE;
}

static gboolean
gdu_volume_is_recognized (GduPresentable *presentable)
{
        GduVolume *volume = GDU_VOLUME (presentable);
        gboolean is_extended_partition;

        is_extended_partition = FALSE;
        if (gdu_device_is_partition (volume->priv->device) &&
            strcmp (gdu_device_partition_get_scheme (volume->priv->device), "mbr") == 0) {
                int type;
                type = strtol (gdu_device_partition_get_type (volume->priv->device), NULL, 0);
                if (type == 0x05 || type == 0x0f || type == 0x85)
                        is_extended_partition = TRUE;
        }

        if (is_extended_partition)
                return TRUE;
        else
                return strlen (gdu_device_id_get_usage (volume->priv->device)) > 0;
}

static void
gdu_volume_presentable_iface_init (GduPresentableIface *iface)
{
        iface->get_id = gdu_volume_get_id;
        iface->get_device = gdu_volume_get_device;
        iface->get_enclosing_presentable = gdu_volume_get_enclosing_presentable;
        iface->get_name = gdu_volume_get_name;
        iface->get_icon = gdu_volume_get_icon;
        iface->get_offset = gdu_volume_get_offset;
        iface->get_size = gdu_volume_get_size;
        iface->get_pool = gdu_volume_get_pool;
        iface->is_allocated = gdu_volume_is_allocated;
        iface->is_recognized = gdu_volume_is_recognized;
}

void
_gdu_volume_rewrite_enclosing_presentable (GduVolume *volume)
{
        if (volume->priv->enclosing_presentable != NULL) {
                const gchar *enclosing_presentable_id;
                GduPresentable *new_enclosing_presentable;

                enclosing_presentable_id = gdu_presentable_get_id (volume->priv->enclosing_presentable);

                new_enclosing_presentable = gdu_pool_get_presentable_by_id (volume->priv->pool,
                                                                            enclosing_presentable_id);
                if (new_enclosing_presentable == NULL) {
                        g_warning ("Error rewriting enclosing_presentable for %s, no such id %s",
                                   volume->priv->id,
                                   enclosing_presentable_id);
                        goto out;
                }

                g_object_unref (volume->priv->enclosing_presentable);
                volume->priv->enclosing_presentable = new_enclosing_presentable;
        }

 out:
        ;
}
