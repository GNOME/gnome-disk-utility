/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*- */
/* gdu-drive.c
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
#include <stdlib.h>
#include <string.h>
#include <glib/gi18n.h>
#include <dbus/dbus-glib.h>

#include "gdu-private.h"
#include "gdu-util.h"
#include "gdu-pool.h"
#include "gdu-drive.h"
#include "gdu-presentable.h"
#include "gdu-device.h"
#include "gdu-error.h"
#include "gdu-volume.h"
#include "gdu-volume-hole.h"

/**
 * SECTION:gdu-drive
 * @title: GduDrive
 * @short_description: Drives
 *
 * The #GduDrive class represents drives attached to the
 * system. Normally, objects of this class corresponds 1:1 to physical
 * drives (hard disks, optical drives, card readers etc.) attached to
 * the system. However, it can also relate to software abstractions
 * such as a Linux md Software RAID array and similar things.
 *
 * See the documentation for #GduPresentable for the big picture.
 */

struct _GduDrivePrivate
{
        GduDevice *device;
        GduPool *pool;
        gchar *id;
};

static GObjectClass *parent_class = NULL;

static void gdu_drive_presentable_iface_init (GduPresentableIface *iface);
G_DEFINE_TYPE_WITH_CODE (GduDrive, gdu_drive, G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (GDU_TYPE_PRESENTABLE,
                                                gdu_drive_presentable_iface_init))

static void device_job_changed (GduDevice *device, gpointer user_data);
static void device_changed (GduDevice *device, gpointer user_data);

static gboolean gdu_drive_has_unallocated_space_real (GduDrive        *drive,
                                                      gboolean        *out_whole_disk_is_unitialized,
                                                      guint64         *out_largest_segment,
                                                      GduPresentable **out_presentable);

static void
gdu_drive_finalize (GduDrive *drive)
{
        //g_debug ("##### finalized drive '%s' %p", drive->priv->id, drive);

        if (drive->priv->device != NULL) {
                g_signal_handlers_disconnect_by_func (drive->priv->device, device_changed, drive);
                g_signal_handlers_disconnect_by_func (drive->priv->device, device_job_changed, drive);
                g_object_unref (drive->priv->device);
        }

        if (drive->priv->pool != NULL)
                g_object_unref (drive->priv->pool);

        g_free (drive->priv->id);

        if (G_OBJECT_CLASS (parent_class)->finalize)
                (* G_OBJECT_CLASS (parent_class)->finalize) (G_OBJECT (drive));
}

static void
gdu_drive_class_init (GduDriveClass *klass)
{
        GObjectClass *obj_class = (GObjectClass *) klass;

        parent_class = g_type_class_peek_parent (klass);

        obj_class->finalize = (GObjectFinalizeFunc) gdu_drive_finalize;

        klass->has_unallocated_space = gdu_drive_has_unallocated_space_real;

        g_type_class_add_private (klass, sizeof (GduDrivePrivate));
}

gboolean
gdu_drive_is_active (GduDrive *drive)
{
        GduDriveClass *klass = GDU_DRIVE_GET_CLASS (drive);
        if (klass->is_active != NULL)
                return klass->is_active (drive);
        else
                return TRUE;
}

gboolean
gdu_drive_is_activatable (GduDrive *drive)
{
        GduDriveClass *klass = GDU_DRIVE_GET_CLASS (drive);
        if (klass->is_activatable != NULL)
                return klass->is_activatable (drive);
        else
                return FALSE;
}

/**
 * gdu_drive_can_activate:
 * @drive: A #GduDrive.
 * @out_degraded: %NULL or return location for whether the drive will be degraded if activated.
 *
 * Checks if @drive can be activated. If this function returns %TRUE,
 * @out_degraded will be set to whether the drive will be started in
 * degraded mode (e.g. starting a mirror RAID array with only one
 * component available).
 *
 * Returns: %TRUE if @drive can be activated (and @out_degraded will be set), %FALSE otherwise.
 **/
gboolean
gdu_drive_can_activate (GduDrive *drive,
                        gboolean  *out_degraded)
{
        GduDriveClass *klass = GDU_DRIVE_GET_CLASS (drive);
        if (klass->can_activate != NULL)
                return klass->can_activate (drive, out_degraded);
        else
                return FALSE;
}

gboolean
gdu_drive_can_deactivate (GduDrive  *drive)
{
        GduDriveClass *klass = GDU_DRIVE_GET_CLASS (drive);
        if (klass->can_deactivate != NULL)
                return klass->can_deactivate (drive);
        else
                return FALSE;
}

void
gdu_drive_activate (GduDrive            *drive,
                    GduDriveActivateFunc    callback,
                    gpointer             user_data)
{
        GduDriveClass *klass = GDU_DRIVE_GET_CLASS (drive);
        if (klass->activate != NULL)
                return klass->activate (drive, callback, user_data);
        else {
                callback (drive,
                          NULL,
                          g_error_new_literal (GDU_ERROR,
                                               GDU_ERROR_NOT_SUPPORTED,
                                               "Drive does not support activate()"),
                          user_data);
        }
}

void
gdu_drive_deactivate (GduDrive                *drive,
                      GduDriveDeactivateFunc   callback,
                      gpointer                 user_data)
{
        GduDriveClass *klass = GDU_DRIVE_GET_CLASS (drive);
        if (klass->deactivate != NULL)
                return klass->deactivate (drive, callback, user_data);
        else {
                callback (drive,
                          g_error_new_literal (GDU_ERROR,
                                               GDU_ERROR_NOT_SUPPORTED,
                                               "Drive does not support deactivate()"),
                          user_data);
        }
}

/**
 * gdu_drive_has_unallocated_space:
 * @drive: A #GduDrive.
 * @out_whole_disk_is_unitialized: Return location for whether @drive is uninitialized or %NULL.
 * @out_largest_segment: Return location biggest contigious free block of @drive or %NULL.
 * @out_presentable: Return location for the presentable that represents free space or %NULL. Free
 * with g_object_unref().
 *
 * This method computes the largest contigious free block of
 * unallocated space on @drive.
 *
 * If @drive uses removable media and there is no media inserted,
 * %FALSE is returned.
 *
 * If @drive appears to be completely uninitialized (such as a hard
 * disk full of zeros), @out_whole_disk_is_unitialized is set to
 * %TRUE, the size of the media/disk is returned in
 * @out_largest_segment and %TRUE is returned. Note that this can
 * also happen if @drive contains signatures unknown to the operating system
 * so be careful.
 *
 * If the disk is partitioned and unallocated space exists but no more
 * partitions can be created (due to e.g. four primary partitions on a
 * MBR partitioned disk), this method returns %FALSE but
 * @out_largest_segment will be set to a non-zero value.
 *
 * Additionally, @out_presentable will be set to either a
 * #GduVolumeHole (if the disk is partitioned and has free space) or
 * the #GduDrive (if the disk is uninitialized).
 *
 * Returns: %TRUE if @drive has unallocated space, %FALSE otherwise.
 */
gboolean
gdu_drive_has_unallocated_space (GduDrive        *drive,
                                 gboolean        *out_whole_disk_is_unitialized,
                                 guint64         *out_largest_segment,
                                 GduPresentable **out_presentable)
{
        GduDriveClass *klass = GDU_DRIVE_GET_CLASS (drive);

        return klass->has_unallocated_space (drive,
                                             out_whole_disk_is_unitialized,
                                             out_largest_segment,
                                             out_presentable);
}

static gboolean
gdu_drive_has_unallocated_space_real (GduDrive        *drive,
                                      gboolean        *out_whole_disk_is_unitialized,
                                      guint64         *out_largest_segment,
                                      GduPresentable **out_presentable)
{
        GduDevice *device;
        GduPool *pool;
        guint64 largest_segment;
        gboolean whole_disk_uninitialized;
        GList *enclosed_presentables;
        GList *l;
        gboolean has_extended_partition;
        gboolean ret;
        guint64 size;
        GduPresentable *pres;

        largest_segment = 0;
        whole_disk_uninitialized = FALSE;
        ret = FALSE;
        device = NULL;
        pool = NULL;
        pres = NULL;

        device = gdu_presentable_get_device (GDU_PRESENTABLE (drive));
        if (device == NULL)
                goto out;

        if (gdu_device_is_read_only (device))
                goto out;

        if (gdu_device_is_removable (device) && !gdu_device_is_media_available (device))
                goto out;

        /* now figure out @whole_disk_is_uninitialized... this is to be set to %TRUE exactly
         * when we _think_ the disk has never been used - we define this to happen when
         *
         *  1. The disk has no partition table; and
         *
         *  2. the whole-disk device is unrecognized
         */
        if (!gdu_device_is_partition_table (device) && strlen (gdu_device_id_get_usage (device)) == 0) {
                whole_disk_uninitialized = TRUE;
                largest_segment = gdu_device_get_size (device);
                ret = TRUE;
                pres = GDU_PRESENTABLE (drive);
                goto out;
        }

        pool = gdu_presentable_get_pool (GDU_PRESENTABLE (drive));

        has_extended_partition = FALSE;
        enclosed_presentables = gdu_pool_get_enclosed_presentables (pool,
                                                                    GDU_PRESENTABLE (drive));
        for (l = enclosed_presentables; l != NULL; l = l->next) {
                GduPresentable *ep = GDU_PRESENTABLE (l->data);

                if (GDU_IS_VOLUME_HOLE (ep)) {
                        size = gdu_presentable_get_size (ep);

                        if (size > largest_segment) {
                                largest_segment = size;
                                pres = ep;
                        }
                } else if (GDU_IS_VOLUME (ep)) {
                        gint type;
                        GduDevice *ep_device;

                        ep_device = gdu_presentable_get_device (ep);

                        type = strtol (gdu_device_partition_get_type (ep_device), NULL, 0);
                        if (type == 0x05 || type == 0x0f || type == 0x85) {
                                GList *logical_partitions;
                                GList *ll;

                                has_extended_partition = TRUE;

                                /* This is MS-DOS extended partition, count volume holes inside */
                                logical_partitions = gdu_pool_get_enclosed_presentables (pool, ep);
                                for (ll = logical_partitions; ll != NULL; ll = ll->next) {
                                        GduPresentable *lep = GDU_PRESENTABLE (ll->data);

                                        if (GDU_IS_VOLUME_HOLE (lep)) {
                                                size = gdu_presentable_get_size (lep);
                                                if (size > largest_segment) {
                                                        largest_segment = size;
                                                        pres = lep;
                                                }
                                        }
                                }
                                g_list_foreach (logical_partitions, (GFunc) g_object_unref, NULL);
                                g_list_free (logical_partitions);
                        }
                        g_object_unref (ep_device);
                }
        }
        g_list_foreach (enclosed_presentables, (GFunc) g_object_unref, NULL);
        g_list_free (enclosed_presentables);

        ret = (largest_segment > 0);

        /* Now igure out if the partition table is full (e.g. four primary partitions already) and
         * return %FALSE and non-zero @out_largest_segment
         */
        if (g_strcmp0 (gdu_device_partition_table_get_scheme (device), "mbr") == 0 &&
            gdu_device_partition_table_get_count (device) == 4 &&
            !has_extended_partition) {
                ret = FALSE;
        }

 out:
        if (device != NULL)
                g_object_unref (device);
        if (pool != NULL)
                g_object_unref (pool);

        if (out_largest_segment != NULL)
                *out_largest_segment = largest_segment;

        if (out_whole_disk_is_unitialized != NULL)
                *out_whole_disk_is_unitialized = whole_disk_uninitialized;

        if (out_presentable != NULL) {
                *out_presentable = (pres != NULL ? g_object_ref (pres) : NULL);
        }

        return ret;
}

static void
gdu_drive_init (GduDrive *drive)
{
        drive->priv = G_TYPE_INSTANCE_GET_PRIVATE (drive, GDU_TYPE_DRIVE, GduDrivePrivate);
}

static void
device_changed (GduDevice *device, gpointer user_data)
{
        GduDrive *drive = GDU_DRIVE (user_data);
        g_signal_emit_by_name (drive, "changed");
        g_signal_emit_by_name (drive->priv->pool, "presentable-changed", drive);
}

static void
device_job_changed (GduDevice *device, gpointer user_data)
{
        GduDrive *drive = GDU_DRIVE (user_data);
        g_signal_emit_by_name (drive, "job-changed");
        g_signal_emit_by_name (drive->priv->pool, "presentable-job-changed", drive);
}

GduDrive *
_gdu_drive_new_from_device (GduPool *pool, GduDevice *device)
{
        GduDrive *drive;

        drive = GDU_DRIVE (g_object_new (GDU_TYPE_DRIVE, NULL));
        drive->priv->device = g_object_ref (device);
        drive->priv->pool = g_object_ref (pool);
        drive->priv->id = g_strdup_printf ("drive_%s", gdu_device_get_device_file (drive->priv->device));

        g_signal_connect (device, "changed", (GCallback) device_changed, drive);
        g_signal_connect (device, "job-changed", (GCallback) device_job_changed, drive);

        return drive;
}

static const gchar *
gdu_drive_get_id (GduPresentable *presentable)
{
        GduDrive *drive = GDU_DRIVE (presentable);
        return drive->priv->id;
}

static GduDevice *
gdu_drive_get_device (GduPresentable *presentable)
{
        GduDrive *drive = GDU_DRIVE (presentable);
        return g_object_ref (drive->priv->device);
}

static GduPresentable *
gdu_drive_get_enclosing_presentable (GduPresentable *presentable)
{
        return NULL;
}

/* TODO: should move to gdu-util.c */
static void
get_drive_name_from_media_compat (GduDevice *device,
                                  GString   *result)
{
        guint n;
        gboolean optical_cd;
        gboolean optical_dvd;
        gboolean optical_bd;
        gboolean optical_hddvd;
        const gchar* const *media_compat;

        media_compat = (const gchar* const *) gdu_device_drive_get_media_compatibility (device);

        optical_cd = FALSE;
        optical_dvd = FALSE;
        optical_bd = FALSE;
        optical_hddvd = FALSE;
        for (n = 0; media_compat != NULL && media_compat[n] != NULL; n++) {
                const gchar *media_name;
                const gchar *media;

                media = media_compat[n];
                media_name = NULL;
                if (g_strcmp0 (media, "flash_cf") == 0) {
                        /* Translators: This word is used to describe the media inserted into a device */
                        media_name = _("CompactFlash");
                } else if (g_strcmp0 (media, "flash_ms") == 0) {
                        /* Translators: This word is used to describe the media inserted into a device */
                        media_name = _("MemoryStick");
                } else if (g_strcmp0 (media, "flash_sm") == 0) {
                        /* Translators: This word is used to describe the media inserted into a device */
                        media_name = _("SmartMedia");
                } else if (g_strcmp0 (media, "flash_sd") == 0) {
                        /* Translators: This word is used to describe the media inserted into a device */
                        media_name = _("SecureDigital");
                } else if (g_strcmp0 (media, "flash_sdhc") == 0) {
                        /* Translators: This word is used to describe the media inserted into a device */
                        media_name = _("SD High Capacity");
                } else if (g_strcmp0 (media, "floppy") == 0) {
                        /* Translators: This word is used to describe the media inserted into a device */
                        media_name = _("Floppy");
                } else if (g_strcmp0 (media, "floppy_zip") == 0) {
                        /* Translators: This word is used to describe the media inserted into a device */
                        media_name = _("Zip");
                } else if (g_strcmp0 (media, "floppy_jaz") == 0) {
                        /* Translators: This word is used to describe the media inserted into a device */
                        media_name = _("Jaz");
                } else if (g_str_has_prefix (media, "flash")) {
                        /* Translators: This word is used to describe the media inserted into a device */
                        media_name = _("Flash");
                } else if (g_str_has_prefix (media, "optical_cd")) {
                        optical_cd = TRUE;
                } else if (g_str_has_prefix (media, "optical_dvd")) {
                        optical_dvd = TRUE;
                } else if (g_str_has_prefix (media, "optical_bd")) {
                        optical_bd = TRUE;
                } else if (g_str_has_prefix (media, "optical_hddvd")) {
                        optical_hddvd = TRUE;
                }

                if (media_name != NULL) {
                        if (result->len > 0)
                                g_string_append_c (result, '/');
                        g_string_append (result, media_name);
                }
        }
        if (optical_cd) {
                if (result->len > 0)
                        g_string_append_c (result, '/');
                /* Translators: This word is used to describe the optical disc type, it may appear
                 * in a slash-separated list e.g. 'CD/DVD/Blu-Ray'
                 */
                g_string_append (result, _("CD"));
        }
        if (optical_dvd) {
                if (result->len > 0)
                        g_string_append_c (result, '/');
                /* Translators: This word is used to describe the optical disc type, it may appear
                 * in a slash-separated list e.g. 'CD/DVD/Blu-Ray'
                 */
                g_string_append (result, _("DVD"));
        }
        if (optical_bd) {
                if (result->len > 0)
                        g_string_append_c (result, '/');
                /* Translators: This word is used to describe the optical disc type, it may appear
                 * in a slash-separated list e.g. 'CD/DVD/Blu-Ray'
                 */
                g_string_append (result, _("Blu-Ray"));
        }
        if (optical_hddvd) {
                if (result->len > 0)
                        g_string_append_c (result, '/');
                /* Translators: This word is used to describe the optical disc type, it may appear
                 * in a slash-separated list e.g. 'CD/DVD/Blu-Ray'
                 */
                g_string_append (result, _("HDDVD"));
        }

}

static gchar *
gdu_drive_get_name (GduPresentable *presentable)
{
        GduDrive *drive = GDU_DRIVE (presentable);
        const gchar *vendor;
        const gchar *model;
        const char *presentation_name;
        guint64 size;
        gboolean is_removable;
        GString *result;
        gboolean is_rotational;
        gboolean has_media;
        gchar *strsize;

        strsize = NULL;
        result = g_string_new (NULL);

        presentation_name = gdu_device_get_presentation_name (drive->priv->device);
        if (presentation_name != NULL && strlen (presentation_name) > 0) {
                g_string_append (result, presentation_name);
                goto out;
        }

        vendor = gdu_device_drive_get_vendor (drive->priv->device);
        model = gdu_device_drive_get_model (drive->priv->device);
        size = gdu_device_get_size (drive->priv->device);
        is_removable = gdu_device_is_removable (drive->priv->device);
        has_media = gdu_device_is_media_available (drive->priv->device);
        is_rotational = gdu_device_drive_get_is_rotational (drive->priv->device);

        if (has_media && size > 0) {
                strsize = gdu_util_get_size_for_display (size, FALSE);
        }

        if (is_removable) {

                get_drive_name_from_media_compat (drive->priv->device, result);

                /* If we know the media type, just append Drive */
                if (result->len > 0) {
                        GString *new_result;

                        new_result = g_string_new (NULL);
                        /* Translators: %s is the media type e.g. 'CD/DVD' or 'CompactFlash' */
                        g_string_append_printf (new_result, _("%s Drive"),
                                                result->str);
                        g_string_free (result, TRUE);
                        result = new_result;
                } else {
                        /* Otherwise use Vendor/Model */
                        if (vendor != NULL && strlen (vendor) == 0)
                                vendor = NULL;

                        if (model != NULL && strlen (model) == 0)
                                model = NULL;

                        g_string_append_printf (result,
                                                "%s%s%s",
                                                vendor != NULL ? vendor : "",
                                                vendor != NULL ? " " : "",
                                                model != NULL ? model : "");
                }

        } else {
                /* Media is not removable, use "Hard Disk" resp. "Solid-State Disk"
                 * unless we actually know what media types the drive is compatible with
                 */

                get_drive_name_from_media_compat (drive->priv->device, result);

                if (result->len > 0) {
                        GString *new_result;

                        new_result = g_string_new (NULL);
                        if (strsize != NULL) {
                                /* Translators: first %s is the size, second %s is the media type
                                 * e.g. 'CD/DVD' or 'CompactFlash'
                                 */
                                g_string_append_printf (new_result, _("%s %s Drive"),
                                                        strsize,
                                                        result->str);
                        } else {
                                /* Translators: %s is the media type e.g. 'CD/DVD' or 'CompactFlash' */
                                g_string_append_printf (new_result, _("%s Drive"),
                                                        result->str);
                        }
                        g_string_free (result, TRUE);
                        result = new_result;
                } else {
                        if (is_rotational) {
                                if (strsize != NULL) {
                                        /* Translators: This string is used to describe a hard disk. The first %s is
                                         * the size of the drive e.g. '45 GB'.
                                         */
                                        g_string_append_printf (result, _("%s Hard Disk"),
                                                                strsize);
                                } else {
                                        /* Translators: This string is used to describe a hard disk where the size
                                         * is not known.
                                         */
                                        g_string_append (result, _("Hard Disk"));
                                }
                        } else {
                                if (strsize != NULL) {
                                        /* Translators: This string is used to describe a SSD. The first %s is
                                         * the size of the drive e.g. '45 GB'.
                                         */
                                        g_string_append_printf (result, _("%s Solid-State Disk"),
                                                                strsize);
                                } else {
                                        /* Translators: This string is used to describe a SSD where the size
                                         * is not known.
                                         */
                                        g_string_append (result, _("Solid-State Disk"));
                                }
                        }
                }
        }

 out:
        g_free (strsize);
        return g_string_free (result, FALSE);
}

static gchar *
gdu_drive_get_description (GduPresentable *presentable)
{
        GduDrive *drive = GDU_DRIVE (presentable);
        const gchar *vendor;
        const gchar *model;
        const gchar *part_table_scheme;
        GString *result;
        guint64 size;
        gboolean is_removable;
        gboolean has_media;

        result = g_string_new (NULL);

        vendor = gdu_device_drive_get_vendor (drive->priv->device);
        model = gdu_device_drive_get_model (drive->priv->device);

        size = gdu_device_get_size (drive->priv->device);
        is_removable = gdu_device_is_removable (drive->priv->device);
        has_media = gdu_device_is_media_available (drive->priv->device);
        part_table_scheme = gdu_device_partition_table_get_scheme (drive->priv->device);

        /* If removable, include size of media or the fact there is no media */
        if (is_removable) {
                if (has_media && size > 0) {
                        gchar *strsize;
                        strsize = gdu_util_get_size_for_display (size, FALSE);
                        /* Translators: This string is the description of a drive. The first %s is the
                         * size of the inserted media, for example '45 GB'.
                         */
                        g_string_append_printf (result, _("%s Media"),
                                                strsize);
                        g_free (strsize);
                } else {
                        /* Translators: This string is used as a description text when no media has
                         * been detected for a drive
                         */
                        g_string_append_printf (result, _("No Media Detected"));
                }
        }

        /* If we have media, include whether partitioned or not */
        if (has_media && size > 0) {
                if (result->len > 0)
                        g_string_append (result, ", ");
                if (gdu_device_is_partition_table (drive->priv->device)) {
                        if (g_strcmp0 (part_table_scheme, "mbr") == 0) {
                                /* Translators: This string is used for conveying the partition table format */
                                g_string_append (result, _("MBR Partition Table"));
                        } else if (g_strcmp0 (part_table_scheme, "gpt") == 0) {
                                /* Translators: This string is used for conveying the partition table format */
                                g_string_append (result, _("GUID Partition Table"));
                        } else if (g_strcmp0 (part_table_scheme, "apm") == 0) {
                                /* Translators: This string is used for conveying the partition table format */
                                g_string_append (result, _("Apple Partition Table"));
                        } else {
                                /* Translators: This string is used for conveying the partition table format when
                                 * the format is unknown
                                 */
                                g_string_append (result, _("Partitioned"));
                        }
                } else {
                        /* Translators: This string is used for conveying a device is not partitioned.
                         */
                        g_string_append (result, _("Not Partitioned"));
                }
        }


        return g_string_free (result, FALSE);
}

static gchar *
gdu_drive_get_vpd_name (GduPresentable *presentable)
{
        GduDrive *drive = GDU_DRIVE (presentable);
        const gchar *vendor;
        const gchar *model;
        GString *result;

        result = g_string_new (NULL);

        vendor = gdu_device_drive_get_vendor (drive->priv->device);
        model = gdu_device_drive_get_model (drive->priv->device);

        if (vendor != NULL && strlen (vendor) == 0)
                vendor = NULL;

        if (model != NULL && strlen (model) == 0)
                model = NULL;

        g_string_append_printf (result,
                                "%s%s%s",
                                vendor != NULL ? vendor : "",
                                vendor != NULL ? " " : "",
                                model != NULL ? model : "");

        return g_string_free (result, FALSE);
}

static gboolean
strv_has (char **strv, const gchar *str)
{
        gboolean ret;
        guint n;

        ret = FALSE;

        for (n = 0; strv != NULL && strv[n] != NULL; n++) {
                if (g_strcmp0 (strv[n], str) == 0) {
                        ret = TRUE;
                        goto out;
                }
        }

 out:
        return ret;
}

static gboolean
strv_has_prefix (char **strv, const gchar *str)
{
        gboolean ret;
        guint n;

        ret = FALSE;

        for (n = 0; strv != NULL && strv[n] != NULL; n++) {
                if (g_str_has_prefix (strv[n], str)) {
                        ret = TRUE;
                        goto out;
                }
        }

 out:
        return ret;
}

static GIcon *
gdu_drive_get_icon (GduPresentable *presentable)
{
        GduDrive *drive = GDU_DRIVE (presentable);
        const char *name;
        const char *connection_interface;
        const char *drive_media;
        const char *presentation_icon_name;
        gchar **drive_media_compat;
        gboolean is_removable;

        connection_interface = gdu_device_drive_get_connection_interface (drive->priv->device);
        is_removable = gdu_device_is_removable (drive->priv->device);
        drive_media = gdu_device_drive_get_media (drive->priv->device);
        drive_media_compat = gdu_device_drive_get_media_compatibility (drive->priv->device);

        name = NULL;

        presentation_icon_name = gdu_device_get_presentation_icon_name (drive->priv->device);
        if (presentation_icon_name != NULL && strlen (presentation_icon_name) > 0) {
                name = presentation_icon_name;
                goto out;
        }

        /* media type */
        if (strv_has (drive_media_compat, "optical_cd")) {
                /* TODO: it would probably be nice to export a property whether this device can
                 *       burn discs etc. so we can use the 'drive-optical-recorder' icon when
                 *       applicable.
                 */
                name = "drive-optical";
        } else if (strv_has (drive_media_compat, "floppy")) {
                name = "drive-removable-media-floppy";
        } else if (strv_has (drive_media_compat, "floppy_zip")) {
                name = "drive-removable-media-floppy-zip";
        } else if (strv_has (drive_media_compat, "floppy_jaz")) {
                name = "drive-removable-media-floppy-jaz";
        } else if (strv_has (drive_media_compat, "flash_cf")) {
                name = "drive-removable-media-flash-cf";
        } else if (strv_has (drive_media_compat, "flash_ms")) {
                name = "drive-removable-media-flash-ms";
        } else if (strv_has (drive_media_compat, "flash_sm")) {
                name = "drive-removable-media-flash-sm";
        } else if (strv_has (drive_media_compat, "flash_sd")) {
                name = "drive-removable-media-flash-sd";
        } else if (strv_has (drive_media_compat, "flash_sdhc")) {
                /* TODO: get icon name for sdhc */
                name = "drive-removable-media-flash-sd";
        } else if (strv_has (drive_media_compat, "flash_mmc")) {
                /* TODO: get icon for mmc */
                name = "drive-removable-media-flash-sd";
        } else if (strv_has_prefix (drive_media_compat, "flash")) {
                name = "drive-removable-media-flash";
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
        /* ultimate fallback */
        if (name == NULL) {
                if (is_removable)
                        name = "drive-removable-media";
                else
                        name = "drive-harddisk";
        }

        return g_themed_icon_new_with_default_fallbacks (name);
}

static guint64
gdu_drive_get_offset (GduPresentable *presentable)
{
        return 0;
}

static guint64
gdu_drive_get_size (GduPresentable *presentable)
{
        GduDrive *drive = GDU_DRIVE (presentable);
        return gdu_device_get_size (drive->priv->device);
}

static GduPool *
gdu_drive_get_pool (GduPresentable *presentable)
{
        GduDrive *drive = GDU_DRIVE (presentable);
        return gdu_device_get_pool (drive->priv->device);
}

static gboolean
gdu_drive_is_allocated (GduPresentable *presentable)
{
        return TRUE;
}

static gboolean
gdu_drive_is_recognized (GduPresentable *presentable)
{
        /* TODO: maybe we need to return FALSE sometimes */
        return TRUE;
}

static void
gdu_drive_presentable_iface_init (GduPresentableIface *iface)
{
        iface->get_id                    = gdu_drive_get_id;
        iface->get_device                = gdu_drive_get_device;
        iface->get_enclosing_presentable = gdu_drive_get_enclosing_presentable;
        iface->get_name                  = gdu_drive_get_name;
        iface->get_description           = gdu_drive_get_description;
        iface->get_vpd_name              = gdu_drive_get_vpd_name;
        iface->get_icon                  = gdu_drive_get_icon;
        iface->get_offset                = gdu_drive_get_offset;
        iface->get_size                  = gdu_drive_get_size;
        iface->get_pool                  = gdu_drive_get_pool;
        iface->is_allocated              = gdu_drive_is_allocated;
        iface->is_recognized             = gdu_drive_is_recognized;
}

