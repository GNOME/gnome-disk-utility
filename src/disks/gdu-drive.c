/* gdu-drive.c
 *
 * Copyright 2023 Mohammed Sadiq <sadiq@sadiqpk.org>
 * Copyright (C) 2008-2013 Red Hat, Inc.
 *
 * Author(s):
 *   David Zeuthen <zeuthen@gmail.com>
 *   Mohammed Sadiq <sadiq@sadiqpk.org>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#undef G_LOG_DOMAIN
#define G_LOG_DOMAIN "gdu-drive"

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <udisks/udisks.h>
#include <glib/gi18n.h>

#include "gdu-ata-smart-dialog.h"
#include "gdu-disk-settings-dialog.h"
#include "gdu-item.h"
#include "gduutils.h"
#include "gdu-block.h"
#include "gdu-drive.h"

struct _GduDrive
{
  GduItem               parent_instance;

  GduItem              *parent;

  UDisksClient         *client;
  UDisksObject         *object;
  UDisksDrive          *drive;
  UDisksDriveAta       *ata;

  GduBlock             *extended_partition;
  UDisksObjectInfo     *info;
  UDisksBlock          *block;
  UDisksObject         *partition_table;
  UDisksPartitionTable *table;
  UDisksFilesystem     *file_system;
  GString              *model;

  int                   partition_color_index;
  GListStore           *partitions;

  GduFeature            features;
  bool                  in_progress;
};


G_DEFINE_TYPE (GduDrive, gdu_drive, GDU_TYPE_ITEM)

static void
gdu_drive_set_block_color(GduDrive *self,
                          GduBlock *block)
{
  GduFeature features;

  features = gdu_item_get_features (GDU_ITEM (block));
  if (features & GDU_FEATURE_CREATE_PARTITION)
    g_object_set_data_full (G_OBJECT (block), "color", g_strdup ("grey"), g_free);
  else
    g_object_set_data_full (G_OBJECT (block), "color", g_strdup (partition_colors[self->partition_color_index++ % NUM_PARTITION_COLORS]), g_free);
}

static void
gdu_drive_add_decrypted (GduDrive     *self,
                         UDisksObject *object,
                         GduItem      *parent)
{
  g_autoptr(UDisksBlock) block = NULL;

  block = udisks_client_get_cleartext_block (self->client,
                                             udisks_object_peek_block (object));
  object = NULL;
  if (block)
    object = (gpointer)g_dbus_interface_get_object ((gpointer)block);

  if (object)
    {
      g_autoptr(GduBlock) partition = NULL;

      partition = gdu_block_new (self->client, object, parent);
      gdu_drive_set_block_color (self, partition);

      g_list_store_append (self->partitions, partition);
    }
}


static const char *
gdu_drive_get_description (GduItem *item)
{
  GduDrive *self = (GduDrive *) item;

  g_assert (GDU_IS_DRIVE (self));

  return udisks_object_info_get_description (self->info);
}

static const char *
gdu_drive_get_partition_type (GduItem *item)
{
  GduDrive *self = (GduDrive *) item;

  g_assert (GDU_IS_DRIVE (self));

  if (self->partition_table)
    {
      UDisksPartitionTable *table;
      const char *type;

      table = udisks_object_peek_partition_table (self->partition_table);
      type = udisks_partition_table_get_type_ (table);

      return udisks_client_get_partition_table_type_for_display (self->client, type);
    }

  return "—";
}

static GIcon *
gdu_drive_get_icon (GduItem *item)
{
  GduDrive *self = (GduDrive *) item;

  g_assert (GDU_IS_DRIVE (self));

  return udisks_object_info_get_icon (self->info);
}

static GduItem *
gdu_drive_get_parent (GduItem *item)
{
  GduDrive *self = (GduDrive *) item;

  g_assert (GDU_IS_DRIVE (self));

  return self->parent;
}

static guint64
gdu_drive_get_size (GduItem *item)
{
  GduDrive *self = (GduDrive *) item;

  g_assert (GDU_IS_DRIVE (self));

  if (self->drive) {
      guint64 drive_size = udisks_drive_get_size (self->drive);
      if (drive_size)
          return drive_size;
  }

  if (self->block)
    return udisks_block_get_size (self->block);

  g_return_val_if_reached (0);
}

static GListModel *
gdu_drive_get_partitions (GduItem *item)
{
  GduDrive *self = (GduDrive *) item;

  g_assert (GDU_IS_DRIVE (self));

  return G_LIST_MODEL (self->partitions);
}

static GduFeature
gdu_drive_get_features (GduItem *item)
{
  GduDrive *self = (GduDrive *)item;
  UDisksObject *drive_object;
  UDisksObject *object;
  UDisksBlock *block;
  UDisksDrive *drive = NULL;
  GduFeature features = 0;
  gboolean read_only;

  g_assert (GDU_IS_DRIVE (self));

  if (self->features)
    return self->features;

  if (self->drive)
    {
      if (udisks_drive_get_can_power_off (self->drive))
        features |= GDU_FEATURE_POWEROFF;
    }

  block = self->block;
  drive = self->drive;
  object = self->object;

  if (udisks_object_peek_drive_ata (object) &&
      !udisks_drive_get_media_removable (drive))
    {
      g_autofree char *s = NULL;
      UDisksDriveAta *ata;
      gboolean smart_is_supported;

      ata = udisks_object_peek_drive_ata (object);
      s = gdu_ata_smart_get_one_liner_assessment (ata, &smart_is_supported, NULL /* out_warning */);
      if (smart_is_supported)
        features |= GDU_FEATURE_SMART;
    }

  if (gdu_disk_settings_dialog_should_show (object))
    features |= GDU_FEATURE_SETTINGS;

  if (block == NULL)
    return features;

  read_only = udisks_block_get_read_only (block);
  drive_object = (UDisksObject *) g_dbus_object_manager_get_object (udisks_client_get_object_manager (self->client),
                                                                    udisks_block_get_drive (block));

  if (drive_object != NULL)
    {
      drive = udisks_object_peek_drive (drive_object);
      g_object_unref (drive_object);
    }

  /* TODO: don't show on CD-ROM drives etc. */
  if (udisks_block_get_size (block) > 0 ||
      (drive != NULL && !udisks_drive_get_media_change_detected (drive)))
    {
      features |= GDU_FEATURE_CREATE_IMAGE;
      features |= GDU_FEATURE_BENCHMARK;
      if (!read_only)
        {
          features |= GDU_FEATURE_RESTORE_IMAGE;
          features |= GDU_FEATURE_CREATE_PARTITION;
          if (udisks_block_get_hint_partitionable (self->block))
            features |= GDU_FEATURE_FORMAT;
        }
    }

  self->features = features;

  return features;
}

static void
gdu_drive_changed (GduItem *item)
{
  GduDrive *self = (GduDrive *) item;

  g_assert (GDU_IS_DRIVE (self));

  g_clear_object (&self->info);
  self->info = udisks_client_get_object_info (self->client, self->object);
  g_clear_object (&self->partition_table);

  if (self->drive)
    {
      g_autoptr(UDisksBlock) block= NULL;
      UDisksObject *object;

      block = udisks_client_get_block_for_drive (self->client, self->drive, false);
      object = (gpointer) g_dbus_interface_get_object ((gpointer)block);
      g_set_object (&self->partition_table, g_object_ref (object));
    }

  g_set_object (&self->block, udisks_object_peek_block (self->object));
  if (self->block)
    {
      g_clear_object (&self->drive);
      self->drive = udisks_client_get_drive_for_block (self->client, self->block);
    }
  else
    {
      g_set_object (&self->drive, udisks_object_peek_drive (self->object));
    }

  g_set_object (&self->file_system, udisks_object_peek_filesystem (self->object));

  if (udisks_object_peek_partition_table (self->object))
    gdu_drive_set_child (self, self->object);

  g_signal_emit_by_name (self, "changed", 0);
}

static void
gdu_drive_dispose (GObject *object)
{
  GduDrive *self = (GduDrive *)object;

  g_object_set_data (G_OBJECT (self->object), "gdu-drive", NULL);

  G_OBJECT_CLASS (gdu_drive_parent_class)->dispose (object);
}

static void
gdu_drive_finalize (GObject *object)
{
  GduDrive *self = (GduDrive *)object;

  g_list_store_remove_all (self->partitions);
  g_clear_object (&self->partitions);

  g_clear_object (&self->client);
  g_clear_object (&self->object);
  g_clear_object (&self->info);
  g_clear_object (&self->block);
  g_clear_object (&self->drive);
  g_clear_object (&self->table);
  g_clear_object (&self->file_system);

  G_OBJECT_CLASS (gdu_drive_parent_class)->finalize (object);
}

static void
gdu_drive_class_init (GduDriveClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GduItemClass *item_class = GDU_ITEM_CLASS (klass);

  object_class->dispose = gdu_drive_dispose;
  object_class->finalize = gdu_drive_finalize;

  item_class->get_description = gdu_drive_get_description;
  item_class->get_partition_type = gdu_drive_get_partition_type;
  item_class->get_icon = gdu_drive_get_icon;
  item_class->get_size = gdu_drive_get_size;
  item_class->get_parent = gdu_drive_get_parent;
  item_class->get_partitions = gdu_drive_get_partitions;
  item_class->get_features = gdu_drive_get_features;
  item_class->changed = gdu_drive_changed;
}

static void
gdu_drive_init (GduDrive *self)
{
  self->partitions = g_list_store_new (GDU_TYPE_BLOCK);
  self->model = g_string_new (NULL);
}

GduDrive *
gdu_drive_new (gpointer  udisk_client,
               gpointer  udisk_object,
               GduItem  *parent)
{
  GduDrive *self;
  guint64 size;

  g_return_val_if_fail (UDISKS_IS_CLIENT (udisk_client), NULL);
  g_return_val_if_fail (UDISKS_IS_OBJECT (udisk_object), NULL);
  g_return_val_if_fail (!parent || GDU_IS_ITEM (parent), NULL);

  self = g_object_new (GDU_TYPE_DRIVE, NULL);

  self->partition_color_index = 0;
  self->client = g_object_ref (udisk_client);
  self->object = g_object_ref (udisk_object);
  g_set_weak_pointer (&self->parent, parent);

  g_set_object (&self->block, udisks_object_peek_block (self->object));
  g_set_object (&self->drive, udisks_object_peek_drive (self->object));

  if (self->block == NULL && self->drive != NULL)
    self->block = udisks_client_get_block_for_drive (self->client,
                                                     self->drive,
                                                     FALSE);
  g_object_set_data (udisk_object, "gdu-drive", self);

  size = gdu_item_get_size (GDU_ITEM (self));

  /*
   * If the device has a non zero size add a block that maps the whole device.
   * If the device has a partition table, this will be removed and real partitions
   * shall be addded there.
   */
  if (size > 0)
    {
      g_autoptr(GduBlock) block = NULL;

      block = gdu_block_sized_new (udisk_client, 0, size, GDU_ITEM (self));
      gdu_drive_set_block_color (self, block);
      g_list_store_append (self->partitions, block);
    }

  /* First, add a single empty full disk partition */
  if (self->block != NULL && self->drive == NULL)
    {
      g_autoptr(GduBlock) partition = NULL;

      /* Remove previously added free partition */
      g_list_store_remove_all (self->partitions);

      partition = gdu_block_new (self->client, udisk_object, GDU_ITEM (self));
      gdu_drive_set_block_color (self, partition);
      g_list_store_append (self->partitions, partition);

      if (udisks_object_peek_encrypted (udisk_object))
        gdu_drive_add_decrypted (self, udisk_object, GDU_ITEM (partition));
    }

  /* Now, try populating the partitions if we have a partition table */
  if (udisks_object_peek_partition_table (self->object))
    gdu_drive_set_child (self, udisk_object);

  gdu_item_changed (GDU_ITEM (self));

  return self;
}

/**
 * gdu_drive_matches_object:
 * @self: A #GduDrive
 * @udisk_object: A #UDisksObject
 *
 * Check if @udisk_object and @self
 * represents the same object.
 *
 * Returns: %TRUE if @udisk_object and @self
 * represents the same object.  %False otherwise
 */
gboolean
gdu_drive_matches_object (GduDrive *self,
                          gpointer  udisk_object)
{
  g_return_val_if_fail (GDU_IS_DRIVE (self), FALSE);
  g_return_val_if_fail (UDISKS_IS_OBJECT (udisk_object), FALSE);

  if (self->object == udisk_object)
    return TRUE;

  if (self->block &&
      self->block == udisks_object_peek_block (udisk_object))
    return TRUE;

  if (self->drive &&
      self->drive == udisks_object_peek_drive (udisk_object))
    return TRUE;

  if (self->partition_table &&
      udisks_object_peek_partition (udisk_object))
    {
      g_autoptr(UDisksPartitionTable) obj_table = NULL;
      UDisksPartitionTable *table;
      UDisksPartition *partition;

      table = udisks_object_peek_partition_table (self->partition_table);
      partition = udisks_object_peek_partition (udisk_object);
      obj_table = udisks_client_get_partition_table (self->client, partition);

      if (obj_table == table)
        return TRUE;
    }

  return FALSE;
}

const char *
gdu_drive_get_name (GduDrive *self)
{
  g_return_val_if_fail (GDU_IS_DRIVE (self), NULL);

  return udisks_object_info_get_name (self->info);
}

const char *
gdu_drive_get_model (GduDrive *self)
{
  g_return_val_if_fail (GDU_IS_DRIVE (self), NULL);

  if (self->model->len)
    return self->model->str;

  if (!self->model->len && self->drive)
    {
      const char *vendor, *model, *revision;

      vendor = udisks_drive_get_vendor (self->drive);
      model = udisks_drive_get_model (self->drive);
      revision = udisks_drive_get_revision (self->drive);

      if (!vendor || !*vendor)
        g_string_append (self->model, model);
      else if (!model || !*model)
        g_string_append (self->model, vendor);
      else
        g_string_append_printf (self->model, "%s %s", vendor, model);
      if (revision && *revision)
        g_string_append_printf (self->model, " (%s)", revision);
    }
  else if (!self->model->len)
    g_string_append (self->model, "Loop Device");

  return self->model->str;
}

const char *
gdu_drive_get_serial (GduDrive *self)
{
  g_return_val_if_fail (GDU_IS_DRIVE (self), NULL);

  if (self->drive)
    return udisks_drive_get_serial (self->drive);

  return "—";
}

/**
 * gdu_drive_get_siblings:
 * @self: A #GduDrive
 *
 * Returns a list of siblings of @self
 *
 * Returns: (transfer container) (nullable): A #GList
 * of #UDisksDrive. Free with g_list_free()
 */
GList *
gdu_drive_get_siblings (GduDrive *self)
{
  GList *objects = NULL;
  GList *siblings;

  g_return_val_if_fail (GDU_IS_DRIVE (self), false);

  siblings = udisks_client_get_drive_siblings (self->client, self->drive);
  for (GList *l = siblings; l != NULL; l = l->next)
    {
      UDisksDrive *sibling = UDISKS_DRIVE (l->data);
      UDisksObject *sibling_object = (UDisksObject *) g_dbus_interface_get_object (G_DBUS_INTERFACE (sibling));

      if (sibling_object != NULL)
        objects = g_list_append (objects, sibling_object);
    }

  g_list_free_full (siblings, g_object_unref);

  return objects;
}

static int
partition_cmp (gpointer a,
               gpointer b)
{
  gint64 offset_a, offset_b;

  offset_a = udisks_partition_get_offset (a);
  offset_b = udisks_partition_get_offset (b);

  if (offset_a > offset_b)
    return 1;

  if (udisks_partition_get_is_container (b) &&
      udisks_partition_get_is_contained (a))
    return 1;

  return -1;
}

static GduItem *
block_get_parent (GduDrive *self,
                  guint64   block_begin_offset,
                  guint64   block_end_offset)
{
  GListModel *partitions;
  guint64 n_items;

  g_assert (GDU_IS_DRIVE (self));

  partitions = G_LIST_MODEL (self->partitions);
  n_items = g_list_model_get_n_items (partitions);

  /*
   * Iterate over all preceding partitions and if their offsets overlap,
   * consider the partition as parent.
   */
  for (guint i = 0; i < n_items; i++)
    {
      g_autoptr(GduItem) partition = NULL;
      guint64 offset_start, offset_end;

      partition = g_list_model_get_item (partitions, i);
      offset_start = gdu_block_get_offset (GDU_BLOCK (partition));
      offset_end = offset_start + gdu_item_get_size (GDU_ITEM (partition));

      if (block_begin_offset >= offset_start &&
          block_end_offset <= offset_end)
        return partition;
    }

  /* We found no overlapping offsets, return the drive as the parent */
  return GDU_ITEM (self);
}

/**
 * gdu_drive_get_object:
 * @self: A #GduDrive
 * @udisk_object: A #UDisksObject
 *
 * Set partition table for the drive @self.
 * This will reload all partition details of the drive.
 */
void
gdu_drive_set_child (GduDrive *self,
                     gpointer  udisk_object)
{
  /* UDisksPartition *upartition; */

  g_return_if_fail (GDU_IS_DRIVE (self));
  g_return_if_fail (UDISKS_IS_OBJECT (udisk_object));
  g_return_if_fail (udisks_object_peek_partition_table (udisk_object));

  g_set_object (&self->block, udisks_object_get_block (self->object));
  g_set_object (&self->drive, udisks_object_get_drive (self->object));
  self->partition_color_index = 0;

  if (self->block == NULL && self->drive != NULL)
    self->block = udisks_client_get_block_for_drive (self->client,
                                                     self->drive,
                                                     FALSE);

  g_set_object (&self->partition_table, udisk_object);
  g_list_store_remove_all (self->partitions);

  if (udisks_block_get_size (udisks_object_peek_block (udisk_object)) == 0)
    return;

  if (self->partition_table)
    {
      UDisksPartitionTable *table;
      GList *partitions;
      guint64 begin = 0, prev_end = 0, end = 0;
      guint64 free_space_slack;
      guint64 extended_partition_end_offset = 0;

      table = udisks_object_peek_partition_table (self->partition_table);
      partitions = udisks_client_get_partitions (self->client, table);
      partitions = g_list_sort (partitions, (GCompareFunc)partition_cmp);

      /* include "Free Space" elements if there is at least this much slack between
       * partitions (currently 1% of the disk, but at most 1MiB)
       */
      free_space_slack = MIN (gdu_item_get_size (GDU_ITEM (self)) / 100, 1024*1024);

      for (GList *part = partitions; part && part->data; part = part->next)
        {
          UDisksObject *object;
          GduBlock *partition;
          GduItem *parent;
          guint64 size;

          begin = udisks_partition_get_offset (part->data);
          size = udisks_partition_get_size (part->data);
          end = begin + size;
          parent = block_get_parent (self, begin, end);

          /* If there is some space between current block start and preceding
           * block end, add it as a free space partition.
           */
          if (begin > prev_end &&
              begin - prev_end > free_space_slack)
            {
              /* the free space block might have different parent than the current block */
              GduItem* free_space_parent = block_get_parent (self, prev_end, begin);
              partition = gdu_block_sized_new (self->client, prev_end, begin - prev_end, free_space_parent);
              gdu_drive_set_block_color (self, partition);

              g_list_store_append (self->partitions, partition);
              g_clear_object (&partition);
            }

          object = (gpointer)g_dbus_interface_get_object (part->data);
          partition = gdu_block_new (self->client, object, parent);
          gdu_drive_set_block_color (self, partition);
          g_list_store_append (self->partitions, partition);

          if (udisks_object_peek_encrypted (object))
            gdu_drive_add_decrypted (self, object, GDU_ITEM (partition));

          /* Keep track of current block end offset to be used in the next iteration 
           * if the current block is extended partition then don't use end offset */
          if(gdu_block_is_extended(partition))
            {
              prev_end = begin;
              extended_partition_end_offset = end;
            }
          else 
            {
              prev_end = end;
            }

          g_clear_object (&partition);
        }

      /* If we still have some blocks left, add it as a free space at the end 
       * Also check if any extended partition is still remaining */
      {
        guint64 size, disk_end_offset;

        size = gdu_item_get_size (GDU_ITEM (self));
        disk_end_offset = size;

        if (extended_partition_end_offset > prev_end 
            && extended_partition_end_offset - prev_end > free_space_slack)
          {
            g_autoptr(GduBlock) partition = NULL;
            GduItem *parent;

            parent = block_get_parent (self, prev_end, extended_partition_end_offset);
            partition = gdu_block_sized_new (self->client, prev_end, extended_partition_end_offset - prev_end, parent);
            gdu_drive_set_block_color (self, partition);
            g_list_store_append (self->partitions, partition);
            prev_end = extended_partition_end_offset;
          }

        if (disk_end_offset > prev_end &&
            disk_end_offset - prev_end > free_space_slack)
          {
            g_autoptr(GduBlock) partition = NULL;
            GduItem *parent;

            parent = block_get_parent (self, prev_end, disk_end_offset);
            partition = gdu_block_sized_new (self->client, prev_end, disk_end_offset - prev_end, parent);
            gdu_drive_set_block_color (self, partition);
            g_list_store_append (self->partitions, partition);
          }
      }

      g_list_free_full (partitions, g_object_unref);
    }
}

static void
ata_pm_standby_cb (GObject      *object,
                   GAsyncResult *result,
                   gpointer      user_data)
{
  g_autoptr(GTask) task = user_data;
  UDisksDriveAta *ata;
  GduDrive *self;
  GError *error = NULL;

  g_assert (G_IS_TASK (task));

  self = g_task_get_source_object (task);
  g_assert (GDU_IS_DRIVE (self));

  ata = udisks_object_peek_drive_ata (self->object);

  if (udisks_drive_ata_call_pm_standby_finish (ata, result, &error))
    g_task_return_boolean (task, TRUE);
  else
    g_task_return_error (task, error);
}

void
gdu_drive_standby_async (GduDrive            *self,
                         GCancellable        *cancellable,
                         GAsyncReadyCallback  callback,
                         gpointer             user_data)
{
  g_autoptr(GTask) task = NULL;
  UDisksDriveAta *ata;

  g_return_if_fail (GDU_IS_DRIVE (self));
  g_return_if_fail (!cancellable || G_IS_CANCELLABLE (cancellable));

  task = g_task_new (self, cancellable, callback, user_data);
  ata = udisks_object_peek_drive_ata (self->object);

  if (ata != NULL)
    {
      udisks_drive_ata_call_pm_standby (ata,
                                        g_variant_new ("a{sv}", NULL), /* options */
                                        cancellable,
                                        ata_pm_standby_cb,
                                        g_steal_pointer (&task));
    }
  else
    {
      g_task_return_new_error (task,
                               G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
                               "Object is not an ATA drive");
    }
}

gboolean
gdu_drive_standby_finish (GduDrive      *self,
                          GAsyncResult  *result,
                          GError       **error)
{
  g_return_val_if_fail (GDU_IS_DRIVE (self), FALSE);
  g_return_val_if_fail (G_IS_TASK (result), FALSE);

  return g_task_propagate_boolean (G_TASK (result), error);
}

static void
ata_pm_wakeup_cb (GObject      *object,
                  GAsyncResult *result,
                  gpointer      user_data)
{
  g_autoptr(GTask) task = user_data;
  UDisksDriveAta *ata;
  GduDrive *self;
  GError *error = NULL;

  g_assert (G_IS_TASK (task));

  self = g_task_get_source_object (task);
  g_assert (GDU_IS_DRIVE (self));

  ata = udisks_object_peek_drive_ata (self->object);

  if (udisks_drive_ata_call_pm_wakeup_finish (ata, result, &error))
    g_task_return_boolean (task, TRUE);
  else
    g_task_return_error (task, error);
}

void
gdu_drive_wakeup_async (GduDrive            *self,
                        GCancellable        *cancellable,
                        GAsyncReadyCallback  callback,
                        gpointer             user_data)
{
  g_autoptr(GTask) task = NULL;
  UDisksDriveAta *ata;

  g_return_if_fail (GDU_IS_DRIVE (self));
  g_return_if_fail (!cancellable || G_IS_CANCELLABLE (cancellable));

  task = g_task_new (self, cancellable, callback, user_data);
  ata = udisks_object_peek_drive_ata (self->object);

  if (ata != NULL)
    {
      udisks_drive_ata_call_pm_wakeup (ata,
                                       g_variant_new ("a{sv}", NULL), /* options */
                                       cancellable,
                                       ata_pm_wakeup_cb,
                                       g_steal_pointer (&task));
    }
  else
    {
      g_task_return_new_error (task,
                               G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
                               "Object is not an ATA drive");
    }
}

gboolean
gdu_drive_wakeup_finish (GduDrive      *self,
                         GAsyncResult  *result,
                         GError       **error)
{
  g_return_val_if_fail (GDU_IS_DRIVE (self), FALSE);
  g_return_val_if_fail (G_IS_TASK (result), FALSE);

  return g_task_propagate_boolean (G_TASK (result), error);
}

static void
power_off_cb (GObject      *object,
              GAsyncResult *result,
              gpointer      user_data)
{
  g_autoptr(GTask) task = user_data;
  GduDrive *self;
  GError *error = NULL;

  g_assert (G_IS_TASK (task));

  self = g_task_get_source_object (task);
  g_assert (GDU_IS_DRIVE (self));

  if (udisks_drive_call_power_off_finish (self->drive, result, &error))
    g_task_return_boolean (task, TRUE);
  else
    g_task_return_error (task, error);
}

static void
power_off_ensure_unused_cb (GObject      *object,
                            GAsyncResult *result,
                            gpointer      user_data)
{
  g_autoptr(GTask) task = user_data;
  GCancellable *cancellable;
  GduDrive *self;
  GError *error = NULL;

  g_assert (G_IS_TASK (task));

  cancellable = g_task_get_cancellable (task);
  self = g_task_get_source_object (task);
  g_assert (GDU_IS_DRIVE (self));

  if (gdu_utils_ensure_unused_list_finish (self->client, result, &error))
    {
      udisks_drive_call_power_off (self->drive,
                                   g_variant_new ("a{sv}", NULL), /* options */
                                   cancellable,
                                   power_off_cb,
                                   g_steal_pointer (&task));
    }
  else
    {
      g_task_return_error (task, error);
    }
}

void
gdu_drive_power_off_async (GduDrive            *self,
                           gpointer             parent_window,
                           GCancellable        *cancellable,
                           GAsyncReadyCallback  callback,
                           gpointer             user_data)
{
  g_autoptr(GList) objects = NULL;
  g_autoptr(GTask) task = NULL;

  g_return_if_fail (GDU_IS_DRIVE (self));
  g_return_if_fail (!cancellable || G_IS_CANCELLABLE (cancellable));

  task = g_task_new (self, cancellable, callback, user_data);
  objects = g_list_append (NULL, self->object);
  /* include other drives this will affect */
  objects = g_list_concat (objects, gdu_drive_get_siblings (self));

  gdu_utils_ensure_unused_list (self->client,
                                parent_window,
                                objects,
                                power_off_ensure_unused_cb,
                                cancellable,
                                g_steal_pointer (&task));
}

gboolean
gdu_drive_power_off_finish (GduDrive      *self,
                            GAsyncResult  *result,
                            GError       **error)
{
  g_return_val_if_fail (GDU_IS_DRIVE (self), FALSE);
  g_return_val_if_fail (G_IS_TASK (result), FALSE);

  return g_task_propagate_boolean (G_TASK (result), error);
}

/**
 * gdu_drive_get_object:
 * @self: A #GduDrive
 *
 * Returns: (tranfer none): The underlying #UDisksObject
 */
gpointer
gdu_drive_get_object (GduDrive *self)
{
  g_return_val_if_fail (GDU_IS_DRIVE (self), NULL);

  return self->object;
}

gpointer
gdu_drive_get_object_for_format (GduDrive *self)
{
  g_return_val_if_fail (GDU_IS_DRIVE (self), NULL);

  if (self->partition_table)
    return self->partition_table;

  return self->object;
}

void
gdu_drive_block_changed (GduDrive *self,
                         gpointer block)
{
  g_autoptr(GduBlock) before = NULL;
  g_autoptr(GduBlock) after = NULL;
  guint position;

  g_return_if_fail (GDU_IS_DRIVE (self));
  g_return_if_fail (GDU_IS_BLOCK (block));

  if (!g_list_store_find (self->partitions, block, &position))
    g_return_if_reached ();

  gdu_block_emit_updated (block);

  /* Update siblings as some changes (like partition size changes)
     also affects siblings */
  /* fixme: This won't work if the siblings is just freespace without any block associated */
  if (position > 0)
    {
      before = g_list_model_get_item (G_LIST_MODEL (self->partitions), position - 1);
      gdu_block_emit_updated (before);
    }

  after = g_list_model_get_item (G_LIST_MODEL (self->partitions), position + 1);

  if (after)
    gdu_block_emit_updated (after);
}
