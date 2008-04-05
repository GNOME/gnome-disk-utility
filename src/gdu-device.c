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
#include <time.h>

#include "gdu-pool.h"
#include "gdu-device.h"
#include "devkit-disks-device-glue.h"

/* --- SUCKY CODE BEGIN --- */

/* This totally sucks; dbus-bindings-tool and dbus-glib should be able
 * to do this for us.
 *
 * TODO: keep in sync with code in tools/devkit-disks in DeviceKit-disks.
 */

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
        gboolean device_is_read_only;
        gboolean device_is_drive;
        gboolean device_is_crypto_cleartext;
        gboolean device_is_mounted;
        gboolean device_is_busy;
        gboolean device_is_linux_md_component;
        gboolean device_is_linux_md;
        char    *device_mount_path;
        guint64  device_size;
        guint64  device_block_size;

        gboolean job_in_progress;
        char    *job_id;
        gboolean job_is_cancellable;
        int      job_num_tasks;
        int      job_cur_task;
        char    *job_cur_task_id;
        double   job_cur_task_percentage;

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

        char    *crypto_cleartext_slave;

        char    *drive_vendor;
        char    *drive_model;
        char    *drive_revision;
        char    *drive_serial;
        char    *drive_connection_interface;
        guint64  drive_connection_speed;
        char   **drive_media_compatibility;
        char    *drive_media;

        char    *linux_md_component_level;
        int      linux_md_component_num_raid_devices;
        char    *linux_md_component_uuid;
        char    *linux_md_component_name;
        char    *linux_md_component_version;
        guint64  linux_md_component_update_time;
        guint64  linux_md_component_events;

        char    *linux_md_level;
        int      linux_md_num_raid_devices;
        char    *linux_md_version;
        char   **linux_md_slaves;
        char   **linux_md_slaves_state;
        gboolean linux_md_is_degraded;
        char    *linux_md_sync_action;
        double   linux_md_sync_percentage;
        guint64  linux_md_sync_speed;
} DeviceProperties;

static void
collect_props (const char *key, const GValue *value, DeviceProperties *props)
{
        gboolean handled = TRUE;

        if (strcmp (key, "native-path") == 0)
                props->native_path = g_strdup (g_value_get_string (value));

        else if (strcmp (key, "device-file") == 0)
                props->device_file = g_strdup (g_value_get_string (value));
        else if (strcmp (key, "device-file-by-id") == 0)
                props->device_file_by_id = g_strdupv (g_value_get_boxed (value));
        else if (strcmp (key, "device-file-by-path") == 0)
                props->device_file_by_path = g_strdupv (g_value_get_boxed (value));
        else if (strcmp (key, "device-is-partition") == 0)
                props->device_is_partition = g_value_get_boolean (value);
        else if (strcmp (key, "device-is-partition-table") == 0)
                props->device_is_partition_table = g_value_get_boolean (value);
        else if (strcmp (key, "device-is-removable") == 0)
                props->device_is_removable = g_value_get_boolean (value);
        else if (strcmp (key, "device-is-media-available") == 0)
                props->device_is_media_available = g_value_get_boolean (value);
        else if (strcmp (key, "device-is-read-only") == 0)
                props->device_is_read_only = g_value_get_boolean (value);
        else if (strcmp (key, "device-is-drive") == 0)
                props->device_is_drive = g_value_get_boolean (value);
        else if (strcmp (key, "device-is-crypto-cleartext") == 0)
                props->device_is_crypto_cleartext = g_value_get_boolean (value);
        else if (strcmp (key, "device-is-linux-md-component") == 0)
                props->device_is_linux_md_component = g_value_get_boolean (value);
        else if (strcmp (key, "device-is-linux-md") == 0)
                props->device_is_linux_md = g_value_get_boolean (value);
        else if (strcmp (key, "device-is-mounted") == 0)
                props->device_is_mounted = g_value_get_boolean (value);
        else if (strcmp (key, "device-is-busy") == 0)
                props->device_is_busy = g_value_get_boolean (value);
        else if (strcmp (key, "device-mount-path") == 0)
                props->device_mount_path = g_strdup (g_value_get_string (value));
        else if (strcmp (key, "device-size") == 0)
                props->device_size = g_value_get_uint64 (value);
        else if (strcmp (key, "device-block-size") == 0)
                props->device_block_size = g_value_get_uint64 (value);

        else if (strcmp (key, "job-in-progress") == 0)
                props->job_in_progress = g_value_get_boolean (value);
        else if (strcmp (key, "job-id") == 0)
                props->job_id = g_strdup (g_value_get_string (value));
        else if (strcmp (key, "job-is-cancellable") == 0)
                props->job_is_cancellable = g_value_get_boolean (value);
        else if (strcmp (key, "job-num-tasks") == 0)
                props->job_num_tasks = g_value_get_int (value);
        else if (strcmp (key, "job-cur-task") == 0)
                props->job_cur_task = g_value_get_int (value);
        else if (strcmp (key, "job-cur-task-id") == 0)
                props->job_cur_task_id = g_strdup (g_value_get_string (value));
        else if (strcmp (key, "job-cur-task-percentage") == 0)
                props->job_cur_task_percentage = g_value_get_double (value);

        else if (strcmp (key, "id-usage") == 0)
                props->id_usage = g_strdup (g_value_get_string (value));
        else if (strcmp (key, "id-type") == 0)
                props->id_type = g_strdup (g_value_get_string (value));
        else if (strcmp (key, "id-version") == 0)
                props->id_version = g_strdup (g_value_get_string (value));
        else if (strcmp (key, "id-uuid") == 0)
                props->id_uuid = g_strdup (g_value_get_string (value));
        else if (strcmp (key, "id-label") == 0)
                props->id_label = g_strdup (g_value_get_string (value));

        else if (strcmp (key, "partition-slave") == 0)
                props->partition_slave = g_strdup (g_value_get_boxed (value));
        else if (strcmp (key, "partition-scheme") == 0)
                props->partition_scheme = g_strdup (g_value_get_string (value));
        else if (strcmp (key, "partition-number") == 0)
                props->partition_number = g_value_get_int (value);
        else if (strcmp (key, "partition-type") == 0)
                props->partition_type = g_strdup (g_value_get_string (value));
        else if (strcmp (key, "partition-label") == 0)
                props->partition_label = g_strdup (g_value_get_string (value));
        else if (strcmp (key, "partition-uuid") == 0)
                props->partition_uuid = g_strdup (g_value_get_string (value));
        else if (strcmp (key, "partition-flags") == 0)
                props->partition_flags = g_strdupv (g_value_get_boxed (value));
        else if (strcmp (key, "partition-offset") == 0)
                props->partition_offset = g_value_get_uint64 (value);
        else if (strcmp (key, "partition-size") == 0)
                props->partition_size = g_value_get_uint64 (value);

        else if (strcmp (key, "partition-table-scheme") == 0)
                props->partition_table_scheme = g_strdup (g_value_get_string (value));
        else if (strcmp (key, "partition-table-count") == 0)
                props->partition_table_count = g_value_get_int (value);
        else if (strcmp (key, "partition-table-max-number") == 0)
                props->partition_table_max_number = g_value_get_int (value);
        else if (strcmp (key, "partition-table-offsets") == 0) {
                GValue dest_value = {0,};
                g_value_init (&dest_value, dbus_g_type_get_collection ("GArray", G_TYPE_UINT64));
                g_value_copy (value, &dest_value);
                props->partition_table_offsets = g_value_get_boxed (&dest_value);
        } else if (strcmp (key, "partition-table-sizes") == 0) {
                GValue dest_value = {0,};
                g_value_init (&dest_value, dbus_g_type_get_collection ("GArray", G_TYPE_UINT64));
                g_value_copy (value, &dest_value);
                props->partition_table_sizes = g_value_get_boxed (&dest_value);
        }

        else if (strcmp (key, "crypto-cleartext-slave") == 0)
                props->crypto_cleartext_slave = g_strdup (g_value_get_boxed (value));

        else if (strcmp (key, "drive-vendor") == 0)
                props->drive_vendor = g_strdup (g_value_get_string (value));
        else if (strcmp (key, "drive-model") == 0)
                props->drive_model = g_strdup (g_value_get_string (value));
        else if (strcmp (key, "drive-revision") == 0)
                props->drive_revision = g_strdup (g_value_get_string (value));
        else if (strcmp (key, "drive-serial") == 0)
                props->drive_serial = g_strdup (g_value_get_string (value));
        else if (strcmp (key, "drive-connection-interface") == 0)
                props->drive_connection_interface = g_strdup (g_value_get_string (value));
        else if (strcmp (key, "drive-connection-speed") == 0)
                props->drive_connection_speed = g_value_get_uint64 (value);
        else if (strcmp (key, "drive-media-compatibility") == 0)
                props->drive_media_compatibility = g_strdupv (g_value_get_boxed (value));
        else if (strcmp (key, "drive-media") == 0)
                props->drive_media = g_strdup (g_value_get_string (value));

        else if (strcmp (key, "linux-md-component-level") == 0)
                props->linux_md_component_level = g_strdup (g_value_get_string (value));
        else if (strcmp (key, "linux-md-component-num-raid-devices") == 0)
                props->linux_md_component_num_raid_devices = g_value_get_int (value);
        else if (strcmp (key, "linux-md-component-uuid") == 0)
                props->linux_md_component_uuid = g_strdup (g_value_get_string (value));
        else if (strcmp (key, "linux-md-component-name") == 0)
                props->linux_md_component_name = g_strdup (g_value_get_string (value));
        else if (strcmp (key, "linux-md-component-version") == 0)
                props->linux_md_component_version = g_strdup (g_value_get_string (value));
        else if (strcmp (key, "linux-md-component-update-time") == 0)
                props->linux_md_component_update_time = g_value_get_uint64 (value);
        else if (strcmp (key, "linux-md-component-events") == 0)
                props->linux_md_component_events = g_value_get_uint64 (value);

        else if (strcmp (key, "linux-md-level") == 0)
                props->linux_md_level = g_strdup (g_value_get_string (value));
        else if (strcmp (key, "linux-md-num-raid-devices") == 0)
                props->linux_md_num_raid_devices = g_value_get_int (value);
        else if (strcmp (key, "linux-md-version") == 0)
                props->linux_md_version = g_strdup (g_value_get_string (value));
        else if (strcmp (key, "linux-md-slaves") == 0) {
                int n;
                GPtrArray *object_paths;

                object_paths = g_value_get_boxed (value);

                props->linux_md_slaves = g_new0 (char *, object_paths->len + 1);
                for (n = 0; n < (int) object_paths->len; n++)
                        props->linux_md_slaves[n] = g_strdup (object_paths->pdata[n]);
                props->linux_md_slaves[n] = NULL;
        }
        else if (strcmp (key, "linux-md-slaves-state") == 0)
                props->linux_md_slaves_state = g_strdupv (g_value_get_boxed (value));
        else if (strcmp (key, "linux-md-is-degraded") == 0)
                props->linux_md_is_degraded = g_value_get_boolean (value);
        else if (strcmp (key, "linux-md-sync-action") == 0)
                props->linux_md_sync_action = g_strdup (g_value_get_string (value));
        else if (strcmp (key, "linux-md-sync-percentage") == 0)
                props->linux_md_sync_percentage = g_value_get_double (value);
        else if (strcmp (key, "linux-md-sync-speed") == 0)
                props->linux_md_sync_speed = g_value_get_uint64 (value);

        else
                handled = FALSE;

        if (!handled)
                g_warning ("unhandled property '%s'", key);
}

static DeviceProperties *
device_properties_get (DBusGConnection *bus,
                       const char *object_path)
{
        DeviceProperties *props;
        GError *error;
        GHashTable *hash_table;
        DBusGProxy *prop_proxy;
        const char *ifname = "org.freedesktop.DeviceKit.Disks.Device";

        props = g_new0 (DeviceProperties, 1);

	prop_proxy = dbus_g_proxy_new_for_name (bus,
                                                "org.freedesktop.DeviceKit.Disks",
                                                object_path,
                                                "org.freedesktop.DBus.Properties");
        error = NULL;
        if (!dbus_g_proxy_call (prop_proxy,
                                "GetAll",
                                &error,
                                G_TYPE_STRING,
                                ifname,
                                G_TYPE_INVALID,
                                dbus_g_type_get_map ("GHashTable", G_TYPE_STRING, G_TYPE_VALUE),
                                &hash_table,
                                G_TYPE_INVALID)) {
                g_warning ("Couldn't call GetAll() to get properties for %s: %s", object_path, error->message);
                g_error_free (error);
                goto out;
        }

        g_hash_table_foreach (hash_table, (GHFunc) collect_props, props);

        g_hash_table_unref (hash_table);

out:
        g_object_unref (prop_proxy);
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
        g_free (props->job_id);
        g_free (props->job_cur_task_id);
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
        g_free (props->crypto_cleartext_slave);
        g_free (props->drive_model);
        g_free (props->drive_vendor);
        g_free (props->drive_revision);
        g_free (props->drive_serial);
        g_free (props->drive_connection_interface);
        g_strfreev (props->drive_media_compatibility);
        g_free (props->drive_media);
        g_free (props->linux_md_component_level);
        g_free (props->linux_md_component_uuid);
        g_free (props->linux_md_component_name);
        g_free (props->linux_md_component_version);
        g_free (props->linux_md_level);
        g_free (props->linux_md_version);
        g_strfreev (props->linux_md_slaves);
        g_strfreev (props->linux_md_slaves_state);
        g_free (props->linux_md_sync_action);
        g_free (props);
}

/* --- SUCKY CODE END --- */

struct _GduDevicePrivate
{
        DBusGConnection *bus;
        DBusGProxy *proxy;
        GduPool *pool;

        char *object_path;

        char *job_last_error_message;

        DeviceProperties *props;

        time_t smart_data_cache_timestamp;
        gboolean smart_data_cache_passed;
        int smart_data_cache_power_on_hours;
        int smart_data_cache_temperature;
        GError *smart_data_cache_error;
};

enum {
        JOB_CHANGED,
        CHANGED,
        REMOVED,
        LAST_SIGNAL,
};

static GObjectClass *parent_class = NULL;
static guint signals[LAST_SIGNAL] = { 0 };

G_DEFINE_TYPE (GduDevice, gdu_device, G_TYPE_OBJECT);

GduPool *
gdu_device_get_pool (GduDevice *device)
{
        return g_object_ref (device->priv->pool);
}

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
        g_free (device->priv->job_last_error_message);

        if (device->priv->smart_data_cache_error != NULL)
                g_error_free (device->priv->smart_data_cache_error);

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
        signals[JOB_CHANGED] =
                g_signal_new ("job-changed",
                              G_TYPE_FROM_CLASS (klass),
                              G_SIGNAL_RUN_LAST,
                              G_STRUCT_OFFSET (GduDeviceClass, job_changed),
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
        dbus_g_proxy_set_default_timeout (device->priv->proxy, INT_MAX);
        dbus_g_proxy_add_signal (device->priv->proxy, "Changed", G_TYPE_INVALID);

        /* TODO: connect signals */

        if (!update_info (device))
                goto error;

        g_print ("%s: %s\n", __FUNCTION__, device->priv->props->device_file);

        return device;
error:
        g_object_unref (device);
        return NULL;
}

void
gdu_device_changed (GduDevice *device)
{
        g_print ("%s: %s\n", __FUNCTION__, device->priv->props->device_file);
        update_info (device);
        g_signal_emit (device, signals[CHANGED], 0);
}

void
gdu_device_job_changed (GduDevice   *device,
                        gboolean     job_in_progress,
                        const char  *job_id,
                        gboolean     job_is_cancellable,
                        int          job_num_tasks,
                        int          job_cur_task,
                        const char  *job_cur_task_id,
                        double       job_cur_task_percentage)
{
        g_print ("%s: %s: %s\n", __FUNCTION__, device->priv->props->device_file, job_id);

        device->priv->props->job_in_progress = job_in_progress;
        g_free (device->priv->props->job_id);
        device->priv->props->job_id = g_strdup (job_id);
        device->priv->props->job_is_cancellable = job_is_cancellable;
        device->priv->props->job_num_tasks = job_num_tasks;
        device->priv->props->job_cur_task = job_cur_task;
        g_free (device->priv->props->job_cur_task_id);
        device->priv->props->job_cur_task_id = g_strdup (job_cur_task_id);
        device->priv->props->job_cur_task_percentage = job_cur_task_percentage;

        g_signal_emit (device, signals[JOB_CHANGED], 0);
}

void
gdu_device_removed (GduDevice *device)
{
        g_print ("%s: %s\n", __FUNCTION__, device->priv->props->device_file);
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
gdu_device_is_read_only (GduDevice *device)
{
        return device->priv->props->device_is_read_only;
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
gdu_device_is_crypto_cleartext (GduDevice *device)
{
        return device->priv->props->device_is_crypto_cleartext;
}

gboolean
gdu_device_is_linux_md_component (GduDevice *device)
{
        return device->priv->props->device_is_linux_md_component;
}

gboolean
gdu_device_is_linux_md (GduDevice *device)
{
        return device->priv->props->device_is_linux_md;
}

gboolean
gdu_device_is_mounted (GduDevice *device)
{
        return device->priv->props->device_is_mounted;
}

gboolean
gdu_device_is_busy (GduDevice *device)
{
        return device->priv->props->device_is_busy;
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

const char *
gdu_device_crypto_cleartext_get_slave (GduDevice *device)
{
        return device->priv->props->crypto_cleartext_slave;
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

const char *
gdu_device_drive_get_connection_interface (GduDevice *device)
{
        return device->priv->props->drive_connection_interface;
}

guint64
gdu_device_drive_get_connection_speed (GduDevice *device)
{
        return device->priv->props->drive_connection_speed;
}

char **
gdu_device_drive_get_media_compatibility (GduDevice *device)
{
        return device->priv->props->drive_media_compatibility;
}

const char *
gdu_device_drive_get_media (GduDevice *device)
{
        return device->priv->props->drive_media;
}

const char *
gdu_device_linux_md_component_get_level (GduDevice *device)
{
        return device->priv->props->linux_md_component_level;
}

int
gdu_device_linux_md_component_get_num_raid_devices (GduDevice *device)
{
        return device->priv->props->linux_md_component_num_raid_devices;
}

const char *
gdu_device_linux_md_component_get_uuid (GduDevice *device)
{
        return device->priv->props->linux_md_component_uuid;
}

const char *
gdu_device_linux_md_component_get_name (GduDevice *device)
{
        return device->priv->props->linux_md_component_name;
}

const char *
gdu_device_linux_md_component_get_version (GduDevice *device)
{
        return device->priv->props->linux_md_component_version;
}

guint64
gdu_device_linux_md_component_get_update_time (GduDevice *device)
{
        return device->priv->props->linux_md_component_update_time;
}

guint64
gdu_device_linux_md_component_get_events (GduDevice *device)
{
        return device->priv->props->linux_md_component_events;
}

const char *
gdu_device_linux_md_get_level (GduDevice *device)
{
        return device->priv->props->linux_md_level;
}

int
gdu_device_linux_md_get_num_raid_devices (GduDevice *device)
{
        return device->priv->props->linux_md_num_raid_devices;
}

const char *
gdu_device_linux_md_get_version (GduDevice *device)
{
        return device->priv->props->linux_md_version;
}

char **
gdu_device_linux_md_get_slaves (GduDevice *device)
{
        return device->priv->props->linux_md_slaves;
}

char **
gdu_device_linux_md_get_slaves_state (GduDevice *device)
{
        return device->priv->props->linux_md_slaves_state;
}

gboolean
gdu_device_linux_md_is_degraded (GduDevice *device)
{
        return device->priv->props->linux_md_is_degraded;
}

const char *
gdu_device_linux_md_get_sync_action (GduDevice *device)
{
        return device->priv->props->linux_md_sync_action;
}

double
gdu_device_linux_md_get_sync_percentage (GduDevice *device)
{
        return device->priv->props->linux_md_sync_percentage;
}

guint64
gdu_device_linux_md_get_sync_speed (GduDevice *device)
{
        return device->priv->props->linux_md_sync_speed;
}

/* ---------------------------------------------------------------------------------------------------- */

gboolean
gdu_device_job_in_progress (GduDevice *device)
{
        return device->priv->props->job_in_progress;
}

const char *
gdu_device_job_get_id (GduDevice *device)
{
        return device->priv->props->job_id;
}

gboolean
gdu_device_job_is_cancellable (GduDevice *device)
{
        return device->priv->props->job_is_cancellable;
}

int
gdu_device_job_get_num_tasks (GduDevice *device)
{
        return device->priv->props->job_num_tasks;
}

int
gdu_device_job_get_cur_task (GduDevice *device)
{
        return device->priv->props->job_cur_task;
}

const char *
gdu_device_job_get_cur_task_id (GduDevice *device)
{
        return device->priv->props->job_cur_task_id;
}

double
gdu_device_job_get_cur_task_percentage (GduDevice *device)
{
        return device->priv->props->job_cur_task_percentage;
}

/* -------------------------------------------------------------------------------- */

void
gdu_device_job_set_failed (GduDevice *device, GError *error)
{
        g_free (device->priv->job_last_error_message);
        device->priv->job_last_error_message = g_strdup (error->message);
        g_signal_emit (device, signals[JOB_CHANGED], 0);
}

const char *
gdu_device_job_get_last_error_message (GduDevice *device)
{
        return device->priv->job_last_error_message;
}

void
gdu_device_job_clear_last_error_message (GduDevice *device)
{
        g_free (device->priv->job_last_error_message);
        device->priv->job_last_error_message = NULL;
}

/* -------------------------------------------------------------------------------- */

typedef struct {
        GduDevice *device;
        GduDeviceMkfsCompletedFunc callback;
        gpointer user_data;
} MkfsData;

static void
op_mkfs_cb (DBusGProxy *proxy, GError *error, gpointer user_data)
{
        MkfsData *data = user_data;

        if (error != NULL) {
                g_warning ("op_mkfs_cb failed: %s", error->message);
        }
        data->callback (data->device, error, data->user_data);
        g_object_unref (data->device);
        g_free (data);
}

void
gdu_device_op_mkfs (GduDevice   *device,
                    const char  *fstype,
                    const char  *fslabel,
                    const char  *fserase,
                    const char  *encrypt_passphrase,
                    GduDeviceMkfsCompletedFunc  callback,
                    gpointer                    user_data)
{
        int n;
        MkfsData *data;
        char *options[16];

        data = g_new0 (MkfsData, 1);
        data->device = g_object_ref (device);
        data->callback = callback;
        data->user_data = user_data;

        n = 0;
        if (fslabel != NULL && strlen (fslabel) > 0) {
                options[n++] = g_strdup_printf ("label=%s", fslabel);
        }
        if (fserase != NULL && strlen (fserase) > 0) {
                options[n++] = g_strdup_printf ("erase=%s", fserase);
        }
        if (encrypt_passphrase != NULL && strlen (encrypt_passphrase) > 0) {
                options[n++] = g_strdup_printf ("encrypt=%s", encrypt_passphrase);
        }
        options[n] = NULL;

        org_freedesktop_DeviceKit_Disks_Device_create_filesystem_async (device->priv->proxy,
                                                                        fstype,
                                                                        (const char **) options,
                                                                        op_mkfs_cb,
                                                                        data);
        while (n >= 0)
                g_free (options[n--]);
}

/* -------------------------------------------------------------------------------- */

static void
op_mount_cb (DBusGProxy *proxy, char *mount_path, GError *error, gpointer user_data)
{
        GduDevice *device = GDU_DEVICE (user_data);
        if (error != NULL) {
                g_warning ("op_mount_cb failed: %s", error->message);
                gdu_device_job_set_failed (device, error);
                g_error_free (error);
        } else {
                g_print ("yay mounted at '%s'\n", mount_path);
                g_free (mount_path);
        }
        g_object_unref (device);
}

void
gdu_device_op_mount (GduDevice *device)
{
        const char *fstype;
        char *options[16];

        options[0] = NULL;
        fstype = NULL;

        org_freedesktop_DeviceKit_Disks_Device_mount_async (device->priv->proxy,
                                                            fstype,
                                                            (const char **) options,
                                                            op_mount_cb,
                                                            g_object_ref (device));
}

/* -------------------------------------------------------------------------------- */

static void
op_unmount_cb (DBusGProxy *proxy, GError *error, gpointer user_data)
{
        GduDevice *device = GDU_DEVICE (user_data);
        if (error != NULL) {
                g_warning ("op_unmount_cb failed: %s", error->message);
                gdu_device_job_set_failed (device, error);
                g_error_free (error);
        }
        g_object_unref (device);
}

void
gdu_device_op_unmount (GduDevice *device)
{
        char *options[16];
        options[0] = NULL;
        org_freedesktop_DeviceKit_Disks_Device_unmount_async (device->priv->proxy,
                                                              (const char **) options,
                                                              op_unmount_cb,
                                                              g_object_ref (device));
}

/* -------------------------------------------------------------------------------- */

static void
op_delete_partition_cb (DBusGProxy *proxy, GError *error, gpointer user_data)
{
        GduDevice *device = GDU_DEVICE (user_data);
        if (error != NULL) {
                g_warning ("op_delete_partition_cb failed: %s", error->message);
                gdu_device_job_set_failed (device, error);
                g_error_free (error);
        }
        g_object_unref (device);
}

void
gdu_device_op_delete_partition (GduDevice *device, const char *secure_erase)
{
        int n;
        char *options[16];

        n = 0;
        if (secure_erase != NULL && strlen (secure_erase) > 0) {
                options[n++] = g_strdup_printf ("erase=%s", secure_erase);
        }
        options[n] = NULL;

        org_freedesktop_DeviceKit_Disks_Device_delete_partition_async (device->priv->proxy,
                                                                       (const char **) options,
                                                                       op_delete_partition_cb,
                                                                       g_object_ref (device));

        while (n >= 0)
                g_free (options[n--]);
}

/* -------------------------------------------------------------------------------- */

typedef struct {
        GduDevice *device;
        GduDeviceCreatePartitionCompletedFunc callback;
        gpointer user_data;
} CreatePartitionData;

static void
op_create_partition_cb (DBusGProxy *proxy, char *created_device_object_path, GError *error, gpointer user_data)
{
        CreatePartitionData *data = user_data;

        if (error != NULL) {
                g_warning ("op_create_partition_cb failed: %s", error->message);
                data->callback (data->device, NULL, error, data->user_data);
        } else {
                g_print ("yay objpath='%s'\n", created_device_object_path);
                data->callback (data->device, created_device_object_path, error, data->user_data);
                g_free (created_device_object_path);
        }
        g_object_unref (data->device);
        g_free (data);
}

void
gdu_device_op_create_partition (GduDevice   *device,
                                guint64      offset,
                                guint64      size,
                                const char  *type,
                                const char  *label,
                                char       **flags,
                                const char  *fstype,
                                const char  *fslabel,
                                const char  *fserase,
                                const char  *encrypt_passphrase,
                                GduDeviceCreatePartitionCompletedFunc callback,
                                gpointer user_data)
{
        int n;
        char *fsoptions[16];
        char *options[16];
        CreatePartitionData *data;

        data = g_new0 (CreatePartitionData, 1);
        data->device = g_object_ref (device);
        data->callback = callback;
        data->user_data = user_data;

        options[0] = NULL;

        n = 0;
        if (fslabel != NULL && strlen (fslabel) > 0) {
                fsoptions[n++] = g_strdup_printf ("label=%s", fslabel);
        }
        if (fserase != NULL && strlen (fserase) > 0) {
                fsoptions[n++] = g_strdup_printf ("erase=%s", fserase);
        }
        if (encrypt_passphrase != NULL && strlen (encrypt_passphrase) > 0) {
                fsoptions[n++] = g_strdup_printf ("encrypt=%s", encrypt_passphrase);
        }
        fsoptions[n] = NULL;

        org_freedesktop_DeviceKit_Disks_Device_create_partition_async (device->priv->proxy,
                                                                       offset,
                                                                       size,
                                                                       type,
                                                                       label,
                                                                       (const char **) flags,
                                                                       (const char **) options,
                                                                       fstype,
                                                                       (const char **) fsoptions,
                                                                       op_create_partition_cb,
                                                                       data);

        while (n >= 0)
                g_free (fsoptions[n--]);
}

/* -------------------------------------------------------------------------------- */

static void
op_modify_partition_cb (DBusGProxy *proxy, GError *error, gpointer user_data)
{
        GduDevice *device = GDU_DEVICE (user_data);
        if (error != NULL) {
                g_warning ("op_modify_partition_cb failed: %s", error->message);
                gdu_device_job_set_failed (device, error);
                g_error_free (error);
        }
        g_object_unref (device);
}

void
gdu_device_op_modify_partition (GduDevice   *device,
                                const char  *type,
                                const char  *label,
                                char       **flags)
{
        org_freedesktop_DeviceKit_Disks_Device_modify_partition_async (device->priv->proxy,
                                                                       type,
                                                                       label,
                                                                       (const char **) flags,
                                                                       op_modify_partition_cb,
                                                                       g_object_ref (device));
}

/* -------------------------------------------------------------------------------- */

static void
op_create_partition_table_cb (DBusGProxy *proxy, GError *error, gpointer user_data)
{
        GduDevice *device = GDU_DEVICE (user_data);
        if (error != NULL) {
                g_warning ("op_create_partition_table_cb failed: %s", error->message);
                gdu_device_job_set_failed (device, error);
                g_error_free (error);
        }
        g_object_unref (device);
}

void
gdu_device_op_create_partition_table (GduDevice  *device,
                                      const char *scheme,
                                      const char *secure_erase)
{
        int n;
        char *options[16];

        n = 0;
        if (secure_erase != NULL && strlen (secure_erase) > 0) {
                options[n++] = g_strdup_printf ("erase=%s", secure_erase);
        }
        options[n] = NULL;

        org_freedesktop_DeviceKit_Disks_Device_create_partition_table_async (device->priv->proxy,
                                                                             scheme,
                                                                             (const char **) options,
                                                                             op_create_partition_table_cb,
                                                                             g_object_ref (device));

        while (n >= 0)
                g_free (options[n--]);
}

/* -------------------------------------------------------------------------------- */

typedef struct {
        GduDevice *device;

        GduDeviceUnlockEncryptedCompletedFunc callback;
        gpointer user_data;
} UnlockData;

static void
op_unlock_encrypted_cb (DBusGProxy *proxy, char *cleartext_object_path, GError *error, gpointer user_data)
{
        UnlockData *data = user_data;

        if (error != NULL) {
                g_warning ("op_unlock_encrypted_cb failed: %s", error->message);
                data->callback (data->device, NULL, error, data->user_data);
        } else {
                g_print ("yay cleartext object is at '%s'\n", cleartext_object_path);
                data->callback (data->device, cleartext_object_path, error, data->user_data);
                g_free (cleartext_object_path);
        }
        g_object_unref (data->device);
        g_free (data);
}

void
gdu_device_op_unlock_encrypted (GduDevice *device,
                                const char *secret,
                                GduDeviceUnlockEncryptedCompletedFunc callback,
                                gpointer user_data)
{
        UnlockData *data;
        char *options[16];
        options[0] = NULL;

        data = g_new0 (UnlockData, 1);
        data->device = g_object_ref (device);
        data->callback = callback;
        data->user_data = user_data;

        org_freedesktop_DeviceKit_Disks_Device_unlock_encrypted_async (device->priv->proxy,
                                                                       secret,
                                                                       (const char **) options,
                                                                       op_unlock_encrypted_cb,
                                                                       data);
}

/* -------------------------------------------------------------------------------- */

typedef struct {
        GduDevice *device;

        GduDeviceChangeSecretForEncryptedCompletedFunc callback;
        gpointer user_data;
} ChangeSecretData;

static void
op_change_secret_for_encrypted_cb (DBusGProxy *proxy, GError *error, gpointer user_data)
{
        ChangeSecretData *data = user_data;

        if (error != NULL) {
                g_warning ("op_change_secret_for_encrypted_cb failed: %s", error->message);
                data->callback (data->device, FALSE, error, data->user_data);
        } else {
                data->callback (data->device, TRUE, error, data->user_data);
        }
        g_object_unref (data->device);
        g_free (data);
}

void
gdu_device_op_change_secret_for_encrypted (GduDevice   *device,
                                           const char  *old_secret,
                                           const char  *new_secret,
                                           GduDeviceChangeSecretForEncryptedCompletedFunc callback,
                                           gpointer user_data)
{
        ChangeSecretData *data;

        data = g_new0 (ChangeSecretData, 1);
        data->device = g_object_ref (device);
        data->callback = callback;
        data->user_data = user_data;

        org_freedesktop_DeviceKit_Disks_Device_change_secret_for_encrypted_async (device->priv->proxy,
                                                                                  old_secret,
                                                                                  new_secret,
                                                                                  op_change_secret_for_encrypted_cb,
                                                                                  data);
}

/* -------------------------------------------------------------------------------- */

static void
op_lock_encrypted_cb (DBusGProxy *proxy, GError *error, gpointer user_data)
{
        GduDevice *device = GDU_DEVICE (user_data);
        if (error != NULL) {
                g_warning ("op_unlock_encrypted_cb failed: %s", error->message);
                gdu_device_job_set_failed (device, error);
                g_error_free (error);
        }
        g_object_unref (device);
}

void
gdu_device_op_lock_encrypted (GduDevice *device)
{
        char *options[16];
        options[0] = NULL;
        org_freedesktop_DeviceKit_Disks_Device_lock_encrypted_async (device->priv->proxy,
                                                                     (const char **) options,
                                                                     op_lock_encrypted_cb,
                                                                     g_object_ref (device));
}

/* -------------------------------------------------------------------------------- */

static void
op_change_fs_label_cb (DBusGProxy *proxy, GError *error, gpointer user_data)
{
        GduDevice *device = GDU_DEVICE (user_data);
        if (error != NULL) {
                g_warning ("op_change_fs_label_cb failed: %s", error->message);
                gdu_device_job_set_failed (device, error);
                g_error_free (error);
        }
        g_object_unref (device);
}

void
gdu_device_op_change_filesystem_label (GduDevice *device, const char *new_label)
{
        org_freedesktop_DeviceKit_Disks_Device_change_filesystem_label_async (device->priv->proxy,
                                                                              new_label,
                                                                              op_change_fs_label_cb,
                                                                              g_object_ref (device));
}
/* -------------------------------------------------------------------------------- */

typedef struct {
        GduDevice *device;

        GduDeviceRetrieveSmartDataCompletedFunc callback;
        gpointer user_data;
} RetrieveSmartDataData;

static void
op_retrieve_smart_data_cb (DBusGProxy *proxy,
                           gboolean passed, int power_on_hours, int temperature,
                           GError *error, gpointer user_data)
{
        RetrieveSmartDataData *data = user_data;

        /* update cache */
        data->device->priv->smart_data_cache_timestamp = time (NULL);
        data->device->priv->smart_data_cache_passed = passed;
        data->device->priv->smart_data_cache_power_on_hours = power_on_hours;
        data->device->priv->smart_data_cache_temperature = temperature;
        if (data->device->priv->smart_data_cache_error != NULL)
                g_error_free (data->device->priv->smart_data_cache_error);
        data->device->priv->smart_data_cache_error = error != NULL ? g_error_copy (error) : NULL;

        if (error != NULL) {
                /* g_warning ("op_retrieve_smart_data_cb failed: %s", error->message); */
                data->callback (data->device, FALSE, 0, 0, error, data->user_data);
        } else {
                data->callback (data->device, passed, power_on_hours, temperature, NULL, data->user_data);
        }
        g_object_unref (data->device);
        g_free (data);
}

void
gdu_device_retrieve_smart_data (GduDevice                              *device,
                                GduDeviceRetrieveSmartDataCompletedFunc callback,
                                gpointer                                user_data)
{
        RetrieveSmartDataData *data;

        data = g_new0 (RetrieveSmartDataData, 1);
        data->device = g_object_ref (device);
        data->callback = callback;
        data->user_data = user_data;

        org_freedesktop_DeviceKit_Disks_Device_retrieve_smart_data_async (device->priv->proxy,
                                                                          op_retrieve_smart_data_cb,
                                                                          data);
}

gboolean
gdu_device_smart_data_is_cached (GduDevice *device, int *age_in_seconds)
{
        time_t now;
        gboolean ret;

        ret = FALSE;

        if (device->priv->smart_data_cache_timestamp == 0)
                goto out;

        now = time (NULL);

        if (age_in_seconds != NULL)
                *age_in_seconds = now - device->priv->smart_data_cache_timestamp;

        ret = TRUE;

out:
        return ret;
}

void
gdu_device_smart_data_purge_cache (GduDevice *device)
{
        device->priv->smart_data_cache_timestamp = 0;
        if (device->priv->smart_data_cache_error != NULL)
                g_error_free (device->priv->smart_data_cache_error);
        device->priv->smart_data_cache_error = NULL;
}

gboolean
gdu_device_retrieve_smart_data_from_cache (GduDevice *device,
                                           GduDeviceRetrieveSmartDataCompletedFunc callback,
                                           gpointer                                user_data)
{
        gboolean ret;

        ret = FALSE;

        if (device->priv->smart_data_cache_timestamp == 0)
                goto out;

        callback (device,
                  device->priv->smart_data_cache_passed,
                  device->priv->smart_data_cache_power_on_hours,
                  device->priv->smart_data_cache_temperature,
                  device->priv->smart_data_cache_error != NULL ?
                      g_error_copy (device->priv->smart_data_cache_error) : NULL,
                  user_data);

        ret = TRUE;

out:
        return ret;
}

/* -------------------------------------------------------------------------------- */

static void
op_run_smart_selftest_cb (DBusGProxy *proxy, GError *error, gpointer user_data)
{
        GduDevice *device = GDU_DEVICE (user_data);
        if (error != NULL) {
                g_warning ("op_run_smart_selftest_cb failed: %s", error->message);
                gdu_device_job_set_failed (device, error);
                g_error_free (error);
        }
        g_object_unref (device);
}

void
gdu_device_op_run_smart_selftest (GduDevice   *device,
                                  const char  *test,
                                  gboolean     captive)
{
        org_freedesktop_DeviceKit_Disks_Device_run_smart_selftest_async (device->priv->proxy,
                                                                         test,
                                                                         captive,
                                                                         op_run_smart_selftest_cb,
                                                                         g_object_ref (device));
}

/* -------------------------------------------------------------------------------- */

static void
op_stop_linux_md_array_cb (DBusGProxy *proxy, GError *error, gpointer user_data)
{
        GduDevice *device = GDU_DEVICE (user_data);
        if (error != NULL) {
                g_warning ("op_stop_linux_md_array_cb failed: %s", error->message);
                gdu_device_job_set_failed (device, error);
                g_error_free (error);
        }
        g_object_unref (device);
}

void
gdu_device_op_stop_linux_md_array (GduDevice *device)
{
        char *options[16];

        options[0] = NULL;

        org_freedesktop_DeviceKit_Disks_Device_stop_linux_md_array_async (device->priv->proxy,
                                                                          (const char **) options,
                                                                          op_stop_linux_md_array_cb,
                                                                          g_object_ref (device));
}

/* -------------------------------------------------------------------------------- */

static void
op_add_component_to_linux_md_array_cb (DBusGProxy *proxy, GError *error, gpointer user_data)
{
        GduDevice *device = GDU_DEVICE (user_data);
        if (error != NULL) {
                g_warning ("op_add_component_to_linux_md_array_cb failed: %s", error->message);
                gdu_device_job_set_failed (device, error);
                g_error_free (error);
        }
        g_object_unref (device);
}

void
gdu_device_op_add_component_to_linux_md_array (GduDevice *device, const char *component_objpath)
{
        char *options[16];

        options[0] = NULL;

        org_freedesktop_DeviceKit_Disks_Device_add_component_to_linux_md_array_async (
                device->priv->proxy,
                component_objpath,
                (const char **) options,
                op_add_component_to_linux_md_array_cb,
                g_object_ref (device));
}

/* -------------------------------------------------------------------------------- */

static void
op_remove_component_from_linux_md_array_cb (DBusGProxy *proxy, GError *error, gpointer user_data)
{
        GduDevice *device = GDU_DEVICE (user_data);
        if (error != NULL) {
                g_warning ("op_remove_component_from_linux_md_array_cb failed: %s", error->message);
                gdu_device_job_set_failed (device, error);
                g_error_free (error);
        }
        g_object_unref (device);
}

void
gdu_device_op_remove_component_from_linux_md_array (GduDevice *device,
                                                    const char *component_objpath,
                                                    const char *secure_erase)
{
        int n;
        char *options[16];

        n = 0;
        if (secure_erase != NULL && strlen (secure_erase) > 0) {
                options[n++] = g_strdup_printf ("erase=%s", secure_erase);
        }
        options[n] = NULL;

        org_freedesktop_DeviceKit_Disks_Device_remove_component_from_linux_md_array_async (
                device->priv->proxy,
                component_objpath,
                (const char **) options,
                op_remove_component_from_linux_md_array_cb,
                g_object_ref (device));

        while (n >= 0)
                g_free (options[n--]);
}

/* -------------------------------------------------------------------------------- */

void
gdu_device_op_cancel_job (GduDevice *device)
{
        GError *error = NULL;
        if (!org_freedesktop_DeviceKit_Disks_Device_cancel_job (device->priv->proxy, &error)) {
                g_warning ("error cancelling op: %s", error->message);
                g_error_free (error);
        }
}
