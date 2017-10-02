/*
 * Copyright (C) 2017 Kai Lüke
 *
 * Licensed under GPL version 2 or later.
 *
 * Author: Kai Lüke <kailueke@riseup.net>
 */

#include "config.h"

#include <inttypes.h>
#include <glib/gi18n.h>
#include <math.h>

#include "gduapplication.h"
#include "gduwindow.h"
#include "gduresizedialog.h"
#include "gduutils.h"

#define FILESYSTEM_WAIT_STEP_MS 500
#define FILESYSTEM_WAIT_SEC 10

typedef struct
{
  volatile guint ref_count;

  GduWindow *window;
  UDisksClient *client;
  UDisksObject *object;
  UDisksBlock *block;
  UDisksPartition *partition;
  UDisksFilesystem *filesystem;
  UDisksPartitionTable *table;
  ResizeFlags support;
  guint64 min_size;
  guint64 max_size;
  guint64 current_size;

  GtkBuilder *builder;
  GtkWidget *dialog;
  GtkWidget *size_stack;
  GtkWidget *resize_number_grid;
  GtkWidget *size_scale;
  GtkWidget *size_spinbutton;
  GtkWidget *size_difference_spinbutton;
  GtkWidget *free_following_spinbutton;
  GtkAdjustment *size_adjustment;
  GtkAdjustment *free_following_adjustment;
  GtkAdjustment *difference_adjustment;

  GtkWidget *size_unit_combobox;
  GtkWidget *size_unit_following_label;
  GtkWidget *size_unit_difference_label;
  gint cur_unit_num;
  GtkStyleProvider *css_provider;

  GtkWidget *difference_label;
  GtkWidget *explanation_label;
  GtkWidget *spinner;
  GtkWidget *apply;

  GCancellable *mount_cancellable;
  guint running_id;
  guint wait_for_filesystem;
} ResizeDialogData;

static ResizeDialogData *
resize_dialog_data_ref (ResizeDialogData *data)
{
  g_atomic_int_inc (&data->ref_count);
  return data;
}

static void
resize_dialog_data_unref (ResizeDialogData *data)
{
  if (g_atomic_int_dec_and_test (&data->ref_count))
    {
      if (data->running_id)
        {
          g_source_remove (data->running_id);
        }

      g_object_unref (data->window);
      g_object_unref (data->object);
      g_object_unref (data->block);
      g_clear_object (&data->filesystem);
      g_clear_object (&data->partition);
      if (data->dialog != NULL)
        {
          gtk_widget_hide (data->dialog);
          gtk_widget_destroy (data->dialog);
        }

      g_clear_object (&data->builder);
      g_clear_object (&data->mount_cancellable);
      g_free (data);
    }
}

static void
resize_dialog_update (ResizeDialogData *data)
{
  gchar *s;

  s = gtk_combo_box_text_get_active_text (GTK_COMBO_BOX_TEXT (data->size_unit_combobox));
  gtk_label_set_text (GTK_LABEL (data->size_unit_following_label), s);
  gtk_label_set_text (GTK_LABEL (data->size_unit_difference_label), s);

  g_free (s);
}

static void
set_unit_num (ResizeDialogData *data,
              gint              unit_num)
{
  GtkStyleContext *context;
  gchar *css;
  gdouble unit_size;
  gdouble value;
  gdouble value_units;
  gdouble min_size_units;
  gdouble max_size_units;
  gdouble current_units;
  gint num_digits;

  g_assert (unit_num < NUM_UNITS);

  gtk_combo_box_set_active (GTK_COMBO_BOX (data->size_unit_combobox), unit_num);

  if (data->cur_unit_num == -1)
    {
      value = data->current_size;
    }
  else
    {
      value = gtk_adjustment_get_value (data->size_adjustment) * ((gdouble) unit_sizes[data->cur_unit_num]);
    }

  unit_size = unit_sizes[unit_num];
  value_units = value / unit_size;
  min_size_units = ((gdouble) data->min_size) / unit_size;
  max_size_units = ((gdouble) data->max_size) / unit_size;
  current_units = ((gdouble) data->current_size) / unit_size;

  /* show at least three digits in the spin buttons */
  num_digits = 3.0 - ceil (log10 (max_size_units));
  if (num_digits < 0)
    num_digits = 0;

  g_object_freeze_notify (G_OBJECT (data->size_adjustment));
  g_object_freeze_notify (G_OBJECT (data->free_following_adjustment));
  g_object_freeze_notify (G_OBJECT (data->difference_adjustment));

  data->cur_unit_num = unit_num;

  gtk_adjustment_configure (data->size_adjustment,
                            value_units,
                            min_size_units,         /* lower */
                            max_size_units,         /* upper */
                            1,                      /* step increment */
                            100,                    /* page increment */
                            0.0);                   /* page_size */
  gtk_adjustment_configure (data->free_following_adjustment,
                            max_size_units - value_units,
                            0.0,                             /* lower */
                            max_size_units - min_size_units, /* upper */
                            1,                               /* step increment */
                            100,                             /* page increment */
                            0.0);                            /* page_size */
  gtk_adjustment_configure (data->difference_adjustment,
                            value_units - current_units,
                            min_size_units - current_units, /* lower */
                            max_size_units - current_units, /* upper */
                            1,                              /* step increment */
                            100,                            /* page increment */
                            0.0);                           /* page_size */

  gtk_spin_button_set_digits (GTK_SPIN_BUTTON (data->size_spinbutton), num_digits);
  gtk_spin_button_set_digits (GTK_SPIN_BUTTON (data->size_difference_spinbutton), num_digits);
  gtk_spin_button_set_digits (GTK_SPIN_BUTTON (data->free_following_spinbutton), num_digits);

  gtk_adjustment_set_value (data->size_adjustment, value_units);
  gtk_adjustment_set_value (data->free_following_adjustment, max_size_units - value_units);
  gtk_adjustment_set_value (data->difference_adjustment, value_units - current_units);

  gtk_scale_clear_marks (GTK_SCALE (data->size_scale));
  gtk_scale_add_mark (GTK_SCALE (data->size_scale), current_units, GTK_POS_TOP, _("Current Size"));

  context = gtk_widget_get_style_context (data->size_scale);
  if (data->css_provider)
    {
      gtk_style_context_remove_provider (context, data->css_provider);
      g_clear_object (&data->css_provider);
    }

  if (data->min_size > 1)
    {
      gtk_scale_add_mark (GTK_SCALE (data->size_scale), min_size_units, GTK_POS_BOTTOM, _("Minimal Size"));
      css = g_strdup_printf (".partition-scale contents {\n"
                             "  border-left-width: %dpx;\n"
                             "}\n",
                             (gint) (min_size_units / max_size_units * gtk_widget_get_allocated_width (data->size_stack)));
      data->css_provider = GTK_STYLE_PROVIDER (gtk_css_provider_new ());
      gtk_css_provider_load_from_data (GTK_CSS_PROVIDER (data->css_provider), css, -1, NULL);
      gtk_style_context_add_provider (context, data->css_provider, GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);

      g_free (css);
    }

  g_object_thaw_notify (G_OBJECT (data->size_adjustment));
  g_object_thaw_notify (G_OBJECT (data->free_following_adjustment));
  g_object_thaw_notify (G_OBJECT (data->difference_adjustment));
}

static void
resize_dialog_property_changed (GObject     *object,
                                GParamSpec  *pspec,
                                gpointer     user_data)
{
  ResizeDialogData *data = user_data;

  resize_dialog_update (data);
}

static void
on_size_unit_combobox_changed (GtkComboBox *combobox,
                               gpointer     user_data)
{
  ResizeDialogData *data = user_data;
  gint unit_num;

  unit_num = gtk_combo_box_get_active (GTK_COMBO_BOX (data->size_unit_combobox));
  set_unit_num (data, unit_num);

  resize_dialog_update (data);
}

static void
resize_dialog_populate (ResizeDialogData *data)
{

  data->dialog = GTK_WIDGET (gdu_application_new_widget (gdu_window_get_application (data->window),
                                                         "resize-dialog.ui",
                                                         "resize-dialog",
                                                         &data->builder));

  gtk_dialog_add_button (GTK_DIALOG (data->dialog), "gtk-cancel", GTK_RESPONSE_CANCEL);
  data->apply = gtk_dialog_add_button (GTK_DIALOG (data->dialog), _("_Resize"), GTK_RESPONSE_APPLY);
  gtk_style_context_add_class (gtk_widget_get_style_context (data->apply), "destructive-action");
  gtk_widget_grab_default (data->apply);

  data->size_spinbutton = GTK_WIDGET (gtk_builder_get_object (data->builder, "size-spinbutton"));
  data->size_difference_spinbutton = GTK_WIDGET (gtk_builder_get_object (data->builder, "size-difference-spinbutton"));
  data->free_following_spinbutton = GTK_WIDGET (gtk_builder_get_object (data->builder, "free-following-spinbutton"));
  data->size_adjustment = GTK_ADJUSTMENT (gtk_builder_get_object (data->builder, "size-adjustment"));
  g_signal_connect (data->size_adjustment, "notify::value", G_CALLBACK (resize_dialog_property_changed), data);
  data->free_following_adjustment = GTK_ADJUSTMENT (gtk_builder_get_object (data->builder, "free-following-adjustment"));
  data->difference_adjustment = GTK_ADJUSTMENT (gtk_builder_get_object (data->builder, "difference-adjustment"));
  data->size_unit_combobox = GTK_WIDGET (gtk_builder_get_object (data->builder, "size-unit-combobox"));
  g_signal_connect (data->size_unit_combobox, "changed", G_CALLBACK (on_size_unit_combobox_changed), data);
  data->size_unit_difference_label = GTK_WIDGET (gtk_builder_get_object (data->builder, "size-unit-difference-label"));
  data->size_unit_following_label = GTK_WIDGET (gtk_builder_get_object (data->builder, "size-unit-following-label"));
  data->difference_label = GTK_WIDGET (gtk_builder_get_object (data->builder, "difference-label"));
  data->explanation_label = GTK_WIDGET (gtk_builder_get_object (data->builder, "explanation"));
  data->spinner = GTK_WIDGET (gtk_builder_get_object (data->builder, "spinner"));
  data->resize_number_grid = GTK_WIDGET (gtk_builder_get_object (data->builder, "resize-number-grid"));;
  data->size_scale = GTK_WIDGET (gtk_builder_get_object (data->builder, "size-scale"));;
  data->size_stack = GTK_WIDGET (gtk_builder_get_object (data->builder, "size-stack"));

  gtk_window_set_transient_for (GTK_WINDOW (data->dialog), GTK_WINDOW (data->window));
  gtk_dialog_set_default_response (GTK_DIALOG (data->dialog), GTK_RESPONSE_APPLY);
  set_unit_num (data, gdu_utils_get_default_unit (data->max_size));
}

static gboolean
free_size_binding_func (GBinding     *binding,
                        const GValue *source_value,
                        GValue       *target_value,
                        gpointer      user_data)
{
  ResizeDialogData *data = user_data;
  gdouble max_size_units;

  max_size_units = ((gdouble) data->max_size) / unit_sizes[data->cur_unit_num];
  g_value_set_double (target_value, max_size_units - g_value_get_double (source_value));

  return TRUE;
}

static gboolean
difference_binding_func (GBinding     *binding,
                         const GValue *source_value,
                         GValue       *target_value,
                         gpointer      user_data)
{
  ResizeDialogData *data = user_data;
  gdouble current_units;

  current_units = ((gdouble) data->current_size) / unit_sizes[data->cur_unit_num];
  g_value_set_double (target_value, g_value_get_double (source_value) - current_units);

  return TRUE;
}

static gboolean
difference_binding_func_back (GBinding     *binding,
                              const GValue *source_value,
                              GValue       *target_value,
                              gpointer      user_data)
{
  ResizeDialogData *data = user_data;
  gdouble current_units;

  current_units = ((gdouble) data->current_size) / unit_sizes[data->cur_unit_num];
  g_value_set_double (target_value, g_value_get_double (source_value) + current_units);

  return TRUE;
}

static guint64
get_size (ResizeDialogData *data)
{
  guint64 size;

  size = gtk_adjustment_get_value (data->size_adjustment) * unit_sizes[data->cur_unit_num];
  /* choose 0 as maximum in order to avoid errors
   * if partition is later 3 MiB smaller due to alignment etc
   */
  if (size >= data->max_size - 3*1024*1024)
    size = 0;

  return size;
}

static gboolean
is_shrinking (ResizeDialogData *data)
{
  return get_size (data) < data->current_size && get_size (data) != 0;
}

static void
fs_resize_cb (UDisksFilesystem *filesystem,
              GAsyncResult     *res,
              ResizeDialogData *data)
{
  GError *error = NULL;

  if (!udisks_filesystem_call_resize_finish (filesystem, res, &error))
    {
      gdu_utils_show_error (GTK_WINDOW (data->window),
                            _("Error resizing filesystem"),
                            error);
      g_error_free (error);
    }

  resize_dialog_data_unref (data);
}

static void
part_resize_cb (UDisksPartition  *partition,
                GAsyncResult     *res,
                ResizeDialogData *data)
{
  GError *error = NULL;

  if (!udisks_partition_call_resize_finish (partition, res, &error))
    {
      gdu_utils_show_error (GTK_WINDOW (data->window),
                            _("Error resizing partition"),
                            error);
      g_error_free (error);
    }

  resize_dialog_data_unref (data);
}

static void
fs_repair_cb_next_part_resize (UDisksFilesystem *filesystem,
                               GAsyncResult     *res,
                               ResizeDialogData *data)
{
  GError *error = NULL;
  gboolean success = FALSE;

  if (!udisks_filesystem_call_repair_finish (filesystem, &success, res, &error) || !success)
    {
      gdu_utils_show_error (GTK_WINDOW (data->window),
                            _("Error repairing filesystem after resize"),
                            error);
      g_error_free (error);
      resize_dialog_data_unref (data);
    }
  else
    {
      udisks_partition_call_resize (data->partition, get_size (data),
                                    g_variant_new ("a{sv}", NULL), NULL,
                                    (GAsyncReadyCallback) part_resize_cb, data);
    }
}

static void
fs_repair_cb (UDisksFilesystem *filesystem,
              GAsyncResult     *res,
              ResizeDialogData *data)
{
  GError *error = NULL;
  gboolean success = FALSE;

  if (!udisks_filesystem_call_repair_finish (filesystem, &success, res, &error) || !success)
    {
      gdu_utils_show_error (GTK_WINDOW (data->window),
                            _("Error repairing filesystem after resize"),
                            error);
      g_error_free (error);
    }

  resize_dialog_data_unref (data);
}

static void
fs_resize_cb_offline_next_repair (UDisksFilesystem *filesystem,
                                  GAsyncResult     *res,
                                  ResizeDialogData *data)
{
  GError *error = NULL;

  if (!udisks_filesystem_call_resize_finish (filesystem, res, &error))
    {
      gdu_utils_show_error (GTK_WINDOW (data->window),
                            _("Error resizing filesystem"),
                            error);
      g_error_free (error);
      resize_dialog_data_unref (data);
    }
  else
    {
      udisks_filesystem_call_repair (data->filesystem,
                                     g_variant_new ("a{sv}", NULL), NULL,
                                     (GAsyncReadyCallback) fs_repair_cb, data);
    }
}

static gboolean
resize_filesystem_waiter (gpointer user_data)
{
  ResizeDialogData *data = user_data;

  if (data->wait_for_filesystem < FILESYSTEM_WAIT_SEC * (1000 / FILESYSTEM_WAIT_STEP_MS)
      && udisks_object_peek_filesystem (data->object) == NULL)
    {
      data->wait_for_filesystem++;
      return G_SOURCE_CONTINUE;
    }
  else if (udisks_object_peek_filesystem (data->object) == NULL)
    {
      GtkWidget *dialog;

      dialog = gtk_message_dialog_new_with_markup (GTK_WINDOW (data->dialog),
                                                   GTK_DIALOG_MODAL,
                                                   GTK_MESSAGE_ERROR,
                                                   GTK_BUTTONS_CLOSE,
                                                   "<big><b>%s</b></big>",
                                                   _("Resizing not ready"));
      gtk_message_dialog_format_secondary_markup (GTK_MESSAGE_DIALOG (dialog),
                                                  _("Waited too long for the filesystem"));
      gtk_dialog_run (GTK_DIALOG (dialog));

      gtk_widget_destroy (dialog);
      resize_dialog_data_unref (data);

      return G_SOURCE_REMOVE;
    }
  else
    {
      udisks_filesystem_call_resize (data->filesystem, get_size (data),
                                     g_variant_new ("a{sv}", NULL), NULL,
                                     (GAsyncReadyCallback) fs_resize_cb, data);
      return G_SOURCE_REMOVE;
    }
}

static void
part_resize_cb_online_next_fs_resize (UDisksPartition  *partition,
                                      GAsyncResult     *res,
                                      ResizeDialogData *data)
{
  GError *error = NULL;

  if (!udisks_partition_call_resize_finish (partition, res, &error))
    {
      gdu_utils_show_error (GTK_WINDOW (data->window),
                            _("Error resizing partition"),
                            error);
      g_error_free (error);
      resize_dialog_data_unref (data);
    }
  else
    {
      /* After the partition is resized the filesystem interface might still
       * take some time to show up again. UDisks would have to open the partition
       * block device, issue the ioctl BLKSIZE64 and wait for the size property
       * of the partition interface to be the same and then wait for the filesystem
       * interface to show up (only if it was there before). This depends on inclusion
       * of https://github.com/storaged-project/libblockdev/pull/264
       */
      udisks_client_settle (gdu_window_get_client (data->window));
      if (resize_filesystem_waiter (data) == G_SOURCE_CONTINUE)
        {
          g_timeout_add (FILESYSTEM_WAIT_STEP_MS, resize_filesystem_waiter, data);
        }
    }
}

static void
part_resize_cb_offline_next_fs_resize (UDisksPartition  *partition,
                                       GAsyncResult     *res,
                                       ResizeDialogData *data)
{
  GError *error = NULL;

  if (!udisks_partition_call_resize_finish (partition, res, &error))
    {
      gdu_utils_show_error (GTK_WINDOW (data->window),
                            _("Error resizing partition"),
                            error);
      g_error_free (error);
      resize_dialog_data_unref (data);
    }
  else
    {
      udisks_filesystem_call_resize (data->filesystem, get_size (data),
                                     g_variant_new ("a{sv}", NULL), NULL,
                                     (GAsyncReadyCallback) fs_resize_cb_offline_next_repair, data);
    }
}

static void
fs_resize_cb_online_next_part_resize (UDisksFilesystem *filesystem,
                                      GAsyncResult     *res,
                                      ResizeDialogData *data)
{
  GError *error = NULL;

  if (!udisks_filesystem_call_resize_finish (filesystem, res, &error))
    {
      gdu_utils_show_error (GTK_WINDOW (data->window),
                            _("Error resizing filesystem"),
                            error);
      g_error_free (error);
      resize_dialog_data_unref (data);
    }
  else
    {
      udisks_partition_call_resize (data->partition, get_size (data),
                                    g_variant_new ("a{sv}", NULL), NULL,
                                    (GAsyncReadyCallback) part_resize_cb, data);
    }
}

static void
fs_resize_cb_offline_next_fs_repair (UDisksFilesystem *filesystem,
                                       GAsyncResult     *res,
                                       ResizeDialogData *data)
{
  GError *error = NULL;

  if (!udisks_filesystem_call_resize_finish (filesystem, res, &error))
    {
      gdu_utils_show_error (GTK_WINDOW (data->window),
                            _("Error resizing filesystem"),
                            error);
      g_error_free (error);
      resize_dialog_data_unref (data);
    }
  else
    {
      udisks_filesystem_call_repair (data->filesystem,
                                     g_variant_new ("a{sv}", NULL), NULL,
                                     (GAsyncReadyCallback) fs_repair_cb_next_part_resize, data);
    }
}

static void
fs_repair_cb_offline_next_resize (UDisksFilesystem *filesystem,
                                  GAsyncResult     *res,
                                  ResizeDialogData *data)
{
  GError *error = NULL;
  gboolean success = FALSE;

  if (!udisks_filesystem_call_repair_finish (filesystem, &success, res, &error) || !success)
    {
      gdu_utils_show_error (GTK_WINDOW (data->window),
                            _("Error repairing filesystem"),
                            error);
      g_error_free (error);
      resize_dialog_data_unref (data);
    }
  else
    {
      if (is_shrinking (data))
        {
          /* shrinking: next is to resize filesystem, run repair, then resize partition */
          udisks_filesystem_call_resize (data->filesystem, get_size (data),
                                         g_variant_new ("a{sv}", NULL), NULL,
                                         (GAsyncReadyCallback) fs_resize_cb_offline_next_fs_repair, data);
        }
      else
        {
          /* growing: next is to resize partition, then resize filesystem, run repair */
          udisks_partition_call_resize (data->partition, get_size (data),
                                        g_variant_new ("a{sv}", NULL), NULL,
                                        (GAsyncReadyCallback) part_resize_cb_offline_next_fs_resize, data);
        }
    }
}

static void
online_resize_no_repair (ResizeDialogData *data)
{
  if (is_shrinking (data))
    {
      /* shrinking: resize filesystem first, then partition: fs-resize, part-resize */
      udisks_filesystem_call_resize (data->filesystem, get_size (data),
                                     g_variant_new ("a{sv}", NULL), NULL,
                                     (GAsyncReadyCallback) fs_resize_cb_online_next_part_resize, data);
    }
  else
    {
      /* growing: resize partition first, then filesystem: part-resize, fs-resize */
      udisks_partition_call_resize (data->partition, get_size (data),
                                    g_variant_new ("a{sv}", NULL), NULL,
                                    (GAsyncReadyCallback) part_resize_cb_online_next_fs_resize, data);
    }
}

static void
offline_resize_with_repair (ResizeDialogData *data)
{
  /* partition and filesystem resize:
   * if growing fs-repair, part-resize, fs-resize, fs-repair
   * if shrinking fs-repair, fs-resize, fs-repair, part-resize
   */
  udisks_filesystem_call_repair (data->filesystem,
                                 g_variant_new ("a{sv}", NULL), NULL,
                                 (GAsyncReadyCallback) fs_repair_cb_offline_next_resize, data);
}

static void
unmount_cb (GObject      *source_object,
            GAsyncResult *res,
            gpointer      user_data)
{
  ResizeDialogData *data = user_data;
  GError *error = NULL;

  if (!gdu_utils_ensure_unused_finish (data->client, res, &error))
    {
      gdu_utils_show_error (GTK_WINDOW (data->window),
                            _("Error unmounting filesystem for resizing"),
                            error);

      g_error_free (error);
      resize_dialog_data_unref (data);
    }
  else
    {
      offline_resize_with_repair (data);
    }
}

static gboolean
calc_usage (gpointer user_data)
{
  ResizeDialogData *data = user_data;
  gint64 unused;

  /* filesystem was mounted before opening the dialog but it still can take some seconds */
  unused = gdu_utils_get_unused_for_block (data->client, data->block);

  if (unused == -1)
    {
      return G_SOURCE_CONTINUE;
    }
  else
    {
      data->running_id = 0;

      if (data->support & ONLINE_SHRINK || data->support & OFFLINE_SHRINK)
        {
          /* set minimal filesystem size from usage if shrinking is supported */
          data->min_size = data->current_size - unused;
        }
      else
        {
          data->min_size = data->current_size; /* do not allow shrinking */
        }

      gtk_spinner_stop (GTK_SPINNER (data->spinner));
      gtk_stack_set_visible_child (GTK_STACK (data->size_stack), data->resize_number_grid);
      if (data->min_size == data->max_size)
        gtk_button_set_label (GTK_BUTTON (data->apply), _("Fit to size"));

      gtk_widget_set_sensitive (data->apply, TRUE);
      set_unit_num (data, data->cur_unit_num);
      resize_dialog_update (data);

      return G_SOURCE_REMOVE;
    }
}

static void
resize_get_usage_mount_cb (UDisksFilesystem *filesystem,
                           GAsyncResult     *res,
                           gpointer          user_data)
{
  ResizeDialogData *data = (ResizeDialogData *) user_data;
  GError *error = NULL;

  if (!udisks_filesystem_call_mount_finish (filesystem,
                                            NULL, /* out_mount_path */
                                            res,
                                            &error))
    {
      if (data->dialog != NULL)
        {
          /* close dialog if still open */
          if (data->running_id)
            {
              g_source_remove (data->running_id);
              data->running_id = 0;
            }

          g_clear_object (&data->mount_cancellable);
          gtk_dialog_response (GTK_DIALOG (data->dialog), GTK_RESPONSE_CANCEL);
          gdu_utils_show_error (GTK_WINDOW (data->window),
                                _("Error mounting filesystem to calculate minimum size"),
                                error);
        }

      g_error_free (error);
    }

  resize_dialog_data_unref (data);
}

void
gdu_resize_dialog_show (GduWindow    *window,
                        UDisksObject *object)
{
  const gchar *const *mount_points;
  ResizeDialogData *data;

  data = g_new0 (ResizeDialogData, 1);
  data->ref_count = 1;
  data->cur_unit_num = -1;
  data->window = g_object_ref (window);
  data->support = 0;
  data->client = gdu_window_get_client (window);
  data->object = g_object_ref (object);
  data->block = udisks_object_get_block (object);
  g_assert (data->block != NULL);
  data->partition = udisks_object_get_partition (object);
  data->filesystem = udisks_object_get_filesystem (object);
  data->table = NULL;
  data->running_id = 0;
  data->mount_cancellable = NULL;
  data->wait_for_filesystem = 0;
  data->css_provider = NULL;

  /* In general no way to find the real filesystem size, just use the partition
   * size. That is ok unless the filesystem was not fitted to the underlaying
   * block size which would just harm the calculation of the minimum shrink size.
   * A workaround then is to resize it first to the partition size.
   * The more serious implication of not detecting the filesystem size is:
   * Resizing just the filesystem without resizing the underlying block device/
   * partition would work but is confusing because the only thing that changes
   * is the free space (since Disks can't show the filesystem size). Therefore
   * this option is not available now. We'll have to add cases like resizing
   * the LUKS, LVM partition or the mounted loop device file.
   */
  g_assert (data->partition != NULL);
  data->current_size = udisks_partition_get_size (data->partition);
  data->table = udisks_client_get_partition_table (data->client, data->partition);
  data->min_size = 1;
  if (data->partition != NULL && udisks_partition_get_is_container (data->partition))
    {
      data->min_size = gdu_utils_calc_space_to_shrink_extended (data->client, data->table, data->partition);
    }

  if (data->filesystem != NULL)
    {
      gboolean available;

      available = gdu_utils_can_resize (data->client, udisks_block_get_id_type (data->block), FALSE,
                                        &data->support, NULL);
      g_assert (available);
    }

  data->max_size = data->current_size;
  if (data->partition != NULL)
    {
      data->max_size = gdu_utils_calc_space_to_grow (data->client, data->table, data->partition);
    }

  resize_dialog_populate (data);
  resize_dialog_update (data);

  if (data->filesystem == NULL)
    {
      gtk_spinner_stop (GTK_SPINNER (data->spinner));
      gtk_stack_set_visible_child (GTK_STACK (data->size_stack), data->resize_number_grid);
      gtk_widget_set_no_show_all (data->explanation_label, TRUE);
      gtk_widget_hide (data->explanation_label);
    }
  else
    {
      gtk_widget_set_sensitive (data->apply, FALSE);
      if (calc_usage (data) == G_SOURCE_CONTINUE)
        {
          data->running_id = g_timeout_add (FILESYSTEM_WAIT_STEP_MS, calc_usage, data);
        }
    }

  g_object_bind_property_full (data->size_adjustment,
                               "value",
                               data->free_following_adjustment,
                               "value",
                               G_BINDING_BIDIRECTIONAL,
                               free_size_binding_func,
                               free_size_binding_func,
                               data,
                               NULL);
  g_object_bind_property_full (data->size_adjustment,
                               "value",
                               data->difference_adjustment,
                               "value",
                               G_BINDING_BIDIRECTIONAL,
                               difference_binding_func,
                               difference_binding_func_back,
                               data,
                               NULL);

  gtk_widget_show_all (data->dialog);
  set_unit_num (data, data->cur_unit_num);

  if (data->filesystem != NULL)
    mount_points = udisks_filesystem_get_mount_points (data->filesystem);

  if (data->filesystem != NULL && g_strv_length ((gchar **) mount_points) == 0)
    {
      /* mount FS to aquire fill level */
      data->mount_cancellable = g_cancellable_new ();
      udisks_filesystem_call_mount (data->filesystem,
                                    g_variant_new ("a{sv}", NULL), /* options */
                                    data->mount_cancellable,
                                    (GAsyncReadyCallback) resize_get_usage_mount_cb,
                                    resize_dialog_data_ref (data));
    }

  if (gtk_dialog_run (GTK_DIALOG (data->dialog)) == GTK_RESPONSE_APPLY)
    {
      gboolean shrinking;

      shrinking = is_shrinking (data);

      gtk_widget_hide (data->dialog);
      g_clear_pointer (&data->dialog, gtk_widget_destroy);

      if (data->filesystem != NULL)
        {
          mount_points = udisks_filesystem_get_mount_points (data->filesystem);

          if (g_strv_length ((gchar **) mount_points) == 0)
            {
              offline_resize_with_repair (data);
            }
          else if ((!(data->support & ONLINE_SHRINK) && shrinking) ||
                   (!(data->support & ONLINE_GROW) && !shrinking))
            {
              gdu_utils_ensure_unused (data->client,
                                       GTK_WINDOW (window),
                                       object,
                                       unmount_cb,
                                       NULL,
                                       data);
            }
          else
            {
              online_resize_no_repair (data);
            }
        }
      else
        {
          /* no filesystem present, just resize partition */
          udisks_partition_call_resize (data->partition, get_size (data),
                                        g_variant_new ("a{sv}", NULL), NULL,
                                        (GAsyncReadyCallback) part_resize_cb, data);
        }
    }
  else
    {
      /* close dialog now, does not need to be closed anymore through the mount error handler */
      if (data->running_id)
        {
          g_source_remove (data->running_id);
          data->running_id = 0;
        }

      gtk_widget_hide (data->dialog);
      g_clear_pointer (&data->dialog, gtk_widget_destroy);
      if (data->mount_cancellable)
        g_cancellable_cancel (data->mount_cancellable);

      resize_dialog_data_unref (data);
    }
}

