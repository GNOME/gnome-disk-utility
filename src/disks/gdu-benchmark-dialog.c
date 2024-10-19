/*
 * Copyright (C) 2008-2013 Red Hat, Inc.
 *
 * Licensed under GPL version 2 or later.
 *
 * Author: David Zeuthen <zeuthen@gmail.com>
 */

#include "config.h"

#include <glib/gi18n.h>

#include <sys/ioctl.h>
#include <linux/fs.h>

#include "gdu-benchmark-dialog.h"

typedef struct {
  guint64 offset;
  gdouble value;
} BMSample;

typedef struct
{
  gdouble max;
  gdouble min;
  gdouble avg;
} BMStats;

struct _GduBenchmarkDialog
{
  AdwDialog      parent_instance;

  GCancellable  *cancellable;

  GtkWidget     *close_button;
  GtkWidget     *cancel_button;
  GtkWidget     *window_title;

  GtkWidget     *pages_stack;

  /* Configuration Page */
  GtkWidget     *sample_row;
  GtkWidget     *sample_size_row;
  GtkWidget     *access_samples_row;
  GtkWidget     *write_bench_switch;

  /* Results Page */
  GtkWidget     *drawing_area;
  GtkWidget     *sample_size_action_row;
  GtkWidget     *read_rate_row;
  GtkWidget     *write_rate_row;
  GtkWidget     *access_time_row;

  /* must hold bm_lock when reading/writing these */
  GError        *bm_error;
  GCancellable  *bm_cancellable;
  gboolean       bm_in_progress;
  gboolean       bm_update_timeout_pending;

  guint64        bm_size;
  GArray        *read_samples;
  GArray        *write_samples;
  GArray        *atime_samples;

  GSettings     *settings;
  UDisksClient  *client;
  UDisksObject  *object;
  UDisksBlock   *block;

  GtkWindow     *parent_window;
};

G_DEFINE_TYPE (GduBenchmarkDialog, gdu_benchmark_dialog, ADW_TYPE_DIALOG)

G_LOCK_DEFINE (bm_lock);

static gpointer
gdu_benchmark_dialog_get_window (GduBenchmarkDialog *self)
{
  return self->parent_window;
}

static void
gdu_benchmark_dialog_restore_options (GduBenchmarkDialog *self)
{
  gint num_samples;
  gint sample_size_mib;
  gint num_access_samples;
  gboolean write_benchmark;

  num_samples = g_settings_get_int (self->settings, "num-samples");
  sample_size_mib = g_settings_get_int (self->settings, "sample-size-mib");
  num_access_samples = g_settings_get_int (self->settings, "num-access-samples");
  write_benchmark = g_settings_get_boolean (self->settings, "do-write");

  adw_spin_row_set_value (ADW_SPIN_ROW (self->sample_row), num_samples);
  adw_spin_row_set_value (ADW_SPIN_ROW (self->sample_size_row), sample_size_mib);
  adw_spin_row_set_value (ADW_SPIN_ROW (self->access_samples_row), num_access_samples);
  adw_switch_row_set_active (ADW_SWITCH_ROW (self->write_bench_switch), write_benchmark);
}

static void
gdu_benchmark_dialog_save_options (GduBenchmarkDialog *self)
{
  gint num_samples;
  gint sample_size_mib;
  gint num_access_samples;
  gboolean write_benchmark;

  num_samples = adw_spin_row_get_value (ADW_SPIN_ROW (self->sample_row));
  sample_size_mib = adw_spin_row_get_value (ADW_SPIN_ROW (self->sample_size_row));
  num_access_samples = adw_spin_row_get_value (ADW_SPIN_ROW (self->access_samples_row));
  write_benchmark = adw_switch_row_get_active (ADW_SWITCH_ROW (self->write_bench_switch));

  g_settings_set_int (self->settings, "num-samples", num_samples);
  g_settings_set_int (self->settings, "sample-size-mib", sample_size_mib);
  g_settings_set_int (self->settings, "num-access-samples", num_access_samples);
  g_settings_set_boolean (self->settings, "do-write", write_benchmark);
}

static BMStats
get_max_min_avg (GArray *array)
{
  guint n;
  gdouble sum;
  BMStats ret = { 0 };

  if (array->len == 0)
    return ret;

  ret.max = -G_MAXDOUBLE;
  ret.min = G_MAXDOUBLE;
  sum = 0;

  for (n = 0; n < array->len; n++)
    {
      BMSample *s = &g_array_index(array, BMSample, n);
      ret.max = MAX (ret.max, s->value);
      ret.min = MIN (ret.min, s->value);
      sum += s->value;
    }

  ret.avg = sum / array->len;

  return ret;
}

static gboolean
on_drawing_area_draw (GtkDrawingArea *widget,
                      cairo_t        *cr,
                      int             width,
                      int             height,
                      gpointer        user_data)
{
  /* gtk4 todo fill this function */
}

/* ---------------------------------------------------------------------------------------------------- */

static gchar *
format_stats (gdouble stat,
              guint   num_samples,
              gdouble is_atime)
{
  g_autofree char *s;
  g_autofree char *s2;

  s = is_atime ? g_strdup_printf ("%.2f msec", stat * 1000.0)
               : g_strdup_printf ("%s/s", g_format_size ((guint64) stat));
  s2 = g_strdup_printf (g_dngettext (GETTEXT_PACKAGE,
                                     "%u sample",
                                     "%u samples",
                                     num_samples),
                                     num_samples);

  return g_strdup_printf ("%s <small>(%s)</small>", s, s2);
}

static void
update_dialog (GduBenchmarkDialog *self)
{
  g_autoptr(GError) error = NULL;
  BMStats read_stats;
  BMStats write_stats;
  BMStats atime_stats;
  char *s = NULL;

  G_LOCK (bm_lock);
  if (self->bm_error != NULL)
    {
      error = g_steal_pointer (&self->bm_error);
    }  
  G_UNLOCK (bm_lock);

  /* present an error if something went wrong */
  if (error != NULL && (error->domain != G_IO_ERROR || error->code != G_IO_ERROR_CANCELLED))
    {
      gdu_utils_show_error (gdu_benchmark_dialog_get_window (self),
                            "An error occurred",
                            error);          

      
      s = g_strdup ("–");
      adw_action_row_set_subtitle (ADW_ACTION_ROW (self->sample_size_action_row), s);
      adw_action_row_set_subtitle (ADW_ACTION_ROW (self->read_rate_row), s);
      adw_action_row_set_subtitle (ADW_ACTION_ROW (self->write_rate_row), s);
      adw_action_row_set_subtitle (ADW_ACTION_ROW (self->access_time_row), s);
      return;
    }

  G_LOCK (bm_lock);
  read_stats = get_max_min_avg (self->read_samples);
  write_stats = get_max_min_avg (self->write_samples);
  atime_stats = get_max_min_avg (self->atime_samples);
  G_UNLOCK (bm_lock);

  if (read_stats.avg != 0.0)
    {
      s = format_stats (read_stats.avg, self->read_samples->len, FALSE);
      adw_action_row_set_subtitle (ADW_ACTION_ROW (self->read_rate_row), s);
      g_free (s);
    }

  if (write_stats.avg != 0.0)
    {
      s = format_stats (write_stats.avg, self->write_samples->len, FALSE);
      adw_action_row_set_subtitle (ADW_ACTION_ROW (self->write_rate_row), s);
      g_free (s);
    }

  if (atime_stats.avg != 0.0)
    {
      s = format_stats (atime_stats.avg, self->atime_samples->len, TRUE);
      adw_action_row_set_subtitle (ADW_ACTION_ROW (self->access_time_row), s);
      g_free (s);
    }

  gtk_widget_queue_draw (self->drawing_area);
}

/* called on main / UI thread */
static gboolean
bmt_on_timeout (gpointer user_data)
{
  GduBenchmarkDialog *self = user_data;
  update_dialog (self);
  G_LOCK (bm_lock);
  self->bm_update_timeout_pending = FALSE;
  G_UNLOCK (bm_lock);
  return FALSE; /* don't run again */
}

static void
bmt_schedule_update (GduBenchmarkDialog *self)
{
  /* rate-limit updates */
  G_LOCK (bm_lock);
  if (!self->bm_update_timeout_pending)
    {
      g_timeout_add (200, /* ms */
                     bmt_on_timeout,
                     self);
      self->bm_update_timeout_pending = TRUE;
    }
  G_UNLOCK (bm_lock);
}

static gpointer
end_benchmark (GduBenchmarkDialog *self,
               GError             *error,
               int                 fd,
               guint               inhibit_cookie)
{
  if (fd != -1)
    {
      close (fd);
    }
  self->bm_in_progress = FALSE;
  gtk_widget_set_visible (self->cancel_button, FALSE);

  if (inhibit_cookie != 0)
    {
      gtk_application_uninhibit ((gpointer) g_application_get_default (),
                                 inhibit_cookie);
    }

  if (error != NULL)
    {
      G_LOCK (bm_lock);
      self->bm_error = error;
      G_UNLOCK (bm_lock);
    }

  bmt_schedule_update (self);

  return NULL;
}

static GError *
open_for_benchmark (GduBenchmarkDialog *self,
                    int *fd)
{
  GVariantBuilder options_builder;
  GError *error = NULL;
  gboolean write_benchmark = 0;
  g_autoptr (GVariant) fd_index = NULL;
  g_autoptr (GUnixFDList) fd_list = NULL;

  g_assert (fd != NULL);

  write_benchmark = g_settings_get_boolean (self->settings, "do-write");

  g_variant_builder_init (&options_builder, G_VARIANT_TYPE_VARDICT);
  g_variant_builder_add (&options_builder, "{sv}", "writable",
                         g_variant_new_boolean (write_benchmark));

  if (!udisks_block_call_open_for_benchmark_sync (self->block,
                                                  g_variant_builder_end (&options_builder),
                                                  NULL, /* fd_list */
                                                  &fd_index, &fd_list,
                                                  self->bm_cancellable, &error))
    {
      return error;
    }

  *fd = g_unix_fd_list_get (fd_list, g_variant_get_handle (fd_index), NULL);

  return NULL;
}

static GError *
benchmark_transfer_rate (GduBenchmarkDialog *self,
                         guchar *buffer,
                         int fd,
                         long page_size,
                         guint64 disk_size)
{
  guint n;
  guint num_samples = 0;
  gint sample_size = 0;
  gboolean write_benchmark = 0;
  GError *error = NULL;

  g_assert (fd != -1);
  g_assert (buffer != NULL);

  num_samples = (guint)g_settings_get_int (self->settings, "num-samples");
  sample_size = g_settings_get_int (self->settings, "sample-size-mib");
  sample_size = sample_size * 1024 * 1024;
  write_benchmark = g_settings_get_boolean (self->settings, "do-write");

  for (n = 0; n < num_samples; n++)
    {
      g_autofree char *s = NULL;
      g_autofree char *s2 = NULL;
      gint64 begin_usec;
      gint64 end_usec;
      gint64 offset;
      ssize_t num_read;
      BMSample sample = { 0 };

      if (g_cancellable_set_error_if_cancelled (self->bm_cancellable, &error))
        return error;

      /* figure out offset and align to page-size */
      offset = n * disk_size / num_samples;
      offset &= ~(page_size - 1);

      if (lseek (fd, offset, SEEK_SET) != offset)
        {
          g_set_error (&error, G_IO_ERROR, g_io_error_from_errno (errno),
                       "Error seeking to offset %lld", (long long int)offset);
          return error;
        }

      if (read (fd, buffer, page_size) != page_size)
        {
          s = g_format_size_full (page_size, G_FORMAT_SIZE_LONG_FORMAT);
          s2 = g_format_size_full (offset, G_FORMAT_SIZE_LONG_FORMAT);
          g_set_error (&error, G_IO_ERROR, g_io_error_from_errno (errno),
                       "Error pre-reading %s from offset %s", s, s2);
          return error;
        }

      if (lseek (fd, offset, SEEK_SET) != offset)
        {
          s = g_format_size_full (offset, G_FORMAT_SIZE_LONG_FORMAT);
          g_set_error (&error, G_IO_ERROR, g_io_error_from_errno (errno),
                       "Error seeking to offset %s", s);
          return error;
        }

      begin_usec = g_get_monotonic_time ();
      num_read = read (fd, buffer, sample_size);
      if (G_UNLIKELY (num_read < 0))
        {
          s = g_format_size_full (sample_size, G_FORMAT_SIZE_LONG_FORMAT);
          s2 = g_format_size_full (offset, G_FORMAT_SIZE_LONG_FORMAT);
          g_set_error (&error, G_IO_ERROR, g_io_error_from_errno (errno),
                       "Error reading %s from offset %s", s, s2);
          return error;
        }
      end_usec = g_get_monotonic_time ();

      sample.offset = offset;
      sample.value = ((gdouble)G_USEC_PER_SEC) * num_read / (end_usec - begin_usec);

      G_LOCK (bm_lock);
      g_array_append_val (self->read_samples, sample);
      G_UNLOCK (bm_lock);

      if (write_benchmark)
        {
          ssize_t num_written;

          /* and now write the same block again... */
          if (lseek (fd, offset, SEEK_SET) != offset)
            {
              g_set_error (&error, G_IO_ERROR, g_io_error_from_errno (errno),
                           "Error seeking to offset %lld",
                           (long long int)offset);
              return error;
            }
          if (read (fd, buffer, page_size) != page_size)
            {
              g_set_error (&error, G_IO_ERROR, g_io_error_from_errno (errno),
                           "Error pre-reading %lld bytes from offset %lld",
                           (long long int)page_size, (long long int)offset);
              return error;
            }
          if (lseek (fd, offset, SEEK_SET) != offset)
            {
              g_set_error (&error, G_IO_ERROR, g_io_error_from_errno (errno),
                           "Error seeking to offset %lld",
                           (long long int)offset);
              return error;
            }

          begin_usec = g_get_monotonic_time ();
          num_written = write (fd, buffer, num_read);
          if (G_UNLIKELY (num_written < 0))
            {
              g_set_error (&error, G_IO_ERROR, g_io_error_from_errno (errno),
                           "Error writing %lld bytes at offset %lld: %m",
                           (long long int)num_read, (long long int)offset);
              return error;
            }

          if (num_written != num_read)
            {
              g_set_error (&error, G_IO_ERROR, g_io_error_from_errno (errno),
                           "Expected to write %lld bytes, only wrote %lld: %m",
                           (long long int)num_read,
                           (long long int)num_written);
              return error;
            }

          if (fsync (fd) != 0)
            {
              g_set_error (&error, G_IO_ERROR, g_io_error_from_errno (errno),
                           "Error syncing (at offset %lld): %m",
                           (long long int)offset);
              return error;
            }
          end_usec = g_get_monotonic_time ();

          sample.offset = offset;
          sample.value = ((gdouble)G_USEC_PER_SEC) * num_written
                         / (end_usec - begin_usec);

          G_LOCK (bm_lock);
          g_array_append_val (self->write_samples, sample);
          G_UNLOCK (bm_lock);
        }
      bmt_schedule_update (self);
    }

  return NULL;
}

static GError *
benchmark_access_time (GduBenchmarkDialog *self,
                       guchar *buffer,
                       int fd,
                       long page_size,
                       guint64 disk_size)
{
  guint n;
  GError *error = NULL;
  guint num_access_samples = 0;
  g_autoptr (GRand) rand = NULL;

  g_assert (buffer != NULL);
  g_assert (fd != -1);

  num_access_samples = (guint) g_settings_get_int (self->settings, "num-access-samples");
  rand = g_rand_new_with_seed (42); /* want this to be deterministic (per size) so it's repeatable */

  for (n = 0; n < num_access_samples; n++)
    {
      gint64 begin_usec;
      gint64 end_usec;
      gint64 offset;
      ssize_t num_read;
      BMSample sample = { 0 };

      if (g_cancellable_set_error_if_cancelled (self->bm_cancellable, &error))
        {
          return error;
        }

      offset = (guint64)g_rand_double_range (rand, 0, (gdouble)disk_size);
      offset &= ~(page_size - 1);

      if (lseek (fd, offset, SEEK_SET) != offset)
        {
          g_set_error (&error, G_IO_ERROR, g_io_error_from_errno (errno),
                       C_ ("benchmarking", "Error seeking to offset %lld: %m"),
                       (long long int)offset);
          return error;
        }

      begin_usec = g_get_monotonic_time ();
      num_read = read (fd, buffer, page_size);
      if (G_UNLIKELY (num_read < 0))
        {
          g_set_error (
              &error, G_IO_ERROR, g_io_error_from_errno (errno),
              C_ ("benchmarking", "Error reading %lld bytes from offset %lld"),
              (long long int)page_size, (long long int)offset);
          return error;
        }
      end_usec = g_get_monotonic_time ();

      sample.offset = offset;
      sample.value = (end_usec - begin_usec) / ((gdouble)G_USEC_PER_SEC);

      G_LOCK (bm_lock);
      g_array_append_val (self->atime_samples, sample);
      G_UNLOCK (bm_lock);

      bmt_schedule_update (self);
    }

  return NULL;
}

static gpointer
benchmark_thread (gpointer user_data)
{
  GduBenchmarkDialog *self = user_data;
  GError *error = NULL;
  guchar *buffer = NULL;
  g_autofree guchar *buffer_unaligned = NULL;
  int fd = -1;
  long page_size;
  guint64 disk_size;
  guint inhibit_cookie;
  gint sample_size_mib = 0;

  sample_size_mib = g_settings_get_int (self->settings, "sample-size-mib");

  inhibit_cookie = gtk_application_inhibit ((gpointer)g_application_get_default (),
                                            self->parent_window,
                                            GTK_APPLICATION_INHIBIT_SUSPEND |
                                            GTK_APPLICATION_INHIBIT_LOGOUT,
                                            /* Translators: Reason why suspend/logout is being inhibited */
                                            "Benchmark in progress");

  error = open_for_benchmark (self, &fd);
  if (error != NULL)
    {
      return end_benchmark (self, error, fd, inhibit_cookie);
    }

  /* We can't use udisks_block_get_size() because the media may have
   * changed and udisks may not have noticed. TODO: maybe have a
   * Block.GetSize() method instead...
   */
  if (ioctl (fd, BLKGETSIZE64, &disk_size) != 0)
    {
      g_set_error (&error, G_IO_ERROR, g_io_error_from_errno (errno),
                   "Error getting size of device: %m");
      return end_benchmark (self, error, fd, inhibit_cookie);
    }

  page_size = sysconf (_SC_PAGESIZE);
  if (page_size < 1)
    {
      g_set_error (&error, G_IO_ERROR, g_io_error_from_errno (errno),
                   "Error getting page size: %m\n");
      return end_benchmark (self, error, fd, inhibit_cookie);
    }

  buffer_unaligned = g_new0 (guchar, sample_size_mib * 1024 * 1024 + page_size);
  buffer = (guchar *)(((gintptr)(buffer_unaligned + page_size)) & (~(page_size - 1)));

  G_LOCK (bm_lock);
  self->bm_size = disk_size;
  G_UNLOCK (bm_lock);

  error = benchmark_transfer_rate (self, buffer, fd, page_size, disk_size);
  if (error != NULL)
    {
      return end_benchmark (self, error, fd, inhibit_cookie);
    }

  error = benchmark_access_time (self, buffer, fd, page_size, disk_size);
  if (error != NULL)
    {
      return end_benchmark (self, error, fd, inhibit_cookie);
    }

  return end_benchmark (self, error, fd, inhibit_cookie);
}

static void
on_cancel_clicked_cb (GduBenchmarkDialog *self)
{
  g_cancellable_cancel (self->bm_cancellable);
}

static void
start_benchmark (GduBenchmarkDialog *self)
{
  gint sample_size = 0;
  g_autofree char *s = NULL;
  self->bm_in_progress = TRUE;
  g_array_set_size (self->read_samples, 0);
  g_array_set_size (self->write_samples, 0);
  g_array_set_size (self->atime_samples, 0);
  g_cancellable_reset (self->bm_cancellable);

  sample_size = g_settings_get_int (self->settings, "sample-size-mib");
  sample_size = sample_size * 1024 * 1024;

  if (sample_size != 0)
  {
    s = g_format_size_full (sample_size, G_FORMAT_SIZE_IEC_UNITS | G_FORMAT_SIZE_LONG_FORMAT);
    adw_action_row_set_subtitle (ADW_ACTION_ROW (self->sample_size_action_row), s);
  }

  g_thread_new ("benchmark-thread",
                benchmark_thread,
                self);
}

static void
ensure_unused_cb (GtkWindow     *window,
                  GAsyncResult  *res,
                  gpointer       user_data)
{
  GduBenchmarkDialog *self = user_data;
  if (gdu_utils_ensure_unused_finish (self->client, res, NULL))
    {
      start_benchmark (self);
    }
}

static void
on_start_clicked_cb (GduBenchmarkDialog *self,
                     GtkButton          *button)
{
  gboolean write_benchmark;

  g_assert (!self->bm_in_progress);

  gdu_benchmark_dialog_save_options (self);
  
  write_benchmark = g_settings_get_boolean (self->settings, "do-write");
  
  if (write_benchmark)
    {
      /* ensure the device is unused (e.g. unmounted) before formatting it... */
      gdu_utils_ensure_unused (self->client,
                               gdu_benchmark_dialog_get_window (self),
                               self->object,
                               (GAsyncReadyCallback) ensure_unused_cb,
                               NULL, /* GCancellable */
                               self);
    }
  else
    {
      start_benchmark (self);
    }

  gtk_stack_set_visible_child_name (GTK_STACK (self->pages_stack), "results");
  gtk_widget_set_visible (self->close_button, FALSE);
}

static void
gdu_benchmark_dialog_set_title (GduBenchmarkDialog *self)
{
  g_autoptr(UDisksObjectInfo) info = NULL;

  info = udisks_client_get_object_info (self->client, self->object);
  adw_window_title_set_subtitle (ADW_WINDOW_TITLE (self->window_title), udisks_object_info_get_one_liner (info));
}

static gboolean
set_sample_size_unit_cb (AdwSpinRow  *spin_row,
                        gpointer    *user_data)
{
  GtkAdjustment *adjustment;
  g_autofree char *unit = NULL;
  
  adjustment = adw_spin_row_get_adjustment (spin_row);
  unit = g_strdup_printf ("%.2f MiB", gtk_adjustment_get_value (adjustment));
  gtk_editable_set_text (GTK_EDITABLE (spin_row), unit);
  
  return TRUE;
}

static void
gdu_benchmark_dialog_finalize (GObject *object)
{
  G_OBJECT_CLASS (gdu_benchmark_dialog_parent_class)->finalize (object);
}

void
gdu_benchmark_dialog_class_init (GduBenchmarkDialogClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);
 
  object_class->finalize = gdu_benchmark_dialog_finalize;

  gtk_widget_class_set_template_from_resource (widget_class,
                                               "/org/gnome/DiskUtility/ui/"
                                               "gdu-benchmark-dialog.ui");

  gtk_widget_class_bind_template_child (widget_class, GduBenchmarkDialog, close_button);
  gtk_widget_class_bind_template_child (widget_class, GduBenchmarkDialog, cancel_button);
  gtk_widget_class_bind_template_child (widget_class, GduBenchmarkDialog, window_title);

  gtk_widget_class_bind_template_child (widget_class, GduBenchmarkDialog, pages_stack);

  gtk_widget_class_bind_template_child (widget_class, GduBenchmarkDialog, sample_row);
  gtk_widget_class_bind_template_child (widget_class, GduBenchmarkDialog, sample_size_row);
  gtk_widget_class_bind_template_child (widget_class, GduBenchmarkDialog, access_samples_row);
  gtk_widget_class_bind_template_child (widget_class, GduBenchmarkDialog, write_bench_switch);

  gtk_widget_class_bind_template_child (widget_class, GduBenchmarkDialog, drawing_area);
  gtk_widget_class_bind_template_child (widget_class, GduBenchmarkDialog, sample_size_action_row);
  gtk_widget_class_bind_template_child (widget_class, GduBenchmarkDialog, read_rate_row);
  gtk_widget_class_bind_template_child (widget_class, GduBenchmarkDialog, write_rate_row);
  gtk_widget_class_bind_template_child (widget_class, GduBenchmarkDialog, access_time_row);

  gtk_widget_class_bind_template_callback (widget_class, set_sample_size_unit_cb);
  gtk_widget_class_bind_template_callback (widget_class, on_start_clicked_cb);
  gtk_widget_class_bind_template_callback (widget_class, on_cancel_clicked_cb);
}

void
gdu_benchmark_dialog_init (GduBenchmarkDialog *self)
{
  gtk_widget_init_template (GTK_WIDGET (self));

  self->settings = g_settings_new ("org.gnome.Disks.benchmark");
  self->bm_cancellable = g_cancellable_new ();

  self->read_samples = g_array_new (FALSE, /* zero-terminated */
                                       FALSE, /* clear */
                                       sizeof (BMSample));
  self->write_samples = g_array_new (FALSE, /* zero-terminated */
                                        FALSE, /* clear */
                                        sizeof (BMSample));
  self->atime_samples = g_array_new (FALSE, /* zero-terminated */
                                              FALSE, /* clear */
                                              sizeof (BMSample));

  gtk_drawing_area_set_draw_func (GTK_DRAWING_AREA (self->drawing_area), (GtkDrawingAreaDrawFunc) on_drawing_area_draw, NULL, NULL);
}

void
gdu_benchmark_dialog_show (GtkWindow    *parent_window,
                           UDisksObject *object,
                           UDisksClient *client)
{
  GduBenchmarkDialog *self;

  self = g_object_new (GDU_TYPE_BENCHMARK_DIALOG, NULL);
  self->object = g_object_ref (object);
  self->parent_window = g_object_ref (parent_window);
  self->block = udisks_object_peek_block (self->object);
  self->client = client;

  gdu_benchmark_dialog_set_title (self);
  gdu_benchmark_dialog_restore_options (self);

  /* if device is read-only, uncheck the "perform write-test"
   * check-button and also make it insensitive
   */
  if (udisks_block_get_read_only (self->block))
    {
      adw_switch_row_set_active (ADW_SWITCH_ROW (self->write_bench_switch), FALSE);
      gtk_widget_set_sensitive (self->write_bench_switch, FALSE);
    }

  /* If the device is currently in use, uncheck the "perform write-test" check-button */
  if (gdu_utils_is_in_use (self->client, self->object))
    {
      adw_switch_row_set_active (ADW_SWITCH_ROW (self->write_bench_switch), FALSE);
    }

  adw_dialog_present (ADW_DIALOG (self), GTK_WIDGET (parent_window));
}
