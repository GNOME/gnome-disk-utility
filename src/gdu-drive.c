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

#include "gdu-main.h"
#include "gdu-pool.h"
#include "gdu-drive.h"
#include "gdu-presentable.h"

struct _GduDrivePrivate
{
        GduDevice *device;
};

static GObjectClass *parent_class = NULL;

static void gdu_drive_presentable_iface_init (GduPresentableIface *iface);
G_DEFINE_TYPE_WITH_CODE (GduDrive, gdu_drive, G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (GDU_TYPE_PRESENTABLE,
                                                gdu_drive_presentable_iface_init))

static void
gdu_drive_finalize (GduDrive *drive)
{
        if (drive->priv->device != NULL)
                g_object_unref (drive->priv->device);

        if (G_OBJECT_CLASS (parent_class)->finalize)
                (* G_OBJECT_CLASS (parent_class)->finalize) (G_OBJECT (drive));
}

static void
gdu_drive_class_init (GduDriveClass *klass)
{
        GObjectClass *obj_class = (GObjectClass *) klass;

        parent_class = g_type_class_peek_parent (klass);

        obj_class->finalize = (GObjectFinalizeFunc) gdu_drive_finalize;

}

static void
gdu_drive_init (GduDrive *drive)
{
        drive->priv = g_new0 (GduDrivePrivate, 1);
}

static void
device_changed (GduDevice *device, gpointer user_data)
{
        GduDrive *drive = GDU_DRIVE (user_data);
        g_signal_emit_by_name (drive, "changed");
}

static void
device_removed (GduDevice *device, gpointer user_data)
{
        GduDrive *drive = GDU_DRIVE (user_data);
        g_signal_emit_by_name (drive, "removed");
}

GduDrive *
gdu_drive_new_from_device (GduDevice *device)
{
        GduDrive *drive;

        drive = GDU_DRIVE (g_object_new (GDU_TYPE_DRIVE, NULL));
        drive->priv->device = g_object_ref (device);
        g_signal_connect (device, "changed", (GCallback) device_changed, drive);
        g_signal_connect (device, "removed", (GCallback) device_removed, drive);

        return drive;
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
        guint64 size;
        gboolean is_removable;
        char *strsize;
        char *result;

        vendor = gdu_device_drive_get_vendor (drive->priv->device);
        model = gdu_device_drive_get_model (drive->priv->device);
        size = gdu_device_get_size (drive->priv->device);
        is_removable = gdu_device_is_removable (drive->priv->device);

        strsize = NULL;
        if (!is_removable && size > 0) {
                strsize = gdu_util_get_size_for_display (size, FALSE);
        }

        if (strsize != NULL) {
                result = g_strdup_printf ("%s %s %s",
                                        strsize,
                                        vendor != NULL ? vendor : "",
                                        model != NULL ? model : "");
        } else {
                result = g_strdup_printf ("%s %s",
                                        vendor != NULL ? vendor : "",
                                        model != NULL ? model : "");
        }
        g_free (strsize);

        return result;
}

static char *
gdu_drive_get_icon_name (GduPresentable *presentable)
{
        //GduDrive *drive = GDU_DRIVE (presentable);
        return g_strdup ("drive-harddisk");
}

static guint64
gdu_drive_get_offset (GduPresentable *presentable)
{
        return 0;
}

static void
gdu_drive_presentable_iface_init (GduPresentableIface *iface)
{
        iface->get_device = gdu_drive_get_device;
        iface->get_enclosing_presentable = gdu_drive_get_enclosing_presentable;
        iface->get_name = gdu_drive_get_name;
        iface->get_icon_name = gdu_drive_get_icon_name;
        iface->get_offset = gdu_drive_get_offset;
}
