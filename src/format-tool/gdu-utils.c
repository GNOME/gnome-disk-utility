/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*- */
/*  gdu-utils.c
 *
 *  Copyright (C) 2008-2009 Red Hat, Inc.
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public
 *  License as published by the Free Software Foundation; either
 *  version 2 of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Library General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public
 *  License along with this library; if not, write to the Free
 *  Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 *  Author: Tomas Bzatek <tbzatek@redhat.com>
 */

#include "config.h"

#include <glib/gi18n-lib.h>
#include <gio/gio.h>
#include <gdu/gdu.h>
#include <gtk/gtk.h>

#include "gdu-utils.h"


GduPresentable *
find_presentable_from_mount_path (const gchar *mount_path)
{
        GduPool *pool;
        GList *devices, *l;
        GduPresentable *presentable = NULL;
        GduDevice *device;
        const gchar *device_mount;
        GFile *file1, *file2;

        g_return_val_if_fail (mount_path != NULL, NULL);
        g_return_val_if_fail (strlen (mount_path) > 1, NULL);

        pool = gdu_pool_new ();
        devices = gdu_pool_get_devices (pool);

        for (l = devices; l != NULL; l = l->next) {
                device = GDU_DEVICE (l->data);
                device_mount = gdu_device_get_mount_path (device);

                if (mount_path && device_mount && strlen (device_mount) > 1) {
                        /*  compare via GFile routines  */
                        file1 = g_file_new_for_commandline_arg (mount_path);
                        file2 = g_file_new_for_path (device_mount);
                        if (g_file_equal (file1, file2))
                                presentable = gdu_pool_get_volume_by_device (pool, device);   /*  auto-ref here  */
                        g_object_unref (file1);
                        g_object_unref (file2);
                        if (presentable)
                                break;
                }
        }

        g_list_foreach (devices, (GFunc) g_object_unref, NULL);
        g_list_free (devices);
        g_object_unref (pool);

        return presentable;
}

GduPresentable *
find_presentable_from_device_path (const gchar *device_path)
{
        GduPool *pool;
        GduDevice *device;
        GduPresentable *presentable = NULL;

        g_return_val_if_fail (device_path != NULL, NULL);
        g_return_val_if_fail (strlen (device_path) > 1, NULL);

        pool = gdu_pool_new ();
        device = gdu_pool_get_by_device_file (pool, device_path);
        if (device) {
                presentable = gdu_pool_get_volume_by_device (pool, device);
                g_object_unref (device);
        }
        g_object_unref (pool);

        return presentable;
}


gchar *
_g_icon_get_string (GIcon *icon)
{
        const gchar *icon_text = NULL;
        const gchar * const *icon_names;

        if (! icon)
                return NULL;

        if (G_IS_THEMED_ICON (icon)) {
                icon_names = g_themed_icon_get_names (G_THEMED_ICON (icon));
                while (icon_text == NULL && icon_names != NULL && *icon_names != NULL) {
                        if (gtk_icon_theme_has_icon (gtk_icon_theme_get_default (), *icon_names))
                                icon_text = *icon_names;
                        icon_names++;
                }
        }

        return g_strdup (icon_text);
}

gboolean
is_active_luks (GduPool *pool, GduPresentable *presentable)
{
        gboolean res;
        GduDevice *device;

        res = FALSE;
        device = gdu_presentable_get_device (presentable);

        if (GDU_IS_VOLUME (presentable) && device && strcmp (gdu_device_id_get_usage (device), "crypto") == 0) {
                GList *enclosed_presentables;
                enclosed_presentables = gdu_pool_get_enclosed_presentables (pool, presentable);
                res = (enclosed_presentables != NULL) && (g_list_length (enclosed_presentables) == 1);
                g_list_foreach (enclosed_presentables, (GFunc) g_object_unref, NULL);
                g_list_free (enclosed_presentables);
                g_object_unref (device);
        }

        return res;
}

const gchar *
trim_dk_error (const gchar *message)
{
        const gchar *text, *p;

        if (g_str_has_prefix (message, "org.freedesktop.DeviceKit"))
                text = (p = strchr (message, ':')) ? p + 1 : message;
        else
                text = message;

        return text;
}
