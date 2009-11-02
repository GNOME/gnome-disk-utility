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
#include <string.h>
#include <stdlib.h>

#include "gdu-pool.h"
#include "gdu-presentable.h"
#include "gdu-device.h"
#include "gdu-drive.h"
#include "gdu-linux-md-drive.h"
#include "gdu-volume.h"
#include "gdu-volume-hole.h"
#include "gdu-known-filesystem.h"
#include "gdu-private.h"

#include "devkit-disks-daemon-glue.h"
#include "gdu-marshal.h"

/**
 * SECTION:gdu-pool
 * @title: GduPool
 * @short_description: Enumerate and monitor storage devices
 *
 * The #GduPool object represents a connection to the DeviceKit-disks daemon.
 */

enum {
        DEVICE_ADDED,
        DEVICE_REMOVED,
        DEVICE_CHANGED,
        DEVICE_JOB_CHANGED,
        PRESENTABLE_ADDED,
        PRESENTABLE_REMOVED,
        PRESENTABLE_CHANGED,
        PRESENTABLE_JOB_CHANGED,
        LAST_SIGNAL,
};

static GObjectClass *parent_class = NULL;
static guint signals[LAST_SIGNAL] = { 0 };

struct _GduPoolPrivate
{
        DBusGConnection *bus;
        DBusGProxy *proxy;

        char *daemon_version;
        gboolean supports_luks_devices;
        GList *known_filesystems;

        /* the current set of presentables we know about */
        GList *presentables;

        /* the current set of devices we know about */
        GHashTable *object_path_to_device;
};

G_DEFINE_TYPE (GduPool, gdu_pool, G_TYPE_OBJECT);

static void
gdu_pool_finalize (GduPool *pool)
{
        dbus_g_connection_unref (pool->priv->bus);
        g_object_unref (pool->priv->proxy);

        g_free (pool->priv->daemon_version);

        g_list_foreach (pool->priv->known_filesystems, (GFunc) g_object_unref, NULL);
        g_list_free (pool->priv->known_filesystems);

        g_hash_table_unref (pool->priv->object_path_to_device);

        g_list_foreach (pool->priv->presentables, (GFunc) g_object_unref, NULL);
        g_list_free (pool->priv->presentables);

        if (G_OBJECT_CLASS (parent_class)->finalize)
                (* G_OBJECT_CLASS (parent_class)->finalize) (G_OBJECT (pool));
}

static void
gdu_pool_class_init (GduPoolClass *klass)
{
        GObjectClass *obj_class = (GObjectClass *) klass;

        parent_class = g_type_class_peek_parent (klass);

        obj_class->finalize = (GObjectFinalizeFunc) gdu_pool_finalize;

        g_type_class_add_private (klass, sizeof (GduPoolPrivate));

        /**
         * GduPool::device-added
         * @pool: The #GduPool emitting the signal.
         * @device: The #GduDevice that was added.
         *
         * Emitted when @device is added to @pool.
         **/
        signals[DEVICE_ADDED] =
                g_signal_new ("device-added",
                              G_TYPE_FROM_CLASS (klass),
                              G_SIGNAL_RUN_LAST,
                              G_STRUCT_OFFSET (GduPoolClass, device_added),
                              NULL, NULL,
                              g_cclosure_marshal_VOID__OBJECT,
                              G_TYPE_NONE, 1,
                              GDU_TYPE_DEVICE);

        /**
         * GduPool::device-removed
         * @pool: The #GduPool emitting the signal.
         * @device: The #GduDevice that was removed.
         *
         * Emitted when @device is removed from @pool. Recipients
         * should release references to @device.
         **/
        signals[DEVICE_REMOVED] =
                g_signal_new ("device-removed",
                              G_TYPE_FROM_CLASS (klass),
                              G_SIGNAL_RUN_LAST,
                              G_STRUCT_OFFSET (GduPoolClass, device_removed),
                              NULL, NULL,
                              g_cclosure_marshal_VOID__OBJECT,
                              G_TYPE_NONE, 1,
                              GDU_TYPE_DEVICE);

        /**
         * GduPool::device-changed
         * @pool: The #GduPool emitting the signal.
         * @device: A #GduDevice.
         *
         * Emitted when @device is changed.
         **/
        signals[DEVICE_CHANGED] =
                g_signal_new ("device-changed",
                              G_TYPE_FROM_CLASS (klass),
                              G_SIGNAL_RUN_LAST,
                              G_STRUCT_OFFSET (GduPoolClass, device_changed),
                              NULL, NULL,
                              g_cclosure_marshal_VOID__OBJECT,
                              G_TYPE_NONE, 1,
                              GDU_TYPE_DEVICE);

        /**
         * GduPool::device-job-changed
         * @pool: The #GduPool emitting the signal.
         * @device: A #GduDevice.
         *
         * Emitted when job status on @device changes.
         **/
        signals[DEVICE_JOB_CHANGED] =
                g_signal_new ("device-job-changed",
                              G_TYPE_FROM_CLASS (klass),
                              G_SIGNAL_RUN_LAST,
                              G_STRUCT_OFFSET (GduPoolClass, device_job_changed),
                              NULL, NULL,
                              g_cclosure_marshal_VOID__OBJECT,
                              G_TYPE_NONE, 1,
                              GDU_TYPE_DEVICE);

        /**
         * GduPool::presentable-added
         * @pool: The #GduPool emitting the signal.
         * @presentable: The #GduPresentable that was added.
         *
         * Emitted when @presentable is added to @pool.
         **/
        signals[PRESENTABLE_ADDED] =
                g_signal_new ("presentable-added",
                              G_TYPE_FROM_CLASS (klass),
                              G_SIGNAL_RUN_LAST,
                              G_STRUCT_OFFSET (GduPoolClass, presentable_added),
                              NULL, NULL,
                              g_cclosure_marshal_VOID__OBJECT,
                              G_TYPE_NONE, 1,
                              GDU_TYPE_PRESENTABLE);

        /**
         * GduPool::presentable-removed
         * @pool: The #GduPool emitting the signal.
         * @presentable: The #GduPresentable that was removed.
         *
         * Emitted when @presentable is removed from @pool. Recipients
         * should release references to @presentable.
         **/
        signals[PRESENTABLE_REMOVED] =
                g_signal_new ("presentable-removed",
                              G_TYPE_FROM_CLASS (klass),
                              G_SIGNAL_RUN_LAST,
                              G_STRUCT_OFFSET (GduPoolClass, presentable_removed),
                              NULL, NULL,
                              g_cclosure_marshal_VOID__OBJECT,
                              G_TYPE_NONE, 1,
                              GDU_TYPE_PRESENTABLE);

        /**
         * GduPool::presentable-changed
         * @pool: The #GduPool emitting the signal.
         * @presentable: A #GduPresentable.
         *
         * Emitted when @presentable changes.
         **/
        signals[PRESENTABLE_CHANGED] =
                g_signal_new ("presentable-changed",
                              G_TYPE_FROM_CLASS (klass),
                              G_SIGNAL_RUN_LAST,
                              G_STRUCT_OFFSET (GduPoolClass, presentable_changed),
                              NULL, NULL,
                              g_cclosure_marshal_VOID__OBJECT,
                              G_TYPE_NONE, 1,
                              GDU_TYPE_PRESENTABLE);

        /**
         * GduPool::presentable-job-changed
         * @pool: The #GduPool emitting the signal.
         * @presentable: A #GduPresentable.
         *
         * Emitted when job status on @presentable changes.
         **/
        signals[PRESENTABLE_CHANGED] =
                g_signal_new ("presentable-job-changed",
                              G_TYPE_FROM_CLASS (klass),
                              G_SIGNAL_RUN_LAST,
                              G_STRUCT_OFFSET (GduPoolClass, presentable_job_changed),
                              NULL, NULL,
                              g_cclosure_marshal_VOID__OBJECT,
                              G_TYPE_NONE, 1,
                              GDU_TYPE_PRESENTABLE);
}

static void
gdu_log_func (const gchar   *log_domain,
              GLogLevelFlags log_level,
              const gchar   *message,
              gpointer       user_data)
{
        gboolean show_debug;
        const gchar *gdu_debug_var;

        gdu_debug_var = g_getenv ("GDU_DEBUG");
        show_debug = (g_strcmp0 (gdu_debug_var, "1") == 0);

        if (G_LIKELY (!show_debug))
                goto out;

        g_print ("%s: %s\n",
                 G_LOG_DOMAIN,
                 message);
 out:
        ;
}

static void
gdu_pool_init (GduPool *pool)
{
        static gboolean log_handler_initialized = FALSE;

        if (!log_handler_initialized) {
                g_log_set_handler (G_LOG_DOMAIN,
                                   G_LOG_LEVEL_MESSAGE | G_LOG_LEVEL_INFO | G_LOG_LEVEL_DEBUG,
                                   gdu_log_func,
                                   NULL);
                log_handler_initialized = TRUE;
        }

        pool->priv = G_TYPE_INSTANCE_GET_PRIVATE (pool, GDU_TYPE_POOL, GduPoolPrivate);

        pool->priv->object_path_to_device = g_hash_table_new_full (g_str_hash,
                                                                   g_str_equal,
                                                                   NULL,
                                                                   g_object_unref);
}

/* ---------------------------------------------------------------------------------------------------- */

static void
diff_sorted_lists (GList         *list1,
                   GList         *list2,
                   GCompareFunc   compare,
                   GList        **added,
                   GList        **removed)
{
  int order;

  *added = *removed = NULL;

  while (list1 != NULL &&
         list2 != NULL)
    {
      order = (*compare) (list1->data, list2->data);
      if (order < 0)
        {
          *removed = g_list_prepend (*removed, list1->data);
          list1 = list1->next;
        }
      else if (order > 0)
        {
          *added = g_list_prepend (*added, list2->data);
          list2 = list2->next;
        }
      else
        { /* same item */
          list1 = list1->next;
          list2 = list2->next;
        }
    }

  while (list1 != NULL)
    {
      *removed = g_list_prepend (*removed, list1->data);
      list1 = list1->next;
    }
  while (list2 != NULL)
    {
      *added = g_list_prepend (*added, list2->data);
      list2 = list2->next;
    }
}

/* note: does not ref the result */
static GduPresentable *
find_presentable_by_object_path (GList *presentables, const gchar *object_path)
{
        GduPresentable *ret;
        GList *l;

        ret = NULL;

        for (l = presentables; l != NULL; l = l->next) {
                GduPresentable *p = GDU_PRESENTABLE (l->data);
                GduDevice *d;
                const gchar *d_object_path;

                d = gdu_presentable_get_device (p);
                if (d == NULL)
                        continue;

                d_object_path = gdu_device_get_object_path (d);
                g_object_unref (d);

                if (g_strcmp0 (object_path, d_object_path) == 0) {
                        ret = p;
                        goto out;
                }
        }

 out:
        return ret;
}

static gboolean
is_msdos_extended_partition (GduDevice *device)
{
        gboolean ret;
        gint type;

        ret = FALSE;

        if (!gdu_device_is_partition (device))
                goto out;

        if (g_strcmp0 (gdu_device_partition_get_scheme (device), "mbr") != 0)
                goto out;

        type = strtol (gdu_device_partition_get_type (device), NULL, 0);
        if (!(type == 0x05 || type == 0x0f || type == 0x85))
                goto out;

        ret = TRUE;

 out:
        return ret;
}

typedef struct {
        int number;
        guint64 offset;
        guint64 size;
} PartEntry;

static int
part_entry_compare (PartEntry *pa, PartEntry *pb, gpointer user_data)
{
        if (pa->offset > pb->offset)
                return 1;
        if (pa->offset < pb->offset)
                return -1;
        return 0;
}

static GList *
get_holes (GduPool        *pool,
           GList          *devices,
           GduDrive       *drive,
           GduDevice      *drive_device,
           GduPresentable *enclosed_in,
           gboolean        ignore_logical,
           guint64         start,
           guint64         size)
{
        GList *ret;
        gint n;
        gint num_entries;
        PartEntry *entries;
        guint64 cursor;
        guint64 gap_size;
        guint64 gap_position;
        const char *scheme;
        GList *l;

        ret = NULL;
        entries = NULL;

        /* no point if adding holes if there's no media */
        if (!gdu_device_is_media_available (drive_device))
                goto out;

        /* neither if the media isn't partitioned */
        if (!gdu_device_is_partition_table (drive_device))
                goto out;

        /*g_debug ("Adding holes for %s between %" G_GUINT64_FORMAT
                 " and %" G_GUINT64_FORMAT " (ignore_logical=%d)",
                 gdu_device_get_device_file (drive_device),
                 start,
                 start + size,
                 ignore_logical);*/

        scheme = gdu_device_partition_table_get_scheme (drive_device);

        /* find the offsets and sizes of existing partitions of the partition table */
        GArray *entries_array;
        entries_array = g_array_new (FALSE, FALSE, sizeof (PartEntry));
        num_entries = 0;
        for (l = devices; l != NULL; l = l->next) {
                GduDevice *partition_device = GDU_DEVICE (l->data);
                guint64 partition_offset;
                guint64 partition_size;
                guint partition_number;

                if (!gdu_device_is_partition (partition_device))
                        continue;
                if (g_strcmp0 (gdu_device_get_object_path (drive_device),
                               gdu_device_partition_get_slave (partition_device)) != 0)
                        continue;

                partition_offset = gdu_device_partition_get_offset (partition_device);
                partition_size = gdu_device_partition_get_size (partition_device);
                partition_number = gdu_device_partition_get_number (partition_device);

                //g_print ("  considering partition number %d at offset=%lldMB size=%lldMB\n",
                //         partition_number,
                //         partition_offset / (1000 * 1000),
                //         partition_size / (1000 * 1000));

                /* only consider partitions in the given space */
                if (partition_offset <= start)
                        continue;
                if (partition_offset >= start + size)
                        continue;

                /* ignore logical partitions if requested */
                if (ignore_logical) {
                        if (strcmp (scheme, "mbr") == 0 && partition_number > 4)
                                continue;
                }

                g_array_set_size (entries_array, num_entries + 1);

                g_array_index (entries_array, PartEntry, num_entries).number = partition_number;
                g_array_index (entries_array, PartEntry, num_entries).offset = partition_offset;
                g_array_index (entries_array, PartEntry, num_entries).size = partition_size;

                num_entries++;
        }
        entries = (PartEntry *) g_array_free (entries_array, FALSE);

        g_qsort_with_data (entries, num_entries, sizeof (PartEntry), (GCompareDataFunc) part_entry_compare, NULL);

        //g_print (" %s: start=%lldMB size=%lldMB num_entries=%d\n",
        //         gdu_device_get_device_file (drive_device),
        //         start / (1000 * 1000),
        //         size / (1000 * 1000),
        //         num_entries);
        for (n = 0, cursor = start; n <= num_entries; n++) {
                if (n < num_entries) {
                        //g_print ("  %d: %d: offset=%lldMB size=%lldMB\n",
                        //         n,
                        //         entries[n].number,
                        //         entries[n].offset / (1000 * 1000),
                        //         entries[n].size / (1000 * 1000));

                        gap_size = entries[n].offset - cursor;
                        gap_position = entries[n].offset - gap_size;
                        cursor = entries[n].offset + entries[n].size;
                } else {
                        //g_print ("  trailing: cursor=%lldMB\n",
                        //         cursor / (1000 * 1000));

                        /* trailing free space */
                        gap_size = start + size - cursor;
                        gap_position = start + size - gap_size;
                }

                /* ignore unallocated space that is less than 1% of the drive */
                if (gap_size >= gdu_device_get_size (drive_device) / 100) {
                        GduVolumeHole *hole;
                        //g_print ("  adding %lldMB gap at %lldMB\n",
                        //         gap_size / (1000 * 1000),
                        //         gap_position / (1000 * 1000));

                        hole = _gdu_volume_hole_new (pool, gap_position, gap_size, enclosed_in);
                        ret = g_list_prepend (ret, hole);
                }

        }

out:
        g_free (entries);
        return ret;
}

static GList *
get_holes_for_drive (GduPool   *pool,
                     GList     *devices,
                     GduDrive  *drive,
                     GduVolume *extended_partition)
{
        GList *ret;
        GduDevice *drive_device;

        ret = NULL;

        drive_device = gdu_presentable_get_device (GDU_PRESENTABLE (drive));

        /* drive_device is NULL for activatable drive that isn't yet activated */
        if (drive_device == NULL)
                goto out;

        /* first add holes between primary partitions */
        ret = get_holes (pool,
                         devices,
                         drive,
                         drive_device,
                         GDU_PRESENTABLE (drive),
                         TRUE,
                         0,
                         gdu_device_get_size (drive_device));

        /* then add holes in the extended partition */
        if (extended_partition != NULL) {
                GList *holes_in_extended_partition;
                GduDevice *extended_partition_device;

                extended_partition_device = gdu_presentable_get_device (GDU_PRESENTABLE (extended_partition));
                if (extended_partition_device == NULL) {
                        g_warning ("No device for extended partition %s",
                                   gdu_presentable_get_id (GDU_PRESENTABLE (extended_partition)));
                        goto out;
                }

                holes_in_extended_partition = get_holes (pool,
                                                         devices,
                                                         drive,
                                                         drive_device,
                                                         GDU_PRESENTABLE (extended_partition),
                                                         FALSE,
                                                         gdu_device_partition_get_offset (extended_partition_device),
                                                         gdu_device_partition_get_size (extended_partition_device));

                ret = g_list_concat (ret, holes_in_extended_partition);

                g_object_unref (extended_partition_device);
        }

 out:
        if (drive_device != NULL)
                g_object_unref (drive_device);
        return ret;
}

static void
recompute_presentables (GduPool *pool)
{
        GList *l;
        GList *devices;
        GList *new_partitioned_drives;
        GList *new_presentables;
        GList *added_presentables;
        GList *removed_presentables;
        GHashTable *hash_map_from_drive_to_extended_partition;
        GHashTable *hash_map_from_linux_md_uuid_to_drive;

        /* The general strategy for (re-)computing presentables is rather brute force; we
         * compute the complete set of presentables every time and diff it against the
         * presentables we computed the last time. Then we send out add/remove events
         * accordingly.
         *
         * The reason for this brute-force approach is that the GduPresentable entities are
         * somewhat complicated since the whole process involves synthesizing GduVolumeHole and
         * GduLinuxMdDrive objects.
         */

        new_presentables = NULL;
        new_partitioned_drives = NULL;

        hash_map_from_drive_to_extended_partition = g_hash_table_new_full ((GHashFunc) gdu_presentable_hash,
                                                                           (GEqualFunc) gdu_presentable_equals,
                                                                           NULL,
                                                                           NULL);

        hash_map_from_linux_md_uuid_to_drive = g_hash_table_new_full (g_str_hash,
                                                                      g_str_equal,
                                                                      NULL,
                                                                      NULL);

        /* TODO: Ensure that pool->priv->devices is in topological sort order, then just loop
         *       through it and handle devices sequentially.
         *
         *       The current approach won't work for a couple of edge cases; notably stacks of devices
         *       e.g. consider a LUKS device inside a LUKS device...
         */
        devices = gdu_pool_get_devices (pool);

        /* Process all devices; the list is sorted in topologically order so we get all deps first */
        for (l = devices; l != NULL; l = l->next) {
                GduDevice *device;

                device = GDU_DEVICE (l->data);

                //g_debug ("Handling device %s", gdu_device_get_device_file (device));

                /* drives */
                if (gdu_device_is_drive (device)) {

                        GduDrive *drive;

                        if (gdu_device_is_linux_md (device)) {
                                const gchar *uuid;

                                uuid = gdu_device_linux_md_get_uuid (device);

                                /* 'clear' and 'inactive' devices may not have an uuid */
                                if (uuid != NULL && strlen (uuid) == 0)
                                        uuid = NULL;

                                if (uuid != NULL) {
                                        drive = GDU_DRIVE (_gdu_linux_md_drive_new (pool, uuid, NULL));

                                        /* Due to the topological sorting of devices, we are guaranteed that
                                         * that running Linux MD arrays come before the slaves.
                                         */
                                        g_warn_if_fail (g_hash_table_lookup (hash_map_from_linux_md_uuid_to_drive, uuid) == NULL);

                                        g_hash_table_insert (hash_map_from_linux_md_uuid_to_drive,
                                                             (gpointer) uuid,
                                                             drive);
                                } else {
                                        drive = GDU_DRIVE (_gdu_linux_md_drive_new (pool,
                                                                                    NULL,
                                                                                    gdu_device_get_device_file (device)));
                                }


                        } else {
                                drive = _gdu_drive_new_from_device (pool, device);
                        }
                        new_presentables = g_list_prepend (new_presentables, drive);

                        if (gdu_device_is_partition_table (device)) {
                                new_partitioned_drives = g_list_prepend (new_partitioned_drives, drive);
                        } else {
                                /* add volume for non-partitioned (e.g. whole-disk) devices if media
                                 * is available and the drive is active
                                 */
                                if (gdu_device_is_media_available (device) && gdu_drive_is_active (drive)) {
                                        GduVolume *volume;
                                        volume = _gdu_volume_new_from_device (pool, device, GDU_PRESENTABLE (drive));
                                        new_presentables = g_list_prepend (new_presentables, volume);
                                }
                        }

                } else if (gdu_device_is_partition (device)) {

                        GduVolume *volume;
                        GduPresentable *enclosing_presentable;

                        if (is_msdos_extended_partition (device)) {
                                enclosing_presentable = find_presentable_by_object_path (new_presentables,
                                                                                         gdu_device_partition_get_slave (device));

                                if (enclosing_presentable == NULL) {
                                        g_warning ("Partition %s claims to be a partition of %s which does not exist",
                                                   gdu_device_get_object_path (device),
                                                   gdu_device_partition_get_slave (device));
                                        continue;
                                }

                                volume = _gdu_volume_new_from_device (pool, device, enclosing_presentable);

                                g_hash_table_insert (hash_map_from_drive_to_extended_partition,
                                                     enclosing_presentable,
                                                     volume);
                        } else {
                                enclosing_presentable = find_presentable_by_object_path (new_presentables,
                                                                                         gdu_device_partition_get_slave (device));
                                if (enclosing_presentable == NULL) {
                                        g_warning ("Partition %s claims to be a partition of %s which does not exist",
                                                   gdu_device_get_object_path (device),
                                                   gdu_device_partition_get_slave (device));
                                        continue;
                                }

                                /* logical partitions should be enclosed by the appropriate extended partition */
                                if (g_strcmp0 (gdu_device_partition_get_scheme (device), "mbr") == 0 &&
                                    gdu_device_partition_get_number (device) >= 5) {

                                        enclosing_presentable = g_hash_table_lookup (hash_map_from_drive_to_extended_partition,
                                                                                     enclosing_presentable);
                                        if (enclosing_presentable == NULL) {
                                                g_warning ("Partition %s is a logical partition but no extended partition exists",
                                                           gdu_device_get_object_path (device));
                                                continue;
                                        }
                                }

                                volume = _gdu_volume_new_from_device (pool, device, enclosing_presentable);

                        }

                        /*g_debug ("%s is enclosed by %s",
                          gdu_device_get_object_path (device),
                          gdu_presentable_get_id (enclosing_presentable));*/

                        new_presentables = g_list_prepend (new_presentables, volume);

                } else if (gdu_device_is_luks_cleartext (device)) {

                        const gchar *luks_cleartext_slave;
                        GduPresentable *enclosing_luks_device;
                        GduVolume *volume;


                        luks_cleartext_slave = gdu_device_luks_cleartext_get_slave (device);

                        enclosing_luks_device = find_presentable_by_object_path (new_presentables, luks_cleartext_slave);
                        if (enclosing_luks_device == NULL) {
                                g_warning ("Cannot find enclosing device %s for LUKS cleartext device %s",
                                           luks_cleartext_slave,
                                           gdu_device_get_object_path (device));
                                continue;
                        }

                        volume = _gdu_volume_new_from_device (pool, device, enclosing_luks_device);
                        new_presentables = g_list_prepend (new_presentables, volume);

                } else {
                        g_debug ("Don't know how to handle device %s", gdu_device_get_device_file (device));
                }

                /* Ensure we have a GduLinuxMdDrive for non-running arrays */
                if (gdu_device_is_linux_md_component (device)) {
                        const gchar *uuid;

                        uuid = gdu_device_linux_md_component_get_uuid (device);
                        if (g_hash_table_lookup (hash_map_from_linux_md_uuid_to_drive, uuid) == NULL) {
                                GduDrive *drive;

                                drive = GDU_DRIVE (_gdu_linux_md_drive_new (pool, uuid, NULL));
                                new_presentables = g_list_prepend (new_presentables, drive);

                                g_hash_table_insert (hash_map_from_linux_md_uuid_to_drive,
                                                     (gpointer) uuid,
                                                     drive);
                        }
                }

        } /* For all devices */

        /* now add holes (representing non-partitioned space) for partitioned drives */
        for (l = new_partitioned_drives; l != NULL; l = l->next) {
                GduDrive *drive;
                GduVolume *extended_partition;
                GList *holes;

                drive = GDU_DRIVE (l->data);
                extended_partition = g_hash_table_lookup (hash_map_from_drive_to_extended_partition, drive);

                holes = get_holes_for_drive (pool, devices, drive, extended_partition);

                new_presentables = g_list_concat (new_presentables, holes);
        }

        /* clean up temporary lists / hashes */
        g_list_free (new_partitioned_drives);
        g_hash_table_unref (hash_map_from_drive_to_extended_partition);
        g_hash_table_unref (hash_map_from_linux_md_uuid_to_drive);

        /* figure out the diff */
        new_presentables = g_list_sort (new_presentables, (GCompareFunc) gdu_presentable_compare);
        pool->priv->presentables = g_list_sort (pool->priv->presentables, (GCompareFunc) gdu_presentable_compare);
        diff_sorted_lists (pool->priv->presentables,
                           new_presentables,
                           (GCompareFunc) gdu_presentable_compare,
                           &added_presentables,
                           &removed_presentables);

        /* remove presentables in the reverse topological order */
        removed_presentables = g_list_sort (removed_presentables, (GCompareFunc) gdu_presentable_compare);
        removed_presentables = g_list_reverse (removed_presentables);
        for (l = removed_presentables; l != NULL; l = l->next) {
                GduPresentable *p = GDU_PRESENTABLE (l->data);

                g_debug ("Removed presentable %s %p", gdu_presentable_get_id (p), p);

                pool->priv->presentables = g_list_remove (pool->priv->presentables, p);
                g_signal_emit (pool, signals[PRESENTABLE_REMOVED], 0, p);
                g_signal_emit_by_name (p, "removed");
                g_object_unref (p);
        }

        /* add presentables in the right topological order */
        added_presentables = g_list_sort (added_presentables, (GCompareFunc) gdu_presentable_compare);
        for (l = added_presentables; l != NULL; l = l->next) {
                GduPresentable *p = GDU_PRESENTABLE (l->data);

                /* rewrite all enclosing_presentable references for presentables we are going to add
                 * such that they really refer to presentables _previously_ added
                 */
                if (GDU_IS_VOLUME (p))
                        _gdu_volume_rewrite_enclosing_presentable (GDU_VOLUME (p));
                else if (GDU_IS_VOLUME_HOLE (p))
                        _gdu_volume_hole_rewrite_enclosing_presentable (GDU_VOLUME_HOLE (p));

                g_debug ("Added presentable %s %p", gdu_presentable_get_id (p), p);

                pool->priv->presentables = g_list_prepend (pool->priv->presentables, g_object_ref (p));
                g_signal_emit (pool, signals[PRESENTABLE_ADDED], 0, p);
        }

        /* keep list sorted */
        pool->priv->presentables = g_list_sort (pool->priv->presentables, (GCompareFunc) gdu_presentable_compare);

        g_list_free (removed_presentables);
        g_list_free (added_presentables);

        g_list_foreach (new_presentables, (GFunc) g_object_unref, NULL);
        g_list_free (new_presentables);
        g_list_foreach (devices, (GFunc) g_object_unref, NULL);
        g_list_free (devices);
}

/* ---------------------------------------------------------------------------------------------------- */

static void
device_changed_signal_handler (DBusGProxy *proxy, const char *object_path, gpointer user_data);

static void
device_added_signal_handler (DBusGProxy *proxy, const char *object_path, gpointer user_data)
{
        GduPool *pool;
        GduDevice *device;

        pool = GDU_POOL (user_data);

        device = gdu_pool_get_by_object_path (pool, object_path);
        if (device != NULL) {
                g_object_unref (device);
                g_warning ("Treating add for previously added device %s as change", object_path);
                device_changed_signal_handler (proxy, object_path, user_data);
                goto out;
        }

        device = _gdu_device_new_from_object_path (pool, object_path);
        if (device == NULL)
                goto out;

        g_hash_table_insert (pool->priv->object_path_to_device,
                             (gpointer) gdu_device_get_object_path (device),
                             device);
        g_signal_emit (pool, signals[DEVICE_ADDED], 0, device);
        //g_debug ("Added device %s", object_path);

        recompute_presentables (pool);

 out:
        ;
}

static void
device_removed_signal_handler (DBusGProxy *proxy, const char *object_path, gpointer user_data)
{
        GduPool *pool;
        GduDevice *device;

        pool = GDU_POOL (user_data);

        device = gdu_pool_get_by_object_path (pool, object_path);
        if (device == NULL) {
                /* This is not fatal - the device may have been removed when GetAll() failed
                 * when getting properties
                 */
                g_debug ("No device to remove for remove %s", object_path);
                goto out;
        }

        g_hash_table_remove (pool->priv->object_path_to_device,
                             gdu_device_get_object_path (device));
        g_signal_emit (pool, signals[DEVICE_REMOVED], 0, device);
        g_signal_emit_by_name (device, "removed");
        g_object_unref (device);
        g_debug ("Removed device %s", object_path);

        recompute_presentables (pool);

 out:
        ;
}

static void
device_changed_signal_handler (DBusGProxy *proxy, const char *object_path, gpointer user_data)
{
        GduPool *pool;
        GduDevice *device;

        pool = GDU_POOL (user_data);

        device = gdu_pool_get_by_object_path (pool, object_path);
        if (device == NULL) {
                g_warning ("Ignoring change event on non-existant device %s", object_path);
                goto out;
        }

        if (_gdu_device_changed (device)) {
                g_signal_emit (pool, signals[DEVICE_CHANGED], 0, device);
                g_signal_emit_by_name (device, "changed");
        }
        g_object_unref (device);

        recompute_presentables (pool);

 out:
        ;
}

static void
device_job_changed_signal_handler (DBusGProxy *proxy,
                                   const char *object_path,
                                   gboolean    job_in_progress,
                                   const char *job_id,
                                   guint32     job_initiated_by_uid,
                                   gboolean    job_is_cancellable,
                                   double      job_percentage,
                                   gpointer user_data)
{
        GduPool *pool = GDU_POOL (user_data);
        GduDevice *device;

        if ((device = gdu_pool_get_by_object_path (pool, object_path)) != NULL) {
                _gdu_device_job_changed (device,
                                         job_in_progress,
                                         job_id,
                                         job_initiated_by_uid,
                                         job_is_cancellable,
                                         job_percentage);
                g_signal_emit_by_name (pool, "device-job-changed", device);
                g_object_unref (device);
        } else {
                g_warning ("Unknown device %s on job-change", object_path);
        }
}

static gboolean
get_properties (GduPool *pool)
{
        gboolean ret;
        GError *error;
        GHashTable *hash_table;
        DBusGProxy *prop_proxy;
        GValue *value;
        GPtrArray *known_filesystems_array;
        int n;

        ret = FALSE;

	prop_proxy = dbus_g_proxy_new_for_name (pool->priv->bus,
                                                "org.freedesktop.DeviceKit.Disks",
                                                "/org/freedesktop/DeviceKit/Disks",
                                                "org.freedesktop.DBus.Properties");
        error = NULL;
        if (!dbus_g_proxy_call (prop_proxy,
                                "GetAll",
                                &error,
                                G_TYPE_STRING,
                                "org.freedesktop.DeviceKit.Disks",
                                G_TYPE_INVALID,
                                dbus_g_type_get_map ("GHashTable", G_TYPE_STRING, G_TYPE_VALUE),
                                &hash_table,
                                G_TYPE_INVALID)) {
                g_debug ("Error calling GetAll() when retrieving properties for /: %s", error->message);
                g_error_free (error);
                goto out;
        }

        value = g_hash_table_lookup (hash_table, "DaemonVersion");
        if (value == NULL) {
                g_warning ("No property 'DaemonVersion'");
                goto out;
        }
        pool->priv->daemon_version = g_strdup (g_value_get_string (value));

        value = g_hash_table_lookup (hash_table, "SupportsLuksDevices");
        if (value == NULL) {
                g_warning ("No property 'SupportsLuksDevices'");
                goto out;
        }
        pool->priv->supports_luks_devices = g_value_get_boolean (value);

        value = g_hash_table_lookup (hash_table, "KnownFilesystems");
        if (value == NULL) {
                g_warning ("No property 'KnownFilesystems'");
                goto out;
        }
        known_filesystems_array = g_value_get_boxed (value);
        pool->priv->known_filesystems = NULL;
        for (n = 0; n < (int) known_filesystems_array->len; n++) {
                pool->priv->known_filesystems = g_list_prepend (
                        pool->priv->known_filesystems,
                        _gdu_known_filesystem_new (known_filesystems_array->pdata[n]));
        }
        pool->priv->known_filesystems = g_list_reverse (pool->priv->known_filesystems);

        g_hash_table_unref (hash_table);

        ret = TRUE;
out:
        g_object_unref (prop_proxy);
        return ret;
}

/**
 * gdu_pool_new:
 *
 * Create a new #GduPool object.
 *
 * Returns: A #GduPool object. Caller must free this object using g_object_unref().
 **/
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

        dbus_g_object_register_marshaller (
                gdu_marshal_VOID__STRING_BOOLEAN_STRING_UINT_BOOLEAN_DOUBLE,
                G_TYPE_NONE,
                DBUS_TYPE_G_OBJECT_PATH,
                G_TYPE_BOOLEAN,
                G_TYPE_STRING,
                G_TYPE_UINT,
                G_TYPE_BOOLEAN,
                G_TYPE_DOUBLE,
                G_TYPE_INVALID);

	pool->priv->proxy = dbus_g_proxy_new_for_name (pool->priv->bus,
                                                       "org.freedesktop.DeviceKit.Disks",
                                                       "/org/freedesktop/DeviceKit/Disks",
                                                       "org.freedesktop.DeviceKit.Disks");
        dbus_g_proxy_add_signal (pool->priv->proxy, "DeviceAdded", DBUS_TYPE_G_OBJECT_PATH, G_TYPE_INVALID);
        dbus_g_proxy_add_signal (pool->priv->proxy, "DeviceRemoved", DBUS_TYPE_G_OBJECT_PATH, G_TYPE_INVALID);
        dbus_g_proxy_add_signal (pool->priv->proxy, "DeviceChanged", DBUS_TYPE_G_OBJECT_PATH, G_TYPE_INVALID);
        dbus_g_proxy_add_signal (pool->priv->proxy,
                                 "DeviceJobChanged",
                                 DBUS_TYPE_G_OBJECT_PATH,
                                 G_TYPE_BOOLEAN,
                                 G_TYPE_STRING,
                                 G_TYPE_UINT,
                                 G_TYPE_BOOLEAN,
                                 G_TYPE_DOUBLE,
                                 G_TYPE_INVALID);

        dbus_g_proxy_connect_signal (pool->priv->proxy, "DeviceAdded",
                                     G_CALLBACK (device_added_signal_handler), pool, NULL);
        dbus_g_proxy_connect_signal (pool->priv->proxy, "DeviceRemoved",
                                     G_CALLBACK (device_removed_signal_handler), pool, NULL);
        dbus_g_proxy_connect_signal (pool->priv->proxy, "DeviceChanged",
                                     G_CALLBACK (device_changed_signal_handler), pool, NULL);
        dbus_g_proxy_connect_signal (pool->priv->proxy, "DeviceJobChanged",
                                     G_CALLBACK (device_job_changed_signal_handler), pool, NULL);

        /* get the properties on the daemon object at / */
        if (!get_properties (pool)) {
                g_warning ("Couldn't get daemon properties");
                goto error;
        }

        /* prime the list of devices */
        error = NULL;
        if (!org_freedesktop_DeviceKit_Disks_enumerate_devices (pool->priv->proxy, &devices, &error)) {
                g_warning ("Couldn't enumerate devices: %s", error->message);
                g_error_free (error);
                goto error;
        }

        /* to check that topological sorting works, enumerate backwards by commenting out the for statement below */
        //for (n = devices->len - 1; n >= 0; n--) {
        for (n = 0; n < (int) devices->len; n++) {
                const char *object_path;
                GduDevice *device;

                object_path = devices->pdata[n];

                device = _gdu_device_new_from_object_path (pool, object_path);

                g_hash_table_insert (pool->priv->object_path_to_device,
                                     (gpointer) gdu_device_get_object_path (device),
                                     device);
        }
        g_ptr_array_foreach (devices, (GFunc) g_free, NULL);
        g_ptr_array_free (devices, TRUE);

        recompute_presentables (pool);

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
        GduDevice *ret;

        ret = g_hash_table_lookup (pool->priv->object_path_to_device, object_path);
        if (ret != NULL)
                g_object_ref (ret);

        return ret;
}

/**
 * gdu_pool_get_by_device_file:
 * @pool: the device pool
 * @device_file: the UNIX block special device file, e.g. /dev/sda1.
 *
 * Looks up #GduDevice object for @device_file.
 *
 * Returns: A #GduDevice object for @object_path, otherwise
 * #NULL. Caller must unref this object using g_object_unref().
 **/
GduDevice *
gdu_pool_get_by_device_file (GduPool *pool, const char *device_file)
{
        GHashTableIter iter;
        GduDevice *device;
        GduDevice *ret;

        ret = NULL;

        /* TODO: use lookaside hash table */

        g_hash_table_iter_init (&iter, pool->priv->object_path_to_device);
        while (g_hash_table_iter_next (&iter, NULL, (gpointer) &device)) {

                if (g_strcmp0 (gdu_device_get_device_file (device), device_file) == 0) {
                        ret = g_object_ref (device);
                        goto out;
                }
        }

 out:
        return ret;
}

static GduDevice *
find_extended_partition (GduPool *pool, const gchar *partition_table_object_path)
{
        GHashTableIter iter;
        GduDevice *device;
        GduDevice *ret;

        ret = NULL;

        g_hash_table_iter_init (&iter, pool->priv->object_path_to_device);
        while (g_hash_table_iter_next (&iter, NULL, (gpointer) &device)) {

                if (!gdu_device_is_partition (device))
                        continue;

                if (g_strcmp0 (gdu_device_partition_get_slave (device), partition_table_object_path) == 0) {
                        gint type;

                        type = strtol (gdu_device_partition_get_type (device), NULL, 0);
                        if (type == 0x05 || type == 0x0f || type == 0x85) {
                                ret = device;
                                goto out;
                        }
                }

        }

 out:
        return ret;
}

static void
device_recurse (GduPool *pool, GduDevice *device, GList **ret, guint depth)
{
        gboolean insert_after;

        /* cycle "detection" */
        g_assert (depth < 100);

        insert_after = FALSE;

        if (gdu_device_is_partition (device)) {
                const gchar *partition_table_object_path;
                GduDevice *partition_table;

                partition_table_object_path = gdu_device_partition_get_slave (device);
                partition_table = gdu_pool_get_by_object_path (pool, partition_table_object_path);

                /* we want the partition table to come before any partition */
                if (partition_table != NULL)
                        device_recurse (pool, partition_table, ret, depth + 1);

                if (g_strcmp0 (gdu_device_partition_get_scheme (device), "mbr") == 0 &&
                    gdu_device_partition_get_number (device) >= 5) {
                        GduDevice *extended_partition;

                        /* logical MSDOS partition, ensure that the extended partition comes before us */
                        extended_partition = find_extended_partition (pool, partition_table_object_path);
                        if (extended_partition != NULL) {
                                device_recurse (pool, extended_partition, ret, depth + 1);
                        }
                }

                if (partition_table != NULL)
                        g_object_unref (partition_table);
        }

        if (gdu_device_is_luks_cleartext (device)) {
                const gchar *luks_device_object_path;
                GduDevice *luks_device;

                luks_device_object_path = gdu_device_luks_cleartext_get_slave (device);
                luks_device = gdu_pool_get_by_object_path (pool, luks_device_object_path);

                /* the LUKS device must be before the cleartext device */
                if (luks_device != NULL) {
                        device_recurse (pool, luks_device, ret, depth + 1);
                        g_object_unref (luks_device);
                }
        }

        if (gdu_device_is_linux_md (device)) {
                gchar **slaves;
                guint n;

                /* Linux-MD slaves must come *after* the array itself */
                insert_after = TRUE;

                slaves = gdu_device_linux_md_get_slaves (device);
                for (n = 0; slaves != NULL && slaves[n] != NULL; n++) {
                        GduDevice *slave;

                        slave = gdu_pool_get_by_object_path (pool, slaves[n]);
                        if (slave != NULL) {
                                device_recurse (pool, slave, ret, depth + 1);
                                g_object_unref (slave);
                        }
                }

        }

        if (!g_list_find (*ret, device)) {
                if (insert_after)
                        *ret = g_list_append (*ret, device);
                else
                        *ret = g_list_prepend (*ret, device);
        }
}

/**
 * gdu_pool_get_devices:
 * @pool: A #GduPool.
 *
 * Get a list of all devices. The returned list is topologically sorted, e.g.
 * for any device A with a dependency on a device B, A is guaranteed to appear
 * after B.
 *
 * Returns: A #GList of #GduDevice objects. Caller must free this
 * (unref all objects, then use g_list_free()).
 **/
GList *
gdu_pool_get_devices (GduPool *pool)
{
        GList *list;
        GList *ret;
        GList *l;

        ret = NULL;

        list = g_hash_table_get_values (pool->priv->object_path_to_device);

        for (l = list; l != NULL; l = l->next) {
                GduDevice *device = GDU_DEVICE (l->data);

                device_recurse (pool, device, &ret, 0);
        }

        g_assert (g_list_length (ret) == g_list_length (list));

        g_list_free (list);

        g_list_foreach (ret, (GFunc) g_object_ref, NULL);

        ret = g_list_reverse (ret);

        return ret;
}

/**
 * gdu_pool_get_presentables:
 * @pool: A #GduPool
 *
 * Get a list of all presentables.
 *
 * Returns: A #GList of objects implementing the #GduPresentable
 * interface. Caller must free this (unref all objects, then use
 * g_list_free()).
 **/
GList *
gdu_pool_get_presentables (GduPool *pool)
{
        GList *ret;
        ret = g_list_copy (pool->priv->presentables);
        g_list_foreach (ret, (GFunc) g_object_ref, NULL);
        return ret;
}

GList *
gdu_pool_get_enclosed_presentables (GduPool *pool, GduPresentable *presentable)
{
        GList *l;
        GList *ret;

        ret = NULL;
        for (l = pool->priv->presentables; l != NULL; l = l->next) {
                GduPresentable *p = l->data;
                GduPresentable *e;

                e = gdu_presentable_get_enclosing_presentable (p);
                if (e != NULL) {
                        if (gdu_presentable_equals (e, presentable))
                                ret = g_list_prepend (ret, g_object_ref (p));

                        g_object_unref (e);
                }
        }

        return ret;
}

/**
 * gdu_pool_get_volume_by_device:
 * @pool: A #GduPool.
 * @device: A #GduDevice.
 *
 * Given @device, find the #GduVolume object for it.
 *
 * Returns: A #GduVolume object or #NULL if no @device isn't a
 * volume. Caller must free this object with g_object_unref().
 **/
GduPresentable *
gdu_pool_get_volume_by_device (GduPool *pool, GduDevice *device)
{
        GduPresentable *ret;
        GList *l;

        /* TODO: use lookaside hash table */

        ret = NULL;

        for (l = pool->priv->presentables; l != NULL; l = l->next) {
                GduPresentable *p = GDU_PRESENTABLE (l->data);
                GduDevice *d;
                const gchar *object_path;

                if (!GDU_IS_VOLUME (p))
                        continue;

                d = gdu_presentable_get_device (p);
                if (d == NULL)
                        continue;

                object_path = gdu_device_get_object_path (d);
                g_object_unref (d);

                if (g_strcmp0 (object_path, gdu_device_get_object_path (device)) == 0) {
                        ret = g_object_ref (p);
                        goto out;
                }
        }

 out:
        return ret;
}

/**
 * gdu_pool_get_drive_by_device:
 * @pool: A #GduPool.
 * @device: A #GduDevice.
 *
 * Given @device, find the #GduDrive object for it.
 *
 * Returns: A #GduDrive object or #NULL if no @device isn't a
 * drive. Caller must free this object with g_object_unref().
 **/
GduPresentable *
gdu_pool_get_drive_by_device (GduPool *pool, GduDevice *device)
{
        GduPresentable *ret;
        GList *l;

        /* TODO: use lookaside hash table */

        ret = NULL;

        for (l = pool->priv->presentables; l != NULL; l = l->next) {
                GduPresentable *p = GDU_PRESENTABLE (l->data);
                GduDevice *d;
                const gchar *object_path;

                if (!GDU_IS_DRIVE (p))
                        continue;

                d = gdu_presentable_get_device (p);
                if (d == NULL)
                        continue;

                object_path = gdu_device_get_object_path (d);
                g_object_unref (d);

                if (g_strcmp0 (object_path, gdu_device_get_object_path (device)) == 0) {
                        ret = g_object_ref (p);
                        goto out;
                }
        }

 out:
        return ret;
}

GduPresentable *
gdu_pool_get_presentable_by_id (GduPool *pool, const gchar *id)
{
        GduPresentable *ret;
        GList *l;

        /* TODO: use lookaside hash table */

        ret = NULL;

        for (l = pool->priv->presentables; l != NULL; l = l->next) {
                GduPresentable *p = GDU_PRESENTABLE (l->data);

                if (g_strcmp0 (id, gdu_presentable_get_id (p)) == 0) {
                        ret = g_object_ref (p);
                        goto out;
                }
        }

 out:
        return ret;
}


/* ---------------------------------------------------------------------------------------------------- */

typedef struct {
        GduPool *pool;
        GduPoolLinuxMdStartCompletedFunc callback;
        gpointer user_data;
} LinuxMdStartData;

static void
op_linux_md_start_cb (DBusGProxy *proxy, char *assembled_array_object_path, GError *error, gpointer user_data)
{
        LinuxMdStartData *data = user_data;
        _gdu_error_fixup (error);
        if (data->callback != NULL)
                data->callback (data->pool, assembled_array_object_path, error, data->user_data);
        g_object_unref (data->pool);
        g_free (data);
}

/**
 * gdu_pool_op_linux_md_start:
 * @pool: A #GduPool.
 * @component_objpaths: A #GPtrArray of object paths.
 * @callback: Callback function.
 * @user_data: User data to pass to @callback.
 *
 * Starts a Linux md Software Array.
 **/
void
gdu_pool_op_linux_md_start (GduPool *pool,
                            GPtrArray *component_objpaths,
                            GduPoolLinuxMdStartCompletedFunc callback,
                            gpointer user_data)
{
        LinuxMdStartData *data;
        char *options[16];

        options[0] = NULL;

        data = g_new0 (LinuxMdStartData, 1);
        data->pool = g_object_ref (pool);
        data->callback = callback;
        data->user_data = user_data;

        org_freedesktop_DeviceKit_Disks_linux_md_start_async (pool->priv->proxy,
                                                              component_objpaths,
                                                              (const char **) options,
                                                              op_linux_md_start_cb,
                                                              data);
}

/* ---------------------------------------------------------------------------------------------------- */

typedef struct {
        GduPool *pool;
        GduPoolLinuxMdCreateCompletedFunc callback;
        gpointer user_data;
} LinuxMdCreateData;

static void
op_linux_md_create_cb (DBusGProxy *proxy, char *assembled_array_object_path, GError *error, gpointer user_data)
{
        LinuxMdCreateData *data = user_data;
        _gdu_error_fixup (error);
        if (data->callback != NULL)
                data->callback (data->pool, assembled_array_object_path, error, data->user_data);
        g_object_unref (data->pool);
        g_free (data);
}

/**
 * gdu_pool_op_linux_md_create:
 * @pool: A #GduPool.
 * @component_objpaths: A #GPtrArray of object paths.
 * @level: RAID level.
 * @name: Name of array.
 * @callback: Callback function.
 * @user_data: User data to pass to @callback.
 *
 * Creates a Linux md Software Array.
 **/
void
gdu_pool_op_linux_md_create (GduPool *pool,
                             GPtrArray *component_objpaths,
                             const gchar *level,
                             guint64      stripe_size,
                             const gchar *name,
                             GduPoolLinuxMdCreateCompletedFunc callback,
                             gpointer user_data)
{
        LinuxMdCreateData *data;
        char *options[16];

        options[0] = NULL;

        data = g_new0 (LinuxMdCreateData, 1);
        data->pool = g_object_ref (pool);
        data->callback = callback;
        data->user_data = user_data;

        org_freedesktop_DeviceKit_Disks_linux_md_create_async (pool->priv->proxy,
                                                               component_objpaths,
                                                               level,
                                                               stripe_size,
                                                               name,
                                                               (const char **) options,
                                                               op_linux_md_create_cb,
                                                               data);
}

/* ---------------------------------------------------------------------------------------------------- */

/**
 * gdu_pool_get_daemon_version:
 * @pool: A #GduPool.
 *
 * Get the version of the DeviceKit-daemon on the system.
 *
 * Returns: The version of DeviceKit-disks daemon. Caller must free
 * this string using g_free().
 **/
char *
gdu_pool_get_daemon_version (GduPool *pool)
{
        return g_strdup (pool->priv->daemon_version);
}

/**
 * gdu_pool_is_daemon_inhibited:
 * @pool: A #GduPool.
 *
 * Checks if the daemon is currently inhibited.
 *
 * Returns: %TRUE if the daemon is inhibited.
 **/
gboolean
gdu_pool_is_daemon_inhibited (GduPool *pool)
{
        DBusGProxy *prop_proxy;
        gboolean ret;
        GError *error;
        GValue value = {0};

        /* TODO: this is a currently a synchronous call; when we port to EggDBus this will be fixed */

        ret = TRUE;

	prop_proxy = dbus_g_proxy_new_for_name (pool->priv->bus,
                                                "org.freedesktop.DeviceKit.Disks",
                                                "/org/freedesktop/DeviceKit/Disks",
                                                "org.freedesktop.DBus.Properties");
        error = NULL;
        if (!dbus_g_proxy_call (prop_proxy,
                                "Get",
                                &error,
                                G_TYPE_STRING,
                                "org.freedesktop.DeviceKit.Disks",
                                G_TYPE_STRING,
                                "daemon-is-inhibited",
                                G_TYPE_INVALID,
                                G_TYPE_VALUE,
                                &value,
                                G_TYPE_INVALID)) {
                g_warning ("Couldn't call Get() to determine if daemon is inhibited  for /: %s", error->message);
                g_error_free (error);
                ret = TRUE;
                goto out;
        }

        ret = g_value_get_boolean (&value);

 out:
        g_object_unref (prop_proxy);
        return ret;
}


/**
 * gdu_pool_get_known_filesystems:
 * @pool: A #GduPool.
 *
 * Get a list of file systems known to the DeviceKit-disks daemon.
 *
 * Returns: A #GList of #GduKnownFilesystem objects. Caller must free
 * this (unref all objects, then use g_list_free()).
 **/
GList *
gdu_pool_get_known_filesystems (GduPool *pool)
{
        GList *ret;
        ret = g_list_copy (pool->priv->known_filesystems);
        g_list_foreach (ret, (GFunc) g_object_ref, NULL);
        return ret;
}

/**
 * gdu_pool_get_known_filesystem_by_id:
 * @pool: A #GduPool.
 * @id: Identifier for the file system, e.g. <literal>ext3</literal> or <literal>vfat</literal>.
 *
 * Looks up a known file system by id.
 *
 * Returns: A #GduKnownFilesystem object or #NULL if file system
 * corresponding to @id exists. Caller must free this object using
 * g_object_unref().
 **/
GduKnownFilesystem *
gdu_pool_get_known_filesystem_by_id (GduPool *pool, const char *id)
{
        GList *l;
        GduKnownFilesystem *ret;

        ret = NULL;
        for (l = pool->priv->known_filesystems; l != NULL; l = l->next) {
                GduKnownFilesystem *kfs = GDU_KNOWN_FILESYSTEM (l->data);
                if (strcmp (gdu_known_filesystem_get_id (kfs), id) == 0) {
                        ret = g_object_ref (kfs);
                        goto out;
                }
        }
out:
        return ret;
}

/**
 * gdu_pool_supports_luks_devices:
 * @pool: A #GduPool.
 *
 * Determine if the DeviceKit-disks daemon supports LUKS encrypted
 * devices.
 *
 * Returns: #TRUE only if the daemon supports LUKS encrypted devices.
 **/
gboolean
gdu_pool_supports_luks_devices (GduPool *pool)
{
        return pool->priv->supports_luks_devices;
}


