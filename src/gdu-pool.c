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
#include "gdu-drive.h"
#include "gdu-volume.h"
#include "gdu-volume-hole.h"

#include "devkit-disks-daemon-glue.h"
#include "gdu-marshal.h"

enum {
        DEVICE_ADDED,
        DEVICE_REMOVED,
        PRESENTABLE_ADDED,
        PRESENTABLE_REMOVED,
        LAST_SIGNAL,
};

static GObjectClass *parent_class = NULL;
static guint signals[LAST_SIGNAL] = { 0 };

struct _GduPoolPrivate
{
        DBusGConnection *bus;
        DBusGProxy *proxy;

        GHashTable *devices;     /* object path -> GduDevice* */
        GHashTable *volumes;     /* object path -> GduVolume* */
        GHashTable *drives;      /* object path -> GduDrive* */
        GHashTable *drive_holes; /* object path -> GList of GduVolumeHole* */

        GList *presentables;
};

G_DEFINE_TYPE (GduPool, gdu_pool, G_TYPE_OBJECT);

static void
remove_holes (GduPool *pool, const char *drive_object_path)
{
        GList *l;
        GList *holes;

        holes = g_hash_table_lookup (pool->priv->drive_holes, drive_object_path);
        for (l = holes; l != NULL; l = l->next) {
                GduPresentable *presentable = l->data;
                pool->priv->presentables = g_list_remove (pool->priv->presentables, presentable);
                g_signal_emit (pool, signals[PRESENTABLE_REMOVED], 0, presentable);
                g_object_unref (presentable);
        }
        g_hash_table_remove (pool->priv->drive_holes, drive_object_path);
}

static void
free_list_of_holes (gpointer data)
{
        GList *l = data;
        g_list_foreach (l, (GFunc) g_object_unref, NULL);
        g_list_free (l);
}

static void
gdu_pool_finalize (GduPool *pool)
{
        dbus_g_connection_unref (pool->priv->bus);
        g_object_unref (pool->priv->proxy);
        g_hash_table_unref (pool->priv->devices);
        g_hash_table_unref (pool->priv->volumes);
        g_hash_table_unref (pool->priv->drives);
        g_hash_table_unref (pool->priv->drive_holes);

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

        signals[PRESENTABLE_ADDED] =
                g_signal_new ("presentable_added",
                              G_TYPE_FROM_CLASS (klass),
                              G_SIGNAL_RUN_LAST,
                              G_STRUCT_OFFSET (GduPoolClass, presentable_added),
                              NULL, NULL,
                              g_cclosure_marshal_VOID__OBJECT,
                              G_TYPE_NONE, 1,
                              GDU_TYPE_PRESENTABLE);

        signals[PRESENTABLE_REMOVED] =
                g_signal_new ("presentable_removed",
                              G_TYPE_FROM_CLASS (klass),
                              G_SIGNAL_RUN_LAST,
                              G_STRUCT_OFFSET (GduPoolClass, presentable_removed),
                              NULL, NULL,
                              g_cclosure_marshal_VOID__OBJECT,
                              G_TYPE_NONE, 1,
                              GDU_TYPE_PRESENTABLE);
}

static void
gdu_pool_init (GduPool *pool)
{
        pool->priv = g_new0 (GduPoolPrivate, 1);
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

static void
add_holes (GduPool *pool,
           GduDrive *drive,
           GduPresentable *presentable,
           gboolean ignore_logical,
           guint64 start,
           guint64 size)
{
        int n, num_entries;
        int max_number;
        guint64 *offsets;
        guint64 *sizes;
        GduDevice *drive_device;
        GduVolumeHole *hole;
        PartEntry *entries;
        guint64 cursor;
        guint64 gap_size;
        guint64 gap_position;

        drive_device = gdu_presentable_get_device (GDU_PRESENTABLE (drive));

        /* no point if adding holes if there's no media */
        if (!gdu_device_is_media_available (drive_device))
                goto out;

        /*g_print ("Adding holes for %s between %lld and %lld (ignore_logical=%d)\n",
                 gdu_device_get_device_file (drive_device),
                 start,
                 start + size,
                 ignore_logical);*/

        offsets = (guint64*) ((gdu_device_partition_table_get_offsets (drive_device))->data);
        sizes = (guint64*) ((gdu_device_partition_table_get_sizes (drive_device))->data);
        max_number = gdu_device_partition_table_get_max_number (drive_device);

        entries = g_new0 (PartEntry, max_number);
        for (n = 0, num_entries = 0; n < max_number; n++) {
                /* ignore unused partition table entries */
                if (offsets[n] == 0)
                        continue;

                /* only consider partitions in the given space */
                if (offsets[n] <= start)
                        continue;
                if (offsets[n] >= start + size)
                        continue;

                /* ignore logical partitions if requested */
                if (ignore_logical) {
                        if (n >= 4)
                                continue;
                }

                entries[num_entries].number = n + 1;
                entries[num_entries].offset = offsets[n];
                entries[num_entries].size = sizes[n];
                num_entries++;
                //g_print ("%d: offset=%lld size=%lld\n", entries[n].number, entries[n].offset, entries[n].size);
        }
        entries = g_realloc (entries, num_entries * sizeof (PartEntry));

        g_qsort_with_data (entries, num_entries, sizeof (PartEntry), (GCompareDataFunc) part_entry_compare, NULL);

        for (n = 0, cursor = start; n <= num_entries; n++) {
                if (n < num_entries) {

                        /*g_print (" %d: offset=%lldMB size=%lldMB\n",
                                 entries[n].number,
                                 entries[n].offset / (1000 * 1000),
                                 entries[n].size / (1000 * 1000));*/


                        gap_size = entries[n].offset - cursor;
                        gap_position = entries[n].offset - gap_size;
                        cursor = entries[n].offset + entries[n].size;
                } else {
                        /* trailing free space */
                        gap_size = start + size - cursor;
                        gap_position = start + size - gap_size;
                }

                /* ignore free space < 1MB */
                if (gap_size >= 1024 * 1024) {
                        GList *hole_list;
                        char *orig_key;

                        /*g_print ("  -> adding gap=%lldMB @ %lldMB\n",
                                 gap_size / (1000 * 1000),
                                 gap_position  / (1000 * 1000));*/

                        hole = gdu_volume_hole_new (gap_position, gap_size, presentable);
                        hole_list = NULL;
                        if (g_hash_table_lookup_extended (pool->priv->drive_holes,
                                                          gdu_device_get_object_path (drive_device),
                                                          (gpointer *) &orig_key,
                                                          (gpointer *) &hole_list)) {
                                g_hash_table_steal (pool->priv->drive_holes, orig_key);
                                g_free (orig_key);
                        }
                        hole_list = g_list_prepend (hole_list, g_object_ref (hole));
                        /*g_print ("hole list now len=%d for %s\n",
                                   g_list_length (hole_list),
                                   gdu_device_get_object_path (drive_device));*/
                        g_hash_table_insert (pool->priv->drive_holes,
                                             g_strdup (gdu_device_get_object_path (drive_device)),
                                             hole_list);
                        pool->priv->presentables = g_list_prepend (pool->priv->presentables, GDU_PRESENTABLE (hole));
                        g_signal_emit (pool, signals[PRESENTABLE_ADDED], 0, GDU_PRESENTABLE (hole));
                }

        }

        g_free (entries);
out:
        g_object_unref (drive_device);
}

/* typically called on 'change' event for a drive */
static void
update_holes (GduPool *pool, const char *drive_object_path)
{
        GduDrive *drive;
        GduDevice *drive_device;
        GList *l;

        /* first remove all existing holes */
        remove_holes (pool, drive_object_path);

        /* then add new holes */
        drive = g_hash_table_lookup (pool->priv->drives, drive_object_path);
        drive_device = gdu_presentable_get_device (GDU_PRESENTABLE (drive));

        /* add holes between primary partitions */
        add_holes (pool,
                   drive,
                   GDU_PRESENTABLE (drive),
                   TRUE,
                   0,
                   gdu_device_get_size (drive_device));

        /* add holes between logical partitions residing in extended partitions */
        for (l = pool->priv->presentables; l != NULL; l = l->next) {
                GduPresentable *presentable = l->data;

                if (gdu_presentable_get_enclosing_presentable (presentable) == GDU_PRESENTABLE (drive)) {
                        GduDevice *partition_device;

                        partition_device = gdu_presentable_get_device (presentable);
                        if (partition_device != NULL &&
                            gdu_device_is_partition (partition_device)) {
                                int partition_type;
                                partition_type = strtol (gdu_device_partition_get_type (partition_device), NULL, 0);
                                if (partition_type == 0x05 ||
                                    partition_type == 0x0f ||
                                    partition_type == 0x85) {
                                        add_holes (pool,
                                                   drive,
                                                   presentable,
                                                   FALSE,
                                                   gdu_device_partition_get_offset (partition_device),
                                                   gdu_device_partition_get_size (partition_device));

                                }
                        }
                        if (partition_device != NULL)
                                g_object_unref (partition_device);
                }
        }

        if (drive_device != NULL)
                g_object_unref (drive_device);
}

static GduDevice *
gdu_pool_add_device_by_object_path (GduPool *pool, const char *object_path)
{
        GduDevice *device;

        //g_print ("object path = %s\n", object_path);

        device = gdu_device_new_from_object_path (pool, object_path);
        if (device != NULL) {
                g_hash_table_insert (pool->priv->devices, g_strdup (object_path), device);
                g_signal_emit (pool, signals[DEVICE_ADDED], 0, device);

                if (gdu_device_is_drive (device)) {
                        GduDrive *drive;
                        drive = gdu_drive_new_from_device (device);

                        g_hash_table_insert (pool->priv->drives, g_strdup (object_path), g_object_ref (drive));

                        pool->priv->presentables = g_list_prepend (pool->priv->presentables, GDU_PRESENTABLE (drive));
                        g_signal_emit (pool, signals[PRESENTABLE_ADDED], 0, GDU_PRESENTABLE (drive));

                        /* Now create and add GVolumeHole objects representing space on the partitioned
                         * device space not yet claimed by any partition. We don't add holes for empty
                         * space on the extended partition; that's handled below.
                         */
                        add_holes (pool,
                                   drive,
                                   GDU_PRESENTABLE (drive),
                                   TRUE,
                                   0,
                                   gdu_device_get_size (device));
                }

                if (gdu_device_is_partition (device)) {
                        GduVolume *volume;
                        GduPresentable *enclosing_presentable;

                        /* make sure logical partitions are enclosed by the volume representing
                         * the extended partition
                         */
                        enclosing_presentable = NULL;
                        if (strcmp (gdu_device_partition_get_scheme (device), "mbr") == 0 &&
                            gdu_device_partition_get_number (device) > 4) {
                                GHashTableIter iter;
                                GduVolume *sibling;

                                /* Iterate over all volumes that also has the same drive and check
                                 * for partition type 0x05, 0x0f, 0x85
                                 *
                                 * TODO: would be nice to have DeviceKit-disks properties to avoid
                                 *       harcoding this for msdos only.
                                 */
                                g_hash_table_iter_init (&iter, pool->priv->volumes);
                                while (g_hash_table_iter_next (&iter, NULL, (gpointer *) &sibling)) {
                                        int sibling_type;
                                        GduDevice *sibling_device;

                                        sibling_device = gdu_presentable_get_device (GDU_PRESENTABLE (sibling));
                                        if (!gdu_device_is_partition (sibling_device))
                                                continue;
                                        sibling_type = strtol (gdu_device_partition_get_type (sibling_device), NULL, 0);
                                        if (sibling_type == 0x05 ||
                                            sibling_type == 0x0f ||
                                            sibling_type == 0x85) {
                                                enclosing_presentable = GDU_PRESENTABLE (sibling);
                                                break;
                                        }
                                }

                                if (enclosing_presentable == NULL) {
                                        g_warning ("TODO: FIXME: handle logical partition %s arriving "
                                                   "before extended", gdu_device_get_device_file (device));
                                        /* .. at least we'll fall back to the drive for now ... */
                                }
                        }

                        if (enclosing_presentable == NULL) {
                                GduDrive *enclosing_drive;
                                enclosing_drive = g_hash_table_lookup (pool->priv->drives,
                                                                       gdu_device_partition_get_slave (device));

                                if (enclosing_drive != NULL)
                                        enclosing_presentable = GDU_PRESENTABLE (enclosing_drive);
                        }

                        /* add the partition */
                        volume = gdu_volume_new_from_device (device, enclosing_presentable);
                        g_hash_table_insert (pool->priv->volumes, g_strdup (object_path), g_object_ref (volume));
                        pool->priv->presentables = g_list_prepend (pool->priv->presentables, GDU_PRESENTABLE (volume));
                        g_signal_emit (pool, signals[PRESENTABLE_ADDED], 0, GDU_PRESENTABLE (volume));

                        /* add holes for the extended partition */
                        if (strcmp (gdu_device_partition_get_scheme (device), "mbr") == 0) {
                                int partition_type;
                                partition_type = strtol (gdu_device_partition_get_type (device), NULL, 0);
                                if (partition_type == 0x05 ||
                                    partition_type == 0x0f ||
                                    partition_type == 0x85) {
                                        GduDrive *enclosing_drive;

                                        enclosing_drive = g_hash_table_lookup (
                                                pool->priv->drives,
                                                gdu_device_partition_get_slave (device));
                                        if (enclosing_drive != NULL) {
                                                /* Now create and add GVolumeHole objects representing space on
                                                 * the extended partition not yet claimed by any partition.
                                                 */
                                                add_holes (pool,
                                                           enclosing_drive,
                                                           GDU_PRESENTABLE (volume),
                                                           FALSE,
                                                           gdu_device_partition_get_offset (device),
                                                           gdu_device_partition_get_size (device));
                                        }
                                }
                        }

                }

                /* TODO: handle whole disk devices */

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
                GList *l;
                GList *ll;

                gdu_device_removed (device);

                g_signal_emit (pool, signals[DEVICE_REMOVED], 0, device);
                g_hash_table_remove (pool->priv->devices, object_path);
                g_hash_table_remove (pool->priv->volumes, object_path);
                if (g_hash_table_remove (pool->priv->drives, object_path))
                        remove_holes (pool, object_path);

                for (l = pool->priv->presentables; l != NULL; l = ll) {
                        GduPresentable *presentable = GDU_PRESENTABLE (l->data);
                        GduDevice *d;

                        ll = l->next;

                        d = gdu_presentable_get_device (presentable);
                        if (d == device) {
                                pool->priv->presentables = g_list_remove (pool->priv->presentables, presentable);
                                g_signal_emit (pool, signals[PRESENTABLE_REMOVED], 0, presentable);
                                g_object_unref (presentable);
                        }
                        if (d != NULL)
                                g_object_unref (d);
                }
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

                if (g_hash_table_lookup (pool->priv->drives, object_path) != NULL)
                        update_holes (pool, object_path);

        } else {
                g_warning ("unknown device to on change, object_path='%s'", object_path);
        }
}

static void
device_job_changed_signal_handler (DBusGProxy *proxy,
                                   const char *object_path,
                                   gboolean    job_in_progress,
                                   const char *job_id,
                                   gboolean    job_is_cancellable,
                                   int         job_num_tasks,
                                   int         job_cur_task,
                                   const char *job_cur_task_id,
                                   double      job_cur_task_percentage,
                                   gpointer user_data)
{
        GduPool *pool = GDU_POOL (user_data);
        GduDevice *device;

        if ((device = gdu_pool_get_by_object_path (pool, object_path)) != NULL) {
                gdu_device_job_changed (device,
                                        job_in_progress,
                                        job_id,
                                        job_is_cancellable,
                                        job_num_tasks,
                                        job_cur_task,
                                        job_cur_task_id,
                                        job_cur_task_percentage);
        } else {
                g_warning ("unknown device to on job-change, object_path='%s'", object_path);
        }
}

static int
ptr_array_strcmp (const char **a, const char **b)
{
        return strcmp (*a, *b);
}

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
        pool->priv->volumes = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_object_unref);
        pool->priv->drives = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_object_unref);
        pool->priv->drive_holes = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, free_list_of_holes);

        dbus_g_object_register_marshaller (
                gdu_marshal_VOID__STRING_BOOLEAN_STRING_BOOLEAN_INT_INT_STRING_DOUBLE,
                G_TYPE_NONE,
                G_TYPE_STRING,
                G_TYPE_BOOLEAN,
                G_TYPE_STRING,
                G_TYPE_BOOLEAN,
                G_TYPE_INT,
                G_TYPE_INT,
                G_TYPE_STRING,
                G_TYPE_DOUBLE,
                G_TYPE_INVALID);

	pool->priv->proxy = dbus_g_proxy_new_for_name (pool->priv->bus,
                                                       "org.freedesktop.DeviceKit.Disks",
                                                       "/",
                                                       "org.freedesktop.DeviceKit.Disks");
        dbus_g_proxy_add_signal (pool->priv->proxy, "DeviceAdded", G_TYPE_STRING, G_TYPE_INVALID);
        dbus_g_proxy_add_signal (pool->priv->proxy, "DeviceRemoved", G_TYPE_STRING, G_TYPE_INVALID);
        dbus_g_proxy_add_signal (pool->priv->proxy, "DeviceChanged", G_TYPE_STRING, G_TYPE_INVALID);
        dbus_g_proxy_add_signal (pool->priv->proxy,
                                 "DeviceJobChanged",
                                 G_TYPE_STRING,
                                 G_TYPE_BOOLEAN,
                                 G_TYPE_STRING,
                                 G_TYPE_BOOLEAN,
                                 G_TYPE_INT,
                                 G_TYPE_INT,
                                 G_TYPE_STRING,
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

        /* prime the list of devices */
        if (!org_freedesktop_DeviceKit_Disks_enumerate_devices (pool->priv->proxy, &devices, &error)) {
                g_warning ("Couldn't enumerate devices: %s", error->message);
                g_error_free (error);
                goto error;
        }

        /* TODO: enumerate should return the tree order.. for now we just sort the list */
        g_ptr_array_sort (devices, (GCompareFunc) ptr_array_strcmp);

        for (n = 0; n < devices->len; n++) {
                const char *object_path;
                GduDevice *device;

                object_path = devices->pdata[n];
                device = gdu_pool_add_device_by_object_path (pool, object_path);
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

GList *
gdu_pool_get_presentables (GduPool *pool)
{
        GList *ret;
        ret = g_list_copy (pool->priv->presentables);
        g_list_foreach (ret, (GFunc) g_object_ref, NULL);
        return ret;
}
