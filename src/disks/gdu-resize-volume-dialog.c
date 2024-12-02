/*
 * Copyright (C) 2017 Kai Lüke
 *
 * Licensed under GPL version 2 or later.
 *
 * Author: Kai Lüke <kailueke@riseup.net>
 */

#include "config.h"

#include <glib/gi18n.h>

#include "gdu-resize-volume-dialog.h"
#include "gduutils.h"

#define FILESYSTEM_WAIT_STEP_MS 500
#define FILESYSTEM_WAIT_SEC 10

struct _GduResizeVolumeDialog
{
  AdwDialog              parent_instance;

  UDisksClient          *client;
  UDisksObject          *object;
  UDisksBlock           *block;
  UDisksPartition       *partition;
  UDisksFilesystem      *filesystem;
  UDisksPartitionTable  *table;

  ResizeFlags            support;
  guint64                min_size;
  guint64                max_size;
  guint64                current_size;

  GtkWidget             *size_scale;

  GtkWidget             *max_size_row;
  GtkWidget             *min_size_row;
  GtkWidget             *current_size_row;
  GtkWidget             *size_unit_combo;

  GtkWidget             *spinner;

  GtkAdjustment         *size_adjustment;
  GtkAdjustment         *free_size_adjustment;

  gint                   cur_unit_num;
  GCancellable          *mount_cancellable;
  guint                  running_id;
  guint                  wait_for_filesystem;
};

G_DEFINE_TYPE (GduResizeVolumeDialog, gdu_resize_volume_dialog, ADW_TYPE_DIALOG)

static void set_unit_num (GduResizeVolumeDialog *self, gint unit_num);

static gpointer
gdu_resize_volume_dialog_get_window (GduResizeVolumeDialog *self)
{
  return gtk_widget_get_ancestor (GTK_WIDGET (self), GTK_TYPE_WINDOW);
}

static void
gdu_resize_volume_dialog_update (GduResizeVolumeDialog *self)
{
  GObject *object;
  const char *unit;

  object = adw_combo_row_get_selected_item (ADW_COMBO_ROW (self->size_unit_combo));
  unit = gtk_string_object_get_string (GTK_STRING_OBJECT (object));

  adw_action_row_set_subtitle (ADW_ACTION_ROW (self->max_size_row),
                               g_strdup_printf ("%ld %s", self->max_size / unit_sizes[self->cur_unit_num], unit));
  adw_action_row_set_subtitle (ADW_ACTION_ROW (self->min_size_row),
                               g_strdup_printf ("%ld %s", self->min_size / unit_sizes[self->cur_unit_num], unit));
  adw_action_row_set_subtitle (ADW_ACTION_ROW (self->current_size_row),
                               g_strdup_printf ("%ld %s", self->current_size / unit_sizes[self->cur_unit_num], unit));
}

static guint64
get_new_size (GduResizeVolumeDialog *self)
{
  guint64 size;

  size = gtk_adjustment_get_value (self->size_adjustment) * unit_sizes[self->cur_unit_num];
  /* choose 0 as maximum in order to avoid errors
   * if partition is later 3 MiB smaller due to alignment etc
   */
  if (size >= self->max_size - 3 * 1024 * 1024)
    size = 0;

  return size;
}

static gboolean
is_shrinking (GduResizeVolumeDialog *self)
{
  return get_new_size (self) < self->current_size && get_new_size (self) != 0;
}

static gboolean
free_size_binding_func (GBinding *binding,
                        const GValue *source_value,
                        GValue *target_value,
                        gpointer user_data)
{
  GduResizeVolumeDialog *self = GDU_RESIZE_VOLUME_DIALOG (user_data);

  gdouble new_val = (self->max_size / unit_sizes[self->cur_unit_num]) - g_value_get_double (source_value) ;
  g_value_set_double (target_value, new_val);

  return TRUE;
}

static gboolean
calculate_usage (gpointer user_data)
{
  GduResizeVolumeDialog *self = user_data;
  gint64 unused;

  /* filesystem was mounted before opening the dialog but it still can take
   * some seconds */
  unused = gdu_utils_get_unused_for_block (self->client, self->block);
  if (unused == -1)
    return G_SOURCE_CONTINUE;

  self->min_size = self->current_size;
  /* set minimal filesystem size from usage if shrinking is supported */
  if (self->support & (ONLINE_SHRINK | OFFLINE_SHRINK))
    self->min_size = self->current_size - unused;

  self->running_id = 0;
  set_unit_num (self, gdu_utils_get_default_unit (self->current_size));
  gdu_resize_volume_dialog_update (self);
  gtk_widget_set_visible (GTK_WIDGET (self->spinner), FALSE);
  return G_SOURCE_REMOVE;
}

static void
resize_get_usage_mount_cb (GObject *source_object,
                           GAsyncResult *res,
                           gpointer user_data)
{
  GduResizeVolumeDialog *self = GDU_RESIZE_VOLUME_DIALOG (user_data);
  UDisksFilesystem *filesystem = UDISKS_FILESYSTEM (source_object);
  g_autoptr(GError) error = NULL;

  if (udisks_filesystem_call_mount_finish (filesystem, NULL, res, &error))
    return;

  if (self->running_id)
    {
      g_source_remove (self->running_id);
      self->running_id = 0;
    }

  g_clear_object (&self->mount_cancellable);

  gdu_utils_show_error (gdu_resize_volume_dialog_get_window (self),
                        _("Error mounting filesystem to calculate minimum size"),
                        error);

  adw_dialog_close (ADW_DIALOG (self));
}

static void
set_unit_num (GduResizeVolumeDialog *self, gint unit_num)
{
  gdouble unit_size;
  gdouble value;
  gdouble value_units;
  gdouble min_size_units;
  gdouble max_size_units;
  gdouble current_units;

  g_assert (unit_num < NUM_UNITS);

  adw_combo_row_set_selected (ADW_COMBO_ROW (self->size_unit_combo),
                              unit_num);

  value = self->cur_unit_num == -1
              ? self->current_size
              : gtk_adjustment_get_value (self->size_adjustment) * unit_sizes[self->cur_unit_num];

  unit_size = unit_sizes[unit_num];
  value_units = value / unit_size;
  min_size_units = ((gdouble)self->min_size) / unit_size;
  max_size_units = ((gdouble)self->max_size) / unit_size;
  current_units = ((gdouble)self->current_size) / unit_size;

  self->cur_unit_num = unit_num;

  gtk_adjustment_configure (self->size_adjustment,
                            value_units,
                            min_size_units, /* lower */
                            max_size_units, /* upper */
                            1,              /* step increment */
                            100,            /* page increment */
                            0.0);           /* page_size */
  gtk_adjustment_configure (self->free_size_adjustment,
                            max_size_units - value_units,
                            0.0,                               /* lower */
                            max_size_units - min_size_units,   /* upper */
                            1,                                 /* step increment */
                            100,                               /* page increment */
                            0.0);                              /* page_size */

  gtk_adjustment_set_value (self->size_adjustment, value_units);

  gtk_scale_clear_marks (GTK_SCALE (self->size_scale));
  gtk_scale_add_mark (GTK_SCALE (self->size_scale), current_units, GTK_POS_BOTTOM, _("Current Size"));

  if (self->min_size > 1)
    gtk_scale_add_mark (GTK_SCALE (self->size_scale), min_size_units, GTK_POS_BOTTOM, _("Minimum Size"));
}

static void
on_size_unit_changed_cb (GduResizeVolumeDialog *self)
{
  gint unit_num;

  unit_num = adw_combo_row_get_selected (ADW_COMBO_ROW (self->size_unit_combo));
  set_unit_num (self, unit_num);

  gdu_resize_volume_dialog_update (self);
}

static void
part_resize_cb (GObject       *object,
                GAsyncResult  *res,
                gpointer       user_data)
{
  GduResizeVolumeDialog *self = GDU_RESIZE_VOLUME_DIALOG (user_data);
  UDisksPartition *partition = UDISKS_PARTITION (object);
  g_autoptr(GError) error = NULL;

  if (!udisks_partition_call_resize_finish (partition, res, &error))
    {
      gdu_utils_show_error (gdu_resize_volume_dialog_get_window (self),
                            _("Error resizing partition"), error);
    }

  adw_dialog_close (ADW_DIALOG (self));
}

static void
fs_resize_cb (GObject       *object,
              GAsyncResult  *res,
              gpointer       user_data)
{
  GduResizeVolumeDialog *self = GDU_RESIZE_VOLUME_DIALOG (user_data);
  UDisksFilesystem *filesystem = UDISKS_FILESYSTEM (object);
  g_autoptr(GError) error = NULL;

  if (!udisks_filesystem_call_resize_finish (filesystem, res, &error))
    {
      gdu_utils_show_error (gdu_resize_volume_dialog_get_window (self),
                            _("Error resizing filesystem"), error);
    }

  adw_dialog_close (ADW_DIALOG (self));
}

static void
fs_repair_cb (GObject       *object,
              GAsyncResult  *res,
              gpointer       user_data)
{
  GduResizeVolumeDialog *self = GDU_RESIZE_VOLUME_DIALOG (user_data);
  UDisksFilesystem *filesystem = UDISKS_FILESYSTEM (object);
  g_autoptr(GError) error = NULL;
  gboolean success = FALSE;

  if (!udisks_filesystem_call_repair_finish (filesystem, &success, res, &error) || !success)
    {
      gdu_utils_show_error (gdu_resize_volume_dialog_get_window (self),
                            _("Error repairing filesystem after resize"),
                            error);
    }

  adw_dialog_close (ADW_DIALOG (self));
}

static void
fs_resize_cb_offline_next_repair (GObject       *object,
                                  GAsyncResult  *res,
                                  gpointer       user_data)
{
  GduResizeVolumeDialog *self = GDU_RESIZE_VOLUME_DIALOG (user_data);
  UDisksFilesystem *filesystem = UDISKS_FILESYSTEM (object);
  g_autoptr(GError) error = NULL;

  if (!udisks_filesystem_call_resize_finish (filesystem, res, &error))
    {
      gdu_utils_show_error (gdu_resize_volume_dialog_get_window (self),
                            _("Error resizing filesystem"), error);
      adw_dialog_close (ADW_DIALOG (self));
      return;
    }

  udisks_filesystem_call_repair (self->filesystem,
                                 g_variant_new ("a{sv}", NULL), NULL,
                                 fs_repair_cb, self);
}

static void
fs_repair_cb_offline_next_grow (GObject       *object,
                                GAsyncResult  *res,
                                gpointer       user_data)
{
  GduResizeVolumeDialog *self = GDU_RESIZE_VOLUME_DIALOG (user_data);
  UDisksFilesystem *filesystem = UDISKS_FILESYSTEM (object);
  g_autoptr(GError) error = NULL;
  gboolean success = FALSE;

  if (!udisks_filesystem_call_repair_finish (filesystem, &success, res, &error) || !success)
    {
      gdu_utils_show_error (gdu_resize_volume_dialog_get_window (self),
                            _("Error repairing filesystem"), error);
      adw_dialog_close (ADW_DIALOG (self));
      return;
    }

  /* growing: next is to resize filesystem, then run repair again */
  udisks_filesystem_call_resize (self->filesystem, get_new_size (self),
                                 g_variant_new ("a{sv}", NULL), NULL,
                                 fs_resize_cb_offline_next_repair, self);
}

static void
ensure_unused_cb_offline_next_repair (GObject *source_object,
                                      GAsyncResult *res,
                                      gpointer user_data)
{
  GduResizeVolumeDialog *self = GDU_RESIZE_VOLUME_DIALOG (user_data);

  if (!gdu_utils_ensure_unused_finish (self->client, res, NULL))
    {
      adw_dialog_close (ADW_DIALOG (self));
      return;
    }

  udisks_filesystem_call_repair (self->filesystem,
                                 g_variant_new ("a{sv}", NULL), NULL,
                                 fs_repair_cb_offline_next_grow, self);
}

static void
part_resize_cb_offline_next_fs_unmount (GObject *source_object,
                                        GAsyncResult *res,
                                        gpointer user_data)
{
  GduResizeVolumeDialog *self = GDU_RESIZE_VOLUME_DIALOG (user_data);
  UDisksPartition *partition = UDISKS_PARTITION (source_object);
  g_autoptr(GError) error = NULL;

  if (!udisks_partition_call_resize_finish (partition, res, &error))
    {
      gdu_utils_show_error (gdu_resize_volume_dialog_get_window (self),
                            _("Error resizing partition"), error);
      adw_dialog_close (ADW_DIALOG (self));
      return;
    }

  gdu_utils_ensure_unused (self->client,
                           gdu_resize_volume_dialog_get_window (self),
                           self->object,
                           ensure_unused_cb_offline_next_repair,
                           NULL,
                           self);
}

static void
fs_repair_cb_next_part_resize (GObject *object,
                               GAsyncResult *res,
                               gpointer user_data)
{
  GduResizeVolumeDialog *self = GDU_RESIZE_VOLUME_DIALOG (user_data);
  UDisksFilesystem *filesystem = UDISKS_FILESYSTEM (object);
  g_autoptr(GError) error = NULL;
  gboolean success = FALSE;

  if (!udisks_filesystem_call_repair_finish (filesystem, &success, res, &error) || !success)
    {
      gdu_utils_show_error (gdu_resize_volume_dialog_get_window (self),
                            _("Error repairing filesystem after resize"),
                            error);

      adw_dialog_close (ADW_DIALOG (self));
      return;
    }

  udisks_partition_call_resize (self->partition, get_new_size (self),
                                g_variant_new ("a{sv}", NULL), NULL,
                                (GAsyncReadyCallback)part_resize_cb, self);
}

static void
fs_resize_cb_offline_next_fs_repair (GObject *object,
                                     GAsyncResult *res,
                                     gpointer user_data)
{
  GduResizeVolumeDialog *self = GDU_RESIZE_VOLUME_DIALOG (user_data);
  UDisksFilesystem *filesystem = UDISKS_FILESYSTEM (object);
  g_autoptr(GError) error = NULL;

  if (!udisks_filesystem_call_resize_finish (filesystem, res, &error))
    {
      gdu_utils_show_error (gdu_resize_volume_dialog_get_window (self),
                            _("Error resizing filesystem"), error);
      adw_dialog_close (ADW_DIALOG (self));
      return;
    }

  udisks_filesystem_call_repair (self->filesystem,
                                 g_variant_new ("a{sv}", NULL), NULL,
                                 fs_repair_cb_next_part_resize, self);
}

static void
fs_repair_cb_offline_next_shrink (GObject      *object,
                                  GAsyncResult *res,
                                  gpointer      user_data)
{
  GduResizeVolumeDialog *self = GDU_RESIZE_VOLUME_DIALOG (user_data);
  UDisksFilesystem *filesystem = UDISKS_FILESYSTEM (object);
  g_autoptr(GError) error = NULL;
  gboolean success = FALSE;

  if (!udisks_filesystem_call_repair_finish (filesystem, &success, res, &error) || !success)
    {
      gdu_utils_show_error (gdu_resize_volume_dialog_get_window (self),
                            _("Error repairing filesystem"), error);
      adw_dialog_close (ADW_DIALOG (self));
      return;
    }

  /* shrinking: next is to resize filesystem, run repair again, then resize
   * partition */
  udisks_filesystem_call_resize (self->filesystem, get_new_size (self),
                                 g_variant_new ("a{sv}", NULL), NULL,
                                 fs_resize_cb_offline_next_fs_repair, self);
}

static void
fs_resize_cb_online_next_part_resize (GObject      *object,
                                      GAsyncResult *res,
                                      gpointer      user_data)
{
  GduResizeVolumeDialog *self = GDU_RESIZE_VOLUME_DIALOG (user_data);
  UDisksFilesystem *filesystem = UDISKS_FILESYSTEM (object);
  g_autoptr(GError) error = NULL;

  if (!udisks_filesystem_call_resize_finish (filesystem, res, &error))
    {
      gdu_utils_show_error (gdu_resize_volume_dialog_get_window (self),
                            _("Error resizing filesystem"), error);
      adw_dialog_close (ADW_DIALOG (self));
      return;
    }

  udisks_partition_call_resize (self->partition, get_new_size (self),
                                g_variant_new ("a{sv}", NULL), NULL,
                                part_resize_cb, self);
}

static gboolean
resize_filesystem_waiter (gpointer user_data)
{
  GduResizeVolumeDialog *self = GDU_RESIZE_VOLUME_DIALOG (user_data);

  if (self->wait_for_filesystem < FILESYSTEM_WAIT_SEC * (1000 / FILESYSTEM_WAIT_STEP_MS)
      && udisks_object_peek_filesystem (self->object) == NULL)
    {
      self->wait_for_filesystem += 1;
      return G_SOURCE_CONTINUE;
    }

  if (udisks_object_peek_filesystem (self->object) == NULL)
    {
      gdu_utils_show_message (_("Resizing not ready"),
                              _("Waited too long for the filesystem"),
                              gdu_resize_volume_dialog_get_window (self));
      adw_dialog_close (ADW_DIALOG (self));
      return G_SOURCE_REMOVE;
    }

  udisks_filesystem_call_resize (self->filesystem, get_new_size (self),
                                 g_variant_new ("a{sv}", NULL), NULL,
                                 fs_resize_cb, self);

  return G_SOURCE_REMOVE;
}

static void
part_resize_cb_online_next_fs_resize (GObject      *object,
                                      GAsyncResult *res,
                                      gpointer      user_data)
{
  GduResizeVolumeDialog *self = GDU_RESIZE_VOLUME_DIALOG (user_data);
  UDisksPartition *partition = UDISKS_PARTITION (object);
  g_autoptr(GError) error = NULL;

  if (!udisks_partition_call_resize_finish (partition, res, &error))
    {
      gdu_utils_show_error (gdu_resize_volume_dialog_get_window (self),
                            _("Error resizing partition"), error);
      adw_dialog_close (ADW_DIALOG (self));
      return;
    }

  /* After the partition is resized the filesystem interface might still
   * take some time to show up again. UDisks would have to open the partition
   * block device, issue the ioctl BLKSIZE64 and wait for the size property
   * of the partition interface to be the same and then wait for the filesystem
   * interface to show up (only if it was there before). This depends on
   * inclusion of https://github.com/storaged-project/libblockdev/pull/264
   */
  udisks_client_settle (self->client);
  if (resize_filesystem_waiter (self) == G_SOURCE_CONTINUE)
    {
      g_timeout_add (FILESYSTEM_WAIT_STEP_MS, resize_filesystem_waiter, self);
    }
}

static void
online_resize_no_repair (GduResizeVolumeDialog *self)
{
if (is_shrinking (self))
    {
      /* shrinking: resize filesystem first, then partition: fs-resize, part-resize */
      udisks_filesystem_call_resize (self->filesystem, get_new_size (self),
                                     g_variant_new ("a{sv}", NULL), NULL,
                                     fs_resize_cb_online_next_part_resize, self);
    }
  else
    {
      /* growing: resize partition first, then filesystem: part-resize, fs-resize */
      udisks_partition_call_resize (self->partition, get_new_size (self),
                                    g_variant_new ("a{sv}", NULL), NULL,
                                    part_resize_cb_online_next_fs_resize, self);
    }
}


static void
unmount_cb (GObject      *source_object,
            GAsyncResult *res,
            gpointer      user_data)
{
  GduResizeVolumeDialog *self = GDU_RESIZE_VOLUME_DIALOG (user_data);

  if (!gdu_utils_ensure_unused_finish (self->client, res, NULL))
    {
      adw_dialog_close (ADW_DIALOG (self));
      return;
    }

  /* shrinking partition and filesystem:
   * fs-repair, fs-resize, fs-repair, part-resize
   */
  udisks_filesystem_call_repair (self->filesystem,
                                 g_variant_new ("a{sv}", NULL), NULL,
                                 fs_repair_cb_offline_next_shrink, self);
}

static void
resize (GduResizeVolumeDialog *self)
{
  gboolean shrinking;

  shrinking = is_shrinking (self);

  if ((self->support & ONLINE_SHRINK && shrinking)
      || (self->support & ONLINE_GROW && !shrinking))
    {
      online_resize_no_repair (self);
      return;
    }

  if (shrinking)
    {
      gdu_utils_ensure_unused (self->client,
                               gdu_resize_volume_dialog_get_window (self),
                               self->object, unmount_cb, NULL, self);
      return;
    }

  /* The offline grow case still needs the FS to be mounted during
   * the partition resize to prevent any race conditions with GVFs
   * automount but will unmount as soon as FS repair and resize take place:
   * part-resize, fs-repair, fs-resize, fs-repair
   */
  udisks_partition_call_resize (self->partition, get_new_size (self),
                                g_variant_new ("a{sv}", NULL), NULL,
                                part_resize_cb_offline_next_fs_unmount, self);
}

static void
mount_cb (GObject      *object,
          GAsyncResult *res,
          gpointer      user_data)
{
  GduResizeVolumeDialog *self = GDU_RESIZE_VOLUME_DIALOG (user_data);
  UDisksFilesystem *filesystem = UDISKS_FILESYSTEM (object);
  g_autoptr(GError) error = NULL;

  if (!udisks_filesystem_call_mount_finish (filesystem,
                                            NULL, /* out_mount_path */
                                            res, &error))
    {
      gdu_utils_show_error (gdu_resize_volume_dialog_get_window (self),
                            _("Error mounting the filesystem"), error);
      adw_dialog_close (ADW_DIALOG (self));
      return;
    }

  resize (self);
}

static void
on_resize_clicked_cb (GduResizeVolumeDialog *self)
{
  gboolean offline_shrink;
  const char *const *mount_points;

  if (self->filesystem == NULL)
    {
      /* no filesystem present, just resize partition */
      udisks_partition_call_resize (self->partition, get_new_size (self),
                                    g_variant_new ("a{sv}", NULL), NULL,
                                    part_resize_cb, self);
      return;
    }

  mount_points = udisks_filesystem_get_mount_points (self->filesystem);
  /* prevent the case of mounting when directly unmount would follow */
  offline_shrink = !(self->support & ONLINE_SHRINK) && is_shrinking (self);
  if (g_strv_length ((gchar **)mount_points) == 0 && !offline_shrink)
    {
      /* FS was unmounted by the user while the dialog stayed open */
      udisks_filesystem_call_mount (self->filesystem,
                                    g_variant_new ("a{sv}", NULL), NULL,
                                    mount_cb, self);
      return;
    }

  resize (self);
}

static void
gdu_resize_volume_dialog_finalize (GObject *object)
{
  G_OBJECT_CLASS (gdu_resize_volume_dialog_parent_class)->finalize (object);
}

void
gdu_resize_volume_dialog_class_init (GduResizeVolumeDialogClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->finalize = gdu_resize_volume_dialog_finalize;

  gtk_widget_class_set_template_from_resource (widget_class,
                                               "/org/gnome/DiskUtility/ui/"
                                               "gdu-resize-volume-dialog.ui");

  gtk_widget_class_bind_template_child (widget_class, GduResizeVolumeDialog,size_scale);

  gtk_widget_class_bind_template_child (widget_class, GduResizeVolumeDialog,size_adjustment);
  gtk_widget_class_bind_template_child (widget_class, GduResizeVolumeDialog,free_size_adjustment);

  gtk_widget_class_bind_template_child (widget_class, GduResizeVolumeDialog,max_size_row);
  gtk_widget_class_bind_template_child (widget_class, GduResizeVolumeDialog,min_size_row);
  gtk_widget_class_bind_template_child (widget_class, GduResizeVolumeDialog,current_size_row);
  gtk_widget_class_bind_template_child (widget_class, GduResizeVolumeDialog,size_unit_combo);

  gtk_widget_class_bind_template_child (widget_class, GduResizeVolumeDialog, spinner);

  gtk_widget_class_bind_template_callback (widget_class, on_resize_clicked_cb);
  gtk_widget_class_bind_template_callback (widget_class, on_size_unit_changed_cb);
}

void
gdu_resize_volume_dialog_init (GduResizeVolumeDialog *self)
{
  gtk_widget_init_template (GTK_WIDGET (self));

  self->cur_unit_num = -1;
  g_object_bind_property_full (self->size_adjustment, "value",
                               self->free_size_adjustment, "value",
                               G_BINDING_BIDIRECTIONAL,
                               free_size_binding_func,
                               free_size_binding_func,
                               self,
                               NULL);
}

void
gdu_resize_dialog_show (GtkWindow    *parent_window,
                        UDisksObject *object,
                        UDisksClient *client)
{
  GduResizeVolumeDialog *self;
  const char *const *mount_points;

  self = g_object_new (GDU_TYPE_RESIZE_VOLUME_DIALOG,
                       NULL);

  self->support = 0;
  self->client = client;
  self->object = g_object_ref (object);
  self->block = udisks_object_get_block (object);
  g_assert (self->block != NULL);

  self->partition = udisks_object_get_partition (object);
  g_assert (self->partition != NULL);

  self->filesystem = udisks_object_get_filesystem (object);
  self->table = udisks_client_get_partition_table (self->client, self->partition);
  /* In general no way to find the real filesystem size, just use the partition
   * size. That is ok unless the filesystem was not fitted to the underlaying
   * block size which would just harm the calculation of the minimum shrink
   * size. A workaround then is to resize it first to the partition size. The
   * more serious implication of not detecting the filesystem size is: Resizing
   * just the filesystem without resizing the underlying block device/
   * partition would work but is confusing because the only thing that changes
   * is the free space (since Disks can't show the filesystem size). Therefore
   * this option is not available now. We'll have to add cases like resizing
   * the LUKS, LVM partition or the mounted loop device file.
   */
  self->current_size = udisks_partition_get_size (self->partition);

  self->min_size = 1;
  if (self->partition != NULL && udisks_partition_get_is_container (self->partition))
    self->min_size = gdu_utils_calc_space_to_shrink_extended (self->client,
                                                              self->table,
                                                              self->partition);

  self->max_size = self->partition == NULL
                       ? self->current_size
                       : gdu_utils_calc_space_to_grow (self->client,
                                                       self->table,
                                                       self->partition);

  if (self->filesystem != NULL)
    {
      gboolean available;

      available = gdu_utils_can_resize (self->client,
                                        udisks_block_get_id_type (self->block),
                                        FALSE, &self->support, NULL);
      g_assert (available);

      if (calculate_usage (self) == G_SOURCE_CONTINUE)
        {
          gtk_widget_set_visible (GTK_WIDGET (self->spinner), TRUE);
          self->running_id = g_timeout_add (FILESYSTEM_WAIT_STEP_MS, calculate_usage, self);
        }
    }

  mount_points = self->filesystem != NULL
                     ? udisks_filesystem_get_mount_points (self->filesystem)
                     : NULL;

  if (self->filesystem != NULL && g_strv_length ((gchar **)mount_points) == 0)
    {
      /* mount FS to aquire fill level */
      self->mount_cancellable = g_cancellable_new ();
      udisks_filesystem_call_mount (self->filesystem,
                                    g_variant_new ("a{sv}", NULL), /* options */
                                    self->mount_cancellable,
                                    resize_get_usage_mount_cb, self);
    }

  adw_dialog_present (ADW_DIALOG (self), GTK_WIDGET (parent_window));
}
