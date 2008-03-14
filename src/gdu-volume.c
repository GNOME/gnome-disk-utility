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

#include "gdu-main.h"
#include "gdu-pool.h"
#include "gdu-volume.h"
#include "gdu-presentable.h"

struct _GduVolumePrivate
{
        GduDevice *device;
        GduPresentable *enclosing_presentable;
};

static GObjectClass *parent_class = NULL;

static void gdu_volume_presentable_iface_init (GduPresentableIface *iface);
G_DEFINE_TYPE_WITH_CODE (GduVolume, gdu_volume, G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (GDU_TYPE_PRESENTABLE,
                                                gdu_volume_presentable_iface_init))

static void
gdu_volume_finalize (GduVolume *volume)
{
        if (volume->priv->device != NULL)
                g_object_unref (volume->priv->device);

        if (volume->priv->enclosing_presentable != NULL)
                g_object_unref (volume->priv->enclosing_presentable);

        if (G_OBJECT_CLASS (parent_class)->finalize)
                (* G_OBJECT_CLASS (parent_class)->finalize) (G_OBJECT (volume));
}

static void
gdu_volume_class_init (GduVolumeClass *klass)
{
        GObjectClass *obj_class = (GObjectClass *) klass;

        parent_class = g_type_class_peek_parent (klass);

        obj_class->finalize = (GObjectFinalizeFunc) gdu_volume_finalize;
}

static void
gdu_volume_init (GduVolume *volume)
{
        volume->priv = g_new0 (GduVolumePrivate, 1);
}

static void
device_changed (GduDevice *device, gpointer user_data)
{
        GduVolume *volume = GDU_VOLUME (user_data);
        g_signal_emit_by_name (volume, "changed");
}

static void
device_removed (GduDevice *device, gpointer user_data)
{
        GduVolume *volume = GDU_VOLUME (user_data);
        g_signal_emit_by_name (volume, "removed");
}

GduVolume *
gdu_volume_new_from_device (GduDevice *device, GduPresentable *enclosing_presentable)
{
        GduVolume *volume;

        volume = GDU_VOLUME (g_object_new (GDU_TYPE_VOLUME, NULL));
        volume->priv->device = g_object_ref (device);
        volume->priv->enclosing_presentable =
                enclosing_presentable != NULL ? g_object_ref (enclosing_presentable) : NULL;

        g_signal_connect (device, "changed", (GCallback) device_changed, volume);
        g_signal_connect (device, "removed", (GCallback) device_removed, volume);

        return volume;
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
        char *result;
        gboolean is_extended_partition;
        char *strsize;
        guint64 size;

        label = gdu_device_id_get_label (volume->priv->device);
        size = gdu_device_get_size (volume->priv->device);

        /* see comment in gdu_pool_add_device_by_object_path() for how to avoid hardcoding 0x05 etc. types */
        is_extended_partition = FALSE;
        if (gdu_device_is_partition (volume->priv->device) &&
            strcmp (gdu_device_partition_get_scheme (volume->priv->device), "mbr") == 0) {
                int type;
                type = strtol (gdu_device_partition_get_type (volume->priv->device), NULL, 0);
                if (type == 0x05 || type == 0x0f || type == 0x85)
                        is_extended_partition = TRUE;
        }

        if (is_extended_partition) {
                size = gdu_device_partition_get_size (volume->priv->device);
                strsize = gdu_util_get_size_for_display (size, FALSE);
                result = g_strdup_printf (_("%s Extended Partition"), strsize);
                g_free (strsize);
        } else if (label != NULL && strlen (label) > 0) {
                result = g_strdup (label);
        } else {
                strsize = gdu_util_get_size_for_display (size, FALSE);
                result = g_strdup_printf (_("%s Partition"), strsize);
                g_free (strsize);
        }

        return result;
}

static char *
gdu_volume_get_icon_name (GduPresentable *presentable)
{
        //GduVolume *volume = GDU_VOLUME (presentable);
        return g_strdup ("drive-harddisk");
}

static guint64
gdu_volume_get_offset (GduPresentable *presentable)
{
        GduVolume *volume = GDU_VOLUME (presentable);
        if (gdu_device_is_partition (volume->priv->device))
                return gdu_device_partition_get_offset (volume->priv->device);
        return 0;
}

static const char *
gpt_part_type_guid_to_string (const char *guid)
{
        int n;
        /* see also http://en.wikipedia.org/wiki/GUID_Partition_Table */
        static struct {
                const char *guid;
                char *name;
        } part_type[] = {
                {"024DEE41-33E7-11D3-9D69-0008C781F39F", N_("MBR Partition Scheme")},
                {"C12A7328-F81F-11D2-BA4B-00A0C93EC93B", N_("EFI System Partition")},
                /* Microsoft */
                {"E3C9E316-0B5C-4DB8-817D-F92DF00215AE", N_("Microsoft Reserved Partition")},
                {"EBD0A0A2-B9E5-4433-87C0-68B6B72699C7", N_("Basic Data Partition")},
                {"5808C8AA-7E8F-42E0-85D2-E1E90434CFB3", N_("LDM meta data Partition")},
                {"AF9B60A0-1431-4F62-BC68-3311714A69AD", N_("LDM data Partition")},
                /* Linux */
                {"EBD0A0A2-B9E5-4433-87C0-68B6B72699C7", N_("Basic Data Partition")}, /* Same GUID as MS! */
                {"A19D880F-05FC-4D3B-A006-743F0F84911E", N_("Linux RAID Partition")},
                {"0657FD6D-A4AB-43C4-84E5-0933C84B4F4F", N_("Linux Swap Partition")},
                {"E6D6D379-F507-44C2-A23C-238F2A3DF928", N_("Linux LVM Partition")},
                {"8DA63339-0007-60C0-C436-083AC8230908", N_("Linux Reserved Partition")},
                /* Mac OS X */
                {"48465300-0000-11AA-AA11-00306543ECAC", N_("Apple HFS/HFS+ Partition")},
                {"55465300-0000-11AA-AA11-00306543ECAC", N_("Apple UFS Partition")},
                {"52414944-0000-11AA-AA11-00306543ECAC", N_("Apple RAID Partition")},
                /* TODO: add more entries */
                {NULL,  NULL}
        };

        for (n = 0; part_type[n].name != NULL; n++) {
                if (g_ascii_strcasecmp (part_type[n].guid, guid) == 0)
                        return part_type[n].name;
        }

        return guid;
}

static const char *
apm_part_type_to_string (const char *type)
{
        int n;
        /* see also http://developer.apple.com/documentation/mac/Devices/Devices-126.html
         * and http://lists.apple.com/archives/Darwin-drivers/2003/May/msg00021.html */
        static struct {
                const char *type;
                char *name;
        } part_type[] = {
                {"Apple_Unix_SVR2", N_("Apple UFS Partition")},
                {"Apple_HFS", N_("Apple HFS/HFS+ Partition")},
                {"Apple_partition_map", N_("Apple Partition Map")},
                {"DOS_FAT_12", N_("FAT 12")},
                {"DOS_FAT_16", N_("FAT 16")},
                {"DOS_FAT_32", N_("FAT 32")},
                {"Windows_FAT_16", N_("FAT 16")},
                {"Windows_FAT_32", N_("FAT 32")},
                /* TODO: add more entries */
                {NULL,  NULL}
        };

        for (n = 0; part_type[n].name != NULL; n++) {
                if (g_ascii_strcasecmp (part_type[n].type, type) == 0)
                        return part_type[n].name;
        }

        return type;
}

static const char *
msdos_part_type_to_string (int msdos_type)
{
        int n;
        /* see also http://www.win.tue.nl/~aeb/partitions/partition_types-1.html */
        static struct {
                int type;
                char *name;
        } part_type[] = {
                {0x00,  N_("Empty")},
                {0x01,  N_("FAT12")},
                {0x04,  N_("FAT16 <32M")},
                {0x05,  N_("Extended")},
                {0x06,  N_("FAT16")},
                {0x07,  N_("HPFS/NTFS")},
                {0x0b,  N_("W95 FAT32")},
                {0x0c,  N_("W95 FAT32 (LBA)")},
                {0x0e,  N_("W95 FAT16 (LBA)")},
                {0x0f,  N_("W95 Ext d (LBA)")},
                {0x10,  N_("OPUS")},
                {0x11,  N_("Hidden FAT12")},
                {0x12,  N_("Compaq diagnostics")},
                {0x14,  N_("Hidden FAT16 <32M")},
                {0x16,  N_("Hidden FAT16")},
                {0x17,  N_("Hidden HPFS/NTFS")},
                {0x1b,  N_("Hidden W95 FAT32")},
                {0x1c,  N_("Hidden W95 FAT32 (LBA)")},
                {0x1e,  N_("Hidden W95 FAT16 (LBA)")},
                {0x3c,  N_("PartitionMagic")},
                {0x82,  N_("Linux swap")},
                {0x83,  N_("Linux")},
                {0x84,  N_("Hibernation")},
                {0x85,  N_("Linux Extended")},
                {0x8e,  N_("Linux LVM")},
                {0xa0,  N_("Hibernation")},
                {0xa5,  N_("FreeBSD")},
                {0xa6,  N_("OpenBSD")},
                {0xa8,  N_("Mac OS X")},
                {0xaf,  N_("Mac OS X")},
                {0xbe,  N_("Solaris boot")},
                {0xbf,  N_("Solaris")},
                {0xeb,  N_("BeOS BFS")},
                {0xec,  N_("SkyOS SkyFS")},
                {0xee,  N_("EFI GPT")},
                {0xef,  N_("EFI (FAT-12/16/32")},
                {0xfd,  N_("Linux RAID autodetect")},
                {0x00,  NULL}
        };

        for (n = 0; part_type[n].name != NULL; n++) {
                if (part_type[n].type == msdos_type)
                        return part_type[n].name;
        }

        return _("Unknown");
}

static GList *
gdu_volume_get_info (GduPresentable *presentable)
{
        GduVolume *volume = GDU_VOLUME (presentable);
        GduDevice *device = volume->priv->device;
        GList *kv_pairs = NULL;

        kv_pairs = g_list_prepend (kv_pairs, g_strdup (_("Mount Point")));
        if (gdu_device_is_mounted (device))
                kv_pairs = g_list_prepend (kv_pairs, g_strdup (gdu_device_get_mount_path (device)));
        else
                kv_pairs = g_list_prepend (kv_pairs, g_strdup (_("-")));
        kv_pairs = g_list_prepend (kv_pairs, g_strdup (_("Label")));
        kv_pairs = g_list_prepend (kv_pairs, g_strdup (gdu_device_id_get_label (device)));
        kv_pairs = g_list_prepend (kv_pairs, g_strdup (_("Device File")));
        kv_pairs = g_list_prepend (kv_pairs, g_strdup (gdu_device_get_device_file (device)));
        kv_pairs = g_list_prepend (kv_pairs, g_strdup (_("UUID")));
        kv_pairs = g_list_prepend (kv_pairs, g_strdup (gdu_device_id_get_uuid (device)));
        kv_pairs = g_list_prepend (kv_pairs, g_strdup (_("Partition Number")));
        if (gdu_device_is_partition (device)) {
                kv_pairs = g_list_prepend (kv_pairs, g_strdup_printf (
                                                   "%d",
                                                   gdu_device_partition_get_number (device)));
        } else {
                kv_pairs = g_list_prepend (kv_pairs, g_strdup (_("-")));
        }
        kv_pairs = g_list_prepend (kv_pairs, g_strdup (_("Partition Type")));
        if (gdu_device_is_partition (device)) {
                const char *type;
                const char *scheme;
                char *name;

                type = gdu_device_partition_get_type (device);
                scheme = gdu_device_partition_get_scheme (device);

                if (strcmp (scheme, "gpt") == 0) {
                        name = g_strdup (gpt_part_type_guid_to_string (type));
                } else if (strcmp (scheme, "mbr") == 0) {
                        int msdos_type;
                        msdos_type = strtol (type, NULL, 0);
                        name = g_strdup_printf (_("%s (0x%02x)"),
                                                msdos_part_type_to_string (msdos_type),
                                                msdos_type);
                } else if (strcmp (scheme, "apm") == 0) {
                        name = g_strdup (apm_part_type_to_string (type));
                } else {
                        if (strlen (type) > 0)
                                name = g_strdup_printf (_("Unknown (%s)"), type);
                        else
                                name = g_strdup (_("-"));
                }
                kv_pairs = g_list_prepend (kv_pairs, name);
        } else {
                kv_pairs = g_list_prepend (kv_pairs, g_strdup (_("-")));
        }
        kv_pairs = g_list_prepend (kv_pairs, g_strdup (_("Usage")));
        {
                const char *usage;
                char *name;
                usage = gdu_device_id_get_usage (device);
                if (strcmp (usage, "filesystem") == 0) {
                        name = g_strdup (_("File system"));
                } else if (strcmp (usage, "crypto") == 0) {
                        name = g_strdup (_("Encrypted Block Device"));
                } else if (strcmp (usage, "raid") == 0) {
                        name = g_strdup (_("Assembled Block Device"));
                } else {
                        if (strlen (usage) > 0)
                                name = g_strdup_printf (_("Unknown (%s)"), usage);
                        else
                                name = g_strdup (_("-"));
                }
                kv_pairs = g_list_prepend (kv_pairs, name);
        }
        kv_pairs = g_list_prepend (kv_pairs, g_strdup (_("Capacity")));
        kv_pairs = g_list_prepend (kv_pairs,
                                   gdu_util_get_size_for_display (gdu_device_get_size (device), TRUE));
        kv_pairs = g_list_prepend (kv_pairs, g_strdup (_("Type")));
        kv_pairs = g_list_prepend (kv_pairs,
                                   gdu_util_get_fstype_for_display (gdu_device_id_get_type (device),
                                                                    gdu_device_id_get_version (device),
                                                                    TRUE));
        kv_pairs = g_list_prepend (kv_pairs, g_strdup (_("Available")));
        kv_pairs = g_list_prepend (kv_pairs, g_strdup (_("-"))); /* TODO */

        kv_pairs = g_list_reverse (kv_pairs);
        return kv_pairs;
}

static void
gdu_volume_presentable_iface_init (GduPresentableIface *iface)
{
        iface->get_device = gdu_volume_get_device;
        iface->get_enclosing_presentable = gdu_volume_get_enclosing_presentable;
        iface->get_name = gdu_volume_get_name;
        iface->get_icon_name = gdu_volume_get_icon_name;
        iface->get_offset = gdu_volume_get_offset;
        iface->get_info = gdu_volume_get_info;
}
