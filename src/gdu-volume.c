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

#include "gdu-util.h"
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

static void device_removed (GduDevice *device, gpointer user_data);
static void device_job_changed (GduDevice *device, gpointer user_data);
static void device_changed (GduDevice *device, gpointer user_data);

static void
gdu_volume_finalize (GduVolume *volume)
{
        if (volume->priv->device != NULL) {
                g_signal_handlers_disconnect_by_func (volume->priv->device, device_changed, volume);
                g_signal_handlers_disconnect_by_func (volume->priv->device, device_job_changed, volume);
                g_signal_handlers_disconnect_by_func (volume->priv->device, device_removed, volume);
                g_object_unref (volume->priv->device);
        }

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
device_job_changed (GduDevice *device, gpointer user_data)
{
        GduVolume *volume = GDU_VOLUME (user_data);
        g_signal_emit_by_name (volume, "job-changed");
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
        g_signal_connect (device, "job-changed", (GCallback) device_job_changed, volume);
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
        const char *usage;
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

        usage = gdu_device_id_get_usage (volume->priv->device);

        if (is_extended_partition) {
                size = gdu_device_partition_get_size (volume->priv->device);
                strsize = gdu_util_get_size_for_display (size, FALSE);
                result = g_strdup_printf (_("%s Extended"), strsize);
                g_free (strsize);
        } else if (label != NULL && strlen (label) > 0) {
                result = g_strdup (label);
        } else if (usage != NULL && strcmp (usage, "crypto") == 0) {
                strsize = gdu_util_get_size_for_display (size, FALSE);
                result = g_strdup_printf (_("%s Encrypted"), strsize);
                g_free (strsize);
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

static guint64
gdu_volume_get_size (GduPresentable *presentable)
{
        GduVolume *volume = GDU_VOLUME (presentable);
        if (gdu_device_is_partition (volume->priv->device))
                return gdu_device_partition_get_size (volume->priv->device);
        return gdu_device_get_size (volume->priv->device);
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
                kv_pairs = g_list_prepend (kv_pairs,
                                           gdu_util_get_desc_for_part_type (gdu_device_partition_get_scheme (device),
                                                                            gdu_device_partition_get_type (device)));
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

static GduPool *
gdu_volume_get_pool (GduPresentable *presentable)
{
        GduVolume *volume = GDU_VOLUME (presentable);
        return gdu_device_get_pool (volume->priv->device);
}

static void
gdu_volume_presentable_iface_init (GduPresentableIface *iface)
{
        iface->get_device = gdu_volume_get_device;
        iface->get_enclosing_presentable = gdu_volume_get_enclosing_presentable;
        iface->get_name = gdu_volume_get_name;
        iface->get_icon_name = gdu_volume_get_icon_name;
        iface->get_offset = gdu_volume_get_offset;
        iface->get_size = gdu_volume_get_size;
        iface->get_info = gdu_volume_get_info;
        iface->get_pool = gdu_volume_get_pool;
}
