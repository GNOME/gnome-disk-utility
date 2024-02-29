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

/* ---------------------------------------------------------------------------------------------------- */

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

/* ---------------------------------------------------------------------------------------------------- */

typedef enum {
  BM_STATE_NONE,
  BM_STATE_OPENING_DEVICE,
  BM_STATE_TRANSFER_RATE,
  BM_STATE_ACCESS_TIME,
} BMState;

struct _GduBenchmarkDialog
{
  AdwWindow      parent_instance;

  GCancellable  *cancellable;

  GtkWidget     *window_title;

  GtkWidget     *pages_stack;

  /* Configuration Page */
  GtkWidget     *sample_row;
  GtkWidget     *sample_size_row;
  GtkWidget     *access_samples_row;
  GtkWidget     *write_bench_switch;

  /* Results Page */
  GtkWidget     *drawing_area;
  GtkWidget     *sample_size_label;
  GtkWidget     *read_rate_label;
  GtkWidget     *write_rate_label;
  GtkWidget     *access_time_label;

  /* ---- */

  /* retrieved from preferences dialog */
  gint           bm_num_samples;
  gint           bm_sample_size_mib;
  gboolean       bm_do_write;
  gint           bm_num_access_samples;

  /* must hold bm_lock when reading/writing these */
  GCancellable  *bm_cancellable;
  gboolean       bm_in_progress;
  BMState        bm_state;
  GError        *bm_error; /* set by benchmark thread on termination */
  gboolean       bm_update_timeout_pending;

  gint64         bm_time_benchmarked_usec; /* 0 if never benchmarked, otherwise micro-seconds since Epoch */
  guint64        bm_size;
  guint64        bm_sample_size;
  GArray        *read_samples;
  GArray        *write_samples;
  GArray        *atime_samples;

  UDisksClient  *client;
  UDisksObject  *object;
  UDisksBlock   *block;
};

G_DEFINE_TYPE (GduBenchmarkDialog, gdu_benchmark_dialog, ADW_TYPE_WINDOW)

G_LOCK_DEFINE (bm_lock);

static void update_dialog (GduBenchmarkDialog *self);

/* ---------------------------------------------------------------------------------------------------- */

static gpointer
gdu_benchmark_dialog_get_window (GduBenchmarkDialog *self)
{
  return gtk_widget_get_ancestor (GTK_WIDGET (self), GTK_TYPE_WINDOW);
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

static gdouble
measure_width (cairo_t     *cr,
               const gchar *s)
{
  cairo_text_extents_t te;
  cairo_text_extents (cr, s, &te);
  return te.width;
}

static gdouble
measure_height (cairo_t     *cr,
                const gchar *s)
{
  cairo_text_extents_t te;
  cairo_text_extents (cr, s, &te);
  return te.height;
}

static gboolean
on_drawing_area_draw (GtkWidget      *widget,
                      cairo_t        *cr,
                      gpointer        user_data)
{}

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
  GdkSurface *window = NULL;
  BMStats read_stats;
  BMStats write_stats;
  BMStats atime_stats;
  char *s = NULL;

  G_LOCK (bm_lock);
  if (self->bm_error != NULL)
    {
      error = self->bm_error;
      self->bm_error = NULL;
    }
  G_UNLOCK (bm_lock);

  /* present an error if something went wrong */
  if (error != NULL && (error->domain != G_IO_ERROR || error->code != G_IO_ERROR_CANCELLED))
    gdu_utils_show_error (gdu_benchmark_dialog_get_window (self), "An error occurred", error);

  G_LOCK (bm_lock);

  read_stats = get_max_min_avg (self->read_samples);
  write_stats = get_max_min_avg (self->write_samples);
  atime_stats = get_max_min_avg (self->atime_samples);

  G_UNLOCK (bm_lock);

  if (self->bm_sample_size != 0)
    {
      s = g_format_size_full (self->bm_sample_size, G_FORMAT_SIZE_IEC_UNITS | G_FORMAT_SIZE_LONG_FORMAT);
      gtk_label_set_markup (GTK_LABEL (self->sample_size_label), s);
      g_free (s);
    }

  if (read_stats.avg != 0.0)
    {
      s = format_stats (read_stats.avg, self->read_samples->len, FALSE);
      gtk_label_set_markup (GTK_LABEL (self->read_rate_label), s);
      g_free (s);
    }

  if (write_stats.avg != 0.0)
    {
      s = format_stats (write_stats.avg, self->write_samples->len, FALSE);
      gtk_label_set_markup (GTK_LABEL (self->write_rate_label), s);
      g_free (s);
    }

  if (atime_stats.avg != 0.0)
    {
      s = format_stats (atime_stats.avg, self->atime_samples->len, TRUE);
      gtk_label_set_markup (GTK_LABEL (self->access_time_label), s);
      g_free (s);
    }

  /* gtk4 todo
  window = gtk_widget_get_window (self->drawing_area);
  if (window != NULL)
    gdk_window_invalidate_rect (window, NULL, TRUE);
  */

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
benchmark_thread (gpointer user_data)
{
  GduBenchmarkDialog *self = user_data;
  g_autoptr(GVariant) fd_index = NULL;
  g_autoptr(GUnixFDList) fd_list = NULL;
  g_autoptr(GError) error = NULL;
  g_autoptr(GRand) rand = NULL;
  g_autofree char *buffer_unaligned = NULL;
  g_autofree char *buffer = NULL;
  int fd = -1;
  gint n;
  long page_size;
  guint64 disk_size;
  GVariantBuilder options_builder;
  guint inhibit_cookie;

  inhibit_cookie = gtk_application_inhibit ((gpointer)g_application_get_default (),
                                            self,
                                            GTK_APPLICATION_INHIBIT_SUSPEND |
                                            GTK_APPLICATION_INHIBIT_LOGOUT,
                                            /* Translators: Reason why suspend/logout is being inhibited */
                                            C_("create-inhibit-message", "Benchmarking device"));

  g_variant_builder_init (&options_builder, G_VARIANT_TYPE_VARDICT);
  g_variant_builder_add (&options_builder, "{sv}", "writable", g_variant_new_boolean (self->bm_do_write));

  if (!udisks_block_call_open_for_benchmark_sync (self->block,
                                                  g_variant_builder_end (&options_builder),
                                                  NULL, /* fd_list */
                                                  &fd_index,
                                                  &fd_list,
                                                  self->bm_cancellable,
                                                  &error))
    goto out;

  fd = g_unix_fd_list_get (fd_list, g_variant_get_handle (fd_index), NULL);

  /* We can't use udisks_block_get_size() because the media may have
   * changed and udisks may not have noticed. TODO: maybe have a
   * Block.GetSize() method instead...
   */
  if (ioctl (fd, BLKGETSIZE64, &disk_size) != 0)
    {
      g_set_error (&error,
                   G_IO_ERROR,
                   g_io_error_from_errno (errno),
                   C_("benchmarking", "Error getting size of device: %m"));
      goto out;
    }

  page_size = sysconf (_SC_PAGESIZE);
  if (page_size < 1)
    {
      g_set_error (&error,
                   G_IO_ERROR,
                   g_io_error_from_errno (errno),
                   C_("benchmarking", "Error getting page size: %m\n"));
      goto out;
    }

  buffer_unaligned = g_new0 (guchar, self->bm_sample_size_mib*1024*1024 + page_size);
  buffer = (guchar*) (((gintptr) (buffer_unaligned + page_size)) & (~(page_size - 1)));

  /* transfer rate... */
  G_LOCK (bm_lock);
  self->bm_size = disk_size;
  self->bm_sample_size = self->bm_sample_size_mib*1024*1024;
  self->bm_state = BM_STATE_TRANSFER_RATE;
  G_UNLOCK (bm_lock);
  for (n = 0; n < self->bm_num_samples; n++)
    {
      gchar *s, *s2;
      gint64 begin_usec;
      gint64 end_usec;
      gint64 offset;
      ssize_t num_read;
      BMSample sample = {0};

      if (g_cancellable_set_error_if_cancelled (self->bm_cancellable, &error))
        goto out;

      /* figure out offset and align to page-size */
      offset = n * disk_size / self->bm_num_samples;
      offset &= ~(page_size - 1);

      if (lseek (fd, offset, SEEK_SET) != offset)
        {
          g_set_error (&error,
                       G_IO_ERROR,
                       g_io_error_from_errno (errno),
                       C_("benchmarking", "Error seeking to offset %lld"),
                       (long long int) offset);
          goto out;
        }
      if (read (fd, buffer, page_size) != page_size)
        {
          s = g_format_size_full (page_size, G_FORMAT_SIZE_LONG_FORMAT);
          s2 = g_format_size_full (offset, G_FORMAT_SIZE_LONG_FORMAT);
          g_set_error (&error,
                       G_IO_ERROR,
                       g_io_error_from_errno (errno),
                       C_("benchmarking", "Error pre-reading %s from offset %s"),
                       s, s2);
          g_free (s2);
          g_free (s);
          goto out;
        }
      if (lseek (fd, offset, SEEK_SET) != offset)
        {
          s = g_format_size_full (offset, G_FORMAT_SIZE_LONG_FORMAT);
          g_set_error (&error,
                       G_IO_ERROR,
                       g_io_error_from_errno (errno),
                       C_("benchmarking", "Error seeking to offset %s"),
                       s);
          g_free (s);
          goto out;
        }
      begin_usec = g_get_monotonic_time ();
      num_read = read (fd, buffer, self->bm_sample_size_mib*1024*1024);
      if (G_UNLIKELY (num_read < 0))
        {
          s = g_format_size_full (self->bm_sample_size_mib * 1024 * 1024, G_FORMAT_SIZE_LONG_FORMAT);
          s2 = g_format_size_full (offset, G_FORMAT_SIZE_LONG_FORMAT);
          g_set_error (&error,
                       G_IO_ERROR,
                       g_io_error_from_errno (errno),
                       C_("benchmarking", "Error reading %s from offset %s"),
                       s, s2);
          g_free (s2);
          g_free (s);
          goto out;
        }
      end_usec = g_get_monotonic_time ();

      sample.offset = offset;
      sample.value = ((gdouble) G_USEC_PER_SEC) * num_read / (end_usec - begin_usec);
      G_LOCK (bm_lock);
      g_array_append_val (self->read_samples, sample);
      G_UNLOCK (bm_lock);

      bmt_schedule_update (self);

      if (self->bm_do_write)
        {
          ssize_t num_written;

          /* and now write the same block again... */
          if (lseek (fd, offset, SEEK_SET) != offset)
            {
              g_set_error (&error,
                           G_IO_ERROR,
                           g_io_error_from_errno (errno),
                           C_("benchmarking", "Error seeking to offset %lld"),
                           (long long int) offset);
              goto out;
            }
          if (read (fd, buffer, page_size) != page_size)
            {
              g_set_error (&error,
                           G_IO_ERROR,
                           g_io_error_from_errno (errno),
                           C_("benchmarking", "Error pre-reading %lld bytes from offset %lld"),
                           (long long int) page_size,
                           (long long int) offset);
              goto out;
            }
          if (lseek (fd, offset, SEEK_SET) != offset)
            {
              g_set_error (&error,
                           G_IO_ERROR,
                           g_io_error_from_errno (errno),
                           C_("benchmarking", "Error seeking to offset %lld"),
                           (long long int) offset);
              goto out;
            }
          begin_usec = g_get_monotonic_time ();
          num_written = write (fd, buffer, num_read);
          if (G_UNLIKELY (num_written < 0))
            {
              g_set_error (&error,
                           G_IO_ERROR,
                           g_io_error_from_errno (errno),
                           C_("benchmarking", "Error writing %lld bytes at offset %lld: %m"),
                           (long long int) num_read,
                           (long long int) offset);
              goto out;
            }
          if (num_written != num_read)
            {
              g_set_error (&error,
                           G_IO_ERROR,
                           g_io_error_from_errno (errno),
                           C_("benchmarking", "Expected to write %lld bytes, only wrote %lld: %m"),
                           (long long int) num_read,
                           (long long int) num_written);
              goto out;
            }
          if (fsync (fd) != 0)
            {
              g_set_error (&error,
                           G_IO_ERROR,
                           g_io_error_from_errno (errno),
                           C_("benchmarking", "Error syncing (at offset %lld): %m"),
                           (long long int) offset);
              goto out;
            }
          end_usec = g_get_monotonic_time ();

          sample.offset = offset;
          sample.value = ((gdouble) G_USEC_PER_SEC) * num_written / (end_usec - begin_usec);
          G_LOCK (bm_lock);
          g_array_append_val (self->write_samples, sample);
          G_UNLOCK (bm_lock);

          bmt_schedule_update (self);
        }
    }

  /* access time... */
  G_LOCK (bm_lock);
  self->bm_state = BM_STATE_ACCESS_TIME;
  G_UNLOCK (bm_lock);
  rand = g_rand_new_with_seed (42); /* want this to be deterministic (per size) so it's repeatable */
  for (n = 0; n < self->bm_num_access_samples; n++)
    {
      gint64 begin_usec;
      gint64 end_usec;
      gint64 offset;
      ssize_t num_read;
      BMSample sample = {0};

      if (g_cancellable_set_error_if_cancelled (self->bm_cancellable, &error))
        goto out;

      offset = (guint64) g_rand_double_range (rand, 0, (gdouble) disk_size);
      offset &= ~(page_size - 1);

      if (lseek (fd, offset, SEEK_SET) != offset)
        {
          g_set_error (&error,
                       G_IO_ERROR,
                       g_io_error_from_errno (errno),
                       C_("benchmarking", "Error seeking to offset %lld: %m"),
                       (long long int) offset);
          goto out;
        }

      begin_usec = g_get_monotonic_time ();
      num_read = read (fd, buffer, page_size);
      if (G_UNLIKELY (num_read < 0))
        {
          g_set_error (&error,
                       G_IO_ERROR,
                       g_io_error_from_errno (errno),
                       C_("benchmarking", "Error reading %lld bytes from offset %lld"),
                       (long long int) page_size,
                       (long long int) offset);
          goto out;
        }
      end_usec = g_get_monotonic_time ();

      sample.offset = offset;
      sample.value = (end_usec - begin_usec) / ((gdouble) G_USEC_PER_SEC);
      G_LOCK (bm_lock);
      g_array_append_val (self->atime_samples, sample);
      G_UNLOCK (bm_lock);

      bmt_schedule_update (self);
    }

  G_LOCK (bm_lock);
  self->bm_time_benchmarked_usec = g_get_real_time ();
  G_UNLOCK (bm_lock);

 out:
  if (fd != -1)
    close (fd);
  self->bm_in_progress = FALSE;
  self->bm_state = BM_STATE_NONE;

  if (inhibit_cookie > 0)
    {
      gtk_application_uninhibit ((gpointer)g_application_get_default (), inhibit_cookie);
    }

  if (error != NULL)
    {
      G_LOCK (bm_lock);
      self->bm_error = error;
      g_array_set_size (self->read_samples, 0);
      g_array_set_size (self->write_samples, 0);
      g_array_set_size (self->atime_samples, 0);
      self->bm_time_benchmarked_usec = 0;
      self->bm_sample_size = 0;
      self->bm_size = 0;
      G_UNLOCK (bm_lock);
    }

  bmt_schedule_update (self);

  return NULL;
}

static void
abort_benchmark (GduBenchmarkDialog *self)
{
  g_cancellable_cancel (self->bm_cancellable);
}

static void
start_benchmark2 (GduBenchmarkDialog *self)
{
  self->bm_in_progress = TRUE;
  self->bm_state = BM_STATE_OPENING_DEVICE;
  g_clear_error (&self->bm_error);
  g_array_set_size (self->read_samples, 0);
  g_array_set_size (self->write_samples, 0);
  g_array_set_size (self->atime_samples, 0);
  self->bm_time_benchmarked_usec = 0;
  g_cancellable_reset (self->bm_cancellable);

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
      start_benchmark2 (self);
    }
}

static void
start_benchmark (GduBenchmarkDialog *self)
{
  GtkWidget *dialog;
  GtkBuilder *builder = NULL;
  GtkWidget *num_samples_spinbutton;
  GtkWidget *sample_size_spinbutton;
  GtkWidget *write_checkbutton;
  GtkWidget *num_access_samples_spinbutton;
  GSettings *settings;
  gint response;

  g_assert (!self->bm_in_progress);
  g_assert_cmpint (self->bm_state, ==, BM_STATE_NONE);

  dialog = GTK_WIDGET (gdu_application_new_widget ((gpointer)g_application_get_default (),
                                                   "gdu-benchmark-dialog.ui",
                                                   "dialog2",
                                                   &builder));
  gtk_window_set_transient_for (GTK_WINDOW (dialog), self);
  gtk_dialog_set_default_response (GTK_DIALOG (dialog), GTK_RESPONSE_OK);

  num_samples_spinbutton = GTK_WIDGET (gtk_builder_get_object (builder, "num-samples-spinbutton"));
  sample_size_spinbutton = GTK_WIDGET (gtk_builder_get_object (builder, "sample-size-spinbutton"));
  write_checkbutton = GTK_WIDGET (gtk_builder_get_object (builder, "write-checkbutton"));
  num_access_samples_spinbutton = GTK_WIDGET (gtk_builder_get_object (builder, "num-access-samples-spinbutton"));

  settings = g_settings_new ("org.gnome.Disks.benchmark");
  self->bm_num_samples = g_settings_get_int (settings, "num-samples");
  self->bm_sample_size_mib = g_settings_get_int (settings, "sample-size-mib");
  self->bm_do_write = g_settings_get_boolean (settings, "do-write");
  self->bm_num_access_samples = g_settings_get_int (settings, "num-access-samples");

  gtk_spin_button_set_value (GTK_SPIN_BUTTON(num_samples_spinbutton), self->bm_num_samples);
  gtk_spin_button_set_value (GTK_SPIN_BUTTON(sample_size_spinbutton), self->bm_sample_size_mib);
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (write_checkbutton), self->bm_do_write);
  gtk_spin_button_set_value (GTK_SPIN_BUTTON(num_access_samples_spinbutton), self->bm_num_access_samples);

  /* if device is read-only, uncheck the "perform write-test"
   * check-button and also make it insensitive
   */
  if (udisks_block_get_read_only (self->block))
    {
      gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (write_checkbutton), FALSE);
      gtk_widget_set_sensitive (write_checkbutton, FALSE);
    }

  /* If the device is currently in use, uncheck the "perform write-test" check-button */
  if (gdu_utils_is_in_use (self->client, self->object))
    {
      gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (write_checkbutton), FALSE);
    }

  /* and scene... */
  // response = gtk_dialog_run (GTK_DIALOG (dialog));

  gtk_widget_set_visible (dialog, FALSE);

  if (response != GTK_RESPONSE_OK)
    goto out;

  self->bm_num_samples = gtk_spin_button_get_value (GTK_SPIN_BUTTON (num_samples_spinbutton));
  self->bm_sample_size_mib = gtk_spin_button_get_value (GTK_SPIN_BUTTON (sample_size_spinbutton));
  self->bm_do_write = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (write_checkbutton));
  self->bm_num_access_samples = gtk_spin_button_get_value (GTK_SPIN_BUTTON (num_access_samples_spinbutton));

  g_settings_set_int (settings, "num-samples", self->bm_num_samples);
  g_settings_set_int (settings, "sample-size-mib", self->bm_sample_size_mib);
  g_settings_set_boolean (settings, "do-write", self->bm_do_write);
  g_settings_set_int (settings, "num-access-samples", self->bm_num_access_samples);

  //g_print ("num_samples=%d\n", self->bm_num_samples);
  //g_print ("sample_size=%d MB\n", self->bm_sample_size_mib);
  //g_print ("do_write=%d\n", self->bm_do_write);
  //g_print ("num_access_samples=%d\n", self->bm_num_access_samples);

  if (self->bm_do_write)
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
      start_benchmark2 (self);
    }

 out:
  gtk_window_close (GTK_WINDOW (dialog));
  g_clear_object (&builder);
  g_clear_object (&settings);
  update_dialog (self);
}

/* ---------------------------------------------------------------------------------------------------- */

static void
on_start_clicked_cb (GduBenchmarkDialog *self,
                     GtkButton          *button)
{
  gtk_stack_set_visible_child_name (GTK_STACK (self->pages_stack), "results");
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

  gtk_widget_class_bind_template_child (widget_class, GduBenchmarkDialog, window_title);

  gtk_widget_class_bind_template_child (widget_class, GduBenchmarkDialog, pages_stack);

  gtk_widget_class_bind_template_child (widget_class, GduBenchmarkDialog, sample_row);
  gtk_widget_class_bind_template_child (widget_class, GduBenchmarkDialog, sample_size_row);
  gtk_widget_class_bind_template_child (widget_class, GduBenchmarkDialog, access_samples_row);
  gtk_widget_class_bind_template_child (widget_class, GduBenchmarkDialog, write_bench_switch);

  gtk_widget_class_bind_template_child (widget_class, GduBenchmarkDialog, drawing_area);
  gtk_widget_class_bind_template_child (widget_class, GduBenchmarkDialog, sample_size_label);
  gtk_widget_class_bind_template_child (widget_class, GduBenchmarkDialog, read_rate_label);
  gtk_widget_class_bind_template_child (widget_class, GduBenchmarkDialog, write_rate_label);
  gtk_widget_class_bind_template_child (widget_class, GduBenchmarkDialog, access_time_label);

  gtk_widget_class_bind_template_callback (widget_class, set_sample_size_unit_cb);
  gtk_widget_class_bind_template_callback (widget_class, on_start_clicked_cb);
}

void
gdu_benchmark_dialog_init (GduBenchmarkDialog *self)
{
  gtk_widget_init_template (GTK_WIDGET (self));

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
}

void
gdu_benchmark_dialog_show (GtkWindow    *parent_window,
                           UDisksObject *object,
                           UDisksClient *client)
{
  GduBenchmarkDialog *self;
  guint timeout_id;
  GError *error = NULL;

  self = g_object_new (GDU_TYPE_BENCHMARK_DIALOG,
                       "transient-for", parent_window,
                       NULL);
  self->object = g_object_ref (object);
  self->block = udisks_object_peek_block (self->object);
  self->client = client;

  /*
  g_signal_connect (self->drawing_area,
                    "draw",
                    G_CALLBACK (on_drawing_area_draw),
                    self);
  */
  gdu_benchmark_dialog_set_title (self);

  // update_dialog (self);

  // while (TRUE)
  //   {
  //     gint response;
  //     // response = gtk_dialog_run (GTK_DIALOG (self->dialog));

  //     if (response < 0)
  //       break;

  //     /* Keep in sync with .ui file */
  //     switch (response)
  //       {
  //       case 0: /* start benchmark */
  //         start_benchmark (self);
  //         break;

  //       case 1: /* abort benchmark */
  //         abort_benchmark (self);
  //         break;

  //       default:
  //         g_assert_not_reached ();
  //       }
  //   }

  // g_source_remove (timeout_id);

  gtk_window_present (GTK_WINDOW (self));
}
