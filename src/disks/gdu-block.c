/* gdu-block.c
 *
 * Copyright 2023 Mohammed Sadiq <sadiq@sadiqpk.org>
 * Copyright (C) 2008-2013 Red Hat, Inc
 *
 * Author(s):
 *   David Zeuthen <zeuthen@gmail.com>
 *   Mohammed Sadiq <sadiq@sadiqpk.org>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#define G_LOG_DOMAIN "gdu-block"

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <glib/gi18n.h>
#include <udisks/udisks.h>

#include "gdu-drive.h"
#include "gdu-item.h"
#include "gduutils.h"
#include "gdu-log.h"
#include "gdu-block.h"

/**
 * GduBlock:
 *
 * `GduBlock` represents a portition of a drive.  It can be a partition
 * (ie, `UDisksPartition`) or a portion of drive specified by lower and
 * upper block limits.
 */

struct _GduBlock
{
  GduItem           parent_instance;

  GduItem          *parent;

  UDisksClient     *client;
  UDisksObjectInfo *info;
  UDisksObject     *object;
  UDisksBlock      *block;
  UDisksPartition  *partition;
  UDisksFilesystem *file_system;

  char             *partition_type;
  char             *description;
  guint64           start_offset;
  guint64           size;
  GduFeature        features;

  bool              in_progress;
};


G_DEFINE_TYPE (GduBlock, gdu_block, GDU_TYPE_ITEM)

#define return_if_progress(self, task) do {                             \
    if (self->in_progress)                                              \
      {                                                                 \
        g_task_return_new_error (task,                                  \
                                 G_IO_ERROR,                            \
                                 G_IO_ERROR_PENDING,                    \
                                 "A Process is already in progress");   \
        return;                                                         \
      }                                                                 \
  } while (0)

static const char *
gdu_block_get_description (GduItem *item)
{
  GduBlock *self = (GduBlock *)item;

  g_assert (GDU_IS_BLOCK (self));

  if (!self->description && !self->block)
    {
      g_autofree char *size_str = NULL;

      size_str = g_format_size (self->size);
      self->description = g_strdup_printf ("%s %s", size_str ?: "", _("Free Space"));
    }
  else if (!self->description)
    {
      const char *id_label;

      id_label = udisks_block_get_id_label (self->block);
      if (id_label && *id_label)
        {
          self->description = udisks_block_dup_id_label (self->block);
        }
      else if (gdu_block_is_extended (self))
        {
          self->description = g_strdup (_("Extended Partition"));
        }
      else
        {
          const char *usage, *type, *version;
          g_autofree char *size_str = NULL;
          g_autofree char *id = NULL;
          guint64 size;

          usage = udisks_block_get_id_usage (self->block);
          type = udisks_block_get_id_type (self->block);
          version = udisks_block_get_id_version (self->block);
          id = udisks_client_get_id_for_display (self->client, usage, type, version, FALSE);

          size = gdu_item_get_size (item);
          if (size)
            size_str = g_format_size (size);

          self->description = g_strdup_printf ("%s %s", size_str ?: "", id);

        }
    }

  return self->description;
}

static const char *
gdu_block_get_partition_type (GduItem *item)
{
  GduBlock *self = (GduBlock *)item;
  g_autoptr(UDisksPartitionTable) table = NULL;
  g_autoptr(UDisksPartition) partition = NULL;
  const char *type, *table_type;

  g_assert (GDU_IS_BLOCK (self));

  if (self->partition_type != NULL)
    return self->partition_type;

  if (self->object == NULL)
    return _("Free Space");

  partition = udisks_object_get_partition (self->object);
  if (partition == NULL)
    {
      const char *usage, *version;

      usage = udisks_block_get_id_usage (self->block);
      type = udisks_block_get_id_type (self->block);
      version = udisks_block_get_id_version (self->block);

      if (self->partition_type == NULL)
        self->partition_type = udisks_client_get_id_for_display (self->client, usage, type, version, TRUE);

      if (self->partition_type != NULL)
        return self->partition_type;

      return "—";
    }

  table = udisks_client_get_partition_table (self->client, partition);
  type = udisks_partition_get_type_ (partition);
  table_type = udisks_partition_table_get_type_ (table);

  return udisks_client_get_partition_type_for_display (self->client, table_type, type);
}

static GduItem *
gdu_block_get_parent (GduItem *item)
{
  GduBlock *self = (GduBlock *)item;

  g_assert (GDU_IS_BLOCK (self));

  return self->parent;
}

static guint64
gdu_block_get_size (GduItem *item)
{
  GduBlock *self = (GduBlock *)item;

  g_assert (GDU_IS_BLOCK (self));

  return self->size;
}

/* https://gitlab.gnome.org/GNOME/gnome-disk-utility/-/blob/3f153fa62e1c2c2c881d747565004341de4ef094/src/disks/gduwindow.c#L2343 */
static UDisksObject *
lookup_cleartext_device_for_crypto_device (UDisksClient *client,
                                           const char   *object_path)
{
  GDBusObjectManager *object_manager;
  UDisksObject *ret;
  GList *objects;
  GList *l;

  ret = NULL;

  object_manager = udisks_client_get_object_manager (client);
  objects = g_dbus_object_manager_get_objects (object_manager);
  for (l = objects; l != NULL; l = l->next)
    {
      UDisksObject *object = UDISKS_OBJECT (l->data);
      UDisksBlock *block;

      block = udisks_object_peek_block (object);
      if (block == NULL)
        continue;

      if (g_strcmp0 (udisks_block_get_crypto_backing_device (block),
                     object_path) == 0)
        {
          ret = g_object_ref (object);
          goto out;
        }
    }

 out:
  g_list_foreach (objects, (GFunc) g_object_unref, NULL);
  g_list_free (objects);
  return ret;
}

static GduFeature
gdu_block_get_features (GduItem *item)
{
  GduBlock *self = (GduBlock *)item;
  GduItem *drive = NULL;
  UDisksObject *object;
  UDisksBlock *block;
  GduFeature features = 0, drive_features;
  gboolean read_only;
  g_autofree gchar *fs_resize_missing_utility = NULL;
  g_autofree gchar *fs_check_missing_utility = NULL;
  g_autofree gchar *fs_repair_missing_utility = NULL;

  g_assert (GDU_IS_BLOCK (self));

  drive = gdu_item_get_parent (item);

  while (!GDU_IS_DRIVE (drive))
    {
      g_assert (drive);
      drive = gdu_item_get_parent (drive);
    }

  g_assert (GDU_IS_DRIVE (drive));

  if (self->features)
    return self->features;

  drive_features = gdu_item_get_features (drive);
  object = self->object;
  block = self->block;

  if (block == NULL)
    return drive_features & GDU_FEATURE_CREATE_PARTITION;

  read_only = udisks_block_get_read_only (self->block);
  features = drive_features & (GDU_FEATURE_CREATE_IMAGE |
                               GDU_FEATURE_BENCHMARK |
                               GDU_FEATURE_RESTORE_IMAGE);

  if (!read_only)
    {
      if (self->partition == NULL || !udisks_partition_get_is_container (self->partition))
        {
          features |= GDU_FEATURE_FORMAT;
        }

      if (self->partition != NULL)
        {
          features |= GDU_FEATURE_DELETE_PARTITION;
          features |= GDU_FEATURE_EDIT_PARTITION;
        }
    }

  /* Since /etc/fstab, /etc/crypttab and so on can reference
   * any device regardless of its content ... we want to show
   * the relevant menu option (to get to the configuration dialog)
   * if the device matches the configuration....
   */
  if (gdu_utils_has_configuration (block, "fstab", NULL))
    features |= GDU_FEATURE_CONFIGURE_FSTAB;
  if (gdu_utils_has_configuration (block, "crypttab", NULL))
    features |= GDU_FEATURE_CONFIGURE_CRYPTTAB;

  /* if the device has no media and there is no existing configuration, then
   * show CONFIGURE_FSTAB since the user might want to add an entry for e.g.
   * /media/cdrom
   */
  if (udisks_block_get_size (block) == 0 &&
      !(features & (GDU_FEATURE_CONFIGURE_FSTAB |
                    GDU_FEATURE_CONFIGURE_CRYPTTAB)))
    {
      features |= GDU_FEATURE_CONFIGURE_FSTAB;
    }

  if (udisks_object_peek_filesystem (object) != NULL)
    {
      const char *const *mount_points;

      mount_points = gdu_block_get_mount_points (self);

      if (g_strv_length ((gchar **) mount_points) > 0)
        features |= GDU_FEATURE_CAN_UNMOUNT;
      else
        features |= GDU_FEATURE_CAN_MOUNT;

      if (!read_only)
        features |= GDU_FEATURE_EDIT_LABEL;

      features |= GDU_FEATURE_CONFIGURE_FSTAB;
    }
  else if (g_strcmp0 (udisks_block_get_id_usage (block), "other") == 0 &&
           g_strcmp0 (udisks_block_get_id_type (block), "swap") == 0)
    {
      UDisksSwapspace *swapspace;

      swapspace = udisks_object_peek_swapspace (object);
      if (udisks_object_peek_swapspace (object) != NULL)
        {
          if (udisks_swapspace_get_active (swapspace))
            features |= GDU_FEATURE_CAN_SWAPOFF;
          else
            features |= GDU_FEATURE_CAN_SWAPON;

          features |= GDU_FEATURE_CONFIGURE_FSTAB;
        }
    }
  else if (g_strcmp0 (udisks_block_get_id_usage (block), "crypto") == 0)
    {
      UDisksObject *cleartext_device;

      cleartext_device = lookup_cleartext_device_for_crypto_device (self->client,
                                                                    g_dbus_object_get_object_path (G_DBUS_OBJECT (object)));
      if (cleartext_device != NULL)
        features |= GDU_FEATURE_CAN_LOCK;
      else
        features |= GDU_FEATURE_CAN_UNLOCK;
    }

  if (g_strcmp0 (udisks_block_get_id_type (block), "crypto_LUKS") == 0)
    {
      features |= GDU_FEATURE_CONFIGURE_CRYPTTAB;
      features |= GDU_FEATURE_CHANGE_PASSPHRASE;
    }

  if (self->partition != NULL &&
      g_strcmp0 (udisks_block_get_id_usage (block), "") == 0 &&
      !read_only)
    {
      /* allow partition resize if no known structured data was found on the device */
      features |= GDU_FEATURE_RESIZE_PARTITION;
    }
  else if (udisks_object_peek_filesystem (object) != NULL)
    {
      const char *type;

      type = udisks_block_get_id_type (block);
      /* for now the filesystem resize on just any block device is not shown, see resize_dialog_show */
      /* partition is considered to be able to perform an operation even if it is missing an utility */
      if (!read_only && self->partition != NULL &&
          (gdu_utils_can_resize (self->client, type, FALSE, NULL, &fs_resize_missing_utility) ||
           g_strcmp0(fs_resize_missing_utility, "") > 0))
        features |= GDU_FEATURE_RESIZE_PARTITION;

      if (!read_only && (gdu_utils_can_repair (self->client, type, FALSE, &fs_repair_missing_utility) ||
          g_strcmp0(fs_repair_missing_utility, "") > 0))
        features |= GDU_FEATURE_REPAIR_FILESYSTEM;

      if (!read_only && gdu_utils_can_take_ownership (type))
        features |= GDU_FEATURE_TAKE_OWNERSHIP;

      if (gdu_utils_can_check (self->client, type, FALSE, &fs_check_missing_utility) ||
          g_strcmp0(fs_check_missing_utility, "") > 0)
        features |= GDU_FEATURE_CHECK_FILESYSTEM;
    }

  self->features = features;

  return self->features;
}

static void
gdu_block_dispose (GObject *object)
{
  GduBlock *self = (GduBlock *)object;

  if (self->object)
    g_object_set_data (G_OBJECT (self->object), "gdu-block", NULL);

  G_OBJECT_CLASS (gdu_block_parent_class)->dispose (object);
}

static void
gdu_block_finalize (GObject *object)
{
  GduBlock *self = (GduBlock *)object;

  g_clear_object (&self->client);
  g_clear_object (&self->object);

  G_OBJECT_CLASS (gdu_block_parent_class)->finalize (object);
}

static void
gdu_block_class_init (GduBlockClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GduItemClass *item_class = GDU_ITEM_CLASS (klass);

  object_class->dispose = gdu_block_dispose;
  object_class->finalize = gdu_block_finalize;

  item_class->get_description = gdu_block_get_description;
  item_class->get_partition_type = gdu_block_get_partition_type;
  item_class->get_size = gdu_block_get_size;
  item_class->get_parent = gdu_block_get_parent;
  item_class->get_features = gdu_block_get_features;
}

static void
gdu_block_init (GduBlock *self)
{
}

GduBlock *
gdu_block_new (gpointer  udisk_client,
               gpointer  udisk_object,
               GduItem  *parent)
{
  GduBlock *self;

  g_return_val_if_fail (UDISKS_IS_CLIENT (udisk_client), NULL);
  g_return_val_if_fail (UDISKS_IS_OBJECT (udisk_object), NULL);
  g_return_val_if_fail (!parent || GDU_IS_ITEM (parent), NULL);

  self = g_object_new (GDU_TYPE_BLOCK, NULL);
  g_set_object (&self->client, udisk_client);
  g_set_object (&self->object, udisk_object);
  g_set_object (&self->block, udisks_object_peek_block (self->object));
  g_set_object (&self->partition, udisks_object_peek_partition (self->object));
  g_set_object (&self->file_system, udisks_object_peek_filesystem (self->object));
  g_set_weak_pointer (&self->parent, parent);

  if (self->partition)
    {
      self->size = udisks_partition_get_size (self->partition);
      self->start_offset = udisks_partition_get_offset (self->partition);
    }
  else
    {
      self->size = udisks_block_get_size (self->block);
      if (GDU_IS_BLOCK (parent))
        self->start_offset = gdu_block_get_offset (GDU_BLOCK (parent));
      else
        self->start_offset = 0;
    }

  g_object_set_data (G_OBJECT (udisk_object), "gdu-block", self);

  return self;
}

GduBlock *
gdu_block_sized_new (gpointer  udisk_client,
                     guint64   start_offset,
                     guint64   size,
                     GduItem  *parent)
{
  GduBlock *self;

  g_return_val_if_fail (UDISKS_IS_CLIENT (udisk_client), NULL);
  g_return_val_if_fail (GDU_IS_ITEM (parent), NULL);

  self = g_object_new (GDU_TYPE_BLOCK, NULL);
  g_set_object (&self->client, udisk_client);
  g_set_weak_pointer (&self->parent, parent);
  self->start_offset = start_offset;
  self->size = size;

  return self;
}

guint64
gdu_block_get_offset (GduBlock *self)
{
  g_return_val_if_fail (GDU_IS_BLOCK (self), 0);

  return self->start_offset;
}

guint64
gdu_block_get_number (GduBlock *self)
{
  UDisksBlock *block;

  g_return_val_if_fail (GDU_IS_BLOCK (self), 0);

  block = udisks_object_peek_block (self->object);
  if (block)
    return udisks_block_get_device_number (block);

  return 0;
}

guint64
gdu_block_get_unused_size (GduBlock *self)
{
  g_return_val_if_fail (GDU_IS_BLOCK (self), 0);

  if (!self->object)
    return self->size;

  return gdu_utils_get_unused_for_block (self->client, self->block);
}

bool
gdu_block_is_extended (GduBlock *self)
{
  UDisksPartition *partition;

  g_return_val_if_fail (GDU_IS_BLOCK (self), false);

  if (!self->block)
    return false;

  partition = udisks_object_peek_partition (self->object);

  return partition && udisks_partition_get_is_container (partition);
}

const char *
gdu_block_get_uuid (GduBlock *self)
{
  g_return_val_if_fail (GDU_IS_BLOCK (self), NULL);

  if (self->block)
    return udisks_block_get_id_uuid (self->block);

  return "—";
}

char *
gdu_block_get_size_str (GduBlock *self)
{
  g_autofree char *unused_str = NULL;
  g_autofree char *size_str = NULL;
  const char *const *mount_points;
  guint64 size, unused;

  g_assert (GDU_IS_BLOCK (self));

  size = gdu_item_get_size (GDU_ITEM (self));

  if (!size)
    return g_strdup ("—");

  /*
   * The unused size can be retrieved only the filesystem
   * has been mounted.
   */
  mount_points = gdu_block_get_mount_points (self);
  if (!mount_points || !mount_points[0] ||
      gdu_block_is_extended (self))
    return g_format_size_full (size, G_FORMAT_SIZE_LONG_FORMAT);

  size_str = g_format_size (size);

  unused = gdu_block_get_unused_size (self);
  unused_str = g_format_size (unused);

  /* Translators: Shown in 'Size' field for a filesystem where we know the amount of unused
   *              space.
   *              The first %s is a short string with the size (e.g. '69 GB (68,719,476,736 bytes)').
   *              The second %s is a short string with the space free (e.g. '43 GB').
   *              The %f is the percentage in use (e.g. 62.2).
   */
  return g_strdup_printf (_("%s — %s free (%.1f%% full)"), size_str, unused_str,
                          100.0 * (size - unused) / size);
}

const char *
gdu_block_get_device_id (GduBlock *self)
{
  g_return_val_if_fail (GDU_IS_BLOCK (self), NULL);

  if (self->block)
    return udisks_block_get_device (self->block);

  return "—";
}

const char *
gdu_block_get_fs_label (GduBlock *self)
{
  g_return_val_if_fail (GDU_IS_BLOCK (self), NULL);

  if (!self->block)
    return "";

  return udisks_block_get_id_label (self->block);
}

const char *
gdu_block_get_fs_type (GduBlock *self)
{
  g_return_val_if_fail (GDU_IS_BLOCK (self), NULL);

  if (!self->block)
    return "";

  return udisks_block_get_id_type (self->block);
}

const char *const *
gdu_block_get_mount_points (GduBlock *self)
{
  UDisksFilesystem *file_system;


  g_return_val_if_fail (GDU_IS_BLOCK (self), NULL);

  if (!self->object)
    return NULL;

  file_system = udisks_object_peek_filesystem (self->object);

  if (file_system)
    return udisks_filesystem_get_mount_points (file_system);

  return NULL;
}

bool
gdu_block_needs_unmount (GduBlock *self)
{
  const char *const *mount_points;
  const char *fs_type;
  bool needs_unmount = false;

  g_return_val_if_fail (GDU_IS_BLOCK (self), false);

  mount_points = udisks_filesystem_get_mount_points (self->file_system);
  fs_type = udisks_block_get_id_type (self->block);

  /* Needs to unmount if there's at least one mount point ... */
  if (mount_points && *mount_points)
    needs_unmount = true;

  /* ... and if they are not ext2, ext3, nor ext4 */
  if (needs_unmount &&
      (g_strcmp0 (fs_type, "ext2") == 0 ||
       g_strcmp0 (fs_type, "ext3") == 0 ||
       g_strcmp0 (fs_type, "ext4") == 0))
    needs_unmount = false;

  return needs_unmount;
}

static void
change_fs_label_cb (GObject      *object,
                    GAsyncResult *result,
                    gpointer      user_data)
{
  GduBlock *self;
  g_autoptr(GTask) task = user_data;
  GError *error = NULL;

  g_assert (G_IS_TASK (task));

  self = g_task_get_source_object (task);
  g_assert (GDU_IS_BLOCK (self));

  self->in_progress = false;

  udisks_filesystem_call_set_label_finish (self->file_system, result, &error);

  g_debug ("Changing fs label %s", gdu_log_bool_str (!error, true));

  if (error)
    {
      g_debug ("Changing fs label error: %s", error->message);
      g_task_return_error (task, error);
    }
  else
    {
      g_task_return_boolean (task, TRUE);
    }
}

static void
unmount_fs_cb (GObject      *object,
               GAsyncResult *result,
               gpointer      user_data)
{
  GduBlock *self;
  g_autoptr(GTask) task = user_data;
  GError *error = NULL;
  const char *label;

  g_assert (G_IS_TASK (task));

  self = g_task_get_source_object (task);
  label = g_task_get_task_data (task);
  g_assert (GDU_IS_BLOCK (self));

  if (gdu_utils_ensure_unused_list_finish (self->client, result, &error))
    {
      udisks_filesystem_call_set_label (self->file_system,
                                        label,
                                        g_variant_new ("a{sv}", NULL), /* options */
                                        NULL, /* cancellable */
                                        change_fs_label_cb,
                                        g_steal_pointer (&task));
    }
  else
    {
      g_debug ("Unmount fs error: %s", error->message);

      self->in_progress = false;
      g_task_return_error (task, error);
    }

  g_debug ("Unmounting fs %s", gdu_log_bool_str (!error, true));
}

void
gdu_block_set_fs_label_async (GduBlock            *self,
                              const char          *label,
                              GAsyncReadyCallback  callback,
                              gpointer             user_data)
{
  g_autoptr(GTask) task = NULL;

  g_return_if_fail (GDU_IS_BLOCK (self));

  if (!label)
    label = "";

  g_debug ("Setting fs label to '%s'", label);

  task = g_task_new (self, NULL, callback, user_data);
  g_task_set_task_data (task, g_strdup (label), g_free);
  return_if_progress (self, task);

  if (!self->file_system || !self->block)
    {
      g_debug ("Error setting fs label, file-system: %p, block: %p",
               self->file_system, self->block);

      g_task_return_new_error (task,
                               G_IO_ERROR,
                               G_IO_ERROR_NOT_SUPPORTED,
                               "Device does not support setting label");
      return;
    }

  self->in_progress = true;
  if (gdu_block_needs_unmount (self))
    {
      g_autoptr(GList) objects = NULL;

      objects = g_list_append (NULL, self->object);

      gdu_utils_ensure_unused_list (self->client,
                                    NULL,
                                    objects,
                                    unmount_fs_cb,
                                    NULL,
                                    g_steal_pointer (&task));
    }
  else
    {
      udisks_filesystem_call_set_label (self->file_system,
                                        label,
                                        g_variant_new ("a{sv}", NULL), /* options */
                                        NULL, /* cancellable */
                                        change_fs_label_cb,
                                        g_steal_pointer (&task));
    }
}

gboolean
gdu_block_set_fs_label_finish (GduBlock      *self,
                               GAsyncResult  *result,
                               GError       **error)
{
   g_return_val_if_fail (GDU_IS_BLOCK (self), FALSE);
   g_return_val_if_fail (G_IS_TASK (result), FALSE);
   g_return_val_if_fail (!error || !*error, FALSE);

   return g_task_propagate_boolean (G_TASK (result), error);
}

/**
 * gdu_block_get_object:
 * @self: A #GduBlock
 *
 * Returns: (tranfer none): The underlying #UDisksObject
 */
gpointer
gdu_block_get_object (GduBlock *self)
{
  g_return_val_if_fail (GDU_IS_BLOCK (self), NULL);

  return self->object;
}

void
gdu_block_emit_updated (GduBlock *self)
{
  g_clear_pointer (&self->description, g_free);
  g_clear_pointer (&self->partition_type, g_free);
  self->features = 0;

  if (self->partition)
    {
      self->size = udisks_partition_get_size (self->partition);
      self->start_offset = udisks_partition_get_offset (self->partition);
    }
  else if (self->block)
    {
      self->size = udisks_block_get_size (self->block);
      if (GDU_IS_BLOCK (self->parent))
        self->start_offset = gdu_block_get_offset (GDU_BLOCK (self->parent));
      else
        self->start_offset = 0;
    }

  /* If it's a block, update every parent as some changes (like partition size changes)
     also affects its parents */
  if (GDU_IS_BLOCK (self->parent))
    gdu_block_emit_updated (GDU_BLOCK (self->parent));

  gdu_item_changed (GDU_ITEM (self));
}
