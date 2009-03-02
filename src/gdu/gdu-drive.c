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

static char *
gdu_drive_get_name (GduPresentable *presentable)
{
        GduDrive *drive = GDU_DRIVE (presentable);
        const char *vendor;
        const char *model;
        const char *presentation_name;
        guint64 size;
        gboolean is_removable;
        char *strsize;
        char *result;

        strsize = NULL;

        presentation_name = gdu_device_get_presentation_name (drive->priv->device);
        if (presentation_name != NULL && strlen (presentation_name) > 0) {
                result = g_strdup (presentation_name);
                goto out;
        }

        vendor = gdu_device_drive_get_vendor (drive->priv->device);
        model = gdu_device_drive_get_model (drive->priv->device);
        size = gdu_device_get_size (drive->priv->device);
        is_removable = gdu_device_is_removable (drive->priv->device);

        if (vendor != NULL && strlen (vendor) == 0)
                vendor = NULL;

        if (model != NULL && strlen (model) == 0)
                model = NULL;

        if (!is_removable && size > 0) {
                strsize = gdu_util_get_size_for_display (size, FALSE);
        }

        if (strsize != NULL) {
                result = g_strdup_printf ("%s %s%s%s",
                                        strsize,
                                        vendor != NULL ? vendor : "",
                                        vendor != NULL ? " " : "",
                                        model != NULL ? model : "");
        } else {
                result = g_strdup_printf ("%s%s%s",
                                          vendor != NULL ? vendor : "",
                                          vendor != NULL ? " " : "",
                                          model != NULL ? model : "");
        }

 out:
        g_free (strsize);
        return result;
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
        iface->get_id = gdu_drive_get_id;
        iface->get_device = gdu_drive_get_device;
        iface->get_enclosing_presentable = gdu_drive_get_enclosing_presentable;
        iface->get_name = gdu_drive_get_name;
        iface->get_icon = gdu_drive_get_icon;
        iface->get_offset = gdu_drive_get_offset;
        iface->get_size = gdu_drive_get_size;
        iface->get_pool = gdu_drive_get_pool;
        iface->is_allocated = gdu_drive_is_allocated;
        iface->is_recognized = gdu_drive_is_recognized;
}
