/* gdu-manager.c
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
#define G_LOG_DOMAIN "gdu-manager"

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <udisks/udisks.h>
#include <stdbool.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "gduutils.h"
#include "gdu-block.h"
#include "gdu-drive.h"
#include "gdu-manager.h"

struct _GduManager
{
  GObject       parent_instance;

  UDisksClient *udisks_client;

  GListStore   *drives;

  gulong        object_add_id;
  gulong        object_remove_id;
  gulong        iface_add_id;
  gulong        iface_remove_id;
  gulong        properties_changed_id;
};


G_DEFINE_TYPE (GduManager, gdu_manager, G_TYPE_OBJECT)

static bool
drive_in_manager (GduManager *self,
                  UDisksObject    *object,
                  guint           *position)
{
  GListModel *model;
  guint n_items;

  model = G_LIST_MODEL (self->drives);
  n_items = g_list_model_get_n_items (model);
  for (guint i = 0; i < n_items; i++)
    {
      g_autoptr(GduDrive) drive = NULL;

      drive = g_list_model_get_item (model, i);
      if (gdu_drive_matches_object (drive, object))
        {
          if (position)
            *position = i;
          return true;
        }
    }

  return false;
}

/* Copied from https://gitlab.gnome.org/GNOME/gnome-disk-utility/-/blob/3eccf2b5fec7200cb16c46dd5d047c083ac318f7/src/disks/gdudevicetreemodel.c#L1181  */
/* NOTE: this should be kept in sync with `src/disks/restore_disk_image_dialog.rs` */
static bool
should_include_block (UDisksObject *object)
{
  UDisksBlock *block;
  UDisksLoop *loop;
  const char *device;
  const char *drive;
  const char *crypto_backing_device;
  guint64 size;

  block = udisks_object_peek_block (object);
  loop = udisks_object_peek_loop (object);

  if (gdu_utils_has_userspace_mount_option (block, "x-gdu.hide"))
    return false;

  /* RAM devices are useless */
  device = udisks_block_get_device (block);
  if (g_str_has_prefix (device, "/dev/ram") || g_str_has_prefix (device, "/dev/zram"))
    return false;

  /* Don't show loop devices of size zero - they're unused.
   *
   * Do show any other block device of size 0.
   *
   * Note that we _do_ want to show any other device of size 0 since
   * that's a good hint that the system may be misconfigured and
   * attention is needed.
   */
  size = udisks_block_get_size (block);
  if (size == 0 && loop)
    return false;

  /* Only include devices if they are top-level */
  if (udisks_object_peek_partition (object))
    return false;

  /* Don't include if already shown in "Direct-Attached devices" */
  drive = udisks_block_get_drive (block);
  if (g_strcmp0 (drive, "/") != 0)
    return false;

  /* Don't include if already shown in volume grid as an unlocked device */
  crypto_backing_device = udisks_block_get_crypto_backing_device (block);
  if (g_strcmp0 (crypto_backing_device, "/") != 0)
    return false;

  return true;
}

static int
compare_drive_path (GduDrive *drive_a,
                    GduDrive *drive_b)
{
  gpointer obj_a, obj_b;
  UDisksDrive *udrive_a, *udrive_b;

  obj_a = gdu_drive_get_object (drive_a);
  obj_b = gdu_drive_get_object (drive_b);

  udrive_a = udisks_object_peek_drive (obj_a);
  udrive_b = udisks_object_peek_drive (obj_b);

  if (udrive_a != NULL && udrive_b == NULL)
    return -1;

  if (udrive_b != NULL && udrive_a == NULL)
    return 1;

  return g_strcmp0 (g_dbus_object_get_object_path (obj_a),
                    g_dbus_object_get_object_path (obj_b));
}

static GduDrive *
manager_get_object_drive (GduManager   *self,
                          UDisksObject *object)
{
  UDisksDrive *drive = NULL;
  UDisksBlock *block;

  g_assert (GDU_IS_MANAGER (self));
  g_assert (UDISKS_IS_OBJECT (object));

  if (g_object_get_data (G_OBJECT (object), "gdu-drive"))
    return g_object_get_data (G_OBJECT (object), "gdu-drive");

  block = udisks_object_peek_block (object);
  if (block)
    drive = udisks_client_get_drive_for_block (self->udisks_client, block);

  if (drive)
    {
      UDisksObject *obj;

      obj = (gpointer)g_dbus_interface_get_object ((gpointer)drive);

      return g_object_get_data (G_OBJECT (obj), "gdu-drive");
    }

  return NULL;
}

static void
manager_add_drive (GduManager   *self,
                   UDisksObject *object)
{
  UDisksDrive *udrive;
  UDisksBlock *block;

  udrive = udisks_object_peek_drive (object);
  block = udisks_object_peek_block (object);

  if (udrive || (block && should_include_block (object)))
    {
      g_autoptr(GduDrive) gdu_drive = NULL;

      gdu_drive = gdu_drive_new (self->udisks_client, object, NULL);
      g_debug ("UDisksObject %p added, GduDrive %p", object, gdu_drive);

      g_list_store_insert_sorted (self->drives, gdu_drive,
                                  (GCompareDataFunc)compare_drive_path,
                                  self);
    }
}

static void
object_added_cb (GduManager   *self,
                 UDisksObject *object)
{
  GduDrive *drive;

  g_assert (GDU_IS_MANAGER (self));
  g_assert (UDISKS_IS_OBJECT (object));

  g_debug ("UDisksObject %p added, GduDrive: %p, GduBlock: %p", object,
           g_object_get_data (G_OBJECT (object), "gdu-drive"),
           g_object_get_data (G_OBJECT (object), "gdu-block"));

  /* If it's a block, update every parent as some changes (like partition size changes)
     also affects its parents */
  if (g_object_get_data (G_OBJECT (object), "gdu-block"))
    {
      GduItem *item = g_object_get_data (G_OBJECT (object), "gdu-block");

      gdu_block_emit_updated (GDU_BLOCK (item));
      item = (GduItem *)gdu_item_get_parent (item);

      if (GDU_IS_DRIVE (item))
        gdu_drive_block_changed(GDU_DRIVE(item), g_object_get_data (G_OBJECT (object), "gdu-block"));
    }

  drive = manager_get_object_drive (self, object);
  if (drive &&
      udisks_object_peek_partition_table (object) &&
      udisks_object_peek_block (object))
    {
      gdu_drive_set_child (drive, object);
    }

  if (!g_object_get_data (G_OBJECT (object), "gdu-block") &&
      !g_object_get_data (G_OBJECT (object), "gdu-drive"))
    manager_add_drive (self, object);
}

static void
object_removed_cb (GduManager *self,
                   UDisksObject   *object)
{
  guint position;

  g_assert (GDU_IS_MANAGER (self));
  g_assert (UDISKS_IS_OBJECT (object));

  if (udisks_object_peek_drive(object) != NULL &&
      drive_in_manager (self, object, &position))
    {
      g_autoptr(GduItem) item = NULL;

      item = g_list_model_get_item (G_LIST_MODEL (self->drives), position);
      g_debug ("UDisksObject %p removed, GduItem: %p", object, item);

      g_list_store_remove (self->drives, position);
    }
}

static int
sort_objects (gpointer a,
              gpointer b)
{
  UDisksBlock *block;

  if (udisks_object_peek_drive (a))
    return -1;

  if (udisks_object_peek_drive (b))
    return 1;

  block = udisks_object_peek_block (a);
  if (block && should_include_block (a))
    return -1;

  return 1;
}

static void
manager_load_drives (GduManager *self)
{
  GDBusObjectManager *object_manager;
  GList *objects;

  g_assert (GDU_IS_MANAGER (self));

  /* Remove existing data */
  g_debug ("Unloading old drives");
  g_clear_signal_handler (&self->object_add_id, self);
  g_clear_signal_handler (&self->object_remove_id, self);
  g_clear_signal_handler (&self->iface_add_id, self);
  g_clear_signal_handler (&self->iface_remove_id, self);
  g_clear_signal_handler (&self->properties_changed_id, self);
  g_list_store_remove_all (self->drives);

  object_manager = udisks_client_get_object_manager (self->udisks_client);

  if (!object_manager)
    return;

  g_debug ("Loading drives");

  self->object_add_id = g_signal_connect_object (object_manager,
                                                 "object-added",
                                                 G_CALLBACK (object_added_cb),
                                                 self, G_CONNECT_SWAPPED);
  self->object_remove_id = g_signal_connect_object (object_manager,
                                                    "object-removed",
                                                    G_CALLBACK (object_removed_cb),
                                                    self, G_CONNECT_SWAPPED);
  self->iface_add_id = g_signal_connect_object (object_manager,
                                                "interface-added",
                                                G_CALLBACK (object_added_cb),
                                                self, G_CONNECT_SWAPPED);
  self->iface_remove_id = g_signal_connect_object (object_manager,
                                                   "interface-removed",
                                                   G_CALLBACK (object_removed_cb),
                                                   self, G_CONNECT_SWAPPED);
  self->properties_changed_id = g_signal_connect_object (object_manager,
                                                         "interface-proxy-properties-changed",
                                                         G_CALLBACK (object_added_cb),
                                                         self, G_CONNECT_SWAPPED);
  objects = g_dbus_object_manager_get_objects (object_manager);
  objects = g_list_sort (objects, (GCompareFunc)sort_objects);

  for (GList *item = objects; item && item->data; item = item->next)
    object_added_cb (self, item->data);

  g_list_free_full (objects, g_object_unref);
}

static void
gdu_manager_finalize (GObject *object)
{
  GduManager *self = (GduManager *)object;

  g_clear_object (&self->udisks_client);
  g_clear_object (&self->drives);

  G_OBJECT_CLASS (gdu_manager_parent_class)->finalize (object);
}

static void
gdu_manager_class_init (GduManagerClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = gdu_manager_finalize;
}

static void
gdu_manager_init (GduManager *self)
{
  self->drives = g_list_store_new (GDU_TYPE_DRIVE);
}

GduManager *
gdu_manager_get_default (GError **error)
{
  static GduManager *self;

  if (!self)
    {
      UDisksClient *client;

      g_return_val_if_fail (!error || !*error, NULL);

      if (!(client = udisks_client_new_sync (NULL, error)))
        return NULL;

      self = g_object_new (GDU_TYPE_MANAGER, NULL);
      g_object_add_weak_pointer (G_OBJECT (self), (gpointer *)&self);
      self->udisks_client = client;

      g_signal_connect_object (client,
                               "notify::object-manager",
                               G_CALLBACK (manager_load_drives),
                               self, G_CONNECT_SWAPPED);
      manager_load_drives (self);
    }

  return self;
}

GListModel *
gdu_manager_get_drives (GduManager *self)
{
  g_return_val_if_fail (GDU_IS_MANAGER (self), NULL);

  return G_LIST_MODEL (self->drives);
}

static void
manager_create_loop_cb (GObject      *object,
                        GAsyncResult *result,
                        gpointer      user_data)
{
  g_autoptr(GTask) task = user_data;
  g_autofree char *path = NULL;
  UDisksManager *manager;
  GError *error = NULL;
  GduManager *self;

  self = g_task_get_source_object (task);
  g_assert (GDU_IS_MANAGER (self));

  manager = udisks_client_get_manager (self->udisks_client);
  if (!udisks_manager_call_loop_setup_finish (manager, &path, NULL, result, &error))
    g_task_return_error (task, error);
  else
    g_task_return_boolean (task, TRUE);
}

void
gdu_manager_open_loop_async (GduManager          *self,
                             GFile               *file,
                             gboolean             read_only,
                             GAsyncReadyCallback  callback,
                             gpointer             user_data)
{
  g_autoptr(GUnixFDList) fd_list = NULL;
  GVariantBuilder options_builder;
  g_autoptr(GTask) task = NULL;
  int fd = -1;
  g_autofree char *path = NULL;
  g_return_if_fail (GDU_IS_MANAGER (self));

  path = g_file_get_path (file);
  g_return_if_fail (path && *path);

  task = g_task_new (self, NULL, callback, user_data);
  g_task_set_task_data (task, g_strdup (path), g_free);
  g_object_set_data (G_OBJECT (task), "read-only", GINT_TO_POINTER (read_only));

  fd = open (path, O_RDWR);
  if (fd == -1)
    fd = open (path, O_RDONLY);

  if (fd == -1)
    {
      g_task_return_new_error (task,
                               G_IO_ERROR,
                               g_io_error_from_errno (errno),
                               "%s", strerror (errno));
      return;
    }

  g_variant_builder_init (&options_builder, G_VARIANT_TYPE_VARDICT);
  if (read_only)
    g_variant_builder_add (&options_builder, "{sv}", "read-only", g_variant_new_boolean (TRUE));
  fd_list = g_unix_fd_list_new_from_array (&fd, 1); /* adopts the fd */

  udisks_manager_call_loop_setup (udisks_client_get_manager (self->udisks_client),
                                  g_variant_new_handle (0),
                                  g_variant_builder_end (&options_builder),
                                  fd_list,
                                  NULL,                       /* GCancellable */
                                  manager_create_loop_cb,
                                  g_steal_pointer (&task));
}

gboolean
gdu_manager_open_loop_finish (GduManager    *self,
                              GAsyncResult  *result,
                              GError       **error)
{
  g_return_val_if_fail (GDU_IS_MANAGER (self), FALSE);
  g_return_val_if_fail (G_IS_TASK (result), FALSE);
  g_return_val_if_fail (!error || !*error, FALSE);

  return g_task_propagate_boolean (G_TASK (result), error);
}


/* xxx: to be removed */
UDisksClient *
gdu_manager_get_client (GduManager *self)
{
  g_return_val_if_fail (GDU_IS_MANAGER (self), NULL);

  return g_object_ref (self->udisks_client);
}
