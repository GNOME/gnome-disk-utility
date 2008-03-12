/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*- */
/* gdu-device.c
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

#include "gdu-pool.h"
#include "gdu-device.h"

/* --- SUCKY CODE BEGIN --- */

/* This totally sucks; dbus-bindings-tool and dbus-glib should be able
 * to do this for us.
 *
 * TODO: keep in sync with code in tools/devkit-disks in DeviceKit-disks.
 */

static char *
get_property_object_path (DBusGConnection *bus,
                          const char *svc_name,
                          const char *obj_path,
                          const char *if_name,
                          const char *prop_name)
{
        char *ret;
        DBusGProxy *proxy;
        GValue value = { 0 };
        GError *error = NULL;

        ret = NULL;
	proxy = dbus_g_proxy_new_for_name (bus,
                                           svc_name,
                                           obj_path,
                                           "org.freedesktop.DBus.Properties");
        if (!dbus_g_proxy_call (proxy,
                                "Get",
                                &error,
                                G_TYPE_STRING,
                                if_name,
                                G_TYPE_STRING,
                                prop_name,
                                G_TYPE_INVALID,
                                G_TYPE_VALUE,
                                &value,
                                G_TYPE_INVALID)) {
                g_warning ("error: %s\n", error->message);
                g_error_free (error);
                goto out;
        }

        ret = (char *) g_value_get_boxed (&value);

out:
        g_object_unref (proxy);
        return ret;
}

static char *
get_property_string (DBusGConnection *bus,
                     const char *svc_name,
                     const char *obj_path,
                     const char *if_name,
                     const char *prop_name)
{
        char *ret;
        DBusGProxy *proxy;
        GValue value = { 0 };
        GError *error = NULL;

        ret = NULL;
	proxy = dbus_g_proxy_new_for_name (bus,
                                           svc_name,
                                           obj_path,
                                           "org.freedesktop.DBus.Properties");
        if (!dbus_g_proxy_call (proxy,
                                "Get",
                                &error,
                                G_TYPE_STRING,
                                if_name,
                                G_TYPE_STRING,
                                prop_name,
                                G_TYPE_INVALID,
                                G_TYPE_VALUE,
                                &value,
                                G_TYPE_INVALID)) {
                g_warning ("error: %s\n", error->message);
                g_error_free (error);
                goto out;
        }

        ret = (char *) g_value_get_string (&value);

out:
        g_object_unref (proxy);
        return ret;
}

static gboolean
get_property_boolean (DBusGConnection *bus,
                      const char *svc_name,
                      const char *obj_path,
                      const char *if_name,
                      const char *prop_name)
{
        gboolean ret;
        DBusGProxy *proxy;
        GValue value = { 0 };
        GError *error = NULL;

        ret = FALSE;
	proxy = dbus_g_proxy_new_for_name (bus,
                                           svc_name,
                                           obj_path,
                                           "org.freedesktop.DBus.Properties");
        if (!dbus_g_proxy_call (proxy,
                                "Get",
                                &error,
                                G_TYPE_STRING,
                                if_name,
                                G_TYPE_STRING,
                                prop_name,
                                G_TYPE_INVALID,
                                G_TYPE_VALUE,
                                &value,
                                G_TYPE_INVALID)) {
                g_warning ("error: %s\n", error->message);
                g_error_free (error);
                goto out;
        }

        ret = (gboolean) g_value_get_boolean (&value);

out:
        g_object_unref (proxy);
        return ret;
}

static guint64
get_property_uint64 (DBusGConnection *bus,
                     const char *svc_name,
                     const char *obj_path,
                     const char *if_name,
                     const char *prop_name)
{
        guint64 ret;
        DBusGProxy *proxy;
        GValue value = { 0 };
        GError *error = NULL;

        ret = 0;
	proxy = dbus_g_proxy_new_for_name (bus,
                                           svc_name,
                                           obj_path,
                                           "org.freedesktop.DBus.Properties");
        if (!dbus_g_proxy_call (proxy,
                                "Get",
                                &error,
                                G_TYPE_STRING,
                                if_name,
                                G_TYPE_STRING,
                                prop_name,
                                G_TYPE_INVALID,
                                G_TYPE_VALUE,
                                &value,
                                G_TYPE_INVALID)) {
                g_warning ("error: %s\n", error->message);
                g_error_free (error);
                goto out;
        }

        ret = (guint64) g_value_get_uint64 (&value);

out:
        g_object_unref (proxy);
        return ret;
}

static GArray *
get_property_uint64_array (DBusGConnection *bus,
                           const char *svc_name,
                           const char *obj_path,
                           const char *if_name,
                           const char *prop_name)
{
        GArray *ret;
        DBusGProxy *proxy;
        GValue value = { 0 };
        GError *error = NULL;

        ret = 0;
	proxy = dbus_g_proxy_new_for_name (bus,
                                           svc_name,
                                           obj_path,
                                           "org.freedesktop.DBus.Properties");
        if (!dbus_g_proxy_call (proxy,
                                "Get",
                                &error,
                                G_TYPE_STRING,
                                if_name,
                                G_TYPE_STRING,
                                prop_name,
                                G_TYPE_INVALID,
                                G_TYPE_VALUE,
                                &value,
                                G_TYPE_INVALID)) {
                g_warning ("error: %s\n", error->message);
                g_error_free (error);
                goto out;
        }

        ret = (GArray*) g_value_get_boxed (&value);

out:
        g_object_unref (proxy);
        return ret;
}

static int
get_property_int (DBusGConnection *bus,
                  const char *svc_name,
                  const char *obj_path,
                  const char *if_name,
                  const char *prop_name)
{
        int ret;
        DBusGProxy *proxy;
        GValue value = { 0 };
        GError *error = NULL;

        ret = 0;
	proxy = dbus_g_proxy_new_for_name (bus,
                                           svc_name,
                                           obj_path,
                                           "org.freedesktop.DBus.Properties");
        if (!dbus_g_proxy_call (proxy,
                                "Get",
                                &error,
                                G_TYPE_STRING,
                                if_name,
                                G_TYPE_STRING,
                                prop_name,
                                G_TYPE_INVALID,
                                G_TYPE_VALUE,
                                &value,
                                G_TYPE_INVALID)) {
                g_warning ("error: %s\n", error->message);
                g_error_free (error);
                goto out;
        }

        ret = (guint64) g_value_get_int (&value);

out:
        g_object_unref (proxy);
        return ret;
}

static char **
get_property_strlist (DBusGConnection *bus,
                      const char *svc_name,
                      const char *obj_path,
                      const char *if_name,
                      const char *prop_name)
{
        char **ret;
        DBusGProxy *proxy;
        GValue value = { 0 };
        GError *error = NULL;

        ret = NULL;
	proxy = dbus_g_proxy_new_for_name (bus,
                                           svc_name,
                                           obj_path,
                                           "org.freedesktop.DBus.Properties");
        if (!dbus_g_proxy_call (proxy,
                                "Get",
                                &error,
                                G_TYPE_STRING,
                                if_name,
                                G_TYPE_STRING,
                                prop_name,
                                G_TYPE_INVALID,
                                G_TYPE_VALUE,
                                &value,
                                G_TYPE_INVALID)) {
                g_warning ("error: %s\n", error->message);
                g_error_free (error);
                goto out;
        }

        ret = (char **) g_value_get_boxed (&value);

out:
        /* don't crash; return an empty list */
        if (ret == NULL) {
                ret = g_new0 (char *,  1);
                *ret = NULL;
        }

        g_object_unref (proxy);
        return ret;
}

typedef struct
{
        char *native_path;

        char    *device_file;
        char   **device_file_by_id;
        char   **device_file_by_path;
        gboolean device_is_partition;
        gboolean device_is_partition_table;
        gboolean device_is_removable;
        gboolean device_is_media_available;
        gboolean device_is_drive;
        gboolean device_is_mounted;
        char    *device_mount_path;
        guint64  device_size;
        guint64  device_block_size;

        char    *id_usage;
        char    *id_type;
        char    *id_version;
        char    *id_uuid;
        char    *id_label;

        char    *partition_slave;
        char    *partition_scheme;
        int      partition_number;
        char    *partition_type;
        char    *partition_label;
        char    *partition_uuid;
        char   **partition_flags;
        guint64  partition_offset;
        guint64  partition_size;

        char    *partition_table_scheme;
        int      partition_table_count;
        int      partition_table_max_number;
        GArray  *partition_table_offsets;
        GArray  *partition_table_sizes;

        char    *drive_vendor;
        char    *drive_model;
        char    *drive_revision;
        char    *drive_serial;
} DeviceProperties;

static DeviceProperties *
device_properties_get (DBusGConnection *bus,
                       const char *object_path)
{
        DeviceProperties *props;

        props = g_new0 (DeviceProperties, 1);
        props->native_path = get_property_string (
                bus,
                "org.freedesktop.DeviceKit.Disks",
                object_path,
                "org.freedesktop.DeviceKit.Disks.Device",
                "native-path");

        props->device_file = get_property_string (
                bus,
                "org.freedesktop.DeviceKit.Disks",
                object_path,
                "org.freedesktop.DeviceKit.Disks.Device",
                "device-file");
        props->device_file_by_id = get_property_strlist (
                bus,
                "org.freedesktop.DeviceKit.Disks",
                object_path,
                "org.freedesktop.DeviceKit.Disks.Device",
                "device-file-by-id");
        props->device_file_by_path = get_property_strlist (
                bus,
                "org.freedesktop.DeviceKit.Disks",
                object_path,
                "org.freedesktop.DeviceKit.Disks.Device",
                "device-file-by-path");
        props->device_is_partition = get_property_boolean (
                bus,
                "org.freedesktop.DeviceKit.Disks",
                object_path,
                "org.freedesktop.DeviceKit.Disks.Device",
                "device-is-partition");
        props->device_is_partition_table = get_property_boolean (
                bus,
                "org.freedesktop.DeviceKit.Disks",
                object_path,
                "org.freedesktop.DeviceKit.Disks.Device",
                "device-is-partition-table");
        props->device_is_removable = get_property_boolean (
                bus,
                "org.freedesktop.DeviceKit.Disks",
                object_path,
                "org.freedesktop.DeviceKit.Disks.Device",
                "device-is-removable");
        props->device_is_media_available = get_property_boolean (
                bus,
                "org.freedesktop.DeviceKit.Disks",
                object_path,
                "org.freedesktop.DeviceKit.Disks.Device",
                "device-is-media-available");
        props->device_is_drive = get_property_boolean (
                bus,
                "org.freedesktop.DeviceKit.Disks",
                object_path,
                "org.freedesktop.DeviceKit.Disks.Device",
                "device-is-drive");
        props->device_is_mounted = get_property_boolean (
                bus,
                "org.freedesktop.DeviceKit.Disks",
                object_path,
                "org.freedesktop.DeviceKit.Disks.Device",
                "device-is-mounted");
        props->device_mount_path = get_property_string (
                bus,
                "org.freedesktop.DeviceKit.Disks",
                object_path,
                "org.freedesktop.DeviceKit.Disks.Device",
                "device-mount-path");
        props->device_size = get_property_uint64 (
                bus,
                "org.freedesktop.DeviceKit.Disks",
                object_path,
                "org.freedesktop.DeviceKit.Disks.Device",
                "device-size");
        props->device_block_size = get_property_uint64 (
                bus,
                "org.freedesktop.DeviceKit.Disks",
                object_path,
                "org.freedesktop.DeviceKit.Disks.Device",
                "device-block-size");

        props->id_usage = get_property_string (
                bus,
                "org.freedesktop.DeviceKit.Disks",
                object_path,
                "org.freedesktop.DeviceKit.Disks.Device",
                "id-usage");
        props->id_type = get_property_string (
                bus,
                "org.freedesktop.DeviceKit.Disks",
                object_path,
                "org.freedesktop.DeviceKit.Disks.Device",
                "id-type");
        props->id_version = get_property_string (
                bus,
                "org.freedesktop.DeviceKit.Disks",
                object_path,
                "org.freedesktop.DeviceKit.Disks.Device",
                "id-version");
        props->id_uuid = get_property_string (
                bus,
                "org.freedesktop.DeviceKit.Disks",
                object_path,
                "org.freedesktop.DeviceKit.Disks.Device",
                "id-uuid");
        props->id_label = get_property_string (
                bus,
                "org.freedesktop.DeviceKit.Disks",
                object_path,
                "org.freedesktop.DeviceKit.Disks.Device",
                "id-label");

        props->partition_slave = get_property_object_path (
                bus,
                "org.freedesktop.DeviceKit.Disks",
                object_path,
                "org.freedesktop.DeviceKit.Disks.Device",
                "partition-slave");
        props->partition_scheme = get_property_string (
                bus,
                "org.freedesktop.DeviceKit.Disks",
                object_path,
                "org.freedesktop.DeviceKit.Disks.Device",
                "partition-scheme");
        props->partition_number = get_property_int (
                bus,
                "org.freedesktop.DeviceKit.Disks",
                object_path,
                "org.freedesktop.DeviceKit.Disks.Device",
                "partition-number");
        props->partition_type = get_property_string (
                bus,
                "org.freedesktop.DeviceKit.Disks",
                object_path,
                "org.freedesktop.DeviceKit.Disks.Device",
                "partition-type");
        props->partition_label = get_property_string (
                bus,
                "org.freedesktop.DeviceKit.Disks",
                object_path,
                "org.freedesktop.DeviceKit.Disks.Device",
                "partition-label");
        props->partition_uuid = get_property_string (
                bus,
                "org.freedesktop.DeviceKit.Disks",
                object_path,
                "org.freedesktop.DeviceKit.Disks.Device",
                "partition-uuid");
        props->partition_flags = get_property_strlist (
                bus,
                "org.freedesktop.DeviceKit.Disks",
                object_path,
                "org.freedesktop.DeviceKit.Disks.Device",
                "partition-flags");
        props->partition_offset = get_property_uint64 (
                bus,
                "org.freedesktop.DeviceKit.Disks",
                object_path,
                "org.freedesktop.DeviceKit.Disks.Device",
                "partition-offset");
        props->partition_size = get_property_uint64 (
                bus,
                "org.freedesktop.DeviceKit.Disks",
                object_path,
                "org.freedesktop.DeviceKit.Disks.Device",
                "partition-size");

        props->partition_table_scheme = get_property_string (
                bus,
                "org.freedesktop.DeviceKit.Disks",
                object_path,
                "org.freedesktop.DeviceKit.Disks.Device",
                "partition-table-scheme");
        props->partition_table_count = get_property_int (
                bus,
                "org.freedesktop.DeviceKit.Disks",
                object_path,
                "org.freedesktop.DeviceKit.Disks.Device",
                "partition-table-count");
        props->partition_table_max_number = get_property_int (
                bus,
                "org.freedesktop.DeviceKit.Disks",
                object_path,
                "org.freedesktop.DeviceKit.Disks.Device",
                "partition-table-max-number");
        props->partition_table_offsets = get_property_uint64_array (
                bus,
                "org.freedesktop.DeviceKit.Disks",
                object_path,
                "org.freedesktop.DeviceKit.Disks.Device",
                "partition-table-offsets");
        props->partition_table_sizes = get_property_uint64_array (
                bus,
                "org.freedesktop.DeviceKit.Disks",
                object_path,
                "org.freedesktop.DeviceKit.Disks.Device",
                "partition-table-sizes");

        props->drive_vendor = get_property_string (
                bus,
                "org.freedesktop.DeviceKit.Disks",
                object_path,
                "org.freedesktop.DeviceKit.Disks.Device",
                "drive-vendor");
        props->drive_model = get_property_string (
                bus,
                "org.freedesktop.DeviceKit.Disks",
                object_path,
                "org.freedesktop.DeviceKit.Disks.Device",
                "drive-model");
        props->drive_revision = get_property_string (
                bus,
                "org.freedesktop.DeviceKit.Disks",
                object_path,
                "org.freedesktop.DeviceKit.Disks.Device",
                "drive-revision");
        props->drive_serial = get_property_string (
                bus,
                "org.freedesktop.DeviceKit.Disks",
                object_path,
                "org.freedesktop.DeviceKit.Disks.Device",
                "drive-serial");

        return props;
}

static void
device_properties_free (DeviceProperties *props)
{
        g_free (props->native_path);
        g_free (props->device_file);
        g_strfreev (props->device_file_by_id);
        g_strfreev (props->device_file_by_path);
        g_free (props->device_mount_path);
        g_free (props->id_usage);
        g_free (props->id_type);
        g_free (props->id_version);
        g_free (props->id_uuid);
        g_free (props->id_label);
        g_free (props->partition_slave);
        g_free (props->partition_type);
        g_free (props->partition_label);
        g_free (props->partition_uuid);
        g_strfreev (props->partition_flags);
        g_free (props->partition_table_scheme);
        g_array_free (props->partition_table_offsets, TRUE);
        g_array_free (props->partition_table_sizes, TRUE);
        g_free (props->drive_model);
        g_free (props->drive_vendor);
        g_free (props->drive_revision);
        g_free (props->drive_serial);
        g_free (props);
}

/* --- SUCKY CODE END --- */

struct _GduDevicePrivate
{
        DBusGConnection *bus;
        DBusGProxy *proxy;
        GduPool *pool;

        char *object_path;

        DeviceProperties *props;
};

enum {
        CHANGED,
        REMOVED,
        LAST_SIGNAL,
};

static GObjectClass *parent_class = NULL;
static guint signals[LAST_SIGNAL] = { 0 };

G_DEFINE_TYPE (GduDevice, gdu_device, G_TYPE_OBJECT);

static void
gdu_device_finalize (GduDevice *device)
{
        dbus_g_connection_unref (device->priv->bus);
        g_free (device->priv->object_path);
        if (device->priv->proxy != NULL)
                g_object_unref (device->priv->proxy);
        if (device->priv->pool != NULL)
                g_object_unref (device->priv->pool);
        if (device->priv->props != NULL)
                device_properties_free (device->priv->props);

        if (G_OBJECT_CLASS (parent_class)->finalize)
                (* G_OBJECT_CLASS (parent_class)->finalize) (G_OBJECT (device));
}

static void
gdu_device_class_init (GduDeviceClass *klass)
{
        GObjectClass *obj_class = (GObjectClass *) klass;

        parent_class = g_type_class_peek_parent (klass);

        obj_class->finalize = (GObjectFinalizeFunc) gdu_device_finalize;

        signals[CHANGED] =
                g_signal_new ("changed",
                              G_TYPE_FROM_CLASS (klass),
                              G_SIGNAL_RUN_LAST,
                              G_STRUCT_OFFSET (GduDeviceClass, changed),
                              NULL, NULL,
                              g_cclosure_marshal_VOID__VOID,
                              G_TYPE_NONE, 0);
        signals[REMOVED] =
                g_signal_new ("removed",
                              G_TYPE_FROM_CLASS (klass),
                              G_SIGNAL_RUN_LAST,
                              G_STRUCT_OFFSET (GduDeviceClass, removed),
                              NULL, NULL,
                              g_cclosure_marshal_VOID__VOID,
                              G_TYPE_NONE, 0);
}

static void
gdu_device_init (GduDevice *device)
{
        device->priv = g_new0 (GduDevicePrivate, 1);
}

static gboolean
update_info (GduDevice *device)
{
        if (device->priv->props != NULL)
                device_properties_free (device->priv->props);
        device->priv->props = device_properties_get (device->priv->bus, device->priv->object_path);
        return TRUE;
}


GduDevice *
gdu_device_new_from_object_path (GduPool *pool, const char *object_path)
{
        GError *error;
        GduDevice *device;

        device = GDU_DEVICE (g_object_new (GDU_TYPE_DEVICE, NULL));
        device->priv->object_path = g_strdup (object_path);
        device->priv->pool = g_object_ref (pool);

        error = NULL;
        device->priv->bus = dbus_g_bus_get (DBUS_BUS_SYSTEM, &error);
        if (device->priv->bus == NULL) {
                g_warning ("Couldn't connect to system bus: %s", error->message);
                g_error_free (error);
                goto error;
        }

	device->priv->proxy = dbus_g_proxy_new_for_name (device->priv->bus,
                                                         "org.freedesktop.DeviceKit.Disks",
                                                         device->priv->object_path,
                                                         "org.freedesktop.DeviceKit.Disks.Device");
        dbus_g_proxy_add_signal (device->priv->proxy, "Changed", G_TYPE_INVALID);

        /* TODO: connect signals */

        if (!update_info (device))
                goto error;

        return device;
error:
        g_object_unref (device);
        return NULL;
}

void
gdu_device_changed (GduDevice   *device)
{
        update_info (device);
        g_signal_emit (device, signals[CHANGED], 0);
}

void
gdu_device_removed (GduDevice   *device)
{
        g_signal_emit (device, signals[REMOVED], 0);
}

const char *
gdu_device_get_object_path (GduDevice *device)
{
        return device->priv->object_path;
}

/**
 * gdu_device_find_parent:
 * @device: the device
 *
 * Finds a parent device for the given @device. Note that this is only
 * useful for presentation purposes; the device tree may be a lot more
 * complex.
 *
 * Returns: The parent of @device if one could be found, otherwise
 * #NULL. Caller must unref this object using g_object_unref().
 **/
GduDevice *
gdu_device_find_parent (GduDevice *device)
{
        GduDevice *parent;

        parent = NULL;

        /* partitioning relationship */
        if (device->priv->props->device_is_partition &&
            device->priv->props->partition_slave != NULL &&
            strlen (device->priv->props->partition_slave) > 0) {
                parent = gdu_pool_get_by_object_path (device->priv->pool,
                                                      device->priv->props->partition_slave);
        }

        return parent;
}

const char *
gdu_device_get_device_file (GduDevice *device)
{
        return device->priv->props->device_file;
}

guint64
gdu_device_get_size (GduDevice *device)
{
        return device->priv->props->device_size;
}

guint64
gdu_device_get_block_size (GduDevice *device)
{
        return device->priv->props->device_block_size;
}

gboolean
gdu_device_is_removable (GduDevice *device)
{
        return device->priv->props->device_is_removable;
}

gboolean
gdu_device_is_media_available (GduDevice *device)
{
        return device->priv->props->device_is_media_available;
}

gboolean
gdu_device_is_partition (GduDevice *device)
{
        return device->priv->props->device_is_partition;
}

gboolean
gdu_device_is_partition_table (GduDevice *device)
{
        return device->priv->props->device_is_partition_table;
}

gboolean
gdu_device_is_mounted (GduDevice *device)
{
        return device->priv->props->device_is_mounted;
}

const char *
gdu_device_get_mount_path (GduDevice *device)
{
        return device->priv->props->device_mount_path;
}


const char *
gdu_device_id_get_usage (GduDevice *device)
{
        return device->priv->props->id_usage;
}

const char *
gdu_device_id_get_type (GduDevice *device)
{
        return device->priv->props->id_type;
}

const char *
gdu_device_id_get_version (GduDevice *device)
{
        return device->priv->props->id_version;
}

const char *
gdu_device_id_get_label (GduDevice *device)
{
        return device->priv->props->id_label;
}

const char *
gdu_device_id_get_uuid (GduDevice *device)
{
        return device->priv->props->id_uuid;
}



const char *
gdu_device_partition_get_slave (GduDevice *device)
{
        return device->priv->props->partition_slave;
}

const char *
gdu_device_partition_get_scheme (GduDevice *device)
{
        return device->priv->props->partition_scheme;
}

const char *
gdu_device_partition_get_type (GduDevice *device)
{
        return device->priv->props->partition_type;
}

const char *
gdu_device_partition_get_label (GduDevice *device)
{
        return device->priv->props->partition_label;
}

const char *
gdu_device_partition_get_uuid (GduDevice *device)
{
        return device->priv->props->partition_uuid;
}

char **
gdu_device_partition_get_flags (GduDevice *device)
{
        return device->priv->props->partition_flags;
}

int
gdu_device_partition_get_number (GduDevice *device)
{
        return device->priv->props->partition_number;
}

guint64
gdu_device_partition_get_offset (GduDevice *device)
{
        return device->priv->props->partition_offset;
}

guint64
gdu_device_partition_get_size (GduDevice *device)
{
        return device->priv->props->partition_size;
}



const char *
gdu_device_partition_table_get_scheme (GduDevice *device)
{
        return device->priv->props->partition_table_scheme;
}

int
gdu_device_partition_table_get_count (GduDevice *device)
{
        return device->priv->props->partition_table_count;
}

int
gdu_device_partition_table_get_max_number (GduDevice *device)
{
        return device->priv->props->partition_table_max_number;
}

GArray *
gdu_device_partition_table_get_offsets (GduDevice *device)
{
        return device->priv->props->partition_table_offsets;
}

GArray *
gdu_device_partition_table_get_sizes (GduDevice *device)
{
        return device->priv->props->partition_table_sizes;
}

gboolean
gdu_device_is_drive (GduDevice *device)
{
        return device->priv->props->device_is_drive;
}

const char *
gdu_device_drive_get_vendor (GduDevice *device)
{
        return device->priv->props->drive_vendor;
}

const char *
gdu_device_drive_get_model (GduDevice *device)
{
        return device->priv->props->drive_model;
}

const char *
gdu_device_drive_get_revision (GduDevice *device)
{
        return device->priv->props->drive_revision;
}

const char *
gdu_device_drive_get_serial (GduDevice *device)
{
        return device->priv->props->drive_serial;
}

