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
        char    *uuid;

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
        if (activatable_drive->priv->device != NULL) {
                g_signal_handlers_disconnect_by_func (activatable_drive->priv->device, device_changed, activatable_drive);
                g_signal_handlers_disconnect_by_func (activatable_drive->priv->device, device_job_changed, activatable_drive);
                g_signal_handlers_disconnect_by_func (activatable_drive->priv->device, device_removed, activatable_drive);
                g_object_unref (activatable_drive->priv->device);
        }

        if (activatable_drive->priv->pool != NULL) {
                g_object_unref (activatable_drive->priv->pool);
        }

        g_free (activatable_drive->priv->uuid);
        g_list_foreach (activatable_drive->priv->slaves, (GFunc) g_object_unref, NULL);
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
}

static void
device_job_changed (GduDevice *device, gpointer user_data)
{
        GduActivatableDrive *activatable_drive = GDU_ACTIVATABLE_DRIVE (user_data);
        g_signal_emit_by_name (activatable_drive, "job-changed");
}

static void
device_removed (GduDevice *device, gpointer user_data)
{
        GduActivatableDrive *activatable_drive = GDU_ACTIVATABLE_DRIVE (user_data);
        g_signal_emit_by_name (activatable_drive, "removed");
}

GduActivatableDrive *
gdu_activatable_drive_new (GduPool               *pool,
                           GduActivableDriveKind  kind,
                           const char            *uuid)
{
        GduActivatableDrive *activatable_drive;

        activatable_drive = GDU_ACTIVATABLE_DRIVE (g_object_new (GDU_TYPE_ACTIVATABLE_DRIVE, NULL));
        activatable_drive->priv->pool = g_object_ref (pool);
        activatable_drive->priv->kind = kind;
        activatable_drive->priv->uuid = g_strdup (uuid);

        return activatable_drive;
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
        int level;
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

                if (name == NULL || strlen (name) == 0) {
                        ret = g_strdup_printf (_("RAID-%d drive"), level);
                } else {
                        ret = g_strdup_printf (_("RAID-%d %s"), level, name);
                }
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
                ret = g_strdup ("drive-removable-media-floppy");
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
        if (activatable_drive->priv->device != NULL) {
                return gdu_device_get_size (activatable_drive->priv->device);
        } else {
                return 0;
        }
}

static GList *
gdu_activatable_drive_get_info (GduPresentable *presentable)
{
        GduActivatableDrive *activatable_drive = GDU_ACTIVATABLE_DRIVE (presentable);
        GduDevice *device = activatable_drive->priv->device;
        GList *kv_pairs = NULL;
        char **activatable_drive_media_compat;
        GString *s;
        int n;

        activatable_drive_media_compat = gdu_device_drive_get_media_compatibility (activatable_drive->priv->device);

	kv_pairs = g_list_prepend (kv_pairs, g_strdup (_("Vendor")));
	kv_pairs = g_list_prepend (kv_pairs, g_strdup (gdu_device_drive_get_vendor (device)));
	kv_pairs = g_list_prepend (kv_pairs, g_strdup (_("Model")));
	kv_pairs = g_list_prepend (kv_pairs, g_strdup (gdu_device_drive_get_model (device)));
	kv_pairs = g_list_prepend (kv_pairs, g_strdup (_("Revision")));
	kv_pairs = g_list_prepend (kv_pairs, g_strdup (gdu_device_drive_get_revision (device)));
	kv_pairs = g_list_prepend (kv_pairs, g_strdup (_("Serial Number")));
	kv_pairs = g_list_prepend (kv_pairs, g_strdup (gdu_device_drive_get_serial (device)));

	kv_pairs = g_list_prepend (kv_pairs, g_strdup (_("Device File")));
	kv_pairs = g_list_prepend (kv_pairs, g_strdup (gdu_device_get_device_file (device)));
	kv_pairs = g_list_prepend (kv_pairs, g_strdup (_("Connection")));
	kv_pairs = g_list_prepend (kv_pairs, gdu_util_get_connection_for_display (
                                           gdu_device_drive_get_connection_interface (device),
                                           gdu_device_drive_get_connection_speed (device)));

	kv_pairs = g_list_prepend (kv_pairs, g_strdup (_("Removable Media")));
	if (gdu_device_is_removable (device)) {
	        if (gdu_device_is_media_available (device)) {
                        kv_pairs = g_list_prepend (kv_pairs, g_strdup (_("Yes")));
	        } else {
                        kv_pairs = g_list_prepend (kv_pairs, g_strdup (_("Yes (No media inserted)")));
	        }
	} else {
	        kv_pairs = g_list_prepend (kv_pairs, g_strdup (_("No")));
	}

	kv_pairs = g_list_prepend (kv_pairs, g_strdup (_("Media Compatibility")));
        s = g_string_new (NULL);
        if (activatable_drive_media_compat != NULL) {
                for (n = 0; activatable_drive_media_compat[n] != NULL; n++) {
                        const char *media = (const char *) activatable_drive_media_compat[n];

                        if (s->len > 0) {
                                /* Translator: the separator for media types */
                                g_string_append (s, _(", "));
                        }

                        if (strcmp (media, "flash_cf") == 0) {
                                g_string_append (s, _("Compact Flash"));
                        } else if (strcmp (media, "flash_ms") == 0) {
                                g_string_append (s, _("Memory Stick"));
                        } else if (strcmp (media, "flash_sm") == 0) {
                                g_string_append (s, _("Smart Media"));
                        } else if (strcmp (media, "flash_sd") == 0) {
                                g_string_append (s, _("SD Card"));
                        } else if (strcmp (media, "flash_sdhc") == 0) {
                                g_string_append (s, _("SDHC Card"));
                        } else if (strcmp (media, "flash_mmc") == 0) {
                                g_string_append (s, _("MMC"));
                        } else if (g_str_has_prefix (media, "flash")) {
                                g_string_append (s, _("Flash"));
                        } else if (g_str_has_prefix (media, "optical")) {
                                /* TODO: handle rest of optical-* */
                                g_string_append (s, _("CD-ROM"));
                        }
                }
        }
        if (s->len == 0)
                g_string_append (s, _("Disk"));
        kv_pairs = g_list_prepend (kv_pairs, g_string_free (s, FALSE));

	kv_pairs = g_list_prepend (kv_pairs, g_strdup (_("Capacity")));
	if (gdu_device_is_media_available (device)) {
	        kv_pairs = g_list_prepend (kv_pairs,
                                           gdu_util_get_size_for_display (gdu_device_get_size (device), TRUE));
	} else {
	        kv_pairs = g_list_prepend (kv_pairs, g_strdup (_("-")));
	}

	kv_pairs = g_list_prepend (kv_pairs, g_strdup (_("Partitioning")));
	if (gdu_device_is_partition_table (device)) {
	        const char *scheme;
	        char *name;
	        scheme = gdu_device_partition_table_get_scheme (device);
	        if (strcmp (scheme, "apm") == 0) {
                        name = g_strdup (_("Apple Partition Map"));
	        } else if (strcmp (scheme, "mbr") == 0) {
                        name = g_strdup (_("Master Boot Record"));
	        } else if (strcmp (scheme, "gpt") == 0) {
                        name = g_strdup (_("GUID Partition Table"));
	        } else {
                        name = g_strdup_printf (_("Unknown (%s)"), scheme);
	        }
	        kv_pairs = g_list_prepend (kv_pairs, name);
	} else {
	        kv_pairs = g_list_prepend (kv_pairs, g_strdup (_("-")));
	}

        kv_pairs = g_list_reverse (kv_pairs);
        return kv_pairs;
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

void
gdu_activatable_drive_add_slave (GduActivatableDrive *activatable_drive,
                                 GduDevice           *device)
{
        activatable_drive->priv->slaves = g_list_prepend (activatable_drive->priv->slaves, device);
        g_signal_emit_by_name (activatable_drive, "changed");
}

void
gdu_activatable_drive_remove_slave (GduActivatableDrive *activatable_drive,
                                    GduDevice           *device)
{
        activatable_drive->priv->slaves = g_list_remove (activatable_drive->priv->slaves, device);
        g_signal_emit_by_name (activatable_drive, "changed");
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

static void
gdu_activatable_drive_presentable_iface_init (GduPresentableIface *iface)
{
        iface->get_device = gdu_activatable_drive_get_device;
        iface->get_enclosing_presentable = gdu_activatable_drive_get_enclosing_presentable;
        iface->get_name = gdu_activatable_drive_get_name;
        iface->get_icon_name = gdu_activatable_drive_get_icon_name;
        iface->get_offset = gdu_activatable_drive_get_offset;
        iface->get_size = gdu_activatable_drive_get_size;
        iface->get_info = gdu_activatable_drive_get_info;
        iface->get_pool = gdu_activatable_drive_get_pool;
        iface->is_allocated = gdu_activatable_drive_is_allocated;
        iface->is_recognized = gdu_activatable_drive_is_recognized;
}
