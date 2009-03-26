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
#include <polkit-dbus/polkit-dbus.h>

#include "gdu-error.h"
#include "gdu-private.h"

/**
 * SECTION:gdu-error
 * @title: GduError
 * @short_description: Error helper functions
 *
 * Contains helper functions for reporting errors to the user.
 **/

/**
 * gdu_error_quark:
 *
 * Gets the #GduError Quark.
 *
 * Returns: a #GQuark
 **/
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
        if (strcmp (name, "org.freedesktop.DeviceKit.Disks.Error.Inhibited") == 0)
                error->code = GDU_ERROR_INHIBITED;
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
        else if (strcmp (name, "org.freedesktop.DeviceKit.Disks.Error.NotSupported") == 0)
                error->code = GDU_ERROR_NOT_SUPPORTED;
        else if (strcmp (name, "org.freedesktop.DeviceKit.Disks.Error.NotFound") == 0)
                error->code = GDU_ERROR_NOT_FOUND;
        else if (strcmp (name, "org.freedesktop.DeviceKit.Disks.Error.AtaSmartNotAvailable") == 0)
                error->code = GDU_ERROR_ATA_SMART_NOT_AVAILABLE;
        else if (strcmp (name, "org.freedesktop.DeviceKit.Disks.Error.AtaSmartWouldWakeup") == 0)
                error->code = GDU_ERROR_ATA_SMART_WOULD_WAKEUP;
        else
                matched = FALSE;

        if (matched)
                error->domain = GDU_ERROR;
}

/**
 * gdu_error_check_polkit_not_authorized:
 * @error: A #GError.
 * @pk_action: Return location for a #PolKitAction object.
 * @pk_result: Return location for #PolKitResult value.
 *
 * Checks if an error from a remote method call is of
 * type <literal>org.freedesktop.PolicyKit.Error.NotAuthorized</literal>
 * and if so, extracts the PolicyKit action and result.
 *
 * Returns: #TRUE only if the error is a PolicyKit exception and
 * @pk_action (caller must free this object with polkit_action_unref())
 * and @pk_result are set.
 **/
gboolean
gdu_error_check_polkit_not_authorized (GError *error,
                                       PolKitAction **pk_action,
                                       PolKitResult *pk_result)
{
        gboolean ret;

        g_return_val_if_fail (error != NULL && pk_action != NULL && pk_result != NULL, FALSE);

        ret = FALSE;

        if (error->domain != DBUS_GERROR ||
            error->code != DBUS_GERROR_REMOTE_EXCEPTION)
                goto out;

        if (!g_str_has_prefix (error->message, "org.freedesktop.PolicyKit.Error.NotAuthorized: "))
                goto out;

        ret = polkit_dbus_error_parse_from_strings ("org.freedesktop.PolicyKit.Error.NotAuthorized",
                                                    error->message + sizeof "org.freedesktop.PolicyKit.Error.NotAuthorized: " - 1,
                                                    pk_action,
                                                    pk_result);

out:
        return ret;
}
