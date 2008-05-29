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
#include <stdlib.h>
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

typedef struct {
        int id;
        char *desc;
        int flags;
        int value;
        int worst;
        int threshold;
        char *raw;
} DeviceSmartAttribute;

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

        gboolean               drive_smart_is_capable;
        gboolean               drive_smart_is_enabled;
        guint64                drive_smart_time_collected;
        gboolean               drive_smart_is_failing;
        double                 drive_smart_temperature;
        guint64                drive_smart_time_powered_on;
        char                  *drive_smart_last_self_test_result;
        int                    num_drive_smart_attributes;
        DeviceSmartAttribute  *drive_smart_attributes;

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

#define SMART_DATA_STRUCT_TYPE (dbus_g_type_get_struct ("GValueArray",   \
                                                        G_TYPE_INT,      \
                                                        G_TYPE_STRING,   \
                                                        G_TYPE_INT,      \
                                                        G_TYPE_INT,      \
                                                        G_TYPE_INT,      \
                                                        G_TYPE_INT,      \
                                                        G_TYPE_STRING,   \
                                                        G_TYPE_INVALID))

#define HISTORICAL_SMART_DATA_STRUCT_TYPE (dbus_g_type_get_struct ("GValueArray",   \
                                                                   G_TYPE_UINT64, \
                                                                   G_TYPE_DOUBLE, \
                                                                   G_TYPE_UINT64, \
                                                                   G_TYPE_STRING, \
                                                                   G_TYPE_BOOLEAN, \
                                                                   dbus_g_type_get_collection ("GPtrArray", SMART_DATA_STRUCT_TYPE), \
                                                                   G_TYPE_INVALID))

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

        else if (strcmp (key, "drive-smart-is-capable") == 0)
                props->drive_smart_is_capable = g_value_get_boolean (value);
        else if (strcmp (key, "drive-smart-is-enabled") == 0)
                props->drive_smart_is_enabled = g_value_get_boolean (value);
        else if (strcmp (key, "drive-smart-time-collected") == 0)
                props->drive_smart_time_collected = g_value_get_uint64 (value);
        else if (strcmp (key, "drive-smart-is-failing") == 0)
                props->drive_smart_is_failing = g_value_get_boolean (value);
        else if (strcmp (key, "drive-smart-temperature") == 0)
                props->drive_smart_temperature = g_value_get_double (value);
        else if (strcmp (key, "drive-smart-time-powered-on") == 0)
                props->drive_smart_time_powered_on = g_value_get_uint64 (value);
        else if (strcmp (key, "drive-smart-last-self-test-result") == 0)
                props->drive_smart_last_self_test_result = g_strdup (g_value_get_string (value));
        else if (strcmp (key, "drive-smart-attributes") == 0) {
                GPtrArray *p = g_value_get_boxed (value);
                int n;
                props->num_drive_smart_attributes = (int) p->len;
                props->drive_smart_attributes = g_new0 (DeviceSmartAttribute, props->num_drive_smart_attributes);
                for (n = 0; n < (int) p->len; n++) {
                        DeviceSmartAttribute *a = props->drive_smart_attributes + n;
                        GValue elem = {0};
                        g_value_init (&elem, SMART_DATA_STRUCT_TYPE);
                        g_value_set_static_boxed (&elem, p->pdata[n]);
                        dbus_g_type_struct_get (&elem,
                                                0, &(a->id),
                                                1, &(a->desc),
                                                2, &(a->flags),
                                                3, &(a->value),
                                                4, &(a->worst),
                                                5, &(a->threshold),
                                                6, &(a->raw),
                                                G_MAXUINT);
                }
        }

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
        int n;

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
        g_free (props->drive_smart_last_self_test_result);
        for (n = 0; n < props->num_drive_smart_attributes; n++) {
                g_free (props->drive_smart_attributes[n].desc);
                g_free (props->drive_smart_attributes[n].raw);
        }
        g_free (props->drive_smart_attributes);
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

        DeviceProperties *props;
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

gboolean
gdu_device_drive_smart_get_is_capable (GduDevice *device)
{
        return device->priv->props->drive_smart_is_capable;
}

gboolean
gdu_device_drive_smart_get_is_enabled (GduDevice *device)
{
        return device->priv->props->drive_smart_is_enabled;
}

guint64
gdu_device_drive_smart_get_time_collected (GduDevice *device)
{
        return device->priv->props->drive_smart_time_collected;
}

gboolean
gdu_device_drive_smart_get_is_failing (GduDevice *device,
                                       gboolean  *out_attr_warn,
                                       gboolean  *out_attr_fail)
{
        int n;
        gboolean warn, fail;

        warn = FALSE;
        fail = FALSE;
        for (n = 0; n < device->priv->props->num_drive_smart_attributes; n++) {
                GduDeviceSmartAttribute *attr =
                        ((GduDeviceSmartAttribute *) device->priv->props->drive_smart_attributes) + n;

                if (attr->value < attr->threshold)
                        fail = TRUE;

                if (!warn)
                        gdu_device_smart_attribute_get_details (attr, NULL, NULL, &warn);
        }

        if (out_attr_warn != NULL)
                *out_attr_warn = warn;
        if (out_attr_fail != NULL)
                *out_attr_fail = fail;
        return device->priv->props->drive_smart_is_failing;
}

double
gdu_device_drive_smart_get_temperature (GduDevice *device)
{
        return device->priv->props->drive_smart_temperature;
}

guint64
gdu_device_drive_smart_get_time_powered_on (GduDevice *device)
{
        return device->priv->props->drive_smart_time_powered_on;
}

const char *
gdu_device_drive_smart_get_last_self_test_result (GduDevice *device)
{
        return device->priv->props->drive_smart_last_self_test_result;
}

GduDeviceSmartAttribute *
gdu_device_drive_smart_get_attributes (GduDevice *device, int *num_attributes)
{
        g_return_val_if_fail (num_attributes != NULL, NULL);
        *num_attributes = device->priv->props->num_drive_smart_attributes;
        /* Keep GduDeviceSmartAttribute in sync with DeviceSmartAttribute */
        return (GduDeviceSmartAttribute *) device->priv->props->drive_smart_attributes;
}

void
gdu_device_smart_attribute_get_details (GduDeviceSmartAttribute  *attr,
                                        char                    **out_name,
                                        char                    **out_description,
                                        gboolean                 *out_should_warn)
{
        const char *n;
        const char *d;
        gboolean warn;
        int raw_int;

        raw_int = atoi (attr->raw);

        /* See http://smartmontools.sourceforge.net/doc.html
         *     http://en.wikipedia.org/wiki/S.M.A.R.T
         *     http://www.t13.org/Documents/UploadedDocuments/docs2005/e05148r0-ACS-SMARTAttributesAnnex.pdf
         */

        n = NULL;
        d = NULL;
        warn = FALSE;
        switch (attr->id) {
        case 1:
                n = _("Read Error Rate");
                d = _("Frequency of errors while reading raw data from the disk. "
                      "A non-zero value indicates a problem with "
                      "either the disk surface or read/write heads.");
                break;
        case 2:
                n = _("Throughput Performance");
                d = _("Average effeciency of the disk.");
                break;
        case 3:
                n = _("Spinup Time");
                d = _("Time needed to spin up the disk.");
                break;
        case 4:
                n = _("Start/Stop Count");
                d = _("Number of spindle start/stop cycles.");
                break;
        case 5:
                n = _("Reallocated Sector Count");
                d = _("Count of remapped sectors. "
                      "When the hard drive finds a read/write/verification error, it mark the sector "
                      "as \"reallocated\" and transfers data to a special reserved area (spare area).");
                break;
        case 7:
                n = _("Seek Error Rate");
                d = _("Frequency of errors while positioning.");
                break;
        case 8:
                n = _("Seek Timer Performance");
                d = _("Average efficiency of operatings while positioning");
                break;
        case 9:
                n = _("Power-On Hours");
                d = _("Number of hours elapsed in the power-on state.");
                break;
        case 10:
                n = _("Spinup Retry Count");
                d = _("Number of retry attempts to spin up.");
                break;
        case 11:
                n = _("Calibration Retry Count");
                d = _("Number of attempts to calibrate the device.");
                break;
        case 12:
                n = _("Power Cycle Count");
                d = _("Number of power-on events.");
                break;
        case 13:
                n = _("Soft read error rate");
                d = _("Frequency of 'program' errors while reading from the disk.");
                break;

        case 191:
                n = _("G-sense Error Rate");
                d = _("Frequency of mistakes as a result of impact loads.");
                break;
        case 192:
                n = _("Power-off Retract Count");
                d = _("Number of power-off or emergency retract cycles.");
                break;
        case 193:
                n = _("Load/Unload Cycle Count");
                d = _("Number of cycles into landing zone position.");
                break;
        case 194:
                n = _("Temperature");
                d = _("Current internal temperature in degrees Celcius.");
                break;
        case 195:
                n = _("Hardware ECC Recovered");
                d = _("Number of ECC on-the-fly errors.");
                break;
        case 196:
                n = _("Reallocation Count");
                d = _("Number of remapping operations. "
                      "The raw value of this attribute shows the total number of (successful "
                      "and unsucessful) attempts to transfer data from reallocated sectors "
                      "to a spare area.");
                break;
        case 197:
                n = _("Current Pending Sector Count");
                d = _("Number of sectors waiting to be remapped. "
                      "If the sector waiting to be remapped is subsequently written or read "
                      "successfully, this value is decreased and the sector is not remapped. Read "
                      "errors on the sector will not remap the sector, it will only be remapped on "
                      "a failed write attempt.");
                if (raw_int > 0)
                        warn = TRUE;
                break;
        case 198:
                n = _("Uncorrectable Sector Count");
                d = _("The total number of uncorrectable errors when reading/writing a sector. "
                      "A rise in the value of this attribute indicates defects of the "
                      "disk surface and/or problems in the mechanical subsystem.");
                break;
        case 199:
                n = _("UDMA CRC Error Rate");
                d = _("Number of CRC errors during UDMA mode.");
                break;
        case 200:
                n = _("Write Error Rate");
                d = _("Number of errors while writing to disk (or) multi-zone error rate (or) flying-height.");
                break;
        case 201:
                n = _("Soft Read Error Rate");
                d = _("Number of off-track errors.");
                break;
        case 202:
                n = _("Data Address Mark Errors");
                d = _("Number of Data Address Mark (DAM) errors (or) vendor-specific.");
                break;
        case 203:
                n = _("Run Out Cancel");
                d = _("Number of ECC errors.");
                break;
        case 204:
                n = _("Soft ECC correction");
                d = _("Number of errors corrected by software ECC.");
                break;
        case 205:
                n = _("Thermal Asperity Rate");
                d = _("Number of Thermal Asperity Rate errors.");
                break;
        case 206:
                n = _("Flying Height");
                d = _("Height of heads above the disk surface.");
                break;
        case 207:
                n = _("Spin High Current");
                d = _("Amount of high current used to spin up the drive.");
                break;
        case 208:
                n = _("Spin Buzz");
                d = _("Number of buzz routines to spin up the drive.");
                break;
        case 209:
                n = _("Offline Seek Performance");
                d = _("Drive's seek performance during offline operations.");
                break;

        case 220:
                n = _("Disk Shift");
                d = _("Shift of disk os possible as a result of strong shock loading in the store, "
                      "as a result of falling (or) temperature.");
                break;
        case 221:
                n = _("G-sense Error Rate");
                d = _("Number of errors as a result of impact loads as detected by a shock sensor.");
                break;
        case 222:
                n = _("Loaded Hours");
                d = _("Number of hours in general operational state.");
                break;
        case 223:
                n = _("Load/Unload Retry Count");
                d = _("Loading on drive caused by numerous recurrences of operations, like reading, "
                      "recording, positioning of heads, etc.");
                break;
        case 224:
                n = _("Load Friction");
                d = _("Load on drive cause by friction in mechanical parts of the store.");
                break;
        case 225:
                n = _("Load/Unload Cycle Count");
                d = _("Total number of load cycles.");
                break;
        case 226:
                n = _("Load-in Time");
                d = _("General time for loading in a drive.");
                break;
        case 227:
                n = _("Torque Amplification Count");
                d = _("Quantity efforts of the rotating moment of a drive.");
                break;
        case 228:
                n = _("Power-off Retract Count");
                d = _("Number of power-off retract events.");
                break;

        case 230:
                n = _("GMR Head Amplitude");
                d = _("Amplitude of heads trembling (GMR-head) in running mode.");
                break;
        case 231:
                n = _("Temperature");
                d = _("Temperature of the drive.");
                break;

        case 240:
                n = _("Head Flying Hours");
                d = _("Time while head is positioning.");
                break;
        case 250:
                n = _("Read Error Retry Rate");
                d = _("Number of errors while reading from a disk.");
                break;
        default:
                break;
        }

        if (n != NULL && d != NULL) {
                if (out_name != NULL)
                        *out_name = g_strdup (n);
                if (out_description != NULL)
                        *out_description = g_strdup (d);
        } else {
                if (out_name != NULL)
                        *out_name = g_strdup (attr->desc);
                if (out_description != NULL)
                        *out_description = g_strdup_printf (_("No description for attribute %d."), attr->id);
        }
        if (out_should_warn != NULL)
                *out_should_warn = warn;
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

        org_freedesktop_DeviceKit_Disks_Device_filesystem_create_async (device->priv->proxy,
                                                                        fstype,
                                                                        (const char **) options,
                                                                        op_mkfs_cb,
                                                                        data);
        while (n >= 0)
                g_free (options[n--]);
}

/* -------------------------------------------------------------------------------- */

typedef struct {
        GduDevice *device;
        GduDeviceMountCompletedFunc callback;
        gpointer user_data;
} MountData;

static void
op_mount_cb (DBusGProxy *proxy, char *mount_path, GError *error, gpointer user_data)
{
        MountData *data = user_data;
        if (data->callback != NULL)
                data->callback (data->device, g_strdup (mount_path), error, data->user_data);
        g_free (mount_path);
        g_object_unref (data->device);
        g_free (data);
}

void
gdu_device_op_mount (GduDevice                   *device,
                     GduDeviceMountCompletedFunc  callback,
                     gpointer                     user_data)
{
        const char *fstype;
        char *options[16];
        MountData *data;

        data = g_new0 (MountData, 1);
        data->device = g_object_ref (device);
        data->callback = callback;
        data->user_data = user_data;

        options[0] = NULL;
        fstype = NULL;

        org_freedesktop_DeviceKit_Disks_Device_filesystem_mount_async (device->priv->proxy,
                                                                       fstype,
                                                                       (const char **) options,
                                                                       op_mount_cb,
                                                                       data);
}

/* -------------------------------------------------------------------------------- */

typedef struct {
        GduDevice *device;
        GduDeviceUnmountCompletedFunc callback;
        gpointer user_data;
} UnmountData;

static void
op_unmount_cb (DBusGProxy *proxy, GError *error, gpointer user_data)
{
        UnmountData *data = user_data;
        if (data->callback != NULL)
                data->callback (data->device, error, data->user_data);
        g_object_unref (data->device);
        g_free (data);
}

void
gdu_device_op_unmount (GduDevice                     *device,
                       GduDeviceUnmountCompletedFunc  callback,
                       gpointer                       user_data)
{
        char *options[16];
        UnmountData *data;

        data = g_new0 (UnmountData, 1);
        data->device = g_object_ref (device);
        data->callback = callback;
        data->user_data = user_data;
        options[0] = NULL;

        org_freedesktop_DeviceKit_Disks_Device_filesystem_unmount_async (device->priv->proxy,
                                                                         (const char **) options,
                                                                         op_unmount_cb,
                                                                         data);
}

/* -------------------------------------------------------------------------------- */

typedef struct {
        GduDevice *device;
        GduDeviceDeletePartitionCompletedFunc callback;
        gpointer user_data;
} DeletePartitionData;

static void
op_delete_partition_cb (DBusGProxy *proxy, GError *error, gpointer user_data)
{
        DeletePartitionData *data = user_data;
        if (data->callback != NULL)
                data->callback (data->device, error, data->user_data);
        g_object_unref (data->device);
        g_free (data);
}

void
gdu_device_op_delete_partition (GduDevice                             *device,
                                const char                            *secure_erase,
                                GduDeviceDeletePartitionCompletedFunc  callback,
                                gpointer                               user_data)
{
        int n;
        char *options[16];
        DeletePartitionData *data;

        data = g_new0 (DeletePartitionData, 1);
        data->device = g_object_ref (device);
        data->callback = callback;
        data->user_data = user_data;

        n = 0;
        if (secure_erase != NULL && strlen (secure_erase) > 0) {
                options[n++] = g_strdup_printf ("erase=%s", secure_erase);
        }
        options[n] = NULL;

        org_freedesktop_DeviceKit_Disks_Device_partition_delete_async (device->priv->proxy,
                                                                       (const char **) options,
                                                                       op_delete_partition_cb,
                                                                       data);

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

        org_freedesktop_DeviceKit_Disks_Device_partition_create_async (device->priv->proxy,
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

typedef struct {
        GduDevice *device;
        GduDeviceModifyPartitionCompletedFunc callback;
        gpointer user_data;
} ModifyPartitionData;

static void
op_modify_partition_cb (DBusGProxy *proxy, GError *error, gpointer user_data)
{
        ModifyPartitionData *data = user_data;
        if (data->callback != NULL)
                data->callback (data->device, error, data->user_data);
        g_object_unref (data->device);
        g_free (data);
}

void
gdu_device_op_modify_partition (GduDevice                             *device,
                                const char                            *type,
                                const char                            *label,
                                char                                 **flags,
                                GduDeviceModifyPartitionCompletedFunc  callback,
                                gpointer                               user_data)
{
        ModifyPartitionData *data;

        data = g_new0 (ModifyPartitionData, 1);
        data->device = g_object_ref (device);
        data->callback = callback;
        data->user_data = user_data;

        org_freedesktop_DeviceKit_Disks_Device_partition_modify_async (device->priv->proxy,
                                                                       type,
                                                                       label,
                                                                       (const char **) flags,
                                                                       op_modify_partition_cb,
                                                                       data);
}

/* -------------------------------------------------------------------------------- */

typedef struct {
        GduDevice *device;
        GduDeviceCreatePartitionTableCompletedFunc callback;
        gpointer user_data;
} CreatePartitionTableData;

static void
op_create_partition_table_cb (DBusGProxy *proxy, GError *error, gpointer user_data)
{
        CreatePartitionTableData *data = user_data;
        if (data->callback != NULL)
                data->callback (data->device, error, data->user_data);
        g_object_unref (data->device);
        g_free (data);
}

void
gdu_device_op_create_partition_table (GduDevice                                  *device,
                                      const char                                 *scheme,
                                      const char                                 *secure_erase,
                                      GduDeviceCreatePartitionTableCompletedFunc  callback,
                                      gpointer                                    user_data)
{
        int n;
        char *options[16];
        CreatePartitionTableData *data;

        data = g_new0 (CreatePartitionTableData, 1);
        data->device = g_object_ref (device);
        data->callback = callback;
        data->user_data = user_data;

        n = 0;
        if (secure_erase != NULL && strlen (secure_erase) > 0) {
                options[n++] = g_strdup_printf ("erase=%s", secure_erase);
        }
        options[n] = NULL;

        org_freedesktop_DeviceKit_Disks_Device_partition_table_create_async (device->priv->proxy,
                                                                             scheme,
                                                                             (const char **) options,
                                                                             op_create_partition_table_cb,
                                                                             data);

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

        org_freedesktop_DeviceKit_Disks_Device_encrypted_unlock_async (device->priv->proxy,
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
                data->callback (data->device, error, data->user_data);
        } else {
                data->callback (data->device, error, data->user_data);
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

        org_freedesktop_DeviceKit_Disks_Device_encrypted_change_passphrase_async (device->priv->proxy,
                                                                                  old_secret,
                                                                                  new_secret,
                                                                                  op_change_secret_for_encrypted_cb,
                                                                                  data);
}

/* -------------------------------------------------------------------------------- */

typedef struct {
        GduDevice *device;
        GduDeviceLockEncryptedCompletedFunc callback;
        gpointer user_data;
} LockEncryptedData;

static void
op_lock_encrypted_cb (DBusGProxy *proxy, GError *error, gpointer user_data)
{
        LockEncryptedData *data = user_data;
        if (data->callback != NULL)
                data->callback (data->device, error, data->user_data);
        g_object_unref (data->device);
        g_free (data);
}

void
gdu_device_op_lock_encrypted (GduDevice                           *device,
                              GduDeviceLockEncryptedCompletedFunc  callback,
                              gpointer                             user_data)
{
        char *options[16];
        LockEncryptedData *data;

        data = g_new0 (LockEncryptedData, 1);
        data->device = g_object_ref (device);
        data->callback = callback;
        data->user_data = user_data;

        options[0] = NULL;
        org_freedesktop_DeviceKit_Disks_Device_encrypted_lock_async (device->priv->proxy,
                                                                     (const char **) options,
                                                                     op_lock_encrypted_cb,
                                                                     data);
}

/* -------------------------------------------------------------------------------- */

typedef struct {
        GduDevice *device;
        GduDeviceChangeFilesystemLabelCompletedFunc callback;
        gpointer user_data;
} ChangeFilesystemLabelData;

static void
op_change_filesystem_label_cb (DBusGProxy *proxy, GError *error, gpointer user_data)
{
        ChangeFilesystemLabelData *data = user_data;
        if (data->callback != NULL)
                data->callback (data->device, error, data->user_data);
        g_object_unref (data->device);
        g_free (data);
}

void
gdu_device_op_change_filesystem_label (GduDevice                                   *device,
                                       const char                                  *new_label,
                                       GduDeviceChangeFilesystemLabelCompletedFunc  callback,
                                       gpointer                                     user_data)
{
        ChangeFilesystemLabelData *data;

        data = g_new0 (ChangeFilesystemLabelData, 1);
        data->device = g_object_ref (device);
        data->callback = callback;
        data->user_data = user_data;

        org_freedesktop_DeviceKit_Disks_Device_filesystem_set_label_async (device->priv->proxy,
                                                                           new_label,
                                                                           op_change_filesystem_label_cb,
                                                                           data);
}
/* -------------------------------------------------------------------------------- */

typedef struct {
        GduDevice *device;
        GduDeviceDriveSmartRefreshDataCompletedFunc callback;
        gpointer user_data;
} RetrieveSmartDataData;

static void
op_retrieve_smart_data_cb (DBusGProxy *proxy, GError *error, gpointer user_data)
{
        RetrieveSmartDataData *data = user_data;

        if (error != NULL) {
                /*g_warning ("op_retrieve_smart_data_cb failed: %s", error->message);*/
                data->callback (data->device, error, data->user_data);
        } else {
                data->callback (data->device, NULL, data->user_data);
        }
        g_object_unref (data->device);
        g_free (data);
}

void
gdu_device_drive_smart_refresh_data (GduDevice                                  *device,
                                     GduDeviceDriveSmartRefreshDataCompletedFunc callback,
                                     gpointer                                    user_data)
{
        RetrieveSmartDataData *data;
        char *options[16];

        options[0] = NULL;

        data = g_new0 (RetrieveSmartDataData, 1);
        data->device = g_object_ref (device);
        data->callback = callback;
        data->user_data = user_data;

        org_freedesktop_DeviceKit_Disks_Device_drive_smart_refresh_data_async (device->priv->proxy,
                                                                               (const char **) options,
                                                                               op_retrieve_smart_data_cb,
                                                                               data);
}

/* -------------------------------------------------------------------------------- */

typedef struct {
        GduDevice *device;
        GduDeviceRunSmartSelftestCompletedFunc callback;
        gpointer user_data;
} RunSmartSelftestData;

static void
op_run_smart_selftest_cb (DBusGProxy *proxy, GError *error, gpointer user_data)
{
        RunSmartSelftestData *data = user_data;
        if (data->callback != NULL)
                data->callback (data->device, error, data->user_data);
        g_object_unref (data->device);
        g_free (data);
}

void
gdu_device_op_run_smart_selftest (GduDevice                              *device,
                                  const char                             *test,
                                  gboolean                                captive,
                                  GduDeviceRunSmartSelftestCompletedFunc  callback,
                                  gpointer                                user_data)
{
        RunSmartSelftestData *data;

        data = g_new0 (RunSmartSelftestData, 1);
        data->device = g_object_ref (device);
        data->callback = callback;
        data->user_data = user_data;

        org_freedesktop_DeviceKit_Disks_Device_drive_smart_initiate_selftest_async (device->priv->proxy,
                                                                                    test,
                                                                                    captive,
                                                                                    op_run_smart_selftest_cb,
                                                                                    data);
}

/* -------------------------------------------------------------------------------- */

typedef struct {
        GduDevice *device;
        GduDeviceStopLinuxMdArrayCompletedFunc callback;
        gpointer user_data;
} StopLinuxMdArrayData;

static void
op_stop_linux_md_array_cb (DBusGProxy *proxy, GError *error, gpointer user_data)
{
        StopLinuxMdArrayData *data = user_data;
        if (data->callback != NULL)
                data->callback (data->device, error, data->user_data);
        g_object_unref (data->device);
        g_free (data);
}

void
gdu_device_op_stop_linux_md_array (GduDevice                              *device,
                                   GduDeviceStopLinuxMdArrayCompletedFunc  callback,
                                   gpointer                                user_data)
{
        char *options[16];
        StopLinuxMdArrayData *data;

        data = g_new0 (StopLinuxMdArrayData, 1);
        data->device = g_object_ref (device);
        data->callback = callback;
        data->user_data = user_data;

        options[0] = NULL;

        org_freedesktop_DeviceKit_Disks_Device_linux_md_stop_async (device->priv->proxy,
                                                                    (const char **) options,
                                                                    op_stop_linux_md_array_cb,
                                                                    data);
}

/* -------------------------------------------------------------------------------- */

typedef struct {
        GduDevice *device;
        GduDeviceAddComponentToLinuxMdArrayCompletedFunc callback;
        gpointer user_data;
} AddComponentToLinuxMdArrayData;

static void
op_add_component_to_linux_md_array_cb (DBusGProxy *proxy, GError *error, gpointer user_data)
{
        AddComponentToLinuxMdArrayData *data = user_data;
        if (data->callback != NULL)
                data->callback (data->device, error, data->user_data);
        g_object_unref (data->device);
        g_free (data);
}

void
gdu_device_op_add_component_to_linux_md_array (GduDevice                                        *device,
                                               const char                                       *component_objpath,
                                               GduDeviceAddComponentToLinuxMdArrayCompletedFunc  callback,
                                               gpointer                                          user_data)
{
        char *options[16];
        AddComponentToLinuxMdArrayData *data;

        data = g_new0 (AddComponentToLinuxMdArrayData, 1);
        data->device = g_object_ref (device);
        data->callback = callback;
        data->user_data = user_data;

        options[0] = NULL;

        org_freedesktop_DeviceKit_Disks_Device_linux_md_add_component_async (
                device->priv->proxy,
                component_objpath,
                (const char **) options,
                op_add_component_to_linux_md_array_cb,
                data);
}

/* -------------------------------------------------------------------------------- */

typedef struct {
        GduDevice *device;
        GduDeviceRemoveComponentFromLinuxMdArrayCompletedFunc callback;
        gpointer user_data;
} RemoveComponentFromLinuxMdArrayData;

static void
op_remove_component_from_linux_md_array_cb (DBusGProxy *proxy, GError *error, gpointer user_data)
{
        RemoveComponentFromLinuxMdArrayData *data = user_data;
        if (data->callback != NULL)
                data->callback (data->device, error, data->user_data);
        g_object_unref (data->device);
        g_free (data);
}

void
gdu_device_op_remove_component_from_linux_md_array (GduDevice                                             *device,
                                                    const char                                            *component_objpath,
                                                    const char                                            *secure_erase,
                                                    GduDeviceRemoveComponentFromLinuxMdArrayCompletedFunc  callback,
                                                    gpointer                                               user_data)
{
        int n;
        char *options[16];
        RemoveComponentFromLinuxMdArrayData *data;

        data = g_new0 (RemoveComponentFromLinuxMdArrayData, 1);
        data->device = g_object_ref (device);
        data->callback = callback;
        data->user_data = user_data;

        n = 0;
        if (secure_erase != NULL && strlen (secure_erase) > 0) {
                options[n++] = g_strdup_printf ("erase=%s", secure_erase);
        }
        options[n] = NULL;

        org_freedesktop_DeviceKit_Disks_Device_linux_md_remove_component_async (
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
        if (!org_freedesktop_DeviceKit_Disks_Device_job_cancel (device->priv->proxy, &error)) {
                g_warning ("error cancelling op: %s", error->message);
                g_error_free (error);
        }
}

/* -------------------------------------------------------------------------------- */

void
gdu_device_historical_smart_data_free (GduDeviceHistoricalSmartData *hsd)
{
        int n;
        g_free (hsd->last_self_test_result);
        for (n = 0; n < hsd->num_attr; n++) {
                g_free (hsd->attrs[n].desc);
                g_free (hsd->attrs[n].raw);
        }
        g_free (hsd->attrs);
        g_free (hsd);
}

/* TODO: async version */

GList *
gdu_device_retrieve_historical_smart_data (GduDevice *device)
{
        int n, m;
        GPtrArray *data;
        GError *error;
        GList *ret;

        ret = NULL;

        error = NULL;
        if (!org_freedesktop_DeviceKit_Disks_Device_drive_smart_get_historical_data (device->priv->proxy,
                                                                                     0,
                                                                                     0,
                                                                                     &data,
                                                                                     &error)) {
                g_warning ("smart history failed: %s", error->message);
                g_error_free (error);
                goto out;
        }

        for (n = 0; n < (int) data->len; n++) {
                GduDeviceHistoricalSmartData *hsd;
                GPtrArray *attrs;
                GValue elem0 = {0};

                hsd = g_new0 (GduDeviceHistoricalSmartData, 1);

                g_value_init (&elem0, HISTORICAL_SMART_DATA_STRUCT_TYPE);
                g_value_set_static_boxed (&elem0, data->pdata[n]);
                dbus_g_type_struct_get (&elem0,
                                        0, &(hsd->time_collected),
                                        1, &(hsd->temperature),
                                        2, &(hsd->time_powered_on),
                                        3, &(hsd->last_self_test_result),
                                        4, &(hsd->is_failing),
                                        5, &attrs,
                                        G_MAXUINT);

                hsd->num_attr = (int) attrs->len;
                hsd->attrs = g_new0 (GduDeviceSmartAttribute, hsd->num_attr);

                for (m = 0; m < (int) attrs->len; m++) {
                        GValue elem = {0};
                        GduDeviceSmartAttribute *a = hsd->attrs + m;

                        g_value_init (&elem, SMART_DATA_STRUCT_TYPE);
                        g_value_set_static_boxed (&elem, attrs->pdata[m]);
                        dbus_g_type_struct_get (&elem,
                                                0, &(a->id),
                                                1, &(a->desc),
                                                2, &(a->flags),
                                                3, &(a->value),
                                                4, &(a->worst),
                                                5, &(a->threshold),
                                                6, &(a->raw),
                                                G_MAXUINT);
                }

                ret = g_list_prepend (ret, hsd);
        }

        ret = g_list_reverse (ret);

out:
        return ret;
}
