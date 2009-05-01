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
#include <unistd.h>
#include <sys/types.h>

#include "gdu-private.h"
#include "gdu-pool.h"
#include "gdu-device.h"
#include "gdu-ata-smart-attribute.h"
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

        guint64  device_detection_time;
        guint64  device_media_detection_time;
        gint64   device_major;
        gint64   device_minor;
        char    *device_file;
        char   **device_file_by_id;
        char   **device_file_by_path;
        gboolean device_is_system_internal;
        gboolean device_is_partition;
        gboolean device_is_partition_table;
        gboolean device_is_removable;
        gboolean device_is_media_available;
        gboolean device_is_media_change_detected;
        gboolean device_is_media_change_detection_polling;
        gboolean device_is_media_change_detection_inhibitable;
        gboolean device_is_media_change_detection_inhibited;
        gboolean device_is_read_only;
        gboolean device_is_drive;
        gboolean device_is_optical_disc;
        gboolean device_is_luks;
        gboolean device_is_luks_cleartext;
        gboolean device_is_mounted;
        gboolean device_is_linux_md_component;
        gboolean device_is_linux_md;
        char   **device_mount_paths;
        uid_t    device_mounted_by_uid;
        gboolean device_presentation_hide;
        char    *device_presentation_name;
        char    *device_presentation_icon_name;
        guint64  device_size;
        guint64  device_block_size;

        gboolean job_in_progress;
        char    *job_id;
        uid_t    job_initiated_by_uid;
        gboolean job_is_cancellable;
        double   job_percentage;

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

        char    *luks_holder;

        char    *luks_cleartext_slave;
        uid_t    luks_cleartext_unlocked_by_uid;

        char    *drive_vendor;
        char    *drive_model;
        char    *drive_revision;
        char    *drive_serial;
        char    *drive_connection_interface;
        guint64  drive_connection_speed;
        char   **drive_media_compatibility;
        char    *drive_media;
        gboolean drive_is_media_ejectable;
        gboolean drive_requires_eject;

        gboolean optical_disc_is_blank;
        gboolean optical_disc_is_appendable;
        gboolean optical_disc_is_closed;
        guint optical_disc_num_tracks;
        guint optical_disc_num_audio_tracks;
        guint optical_disc_num_sessions;

        gboolean drive_ata_smart_is_available;
        gboolean drive_ata_smart_is_failing;
        gboolean drive_ata_smart_is_failing_valid;
        gboolean drive_ata_smart_has_bad_sectors;
        gboolean drive_ata_smart_has_bad_attributes;
        gdouble drive_ata_smart_temperature_kelvin;
        guint64 drive_ata_smart_power_on_seconds;
        guint64 drive_ata_smart_time_collected;
        guint drive_ata_smart_offline_data_collection_status;
        guint drive_ata_smart_offline_data_collection_seconds;
        guint drive_ata_smart_self_test_execution_status;
        guint drive_ata_smart_self_test_execution_percent_remaining;
        gboolean drive_ata_smart_short_and_extended_self_test_available;
        gboolean drive_ata_smart_conveyance_self_test_available;
        gboolean drive_ata_smart_start_self_test_available;
        gboolean drive_ata_smart_abort_self_test_available;
        guint drive_ata_smart_short_self_test_polling_minutes;
        guint drive_ata_smart_extended_self_test_polling_minutes;
        guint drive_ata_smart_conveyance_self_test_polling_minutes;
        GValue drive_ata_smart_attributes;

        char    *linux_md_component_level;
        int      linux_md_component_num_raid_devices;
        char    *linux_md_component_uuid;
        char    *linux_md_component_home_host;
        char    *linux_md_component_name;
        char    *linux_md_component_version;
        char    *linux_md_component_holder;
        char   **linux_md_component_state;

        char    *linux_md_state;
        char    *linux_md_level;
        int      linux_md_num_raid_devices;
        char    *linux_md_uuid;
        char    *linux_md_home_host;
        char    *linux_md_name;
        char    *linux_md_version;
        char   **linux_md_slaves;
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

        else if (strcmp (key, "device-detection-time") == 0)
                props->device_detection_time = g_value_get_uint64 (value);
        else if (strcmp (key, "device-media-detection-time") == 0)
                props->device_media_detection_time = g_value_get_uint64 (value);
        else if (strcmp (key, "device-major") == 0)
                props->device_major = g_value_get_int64 (value);
        else if (strcmp (key, "device-minor") == 0)
                props->device_minor = g_value_get_int64 (value);
        else if (strcmp (key, "device-file") == 0)
                props->device_file = g_strdup (g_value_get_string (value));
        else if (strcmp (key, "device-file-by-id") == 0)
                props->device_file_by_id = g_strdupv (g_value_get_boxed (value));
        else if (strcmp (key, "device-file-by-path") == 0)
                props->device_file_by_path = g_strdupv (g_value_get_boxed (value));
        else if (strcmp (key, "device-is-system-internal") == 0)
                props->device_is_system_internal = g_value_get_boolean (value);
        else if (strcmp (key, "device-is-partition") == 0)
                props->device_is_partition = g_value_get_boolean (value);
        else if (strcmp (key, "device-is-partition-table") == 0)
                props->device_is_partition_table = g_value_get_boolean (value);
        else if (strcmp (key, "device-is-removable") == 0)
                props->device_is_removable = g_value_get_boolean (value);
        else if (strcmp (key, "device-is-media-available") == 0)
                props->device_is_media_available = g_value_get_boolean (value);
        else if (strcmp (key, "device-is-media-change-detected") == 0)
                props->device_is_media_change_detected = g_value_get_boolean (value);
        else if (strcmp (key, "device-is-media-change-detection-polling") == 0)
                props->device_is_media_change_detection_polling = g_value_get_boolean (value);
        else if (strcmp (key, "device-is-media-change-detection-inhibitable") == 0)
                props->device_is_media_change_detection_inhibitable = g_value_get_boolean (value);
        else if (strcmp (key, "device-is-media-change-detection-inhibited") == 0)
                props->device_is_media_change_detection_inhibited = g_value_get_boolean (value);
        else if (strcmp (key, "device-is-read-only") == 0)
                props->device_is_read_only = g_value_get_boolean (value);
        else if (strcmp (key, "device-is-drive") == 0)
                props->device_is_drive = g_value_get_boolean (value);
        else if (strcmp (key, "device-is-optical-disc") == 0)
                props->device_is_optical_disc = g_value_get_boolean (value);
        else if (strcmp (key, "device-is-luks") == 0)
                props->device_is_luks = g_value_get_boolean (value);
        else if (strcmp (key, "device-is-luks-cleartext") == 0)
                props->device_is_luks_cleartext = g_value_get_boolean (value);
        else if (strcmp (key, "device-is-linux-md-component") == 0)
                props->device_is_linux_md_component = g_value_get_boolean (value);
        else if (strcmp (key, "device-is-linux-md") == 0)
                props->device_is_linux_md = g_value_get_boolean (value);
        else if (strcmp (key, "device-is-mounted") == 0)
                props->device_is_mounted = g_value_get_boolean (value);
        else if (strcmp (key, "device-mount-paths") == 0)
                props->device_mount_paths = g_strdupv (g_value_get_boxed (value));
        else if (strcmp (key, "device-mounted-by-uid") == 0)
                props->device_mounted_by_uid = g_value_get_uint (value);
        else if (strcmp (key, "device-presentation-hide") == 0)
                props->device_presentation_hide = g_value_get_boolean (value);
        else if (strcmp (key, "device-presentation-name") == 0)
                props->device_presentation_name = g_strdup (g_value_get_string (value));
        else if (strcmp (key, "device-presentation-icon-name") == 0)
                props->device_presentation_icon_name = g_strdup (g_value_get_string (value));
        else if (strcmp (key, "device-size") == 0)
                props->device_size = g_value_get_uint64 (value);
        else if (strcmp (key, "device-block-size") == 0)
                props->device_block_size = g_value_get_uint64 (value);

        else if (strcmp (key, "job-in-progress") == 0)
                props->job_in_progress = g_value_get_boolean (value);
        else if (strcmp (key, "job-id") == 0)
                props->job_id = g_strdup (g_value_get_string (value));
        else if (strcmp (key, "job-initiated-by-uid") == 0)
                props->job_initiated_by_uid = g_value_get_uint (value);
        else if (strcmp (key, "job-is-cancellable") == 0)
                props->job_is_cancellable = g_value_get_boolean (value);
        else if (strcmp (key, "job-percentage") == 0)
                props->job_percentage = g_value_get_double (value);

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

        else if (strcmp (key, "luks-holder") == 0)
                props->luks_holder = g_strdup (g_value_get_boxed (value));

        else if (strcmp (key, "luks-cleartext-slave") == 0)
                props->luks_cleartext_slave = g_strdup (g_value_get_boxed (value));
        else if (strcmp (key, "luks-cleartext-unlocked-by-uid") == 0)
                props->luks_cleartext_unlocked_by_uid = g_value_get_uint (value);

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
        else if (strcmp (key, "drive-is-media-ejectable") == 0)
                props->drive_is_media_ejectable = g_value_get_boolean (value);
        else if (strcmp (key, "drive-requires-eject") == 0)
                props->drive_requires_eject = g_value_get_boolean (value);

        else if (strcmp (key, "optical-disc-is-blank") == 0)
                props->optical_disc_is_blank = g_value_get_boolean (value);
        else if (strcmp (key, "optical-disc-is-appendable") == 0)
                props->optical_disc_is_appendable = g_value_get_boolean (value);
        else if (strcmp (key, "optical-disc-is-closed") == 0)
                props->optical_disc_is_closed = g_value_get_boolean (value);
        else if (strcmp (key, "optical-disc-num-tracks") == 0)
                props->optical_disc_num_tracks = g_value_get_uint (value);
        else if (strcmp (key, "optical-disc-num-audio-tracks") == 0)
                props->optical_disc_num_audio_tracks = g_value_get_uint (value);
        else if (strcmp (key, "optical-disc-num-sessions") == 0)
                props->optical_disc_num_sessions = g_value_get_uint (value);

        else if (strcmp (key, "drive-ata-smart-is-available") == 0)
                props->drive_ata_smart_is_available = g_value_get_boolean (value);
        else if (strcmp (key, "drive-ata-smart-is-failing") == 0)
                props->drive_ata_smart_is_failing = g_value_get_boolean (value);
        else if (strcmp (key, "drive-ata-smart-is-failing-valid") == 0)
                props->drive_ata_smart_is_failing_valid = g_value_get_boolean (value);
        else if (strcmp (key, "drive-ata-smart-has-bad-sectors") == 0)
                props->drive_ata_smart_has_bad_sectors = g_value_get_boolean (value);
        else if (strcmp (key, "drive-ata-smart-has-bad-attributes") == 0)
                props->drive_ata_smart_has_bad_attributes = g_value_get_boolean (value);
        else if (strcmp (key, "drive-ata-smart-temperature-kelvin") == 0)
                props->drive_ata_smart_temperature_kelvin = g_value_get_double (value);
        else if (strcmp (key, "drive-ata-smart-power-on-seconds") == 0)
                props->drive_ata_smart_power_on_seconds = g_value_get_uint64 (value);
        else if (strcmp (key, "drive-ata-smart-time-collected") == 0)
                props->drive_ata_smart_time_collected = g_value_get_uint64 (value);
        else if (strcmp (key, "drive-ata-smart-offline-data-collection-status") == 0)
                props->drive_ata_smart_offline_data_collection_status = g_value_get_uint (value);
        else if (strcmp (key, "drive-ata-smart-offline-data-collection-seconds") == 0)
                props->drive_ata_smart_offline_data_collection_seconds = g_value_get_uint (value);
        else if (strcmp (key, "drive-ata-smart-self-test-execution-status") == 0)
                props->drive_ata_smart_self_test_execution_status = g_value_get_uint (value);
        else if (strcmp (key, "drive-ata-smart-self-test-execution-percent-remaining") == 0)
                props->drive_ata_smart_self_test_execution_percent_remaining = g_value_get_uint (value);
        else if (strcmp (key, "drive-ata-smart-short-and-extended-self-test-available") == 0)
                props->drive_ata_smart_short_and_extended_self_test_available = g_value_get_boolean (value);
        else if (strcmp (key, "drive-ata-smart-conveyance-self-test-available") == 0)
                props->drive_ata_smart_conveyance_self_test_available = g_value_get_boolean (value);
        else if (strcmp (key, "drive-ata-smart-start-self-test-available") == 0)
                props->drive_ata_smart_start_self_test_available = g_value_get_boolean (value);
        else if (strcmp (key, "drive-ata-smart-abort-self-test-available") == 0)
                props->drive_ata_smart_abort_self_test_available = g_value_get_boolean (value);
        else if (strcmp (key, "drive-ata-smart-short-self-test-polling-minutes") == 0)
                props->drive_ata_smart_short_self_test_polling_minutes = g_value_get_uint (value);
        else if (strcmp (key, "drive-ata-smart-extended-self-test-polling-minutes") == 0)
                props->drive_ata_smart_extended_self_test_polling_minutes = g_value_get_uint (value);
        else if (strcmp (key, "drive-ata-smart-conveyance-self-test-polling-minutes") == 0)
                props->drive_ata_smart_conveyance_self_test_polling_minutes = g_value_get_uint (value);
        else if (strcmp (key, "drive-ata-smart-attributes") == 0) {
                g_value_copy (value, &(props->drive_ata_smart_attributes));
        }

        else if (strcmp (key, "linux-md-component-level") == 0)
                props->linux_md_component_level = g_strdup (g_value_get_string (value));
        else if (strcmp (key, "linux-md-component-num-raid-devices") == 0)
                props->linux_md_component_num_raid_devices = g_value_get_int (value);
        else if (strcmp (key, "linux-md-component-uuid") == 0)
                props->linux_md_component_uuid = g_strdup (g_value_get_string (value));
        else if (strcmp (key, "linux-md-component-home-host") == 0)
                props->linux_md_component_home_host = g_strdup (g_value_get_string (value));
        else if (strcmp (key, "linux-md-component-name") == 0)
                props->linux_md_component_name = g_strdup (g_value_get_string (value));
        else if (strcmp (key, "linux-md-component-version") == 0)
                props->linux_md_component_version = g_strdup (g_value_get_string (value));
        else if (strcmp (key, "linux-md-component-holder") == 0)
                props->linux_md_component_holder = g_strdup (g_value_get_boxed (value));
        else if (strcmp (key, "linux-md-component-state") == 0)
                props->linux_md_component_state = g_strdupv (g_value_get_boxed (value));

        else if (strcmp (key, "linux-md-state") == 0)
                props->linux_md_state = g_strdup (g_value_get_string (value));
        else if (strcmp (key, "linux-md-level") == 0)
                props->linux_md_level = g_strdup (g_value_get_string (value));
        else if (strcmp (key, "linux-md-num-raid-devices") == 0)
                props->linux_md_num_raid_devices = g_value_get_int (value);
        else if (strcmp (key, "linux-md-uuid") == 0)
                props->linux_md_uuid = g_strdup (g_value_get_string (value));
        else if (strcmp (key, "linux-md-home-host") == 0)
                props->linux_md_home_host = g_strdup (g_value_get_string (value));
        else if (strcmp (key, "linux-md-name") == 0)
                props->linux_md_name = g_strdup (g_value_get_string (value));
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

static void
device_properties_free (DeviceProperties *props)
{
        g_free (props->native_path);
        g_free (props->device_file);
        g_strfreev (props->device_file_by_id);
        g_strfreev (props->device_file_by_path);
        g_strfreev (props->device_mount_paths);
        g_free (props->device_presentation_name);
        g_free (props->device_presentation_icon_name);
        g_free (props->job_id);
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
        g_free (props->luks_holder);
        g_free (props->luks_cleartext_slave);
        g_free (props->drive_model);
        g_free (props->drive_vendor);
        g_free (props->drive_revision);
        g_free (props->drive_serial);
        g_free (props->drive_connection_interface);
        g_strfreev (props->drive_media_compatibility);
        g_free (props->drive_media);

        g_value_unset (&(props->drive_ata_smart_attributes));

        g_free (props->linux_md_component_level);
        g_free (props->linux_md_component_uuid);
        g_free (props->linux_md_component_home_host);
        g_free (props->linux_md_component_name);
        g_free (props->linux_md_component_version);
        g_free (props->linux_md_component_holder);
        g_strfreev (props->linux_md_component_state);

        g_free (props->linux_md_state);
        g_free (props->linux_md_level);
        g_free (props->linux_md_uuid);
        g_free (props->linux_md_home_host);
        g_free (props->linux_md_name);
        g_free (props->linux_md_version);
        g_strfreev (props->linux_md_slaves);
        g_free (props->linux_md_sync_action);
        g_free (props);
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
        g_value_init (&(props->drive_ata_smart_attributes),
                      dbus_g_type_get_collection ("GPtrArray", ATA_SMART_ATTRIBUTE_STRUCT_TYPE));

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

                device_properties_free (props);
                props = NULL;
                goto out;
        }

        g_hash_table_foreach (hash_table, (GHFunc) collect_props, props);

        g_hash_table_unref (hash_table);

out:
        g_object_unref (prop_proxy);
        return props;
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
        g_debug ("##### finalized device %s",
                 device->priv->props != NULL ? device->priv->props->device_file : device->priv->object_path);

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

        g_type_class_add_private (klass, sizeof (GduDevicePrivate));

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
        device->priv = G_TYPE_INSTANCE_GET_PRIVATE (device, GDU_TYPE_DEVICE, GduDevicePrivate);
}

static gboolean
update_info (GduDevice *device)
{
        DeviceProperties *new_properties;

        new_properties = device_properties_get (device->priv->bus, device->priv->object_path);
        if (new_properties != NULL) {
                if (device->priv->props != NULL)
                        device_properties_free (device->priv->props);
                device->priv->props = new_properties;
                return TRUE;
        } else {
                return FALSE;
        }
}


GduDevice *
_gdu_device_new_from_object_path (GduPool *pool, const char *object_path)
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

        g_debug ("_gdu_device_new_from_object_path: %s", device->priv->props->device_file);

        return device;
error:
        g_object_unref (device);
        return NULL;
}

gboolean
_gdu_device_changed (GduDevice *device)
{
        g_debug ("_gdu_device_changed: %s", device->priv->props->device_file);
        if (update_info (device)) {
                g_signal_emit (device, signals[CHANGED], 0);
                return TRUE;
        } else {
                return FALSE;
        }
}

void
_gdu_device_job_changed (GduDevice   *device,
                         gboolean     job_in_progress,
                         const char  *job_id,
                         uid_t        job_initiated_by_uid,
                         gboolean     job_is_cancellable,
                         double       job_percentage)
{
        g_debug ("_gdu_device_job_changed: %s: %s", device->priv->props->device_file, job_id);

        device->priv->props->job_in_progress = job_in_progress;
        g_free (device->priv->props->job_id);
        device->priv->props->job_id = g_strdup (job_id);
        device->priv->props->job_initiated_by_uid = job_initiated_by_uid;
        device->priv->props->job_is_cancellable = job_is_cancellable;
        device->priv->props->job_percentage = job_percentage;

        g_signal_emit (device, signals[JOB_CHANGED], 0);
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

guint64
gdu_device_get_detection_time (GduDevice *device)
{
        return device->priv->props->device_detection_time;
}

guint64
gdu_device_get_media_detection_time (GduDevice *device)
{
        return device->priv->props->device_media_detection_time;
}

dev_t
gdu_device_get_dev (GduDevice *device)
{
        return makedev (device->priv->props->device_major, device->priv->props->device_minor);
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
gdu_device_is_media_change_detected (GduDevice *device)
{
        return device->priv->props->device_is_media_change_detected;
}

gboolean
gdu_device_is_media_change_detection_polling (GduDevice *device)
{
        return device->priv->props->device_is_media_change_detection_polling;
}

gboolean
gdu_device_is_media_change_detection_inhibitable (GduDevice *device)
{
        return device->priv->props->device_is_media_change_detection_inhibitable;
}

gboolean
gdu_device_is_media_change_detection_inhibited (GduDevice *device)
{
        return device->priv->props->device_is_media_change_detection_inhibited;
}

gboolean
gdu_device_is_read_only (GduDevice *device)
{
        return device->priv->props->device_is_read_only;
}

gboolean
gdu_device_is_system_internal (GduDevice *device)
{
        return device->priv->props->device_is_system_internal;
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
gdu_device_is_luks (GduDevice *device)
{
        return device->priv->props->device_is_luks;
}

gboolean
gdu_device_is_luks_cleartext (GduDevice *device)
{
        return device->priv->props->device_is_luks_cleartext;
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

/* keep this around for a while to avoid breaking ABI */
const char *
gdu_device_get_mount_path (GduDevice *device)
{
        if (device->priv->props->device_mount_paths == NULL || device->priv->props->device_mount_paths[0] == NULL)
                return NULL;
        return (const char *) device->priv->props->device_mount_paths[0];
}

char **
gdu_device_get_mount_paths (GduDevice *device)
{
        return device->priv->props->device_mount_paths;
}

gboolean
gdu_device_get_presentation_hide (GduDevice *device)
{
        return device->priv->props->device_presentation_hide;
}

const char *
gdu_device_get_presentation_name (GduDevice *device)
{
        return device->priv->props->device_presentation_name;
}

const char *
gdu_device_get_presentation_icon_name (GduDevice *device)
{
        return device->priv->props->device_presentation_icon_name;
}

uid_t
gdu_device_get_mounted_by_uid (GduDevice *device)
{
        return device->priv->props->device_mounted_by_uid;
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

const char *
gdu_device_luks_get_holder (GduDevice *device)
{
        return device->priv->props->luks_holder;
}

const char *
gdu_device_luks_cleartext_get_slave (GduDevice *device)
{
        return device->priv->props->luks_cleartext_slave;
}

uid_t
gdu_device_luks_cleartext_unlocked_by_uid (GduDevice *device)
{
        return device->priv->props->luks_cleartext_unlocked_by_uid;
}


gboolean
gdu_device_is_drive (GduDevice *device)
{
        return device->priv->props->device_is_drive;
}

gboolean
gdu_device_is_optical_disc (GduDevice *device)
{
        return device->priv->props->device_is_optical_disc;
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
gdu_device_drive_get_is_media_ejectable (GduDevice *device)
{
        return device->priv->props->drive_is_media_ejectable;
}

gboolean
gdu_device_drive_get_requires_eject (GduDevice *device)
{
        return device->priv->props->drive_requires_eject;
}

gboolean
gdu_device_optical_disc_get_is_blank (GduDevice *device)
{
        return device->priv->props->optical_disc_is_blank;
}

gboolean
gdu_device_optical_disc_get_is_appendable (GduDevice *device)
{
        return device->priv->props->optical_disc_is_appendable;
}

gboolean
gdu_device_optical_disc_get_is_closed (GduDevice *device)
{
        return device->priv->props->optical_disc_is_closed;
}

guint
gdu_device_optical_disc_get_num_tracks (GduDevice *device)
{
        return device->priv->props->optical_disc_num_tracks;
}

guint
gdu_device_optical_disc_get_num_audio_tracks (GduDevice *device)
{
        return device->priv->props->optical_disc_num_audio_tracks;
}

guint
gdu_device_optical_disc_get_num_sessions (GduDevice *device)
{
        return device->priv->props->optical_disc_num_sessions;
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
gdu_device_linux_md_component_get_home_host (GduDevice *device)
{
        return device->priv->props->linux_md_component_home_host;
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

const char *
gdu_device_linux_md_component_get_holder (GduDevice *device)
{
        return device->priv->props->linux_md_component_holder;
}

char **
gdu_device_linux_md_component_get_state (GduDevice *device)
{
        return device->priv->props->linux_md_component_state;
}

const char *
gdu_device_linux_md_get_state (GduDevice *device)
{
        return device->priv->props->linux_md_state;
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
gdu_device_linux_md_get_uuid (GduDevice *device)
{
        return device->priv->props->linux_md_uuid;
}

const char *
gdu_device_linux_md_get_home_host (GduDevice *device)
{
        return device->priv->props->linux_md_home_host;
}

const char *
gdu_device_linux_md_get_name (GduDevice *device)
{
        return device->priv->props->linux_md_name;
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
gdu_device_drive_ata_smart_get_is_available (GduDevice *device)
{
        return device->priv->props->drive_ata_smart_is_available;
}

gboolean
gdu_device_drive_ata_smart_get_is_failing (GduDevice *device)
{
        return device->priv->props->drive_ata_smart_is_failing;
}

gboolean
gdu_device_drive_ata_smart_get_is_failing_valid (GduDevice *device)
{
        return device->priv->props->drive_ata_smart_is_failing_valid;
}

gboolean
gdu_device_drive_ata_smart_get_has_bad_sectors (GduDevice *device)
{
        return device->priv->props->drive_ata_smart_has_bad_sectors;
}

gboolean
gdu_device_drive_ata_smart_get_has_bad_attributes (GduDevice *device)
{
        return device->priv->props->drive_ata_smart_has_bad_attributes;
}

gdouble
gdu_device_drive_ata_smart_get_temperature_kelvin (GduDevice *device)
{
        return device->priv->props->drive_ata_smart_temperature_kelvin;
}

guint64 gdu_device_drive_ata_smart_get_power_on_seconds (GduDevice *device)
{
        return device->priv->props->drive_ata_smart_power_on_seconds;
}

guint64
gdu_device_drive_ata_smart_get_time_collected (GduDevice *device)
{
        return device->priv->props->drive_ata_smart_time_collected;
}

GduAtaSmartOfflineDataCollectionStatus
gdu_device_drive_ata_smart_get_offline_data_collection_status (GduDevice *device)
{
        return device->priv->props->drive_ata_smart_offline_data_collection_status;
}

guint
gdu_device_drive_ata_smart_get_offline_data_collection_seconds (GduDevice *device)
{
        return device->priv->props->drive_ata_smart_offline_data_collection_seconds;
}

GduAtaSmartSelfTestExecutionStatus
gdu_device_drive_ata_smart_get_self_test_execution_status (GduDevice *device)
{
        return device->priv->props->drive_ata_smart_self_test_execution_status;
}

guint
gdu_device_drive_ata_smart_get_self_test_execution_percent_remaining (GduDevice *device)
{
        return device->priv->props->drive_ata_smart_self_test_execution_percent_remaining;
}

gboolean
gdu_device_drive_ata_smart_get_short_and_extended_self_test_available (GduDevice *device)
{
        return device->priv->props->drive_ata_smart_short_and_extended_self_test_available;
}

gboolean
gdu_device_drive_ata_smart_get_conveyance_self_test_available (GduDevice *device)
{
        return device->priv->props->drive_ata_smart_conveyance_self_test_available;
}

gboolean
gdu_device_drive_ata_smart_get_start_self_test_available (GduDevice *device)
{
        return device->priv->props->drive_ata_smart_start_self_test_available;
}

gboolean
gdu_device_drive_ata_smart_get_abort_self_test_available (GduDevice *device)
{
        return device->priv->props->drive_ata_smart_abort_self_test_available;
}

guint
gdu_device_drive_ata_smart_get_short_self_test_polling_minutes (GduDevice *device)
{
        return device->priv->props->drive_ata_smart_short_self_test_polling_minutes;
}

guint
gdu_device_drive_ata_smart_get_extended_self_test_polling_minutes (GduDevice *device)
{
        return device->priv->props->drive_ata_smart_extended_self_test_polling_minutes;
}

guint
gdu_device_drive_ata_smart_get_conveyance_self_test_polling_minutes (GduDevice *device)
{
        return device->priv->props->drive_ata_smart_conveyance_self_test_polling_minutes;
}

GList *
gdu_device_drive_ata_smart_get_attributes (GduDevice *device)
{
        GList *ret;
        GPtrArray *p;
        guint n;

        ret = NULL;

        p = g_value_get_boxed (&(device->priv->props->drive_ata_smart_attributes));
        for (n = 0; n < p->len; n++) {
                ret = g_list_prepend (ret, _gdu_ata_smart_attribute_new (p->pdata[n]));
        }

        return ret;
}

GduAtaSmartAttribute *
gdu_device_drive_ata_smart_get_attribute (GduDevice *device, const gchar *attr_name)
{
        GList *attrs;
        GList *l;
        GduAtaSmartAttribute *ret;

        ret = NULL;

        attrs = gdu_device_drive_ata_smart_get_attributes (device);
        for (l = attrs; l != NULL; l = l->next) {
                GduAtaSmartAttribute *a = GDU_ATA_SMART_ATTRIBUTE (l->data);
                if (g_strcmp0 (attr_name, gdu_ata_smart_attribute_get_name (a)) == 0) {
                        ret = g_object_ref (a);
                        break;
                }
        }
        g_list_foreach (attrs, (GFunc) g_object_unref, NULL);
        g_list_free (attrs);

        return ret;
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

uid_t
gdu_device_job_get_initiated_by_uid (GduDevice *device)
{
        return device->priv->props->job_initiated_by_uid;
}

gboolean
gdu_device_job_is_cancellable (GduDevice *device)
{
        return device->priv->props->job_is_cancellable;
}

double
gdu_device_job_get_percentage (GduDevice *device)
{
        return device->priv->props->job_percentage;
}

/* -------------------------------------------------------------------------------- */

typedef struct {
        GduDevice *device;
        GduDeviceFilesystemCreateCompletedFunc callback;
        gpointer user_data;
} FilesystemCreateData;

static void
op_mkfs_cb (DBusGProxy *proxy, GError *error, gpointer user_data)
{
        FilesystemCreateData *data = user_data;
        _gdu_error_fixup (error);
        data->callback (data->device, error, data->user_data);
        g_object_unref (data->device);
        g_free (data);
}

void
gdu_device_op_filesystem_create (GduDevice                              *device,
                                 const char                             *fstype,
                                 const char                             *fslabel,
                                 const char                             *encrypt_passphrase,
                                 gboolean                                fs_take_ownership,
                                 GduDeviceFilesystemCreateCompletedFunc  callback,
                                 gpointer                                user_data)
{
        int n;
        FilesystemCreateData *data;
        char *options[16];

        data = g_new0 (FilesystemCreateData, 1);
        data->device = g_object_ref (device);
        data->callback = callback;
        data->user_data = user_data;

        n = 0;
        if (fslabel != NULL && strlen (fslabel) > 0) {
                options[n++] = g_strdup_printf ("label=%s", fslabel);
        }
        if (encrypt_passphrase != NULL && strlen (encrypt_passphrase) > 0) {
                options[n++] = g_strdup_printf ("luks_encrypt=%s", encrypt_passphrase);
        }
        if (fs_take_ownership) {
                options[n++] = g_strdup_printf ("take_ownership_uid=%d", getuid ());
                options[n++] = g_strdup_printf ("take_ownership_gid=%d", getgid ());
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
        GduDeviceFilesystemMountCompletedFunc callback;
        gpointer user_data;
} FilesystemMountData;

static void
op_mount_cb (DBusGProxy *proxy, char *mount_path, GError *error, gpointer user_data)
{
        FilesystemMountData *data = user_data;
        _gdu_error_fixup (error);
        if (data->callback != NULL)
                data->callback (data->device, mount_path, error, data->user_data);
        g_object_unref (data->device);
        g_free (data);
}

void
gdu_device_op_filesystem_mount (GduDevice                   *device,
                                gchar                      **options,
                                GduDeviceFilesystemMountCompletedFunc  callback,
                                gpointer                     user_data)
{
        const char *fstype;
        gchar *null_options[16];
        FilesystemMountData *data;

        data = g_new0 (FilesystemMountData, 1);
        data->device = g_object_ref (device);
        data->callback = callback;
        data->user_data = user_data;

        fstype = NULL;

        null_options[0] = NULL;
        if (options == NULL)
                options = null_options;

        org_freedesktop_DeviceKit_Disks_Device_filesystem_mount_async (device->priv->proxy,
                                                                       fstype,
                                                                       (const char **) options,
                                                                       op_mount_cb,
                                                                       data);
}

/* -------------------------------------------------------------------------------- */

typedef struct {
        GduDevice *device;
        GduDeviceFilesystemUnmountCompletedFunc callback;
        gpointer user_data;
} FilesystemUnmountData;

static void
op_unmount_cb (DBusGProxy *proxy, GError *error, gpointer user_data)
{
        FilesystemUnmountData *data = user_data;
        _gdu_error_fixup (error);
        if (data->callback != NULL)
                data->callback (data->device, error, data->user_data);
        g_object_unref (data->device);
        g_free (data);
}

void
gdu_device_op_filesystem_unmount (GduDevice                     *device,
                                  GduDeviceFilesystemUnmountCompletedFunc  callback,
                                  gpointer                       user_data)
{
        char *options[16];
        FilesystemUnmountData *data;

        data = g_new0 (FilesystemUnmountData, 1);
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
        GduDeviceFilesystemCheckCompletedFunc callback;
        gpointer user_data;
} FilesystemCheckData;

static void
op_check_cb (DBusGProxy *proxy, gboolean is_clean, GError *error, gpointer user_data)
{
        FilesystemCheckData *data = user_data;
        _gdu_error_fixup (error);
        if (data->callback != NULL)
                data->callback (data->device, is_clean, error, data->user_data);
        g_object_unref (data->device);
        g_free (data);
}

void
gdu_device_op_filesystem_check (GduDevice                             *device,
                                GduDeviceFilesystemCheckCompletedFunc  callback,
                                gpointer                               user_data)
{
        char *options[16];
        FilesystemCheckData *data;

        data = g_new0 (FilesystemCheckData, 1);
        data->device = g_object_ref (device);
        data->callback = callback;
        data->user_data = user_data;
        options[0] = NULL;

        org_freedesktop_DeviceKit_Disks_Device_filesystem_check_async (device->priv->proxy,
                                                                       (const char **) options,
                                                                       op_check_cb,
                                                                       data);
}

/* -------------------------------------------------------------------------------- */

typedef struct {
        GduDevice *device;
        GduDevicePartitionDeleteCompletedFunc callback;
        gpointer user_data;
} PartitionDeleteData;

static void
op_partition_delete_cb (DBusGProxy *proxy, GError *error, gpointer user_data)
{
        PartitionDeleteData *data = user_data;
        _gdu_error_fixup (error);
        if (data->callback != NULL)
                data->callback (data->device, error, data->user_data);
        g_object_unref (data->device);
        g_free (data);
}

void
gdu_device_op_partition_delete (GduDevice                             *device,
                                GduDevicePartitionDeleteCompletedFunc  callback,
                                gpointer                               user_data)
{
        int n;
        char *options[16];
        PartitionDeleteData *data;

        data = g_new0 (PartitionDeleteData, 1);
        data->device = g_object_ref (device);
        data->callback = callback;
        data->user_data = user_data;

        n = 0;
        options[n] = NULL;

        org_freedesktop_DeviceKit_Disks_Device_partition_delete_async (device->priv->proxy,
                                                                       (const char **) options,
                                                                       op_partition_delete_cb,
                                                                       data);

        while (n >= 0)
                g_free (options[n--]);
}

/* -------------------------------------------------------------------------------- */

typedef struct {
        GduDevice *device;
        GduDevicePartitionCreateCompletedFunc callback;
        gpointer user_data;
} PartitionCreateData;

static void
op_create_partition_cb (DBusGProxy *proxy, char *created_device_object_path, GError *error, gpointer user_data)
{
        PartitionCreateData *data = user_data;
        _gdu_error_fixup (error);
        if (data->callback != NULL)
                data->callback (data->device, created_device_object_path, error, data->user_data);
        g_object_unref (data->device);
        g_free (data);
}

void
gdu_device_op_partition_create (GduDevice   *device,
                                guint64      offset,
                                guint64      size,
                                const char  *type,
                                const char  *label,
                                char       **flags,
                                const char  *fstype,
                                const char  *fslabel,
                                const char  *encrypt_passphrase,
                                gboolean     fs_take_ownership,
                                GduDevicePartitionCreateCompletedFunc callback,
                                gpointer user_data)
{
        int n;
        char *fsoptions[16];
        char *options[16];
        PartitionCreateData *data;

        data = g_new0 (PartitionCreateData, 1);
        data->device = g_object_ref (device);
        data->callback = callback;
        data->user_data = user_data;

        options[0] = NULL;

        n = 0;
        if (fslabel != NULL && strlen (fslabel) > 0) {
                fsoptions[n++] = g_strdup_printf ("label=%s", fslabel);
        }
        if (encrypt_passphrase != NULL && strlen (encrypt_passphrase) > 0) {
                fsoptions[n++] = g_strdup_printf ("luks_encrypt=%s", encrypt_passphrase);
        }
        if (fs_take_ownership) {
                fsoptions[n++] = g_strdup_printf ("take_ownership_uid=%d", getuid ());
                fsoptions[n++] = g_strdup_printf ("take_ownership_gid=%d", getgid ());
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
        GduDevicePartitionModifyCompletedFunc callback;
        gpointer user_data;
} PartitionModifyData;

static void
op_partition_modify_cb (DBusGProxy *proxy, GError *error, gpointer user_data)
{
        PartitionModifyData *data = user_data;
        _gdu_error_fixup (error);
        if (data->callback != NULL)
                data->callback (data->device, error, data->user_data);
        g_object_unref (data->device);
        g_free (data);
}

void
gdu_device_op_partition_modify (GduDevice                             *device,
                                const char                            *type,
                                const char                            *label,
                                char                                 **flags,
                                GduDevicePartitionModifyCompletedFunc  callback,
                                gpointer                               user_data)
{
        PartitionModifyData *data;

        data = g_new0 (PartitionModifyData, 1);
        data->device = g_object_ref (device);
        data->callback = callback;
        data->user_data = user_data;

        org_freedesktop_DeviceKit_Disks_Device_partition_modify_async (device->priv->proxy,
                                                                       type,
                                                                       label,
                                                                       (const char **) flags,
                                                                       op_partition_modify_cb,
                                                                       data);
}

/* -------------------------------------------------------------------------------- */

typedef struct {
        GduDevice *device;
        GduDevicePartitionTableCreateCompletedFunc callback;
        gpointer user_data;
} CreatePartitionTableData;

static void
op_create_partition_table_cb (DBusGProxy *proxy, GError *error, gpointer user_data)
{
        CreatePartitionTableData *data = user_data;
        _gdu_error_fixup (error);
        if (data->callback != NULL)
                data->callback (data->device, error, data->user_data);
        g_object_unref (data->device);
        g_free (data);
}

void
gdu_device_op_partition_table_create (GduDevice                                  *device,
                                      const char                                 *scheme,
                                      GduDevicePartitionTableCreateCompletedFunc  callback,
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
        GduDeviceLuksUnlockCompletedFunc callback;
        gpointer user_data;
} UnlockData;

static void
op_unlock_luks_cb (DBusGProxy *proxy, char *cleartext_object_path, GError *error, gpointer user_data)
{
        UnlockData *data = user_data;
        _gdu_error_fixup (error);
        if (data->callback != NULL)
                data->callback (data->device, cleartext_object_path, error, data->user_data);
        g_object_unref (data->device);
        g_free (data);
}

void
gdu_device_op_luks_unlock (GduDevice *device,
                                const char *secret,
                                GduDeviceLuksUnlockCompletedFunc callback,
                                gpointer user_data)
{
        UnlockData *data;
        char *options[16];
        options[0] = NULL;

        data = g_new0 (UnlockData, 1);
        data->device = g_object_ref (device);
        data->callback = callback;
        data->user_data = user_data;

        org_freedesktop_DeviceKit_Disks_Device_luks_unlock_async (device->priv->proxy,
                                                                       secret,
                                                                       (const char **) options,
                                                                       op_unlock_luks_cb,
                                                                       data);
}

/* -------------------------------------------------------------------------------- */

typedef struct {
        GduDevice *device;

        GduDeviceLuksChangePassphraseCompletedFunc callback;
        gpointer user_data;
} ChangeSecretData;

static void
op_change_secret_for_luks_cb (DBusGProxy *proxy, GError *error, gpointer user_data)
{
        ChangeSecretData *data = user_data;
        _gdu_error_fixup (error);
        if (data->callback != NULL)
                data->callback (data->device, error, data->user_data);
        g_object_unref (data->device);
        g_free (data);
}

void
gdu_device_op_luks_change_passphrase (GduDevice   *device,
                                           const char  *old_secret,
                                           const char  *new_secret,
                                           GduDeviceLuksChangePassphraseCompletedFunc callback,
                                           gpointer user_data)
{
        ChangeSecretData *data;

        data = g_new0 (ChangeSecretData, 1);
        data->device = g_object_ref (device);
        data->callback = callback;
        data->user_data = user_data;

        org_freedesktop_DeviceKit_Disks_Device_luks_change_passphrase_async (device->priv->proxy,
                                                                                  old_secret,
                                                                                  new_secret,
                                                                                  op_change_secret_for_luks_cb,
                                                                                  data);
}

/* -------------------------------------------------------------------------------- */

typedef struct {
        GduDevice *device;
        GduDeviceLuksLockCompletedFunc callback;
        gpointer user_data;
} LockLuksData;

static void
op_lock_luks_cb (DBusGProxy *proxy, GError *error, gpointer user_data)
{
        LockLuksData *data = user_data;
        _gdu_error_fixup (error);
        if (data->callback != NULL)
                data->callback (data->device, error, data->user_data);
        g_object_unref (data->device);
        g_free (data);
}

void
gdu_device_op_luks_lock (GduDevice                           *device,
                              GduDeviceLuksLockCompletedFunc  callback,
                              gpointer                             user_data)
{
        char *options[16];
        LockLuksData *data;

        data = g_new0 (LockLuksData, 1);
        data->device = g_object_ref (device);
        data->callback = callback;
        data->user_data = user_data;

        options[0] = NULL;
        org_freedesktop_DeviceKit_Disks_Device_luks_lock_async (device->priv->proxy,
                                                                     (const char **) options,
                                                                     op_lock_luks_cb,
                                                                     data);
}

/* -------------------------------------------------------------------------------- */

typedef struct {
        GduDevice *device;
        GduDeviceFilesystemSetLabelCompletedFunc callback;
        gpointer user_data;
} FilesystemSetLabelData;

static void
op_change_filesystem_label_cb (DBusGProxy *proxy, GError *error, gpointer user_data)
{
        FilesystemSetLabelData *data = user_data;
        _gdu_error_fixup (error);
        if (data->callback != NULL)
                data->callback (data->device, error, data->user_data);
        g_object_unref (data->device);
        g_free (data);
}

void
gdu_device_op_filesystem_set_label (GduDevice                                *device,
                                    const char                               *new_label,
                                    GduDeviceFilesystemSetLabelCompletedFunc  callback,
                                    gpointer                                  user_data)
{
        FilesystemSetLabelData *data;

        data = g_new0 (FilesystemSetLabelData, 1);
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
        GduDeviceFilesystemListOpenFilesCompletedFunc callback;
        gpointer user_data;
} FilesystemListOpenFilesData;

static GList *
op_filesystem_list_open_files_compute_ret (GPtrArray *processes)
{
        GList *ret;
        int n;

        ret = NULL;
        for (n = 0; n < (int) processes->len; n++) {
                ret = g_list_prepend (ret, _gdu_process_new (processes->pdata[n]));
        }
        ret = g_list_reverse (ret);
        return ret;
}

static void
op_filesystem_list_open_files_cb (DBusGProxy *proxy, GPtrArray *processes, GError *error, gpointer user_data)
{
        FilesystemListOpenFilesData *data = user_data;
        GList *ret;

        _gdu_error_fixup (error);

        ret = NULL;
        if (processes != NULL && error == NULL)
                ret = op_filesystem_list_open_files_compute_ret (processes);

        if (data->callback == NULL)
                data->callback (data->device, ret, error, data->user_data);

        g_object_unref (data->device);
        g_free (data);
}

void
gdu_device_filesystem_list_open_files (GduDevice                                     *device,
                                       GduDeviceFilesystemListOpenFilesCompletedFunc  callback,
                                       gpointer                                       user_data)
{
        FilesystemListOpenFilesData *data;

        data = g_new0 (FilesystemListOpenFilesData, 1);
        data->device = g_object_ref (device);
        data->callback = callback;
        data->user_data = user_data;

        org_freedesktop_DeviceKit_Disks_Device_filesystem_list_open_files_async (device->priv->proxy,
                                                                                 op_filesystem_list_open_files_cb,
                                                                                 data);
}

GList *
gdu_device_filesystem_list_open_files_sync (GduDevice  *device,
                                            GError    **error)
{
        GList *ret;
        GPtrArray *processes;

        ret = NULL;
        if (!org_freedesktop_DeviceKit_Disks_Device_filesystem_list_open_files (device->priv->proxy,
                                                                                &processes,
                                                                                error))
                goto out;

        ret = op_filesystem_list_open_files_compute_ret (processes);
out:
        return ret;
}

/* -------------------------------------------------------------------------------- */

typedef struct {
        GduDevice *device;
        GduDeviceDriveAtaSmartRefreshDataCompletedFunc callback;
        gpointer user_data;
} RetrieveAtaSmartDataData;

static void
op_retrieve_ata_smart_data_cb (DBusGProxy *proxy, GError *error, gpointer user_data)
{
        RetrieveAtaSmartDataData *data = user_data;
        _gdu_error_fixup (error);
        if (data->callback != NULL)
                data->callback (data->device, error, data->user_data);
        g_object_unref (data->device);
        g_free (data);
}

void
gdu_device_drive_ata_smart_refresh_data (GduDevice                                  *device,
                                     GduDeviceDriveAtaSmartRefreshDataCompletedFunc callback,
                                     gpointer                                    user_data)
{
        RetrieveAtaSmartDataData *data;
        char *options[16];

        options[0] = NULL;

        data = g_new0 (RetrieveAtaSmartDataData, 1);
        data->device = g_object_ref (device);
        data->callback = callback;
        data->user_data = user_data;

        org_freedesktop_DeviceKit_Disks_Device_drive_ata_smart_refresh_data_async (device->priv->proxy,
                                                                               (const char **) options,
                                                                               op_retrieve_ata_smart_data_cb,
                                                                               data);
}

/* -------------------------------------------------------------------------------- */

typedef struct {
        GduDevice *device;
        GduDeviceDriveAtaSmartInitiateSelftestCompletedFunc callback;
        gpointer user_data;
} DriveAtaSmartInitiateSelftestData;

static void
op_run_ata_smart_selftest_cb (DBusGProxy *proxy, GError *error, gpointer user_data)
{
        DriveAtaSmartInitiateSelftestData *data = user_data;
        _gdu_error_fixup (error);
        if (data->callback != NULL)
                data->callback (data->device, error, data->user_data);
        g_object_unref (data->device);
        g_free (data);
}

void
gdu_device_op_drive_ata_smart_initiate_selftest (GduDevice                                        *device,
                                                 const char                                       *test,
                                                 GduDeviceDriveAtaSmartInitiateSelftestCompletedFunc  callback,
                                                 gpointer                                          user_data)
{
        DriveAtaSmartInitiateSelftestData *data;
        gchar *options = {NULL};

        data = g_new0 (DriveAtaSmartInitiateSelftestData, 1);
        data->device = g_object_ref (device);
        data->callback = callback;
        data->user_data = user_data;

        org_freedesktop_DeviceKit_Disks_Device_drive_ata_smart_initiate_selftest_async (device->priv->proxy,
                                                                                        test,
                                                                                        (const gchar **) options,
                                                                                        op_run_ata_smart_selftest_cb,
                                                                                        data);
}

/* -------------------------------------------------------------------------------- */

typedef struct {
        GduDevice *device;
        GduDeviceLinuxMdStopCompletedFunc callback;
        gpointer user_data;
} LinuxMdStopData;

static void
op_stop_linux_md_array_cb (DBusGProxy *proxy, GError *error, gpointer user_data)
{
        LinuxMdStopData *data = user_data;
        _gdu_error_fixup (error);
        if (data->callback != NULL)
                data->callback (data->device, error, data->user_data);
        g_object_unref (data->device);
        g_free (data);
}

void
gdu_device_op_linux_md_stop (GduDevice                         *device,
                             GduDeviceLinuxMdStopCompletedFunc  callback,
                             gpointer                           user_data)
{
        char *options[16];
        LinuxMdStopData *data;

        data = g_new0 (LinuxMdStopData, 1);
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
        GduDeviceLinuxMdAddComponentCompletedFunc callback;
        gpointer user_data;
} LinuxMdAddComponentData;

static void
op_add_component_to_linux_md_array_cb (DBusGProxy *proxy, GError *error, gpointer user_data)
{
        LinuxMdAddComponentData *data = user_data;
        _gdu_error_fixup (error);
        if (data->callback != NULL)
                data->callback (data->device, error, data->user_data);
        g_object_unref (data->device);
        g_free (data);
}

void
gdu_device_op_linux_md_add_component (GduDevice                                 *device,
                                      const char                                *component_objpath,
                                      GduDeviceLinuxMdAddComponentCompletedFunc  callback,
                                      gpointer                                   user_data)
{
        char *options[16];
        LinuxMdAddComponentData *data;

        data = g_new0 (LinuxMdAddComponentData, 1);
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
        GduDeviceLinuxMdRemoveComponentCompletedFunc callback;
        gpointer user_data;
} LinuxMdRemoveComponentData;

static void
op_remove_component_from_linux_md_array_cb (DBusGProxy *proxy, GError *error, gpointer user_data)
{
        LinuxMdRemoveComponentData *data = user_data;
        _gdu_error_fixup (error);
        if (data->callback != NULL)
                data->callback (data->device, error, data->user_data);
        g_object_unref (data->device);
        g_free (data);
}

void
gdu_device_op_linux_md_remove_component (GduDevice                                    *device,
                                         const char                                   *component_objpath,
                                         GduDeviceLinuxMdRemoveComponentCompletedFunc  callback,
                                         gpointer                                      user_data)
{
        int n;
        char *options[16];
        LinuxMdRemoveComponentData *data;

        data = g_new0 (LinuxMdRemoveComponentData, 1);
        data->device = g_object_ref (device);
        data->callback = callback;
        data->user_data = user_data;

        n = 0;
        options[n] = NULL;

        org_freedesktop_DeviceKit_Disks_Device_linux_md_remove_component_async (
                device->priv->proxy,
                component_objpath,
                (const char **) options,
                op_remove_component_from_linux_md_array_cb,
                data);

        while (n >= 0)
                g_free (options[n--]);
}

/* -------------------------------------------------------------------------------- */

typedef struct {
        GduDevice *device;
        GduDeviceCancelJobCompletedFunc callback;
        gpointer user_data;
} CancelJobData;

static void
op_cancel_job_cb (DBusGProxy *proxy, GError *error, gpointer user_data)
{
        CancelJobData *data = user_data;
        _gdu_error_fixup (error);
        if (data->callback != NULL)
                data->callback (data->device, error, data->user_data);
        g_object_unref (data->device);
        g_free (data);
}

void
gdu_device_op_cancel_job (GduDevice *device, GduDeviceCancelJobCompletedFunc callback, gpointer user_data)
{
        CancelJobData *data;

        data = g_new0 (CancelJobData, 1);
        data->device = g_object_ref (device);
        data->callback = callback;
        data->user_data = user_data;

        org_freedesktop_DeviceKit_Disks_Device_job_cancel_async (device->priv->proxy,
                                                                 op_cancel_job_cb,
                                                                 data);
}

/* -------------------------------------------------------------------------------- */

typedef struct {
        GduDevice *device;
        GduDeviceDriveAtaSmartGetHistoricalDataCompletedFunc callback;
        gpointer user_data;
} DriveAtaSmartGetHistoricalDataData;

static GList *
op_ata_smart_historical_data_compute_ret (GPtrArray *historical_data)
{
        GList *ret;
        int n;

        ret = NULL;
        for (n = 0; n < (int) historical_data->len; n++) {
                ret = g_list_prepend (ret, _gdu_ata_smart_historical_data_new (historical_data->pdata[n]));
        }
        ret = g_list_reverse (ret);
        return ret;
}

static void
op_ata_smart_historical_data_cb (DBusGProxy *proxy, GPtrArray *historical_data, GError *error, gpointer user_data)
{
        DriveAtaSmartGetHistoricalDataData *data = user_data;
        GList *ret;

        _gdu_error_fixup (error);

        ret = NULL;
        if (historical_data != NULL && error == NULL)
                ret = op_ata_smart_historical_data_compute_ret (historical_data);

        if (data->callback == NULL)
                data->callback (data->device, ret, error, data->user_data);

        g_object_unref (data->device);
        g_free (data);
}

void
gdu_device_drive_ata_smart_get_historical_data (GduDevice                                         *device,
                                                guint64                                            since,
                                                guint64                                            until,
                                                guint64                                            spacing,
                                                GduDeviceDriveAtaSmartGetHistoricalDataCompletedFunc  callback,
                                                gpointer                                           user_data)
{
        DriveAtaSmartGetHistoricalDataData *data;

        data = g_new0 (DriveAtaSmartGetHistoricalDataData, 1);
        data->device = g_object_ref (device);
        data->callback = callback;
        data->user_data = user_data;

        org_freedesktop_DeviceKit_Disks_Device_drive_ata_smart_get_historical_data_async (device->priv->proxy,
                                                                                          since,
                                                                                          until,
                                                                                          spacing,
                                                                                          op_ata_smart_historical_data_cb,
                                                                                          data);
}

GList *
gdu_device_drive_ata_smart_get_historical_data_sync (GduDevice  *device,
                                                     guint64     since,
                                                     guint64     until,
                                                     guint64     spacing,
                                                     GError    **error)
{
        GList *ret;
        GPtrArray *historical_data;

        ret = NULL;
        if (!org_freedesktop_DeviceKit_Disks_Device_drive_ata_smart_get_historical_data (device->priv->proxy,
                                                                                         since,
                                                                                         until,
                                                                                         spacing,
                                                                                         &historical_data,
                                                                                         error))
                goto out;

        ret = op_ata_smart_historical_data_compute_ret (historical_data);
out:
        return ret;
}

/* -------------------------------------------------------------------------------- */

typedef struct {
        GduDevice *device;
        GduDeviceDriveEjectCompletedFunc callback;
        gpointer user_data;
} DriveEjectData;

static void
op_eject_cb (DBusGProxy *proxy, GError *error, gpointer user_data)
{
        DriveEjectData *data = user_data;
        _gdu_error_fixup (error);
        if (data->callback != NULL)
                data->callback (data->device, error, data->user_data);
        g_object_unref (data->device);
        g_free (data);
}

void
gdu_device_op_drive_eject (GduDevice                        *device,
                           GduDeviceDriveEjectCompletedFunc  callback,
                           gpointer                          user_data)
{
        char *options[16];
        DriveEjectData *data;

        data = g_new0 (DriveEjectData, 1);
        data->device = g_object_ref (device);
        data->callback = callback;
        data->user_data = user_data;
        options[0] = NULL;

        org_freedesktop_DeviceKit_Disks_Device_drive_eject_async (device->priv->proxy,
                                                                  (const char **) options,
                                                                  op_eject_cb,
                                                                  data);
}

/* -------------------------------------------------------------------------------- */

typedef struct {
        GduDevice *device;
        GduDeviceDrivePollMediaCompletedFunc callback;
        gpointer user_data;
} DrivePollMediaData;

static void
op_poll_media_cb (DBusGProxy *proxy, GError *error, gpointer user_data)
{
        DrivePollMediaData *data = user_data;
        _gdu_error_fixup (error);
        if (data->callback != NULL)
                data->callback (data->device, error, data->user_data);
        g_object_unref (data->device);
        g_free (data);
}

void
gdu_device_op_drive_poll_media (GduDevice                        *device,
                                GduDeviceDrivePollMediaCompletedFunc  callback,
                                gpointer                          user_data)
{
        DrivePollMediaData *data;

        data = g_new0 (DrivePollMediaData, 1);
        data->device = g_object_ref (device);
        data->callback = callback;
        data->user_data = user_data;

        org_freedesktop_DeviceKit_Disks_Device_drive_poll_media_async (device->priv->proxy,
                                                                       op_poll_media_cb,
                                                                       data);
}
