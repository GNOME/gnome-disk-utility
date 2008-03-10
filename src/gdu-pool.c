/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*- */
/* gdu-pool.c
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

#include "gdu-pool.h"
#include "devkit-disks-daemon-glue.h"

enum {
        DEVICE_ADDED,
        DEVICE_REMOVED,
        DEVICE_CHANGED,
        LAST_SIGNAL,
};

static GObjectClass *parent_class = NULL;
static guint signals[LAST_SIGNAL] = { 0 };

struct _GduPoolPrivate
{
        DBusGConnection *bus;
        DBusGProxy *proxy;
        GHashTable *devices;
};

G_DEFINE_TYPE (GduPool, gdu_pool, G_TYPE_OBJECT);

static void
gdu_pool_finalize (GduPool *pool)
{
        dbus_g_connection_unref (pool->priv->bus);
        g_object_unref (pool->priv->proxy);
        g_hash_table_unref (pool->priv->devices);

        if (G_OBJECT_CLASS (parent_class)->finalize)
                (* G_OBJECT_CLASS (parent_class)->finalize) (G_OBJECT (pool));
}

static void
gdu_pool_class_init (GduPoolClass *klass)
{
        GObjectClass *obj_class = (GObjectClass *) klass;

        parent_class = g_type_class_peek_parent (klass);

        obj_class->finalize = (GObjectFinalizeFunc) gdu_pool_finalize;

        signals[DEVICE_ADDED] =
                g_signal_new ("device_added",
                              G_TYPE_FROM_CLASS (klass),
                              G_SIGNAL_RUN_LAST,
                              G_STRUCT_OFFSET (GduPoolClass, device_added),
                              NULL, NULL,
                              g_cclosure_marshal_VOID__OBJECT,
                              G_TYPE_NONE, 1,
                              GDU_TYPE_DEVICE);

        signals[DEVICE_REMOVED] =
                g_signal_new ("device_removed",
                              G_TYPE_FROM_CLASS (klass),
                              G_SIGNAL_RUN_LAST,
                              G_STRUCT_OFFSET (GduPoolClass, device_removed),
                              NULL, NULL,
                              g_cclosure_marshal_VOID__OBJECT,
                              G_TYPE_NONE, 1,
                              GDU_TYPE_DEVICE);

        signals[DEVICE_CHANGED] =
                g_signal_new ("device_changed",
                              G_TYPE_FROM_CLASS (klass),
                              G_SIGNAL_RUN_LAST,
                              G_STRUCT_OFFSET (GduPoolClass, device_changed),
                              NULL, NULL,
                              g_cclosure_marshal_VOID__OBJECT,
                              G_TYPE_NONE, 1,
                              GDU_TYPE_DEVICE);

}

static void
gdu_pool_init (GduPool *pool)
{
        pool->priv = g_new0 (GduPoolPrivate, 1);
}

static GduDevice *
gdu_pool_add_device_by_object_path (GduPool *pool, const char *object_path)
{
        GduDevice *device;

        device = gdu_device_new_from_object_path (pool, object_path);
        if (device != NULL) {
                g_hash_table_insert (pool->priv->devices, g_strdup (object_path), device);
                g_signal_emit (pool, signals[DEVICE_ADDED], 0, device);
        }

        return device;
}

static void
device_added_signal_handler (DBusGProxy *proxy, const char *object_path, gpointer user_data)
{
        GduPool *pool = GDU_POOL (user_data);

        gdu_pool_add_device_by_object_path (pool, object_path);
}

static void
device_removed_signal_handler (DBusGProxy *proxy, const char *object_path, gpointer user_data)
{
        GduPool *pool = GDU_POOL (user_data);
        GduDevice *device;

        if ((device = gdu_pool_get_by_object_path (pool, object_path)) != NULL) {
                g_signal_emit (pool, signals[DEVICE_REMOVED], 0, device);
                g_hash_table_remove (pool->priv->devices, object_path);

        } else {
                g_warning ("unknown device to remove, object_path='%s'", object_path);
        }
}

static void
device_changed_signal_handler (DBusGProxy *proxy, const char *object_path, gpointer user_data)
{
        GduPool *pool = GDU_POOL (user_data);
        GduDevice *device;

        if ((device = gdu_pool_get_by_object_path (pool, object_path)) != NULL) {
                gdu_device_changed (device);
                g_signal_emit (pool, signals[DEVICE_CHANGED], 0, device);

        } else {
                g_warning ("unknown device to on change, object_path='%s'", object_path);
        }
}

#if 0
static void
_hal_property_modified (LibHalContext *ctx,
                        const char *udi,
                        const char *key,
                        dbus_bool_t is_removed,
                        dbus_bool_t is_added)
{
        GduPool *pool;
        GduDevice *device;

        /*g_debug ("*** _hal_property_modified, udi=%s, key=%s is_removed=%s, is_added=%s\n",
                 udi, key,
                 is_removed ? "true" : "false",
                 is_added ? "true" : "false");*/

        pool = GDU_POOL (libhal_ctx_get_user_data (ctx));

        device = gdu_pool_get_device_by_udi (pool, udi);
        if (device != NULL) {
                gdu_device_hal_property_changed (device, key);
                g_signal_emit (pool, signals[DEVICE_PROPERTY_CHANGED], 0, device, key);
        } else {
                g_warning ("unknown device with property modified, udi='%s'", udi);
        }
}
#endif

GduPool *
gdu_pool_new (void)
{
        int n;
        GPtrArray *devices;
        GduPool *pool;
        GError *error;

        pool = GDU_POOL (g_object_new (GDU_TYPE_POOL, NULL));

        error = NULL;
        pool->priv->bus = dbus_g_bus_get (DBUS_BUS_SYSTEM, &error);
        if (pool->priv->bus == NULL) {
                g_warning ("Couldn't connect to system bus: %s", error->message);
                g_error_free (error);
                goto error;
        }

        pool->priv->devices = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_object_unref);

	pool->priv->proxy = dbus_g_proxy_new_for_name (pool->priv->bus,
                                                       "org.freedesktop.DeviceKit.Disks",
                                                       "/",
                                                       "org.freedesktop.DeviceKit.Disks");
        dbus_g_proxy_add_signal (pool->priv->proxy, "DeviceAdded", G_TYPE_STRING, G_TYPE_INVALID);
        dbus_g_proxy_add_signal (pool->priv->proxy, "DeviceRemoved", G_TYPE_STRING, G_TYPE_INVALID);
        dbus_g_proxy_add_signal (pool->priv->proxy, "DeviceChanged", G_TYPE_STRING, G_TYPE_INVALID);
        dbus_g_proxy_connect_signal (pool->priv->proxy, "DeviceAdded",
                                     G_CALLBACK (device_added_signal_handler), pool, NULL);
        dbus_g_proxy_connect_signal (pool->priv->proxy, "DeviceRemoved",
                                     G_CALLBACK (device_removed_signal_handler), pool, NULL);
        dbus_g_proxy_connect_signal (pool->priv->proxy, "DeviceChanged",
                                     G_CALLBACK (device_changed_signal_handler), pool, NULL);

        /* TODO: connect signals */

        /* prime the list of devices */
        if (!org_freedesktop_DeviceKit_Disks_enumerate_devices (pool->priv->proxy, &devices, &error)) {
                g_warning ("Couldn't enumerate devices: %s", error->message);
                g_error_free (error);
                goto error;
        }
        for (n = 0; n < devices->len; n++) {
                const char *object_path;
                GduDevice *device;

                object_path = devices->pdata[n];
                device = gdu_pool_add_device_by_object_path (pool, object_path);
                if (device != NULL) {

                }
        }
        g_ptr_array_foreach (devices, (GFunc) g_free, NULL);
        g_ptr_array_free (devices, TRUE);

        return pool;

error:
        g_object_unref (pool);
        return NULL;
}

/**
 * gdu_pool_get_by_object_path:
 * @pool: the device pool
 * @object_path: the D-Bus object path
 *
 * Looks up #GduDevice object for @object_path.
 *
 * Returns: A #GduDevice object for @object_path, otherwise
 * #NULL. Caller must unref this object using g_object_unref().
 **/
GduDevice *
gdu_pool_get_by_object_path (GduPool *pool, const char *object_path)
{
        GduDevice *device;

        device = g_hash_table_lookup (pool->priv->devices, object_path);
        if (device != NULL)
                return g_object_ref (device);
        else
                return NULL;
}

static void
get_devices_cb (gpointer key, gpointer value, gpointer user_data)
{
        GList **l = user_data;
        *l = g_list_prepend (*l, g_object_ref (GDU_DEVICE (value)));
}

GList *
gdu_pool_get_devices (GduPool *pool)
{
        GList *ret;

        ret = NULL;
        g_hash_table_foreach (pool->priv->devices, get_devices_cb, &ret);
        return ret;
}
