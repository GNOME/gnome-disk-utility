/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*- */
/* gdu-error.c
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

#include <dbus/dbus-glib.h>
#include <string.h>
#include "gdu-error.h"
#include "gdu-private.h"

GQuark
gdu_error_quark (void)
{
        static GQuark ret = 0;
        if (ret == 0)
                ret = g_quark_from_static_string ("gdu-error-quark");
        return ret;
}

void
_gdu_error_fixup (GError *error)
{
        char *s;
        const char *name;
        gboolean matched;

        if (error == NULL)
                return;

        if (error->domain != DBUS_GERROR ||
            error->code != DBUS_GERROR_REMOTE_EXCEPTION)
                return;

        name = dbus_g_error_get_name (error);
        if (name == NULL)
                return;

        matched = TRUE;
        if (strcmp (name, "org.freedesktop.DeviceKit.Disks.Error.Failed") == 0)
                error->code = GDU_ERROR_FAILED;
        else if (strcmp (name, "org.freedesktop.DeviceKit.Disks.Error.Busy") == 0)
                error->code = GDU_ERROR_BUSY;
        else if (strcmp (name, "org.freedesktop.DeviceKit.Disks.Error.Cancelled") == 0)
                error->code = GDU_ERROR_CANCELLED;
        else if (strcmp (name, "org.freedesktop.DeviceKit.Disks.Error.InvalidOption") == 0)
                error->code = GDU_ERROR_INVALID_OPTION;
        else if (strcmp (name, "org.freedesktop.DeviceKit.Disks.Error.AlreadyMounted") == 0)
                error->code = GDU_ERROR_ALREADY_MOUNTED;
        else if (strcmp (name, "org.freedesktop.DeviceKit.Disks.Error.NotMounted") == 0)
                error->code = GDU_ERROR_NOT_MOUNTED;
        else if (strcmp (name, "org.freedesktop.DeviceKit.Disks.Error.NotCancellable") == 0)
                error->code = GDU_ERROR_NOT_CANCELLABLE;
        else if (strcmp (name, "org.freedesktop.DeviceKit.Disks.Error.NotPartition") == 0)
                error->code = GDU_ERROR_NOT_PARTITION;
        else if (strcmp (name, "org.freedesktop.DeviceKit.Disks.Error.NotPartitionTable") == 0)
                error->code = GDU_ERROR_NOT_PARTITION_TABLE;
        else if (strcmp (name, "org.freedesktop.DeviceKit.Disks.Error.NotLabeled") == 0)
                error->code = GDU_ERROR_NOT_LABELED;
        else if (strcmp (name, "org.freedesktop.DeviceKit.Disks.Error.NotFilesystem") == 0)
                error->code = GDU_ERROR_NOT_FILESYSTEM;
        else if (strcmp (name, "org.freedesktop.DeviceKit.Disks.Error.NotLuks") == 0)
                error->code = GDU_ERROR_NOT_LUKS;
        else if (strcmp (name, "org.freedesktop.DeviceKit.Disks.Error.NotLocked") == 0)
                error->code = GDU_ERROR_NOT_LOCKED;
        else if (strcmp (name, "org.freedesktop.DeviceKit.Disks.Error.NotUnlocked") == 0)
                error->code = GDU_ERROR_NOT_UNLOCKED;
        else if (strcmp (name, "org.freedesktop.DeviceKit.Disks.Error.NotLinuxMd") == 0)
                error->code = GDU_ERROR_NOT_LINUX_MD;
        else if (strcmp (name, "org.freedesktop.DeviceKit.Disks.Error.NotLinuxMdComponent") == 0)
                error->code = GDU_ERROR_NOT_LINUX_MD_COMPONENT;
        else if (strcmp (name, "org.freedesktop.DeviceKit.Disks.Error.NotDrive") == 0)
                error->code = GDU_ERROR_NOT_DRIVE;
        else if (strcmp (name, "org.freedesktop.DeviceKit.Disks.Error.NotSmartCapable") == 0)
                error->code = GDU_ERROR_NOT_SMART_CAPABLE;
        else if (strcmp (name, "org.freedesktop.DeviceKit.Disks.Error.NotSupported") == 0)
                error->code = GDU_ERROR_NOT_SUPPORTED;
        else if (strcmp (name, "org.freedesktop.DeviceKit.Disks.Error.NotFound") == 0)
                error->code = GDU_ERROR_NOT_FOUND;
        else
                matched = FALSE;

        if (matched)
                error->domain = GDU_ERROR;

        /* either way, prepend the D-Bus exception name to the message */
        s = g_strdup_printf ("%s: %s", name, error->message);
        g_free (error->message);
        error->message = s;
}
