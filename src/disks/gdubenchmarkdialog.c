/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*-
 *
 * Copyright (C) 2008-2013 Red Hat, Inc.
 *
 * Licensed under GPL version 2 or later.
 *
 * Author: David Zeuthen <zeuthen@gmail.com>
 */

#include "config.h"

#define _FILE_OFFSET_BITS 64
#include <glib/gi18n.h>
#include <gio/gunixfdlist.h>
#include <gio/gunixinputstream.h>
#include <gio/gunixoutputstream.h>

#include <glib-unix.h>
#include <sys/ioctl.h>
#include <linux/fs.h>

#include <math.h>

#include "gduapplication.h"
#include "gduwindow.h"
#include "gdubenchmarkdialog.h"

/* ---------------------------------------------------------------------------------------------------- */

typedef struct {
  guint64 offset;
  gdouble value;
} BMSample;

/* ---------------------------------------------------------------------------------------------------- */

typedef enum {
  BM_STATE_NONE,
  BM_STATE_OPENING_DEVICE,
  BM_STATE_TRANSFER_RATE,
  BM_STATE_ACCESS_TIME,
} BMState;

typedef struct
{
  volatile gint ref_count;

  UDisksObject *object;
  UDisksBlock *block;

  GCancellable *cancellable;

  GduWindow *window;
  GtkBuilder *builder;

  GtkWidget *dialog;

  GtkWidget *graph_drawing_area;

  GtkWidget *device_label;
  GtkWidget *updated_label;
  GtkWidget *sample_size_label;
  GtkWidget *read_rate_label;
  GtkWidget *write_rate_label;
  GtkWidget *access_time_label;

  GtkWidget *start_benchmark_button;
  GtkWidget *stop_benchmark_button;

  gboolean closed;

  /* ---- */

  /* retrieved from preferences dialog */
  gint bm_num_samples;
  gint bm_sample_size_mib;
  gboolean bm_do_write;
  gint bm_num_access_samples;

  /* must hold bm_lock when reading/writing these */
  GThread *bm_thread;
  GCancellable *bm_cancellable;
  gboolean bm_in_progress;
  BMState bm_state;
  GError *bm_error; /* set by benchmark thread on termination */
  gboolean bm_update_timeout_pending;

  gint64 bm_time_benchmarked_usec; /* 0 if never benchmarked, otherwise micro-seconds since Epoch */
  guint64 bm_size;
  guint64 bm_sample_size;
  GArray *bm_read_samples;
  GArray *bm_write_samples;
  GArray *bm_access_time_samples;

} DialogData;

G_LOCK_DEFINE (bm_lock);

static const struct {
  goffset offset;
  const gchar *name;
} widget_mapping[] = {
  {G_STRUCT_OFFSET (DialogData, graph_drawing_area), "graph-drawing-area"},
  {G_STRUCT_OFFSET (DialogData, device_label), "device-label"},
  {G_STRUCT_OFFSET (DialogData, updated_label), "updated-label"},
  {G_STRUCT_OFFSET (DialogData, sample_size_label), "sample-size-label"},
  {G_STRUCT_OFFSET (DialogData, read_rate_label), "read-rate-label"},
  {G_STRUCT_OFFSET (DialogData, write_rate_label), "write-rate-label"},
  {G_STRUCT_OFFSET (DialogData, access_time_label), "access-time-label"},
  {0, NULL}
};

static void update_dialog (DialogData *data);

static gboolean maybe_load_data (DialogData  *data,
                                 GError     **error);

/* ---------------------------------------------------------------------------------------------------- */

static DialogData *
dialog_data_ref (DialogData *data)
{
  g_atomic_int_inc (&data->ref_count);
  return data;
}

static void
dialog_data_unref (DialogData *data)
{
  if (g_atomic_int_dec_and_test (&data->ref_count))
    {
      if (data->dialog != NULL)
        {
          gtk_widget_hide (data->dialog);
          gtk_widget_destroy (data->dialog);
          data->dialog = NULL;
        }

      g_clear_object (&data->object);
      g_clear_object (&data->window);
      g_clear_object (&data->builder);

      g_array_unref (data->bm_read_samples);
      g_array_unref (data->bm_write_samples);
      g_array_unref (data->bm_access_time_samples);
      g_clear_object (&data->bm_cancellable);
      g_clear_error (&data->bm_error);

      g_free (data);
    }
}

static void
dialog_data_close (DialogData *data)
{
  g_cancellable_cancel (data->bm_cancellable);
  data->closed = TRUE;
  gtk_dialog_response (GTK_DIALOG (data->dialog), GTK_RESPONSE_CANCEL);
  dialog_data_unref (data);
}

/* ---------------------------------------------------------------------------------------------------- */

static void
get_max_min_avg (GArray  *array,
                 gdouble *out_max,
                 gdouble *out_min,
                 gdouble *out_avg)
{
  guint n;
  gdouble max = 0;
  gdouble min = 0;
  gdouble avg = 0;
  gdouble sum = 0;

  if (array->len == 0)
    goto out;

  max = -G_MAXDOUBLE;
  min = G_MAXDOUBLE;
  sum = 0;

  for (n = 0; n < array->len; n++)
    {
      BMSample *s = &g_array_index (array, BMSample, n);
      if (s->value > max)
        max = s->value;
      if (s->value < min)
        min = s->value;
      sum += s->value;
    }
  avg = sum / array->len;

 out:
  if (out_max != NULL)
    *out_max = max;
  if (out_min != NULL)
    *out_min = min;
  if (out_avg != NULL)
    *out_avg = avg;
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
{
  DialogData *data = user_data;
  GtkAllocation allocation;
  gdouble width, height;
  gdouble gx, gy, gw, gh;
  guint n;
  gdouble w, h;
  gdouble x, y;
  gdouble x_marker_height;
  gchar *s;
  gdouble max_speed;
  gdouble max_visible_speed;
  gdouble speed_res;
  gdouble max_time;
  gdouble time_res;
  gdouble max_visible_time;
  gchar **y_left_markers;
  gchar **y_right_markers;
  guint num_y_markers;
  GPtrArray *p;
  GPtrArray *p2;
  gdouble read_transfer_rate_max = 0.0;
  gdouble write_transfer_rate_max = 0.0;
  gdouble access_time_max = 0.0;
  gdouble prev_x;
  gdouble prev_y;

  G_LOCK (bm_lock);

  //g_print ("drawing: %d %d %d\n",
  //         data->bm_read_samples->len,
  //         data->bm_write_samples->len,
  //         data->bm_access_time_samples->len);

  get_max_min_avg (data->bm_read_samples,
                   &read_transfer_rate_max,
                   NULL,
                   NULL);
  get_max_min_avg (data->bm_write_samples,
                   &write_transfer_rate_max,
                   NULL,
                   NULL);
  get_max_min_avg (data->bm_access_time_samples,
                   &access_time_max,
                   NULL,
                   NULL);

  max_speed = MAX (read_transfer_rate_max, write_transfer_rate_max);
  max_time = access_time_max;

  if (max_speed == 0)
    max_speed = 100 * 1000 * 1000;

  if (max_time == 0)
    max_time = 50 / 1000.0;

  //speed_res = (floor (((gdouble) max_speed) / (100 * 1000 * 1000)) + 1) * 1000 * 1000;
  //speed_res *= 10.0;

  speed_res = max_speed / 10.0;
  /* round up to nearest multiple of 10 MB/s */
  max_visible_speed = ceil (max_speed / (10*1000*1000)) * 10*1000*1000;
  speed_res = max_visible_speed / 10.0;
  num_y_markers = 10;

  time_res = max_time / num_y_markers;
  if (time_res < 0.0001)
    {
      time_res = 0.0001;
    }
  else if (time_res < 0.0005)
    {
      time_res = 0.0005;
    }
  else if (time_res < 0.001)
    {
      time_res = 0.001;
    }
  else if (time_res < 0.0025)
    {
      time_res = 0.0025;
    }
  else if (time_res < 0.005)
    {
      time_res = 0.005;
    }
  else
    {
      time_res = ceil (((gdouble) time_res) / 0.005) * 0.005;
    }
  max_visible_time = time_res * num_y_markers;

  //g_print ("max_visible_speed=%f, max_speed=%f, speed_res=%f\n", max_visible_speed, max_speed, speed_res);
  //g_print ("max_visible_time=%f, max_time=%f, time_res=%f\n", max_visible_time, max_time, time_res);

  p = g_ptr_array_new ();
  p2 = g_ptr_array_new ();
  for (n = 0; n <= num_y_markers; n++)
    {
      gdouble val;

      val = n * speed_res;
      /* Translators: This is used in the benchmark graph - %d is megabytes per second */
      s = g_strdup_printf (C_("benchmark-graph", "%d MB/s"), (gint) (val / (1000 * 1000)));
      g_ptr_array_add (p, s);

      val = n * time_res;
      /* Translators: This is used in the benchmark graph - %g is number of milliseconds */
      s = g_strdup_printf (C_("benchmark-graph", "%3g ms"), val * 1000.0);
      g_ptr_array_add (p2, s);
    }
  g_ptr_array_add (p, NULL);
  g_ptr_array_add (p2, NULL);
  y_left_markers = (gchar **) g_ptr_array_free (p, FALSE);
  y_right_markers = (gchar **) g_ptr_array_free (p2, FALSE);

  gtk_widget_get_allocation (widget, &allocation);
  width = allocation.width;
  height = allocation.height;

  cairo_select_font_face (cr, "sans",
                          CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
  cairo_set_font_size (cr, 8.0);
  cairo_set_line_width (cr, 1.0);

#if 0
  cairo_set_source_rgb (cr, 0.25, 0.25, 0.25);
  cairo_rectangle (cr, 0, 0, width, height);
  cairo_set_line_width (cr, 0.0);
  cairo_fill (cr);
#endif

  gx = 0;
  gy = 0;
  gw = width;
  gh = height;

  /* make horizontal and vertical room for x markers ("%d%%") */
  w = ceil (measure_width (cr, "0%") / 2.0);
  gx +=  w;
  gw -=  w;
  w = ceil (measure_width (cr, "100%") / 2.0);
  x_marker_height = ceil (measure_height (cr, "100%")) + 10;
  gw -= w;
  gh -= x_marker_height;

  /* make horizontal room for left y markers ("%d MB/s") */
  for (n = 0; n <= num_y_markers; n++)
    {
      w = ceil (measure_width (cr, y_left_markers[n])) + 2 * 3;
      if (w > gx)
        {
          gdouble needed = w - gx;
          gx += needed;
          gw -= needed;
        }
    }

  /* make vertical room for top-left y marker */
  h = ceil (measure_height (cr, y_left_markers[num_y_markers]) / 2.0);
  if (h > gy)
    {
      gdouble needed = h - gy;
      gy += needed;
      gh -= needed;
    }

  /* make horizontal room for right y markers ("%d ms") */
  for (n = 0; n <= num_y_markers; n++)
    {
      w = ceil (measure_width (cr, y_right_markers[n])) + 2 * 3;
      if (w > width - (gx + gw))
        {
          gdouble needed = w - (width - (gx + gw));
          gw -= needed;
        }
    }

  /* make vertical room for top-right y marker */
  h = ceil (measure_height (cr, y_right_markers[num_y_markers]) / 2.0);
  if (h > gy)
    {
      gdouble needed = h - gy;
      gy += needed;
      gh -= needed;
    }

  /* draw x markers ("%d%%") + vertical grid */
  for (n = 0; n <= 10; n++)
    {
      cairo_text_extents_t te;

      x = gx + ceil (n * gw / 10.0);
      y = gy + gh + x_marker_height/2.0;

      s = g_strdup_printf ("%d%%", n * 10);

      cairo_text_extents (cr, s, &te);

      cairo_move_to (cr,
                     x - te.x_bearing - te.width/2,
                     y - te.y_bearing - te.height/2);
      cairo_set_source_rgb (cr, 0, 0, 0);
      cairo_show_text (cr, s);

      g_free (s);
    }

  /* draw left y markers ("%d MB/s") */
  for (n = 0; n <= num_y_markers; n++)
    {
      cairo_text_extents_t te;

      x = gx/2.0;
      y = gy + gh - gh * n / num_y_markers;

      s = y_left_markers[n];
      cairo_text_extents (cr, s, &te);
      cairo_move_to (cr,
                     x - te.x_bearing - te.width/2,
                     y - te.y_bearing - te.height/2);
      cairo_set_source_rgb (cr, 0, 0, 0);
      cairo_show_text (cr, s);
    }

  /* draw right y markers ("%d ms") */
  for (n = 0; n <= num_y_markers; n++)
    {
      cairo_text_extents_t te;

      x = gx + gw + (width - (gx + gw))/2.0;
      y = gy + gh - gh * n / num_y_markers;

      s = y_right_markers[n];
      cairo_text_extents (cr, s, &te);
      cairo_move_to (cr,
                     x - te.x_bearing - te.width/2,
                     y - te.y_bearing - te.height/2);
      cairo_set_source_rgb (cr, 0, 0, 0);
      cairo_show_text (cr, s);
    }

  /* fill graph area */
  cairo_set_source_rgb (cr, 1, 1, 1);
  cairo_rectangle (cr, gx + 0.5, gy + 0.5, gw, gh);
  cairo_fill_preserve (cr);
  /* grid - first a rect */
  cairo_set_source_rgba (cr, 0, 0, 0, 0.25);
  cairo_set_line_width (cr, 1.0);
  /* rect - also clip to rect for all future drawing operations */
  cairo_stroke_preserve (cr);
  cairo_clip (cr);
  /* vertical lines */
  for (n = 1; n < 10; n++)
    {
      x = gx + ceil (n * gw / 10.0);
      cairo_move_to (cr, x + 0.5, gy + 0.5);
      cairo_line_to (cr, x + 0.5, gy + gh + 0.5);
      cairo_stroke (cr);
    }
  /* horizontal lines */
  for (n = 1; n < num_y_markers; n++)
    {
      y = gy + ceil (n * gh / num_y_markers);
      cairo_move_to (cr, gx + 0.5, y + 0.5);
      cairo_line_to (cr, gx + gw + 0.5, y + 0.5);
      cairo_stroke (cr);
    }

  /* draw read graph */
  cairo_set_source_rgb (cr, 0.5, 0.5, 1.0);
  cairo_set_line_width (cr, 1.5);
  for (n = 0; n < data->bm_read_samples->len; n++)
    {
      BMSample *sample = &g_array_index (data->bm_read_samples, BMSample, n);

      x = gx + gw * sample->offset / data->bm_size;
      y = gy + gh - gh * sample->value / max_visible_speed;

      if (n == 0)
        cairo_move_to (cr, x, y);
      else
        cairo_line_to (cr, x, y);
    }
  cairo_stroke (cr);

  /* draw write graph */
  cairo_set_source_rgb (cr, 1.0, 0.5, 0.5);
  cairo_set_line_width (cr, 1.5);
  for (n = 0; n < data->bm_write_samples->len; n++)
    {
      BMSample *sample = &g_array_index (data->bm_write_samples, BMSample, n);
      x = gx + gw * sample->offset / data->bm_size;
      y = gy + gh - gh * sample->value / max_visible_speed;

      if (n == 0)
        cairo_move_to (cr, x, y);
      else
        cairo_line_to (cr, x, y);
    }
  cairo_stroke (cr);

  /* draw access time dots + lines */
  cairo_set_line_width (cr, 0.5);
  for (n = 0; n < data->bm_access_time_samples->len; n++)
    {
      BMSample *sample = &g_array_index (data->bm_access_time_samples, BMSample, n);

      x = gx + gw * sample->offset / data->bm_size;
      y = gy + gh - gh * sample->value / max_visible_time;

      /*g_debug ("time = %f @ %f", point->value, x);*/

      cairo_set_source_rgba (cr, 0.4, 1.0, 0.4, 0.5);
      cairo_arc (cr, x, y, 1.5, 0, 2 * M_PI);
      cairo_fill (cr);

      if (n > 0)
        {
          cairo_set_source_rgba (cr, 0.2, 0.5, 0.2, 0.10);
          cairo_move_to (cr, prev_x, prev_y);
          cairo_line_to (cr, x, y);
          cairo_stroke (cr);
        }

      prev_x = x;
      prev_y = y;
    }

#if 0
  if (dialog->priv->benchmark_data != NULL) {
                BenchmarkData *data = dialog->priv->benchmark_data;
                gdouble prev_x;
                gdouble prev_y;

                /* draw access time dots + lines */
                cairo_set_line_width (cr, 0.5);
                for (n = 0; n < data->access_time_samples->len; n++) {
                        BenchmarkPoint *point = &g_array_index (data->access_time_samples, BenchmarkPoint, n);

                        x = gx + gw * point->offset / data->disk_size;
                        y = gy + gh - gh * point->value / max_visible_time;

                        /*g_debug ("time = %f @ %f", point->value, x);*/

                        cairo_set_source_rgba (cr, 0.4, 1.0, 0.4, 0.5);
                        cairo_arc (cr, x, y, 1.5, 0, 2 * M_PI);
                        cairo_fill (cr);

                        if (n > 0) {
                                cairo_set_source_rgba (cr, 0.2, 0.5, 0.2, 0.10);
                                cairo_move_to (cr, prev_x, prev_y);
                                cairo_line_to (cr, x, y);
                                cairo_stroke (cr);
                        }

                        prev_x = x;
                        prev_y = y;
                }

                /* draw write transfer rate graph */
                cairo_set_source_rgb (cr, 1.0, 0.5, 0.5);
                cairo_set_line_width (cr, 2.0);
                for (n = 0; n < data->write_transfer_rate_samples->len; n++) {
                        BenchmarkPoint *point = &g_array_index (data->write_transfer_rate_samples, BenchmarkPoint, n);

                        x = gx + gw * point->offset / data->disk_size;
                        y = gy + gh - gh * point->value / max_visible_speed;

                        if (n == 0)
                                cairo_move_to (cr, x, y);
                        else
                                cairo_line_to (cr, x, y);
                }
                cairo_stroke (cr);

                /* draw read transfer rate graph */
                cairo_set_source_rgb (cr, 0.5, 0.5, 1.0);
                cairo_set_line_width (cr, 1.5);
                for (n = 0; n < data->read_transfer_rate_samples->len; n++) {
                        BenchmarkPoint *point = &g_array_index (data->read_transfer_rate_samples, BenchmarkPoint, n);

                        x = gx + gw * point->offset / data->disk_size;
                        y = gy + gh - gh * point->value / max_visible_speed;

                        if (n == 0)
                                cairo_move_to (cr, x, y);
                        else
                                cairo_line_to (cr, x, y);
                }
                cairo_stroke (cr);

        } else {
                /* TODO: draw some text saying we don't have any data */
        }
#endif

        g_strfreev (y_left_markers);
        g_strfreev (y_right_markers);

  G_UNLOCK (bm_lock);

  /* propagate event further */
  return FALSE;
}

/* ---------------------------------------------------------------------------------------------------- */

static gchar *
format_transfer_rate (gdouble bytes_per_sec)
{
  gchar *ret = NULL;
  gchar *s;

  s = g_format_size ((guint64) bytes_per_sec);
  /* Translators: %s is the formatted size, e.g. "42 MB" and the trailing "/s" means per second */
  ret = g_strdup_printf (C_("benchmark-transfer-rate", "%s/s"), s);
  g_free (s);
  return ret;
}

static gchar *
format_transfer_rate_and_num_samples (gdouble bytes_per_sec,
                                      guint   num_samples)
{
  gchar *ret = NULL;
  gchar *s;
  gchar *s2;

  s = format_transfer_rate (bytes_per_sec);
  s2 = g_strdup_printf (g_dngettext (GETTEXT_PACKAGE,
                                     "%d sample",
                                     "%d samples",
                                     num_samples),
                        num_samples);
  ret = g_strdup_printf ("%s <small>(%s)</small>", s, s2);
  g_free (s2);
  g_free (s);
  return ret;
}


static void
update_updated_label (DialogData *data)
{
  gchar *s = NULL;

  G_LOCK (bm_lock);
  switch (data->bm_state)
    {
    case BM_STATE_NONE:
      if (data->bm_time_benchmarked_usec > 0)
        {
          gint64 now_usec;
          gchar *s2;
          GDateTime *time_benchmarked_dt;
          GDateTime *time_benchmarked_dt_local;
          gchar *time_benchmarked_str;

          now_usec = g_get_real_time ();

          time_benchmarked_dt = g_date_time_new_from_unix_utc (data->bm_time_benchmarked_usec / G_USEC_PER_SEC);
          time_benchmarked_dt_local = g_date_time_to_local (time_benchmarked_dt);
          time_benchmarked_str = g_date_time_format (time_benchmarked_dt_local, "%c");

          s = gdu_utils_format_duration_usec ((now_usec - data->bm_time_benchmarked_usec),
                                              GDU_FORMAT_DURATION_FLAGS_NO_SECONDS);
          /* Translators: The first %s is the date and time the benchmark took place in the preferred
           * format for the locale (e.g. "%c" for strftime()/g_date_time_format()), for example
           * "Tue 12 Jun 2012 03:57:08 PM EDT". The second %s is how long ago that is from right
           * now, for example "3 days" or "2 hours" or "12 minutes".
           */
          s2 = g_strdup_printf (C_("benchmark-updated", "%s (%s ago)"),
                                time_benchmarked_str,
                                s);
          gtk_label_set_text (GTK_LABEL (data->updated_label), s2);
          g_free (s2);
          g_free (s);
          g_free (time_benchmarked_str);
          g_date_time_unref (time_benchmarked_dt_local);
          g_date_time_unref (time_benchmarked_dt);
        }
      else
        {
          gtk_label_set_markup (GTK_LABEL (data->updated_label), C_("benchmark-updated", "No benchmark data available"));
        }
      break;

    case BM_STATE_OPENING_DEVICE:
      gtk_label_set_markup (GTK_LABEL (data->updated_label), C_("benchmark-updated", "Opening Device…"));
      break;

    case BM_STATE_TRANSFER_RATE:
      s = g_strdup_printf (C_("benchmark-updated", "Measuring transfer rate (%2.1f%% complete)…"),
                           data->bm_read_samples->len * 100.0 / data->bm_num_samples);
      gtk_label_set_markup (GTK_LABEL (data->updated_label), s);
      g_free (s);
      break;

    case BM_STATE_ACCESS_TIME:
      s = g_strdup_printf (C_("benchmark-updated", "Measuring access time (%2.1f%% complete)…"),
                           data->bm_access_time_samples->len * 100.0 / data->bm_num_access_samples);
      gtk_label_set_markup (GTK_LABEL (data->updated_label), s);
      g_free (s);
      break;
    }
  G_UNLOCK (bm_lock);
}

/* returns NULL if it doesn't make sense to load/save benchmark data (removable media,
 * non-drive devices etc.)
 */
static gchar *
get_bm_filename (DialogData *data)
{
  gchar *ret = NULL;
  gchar *bench_dir = NULL;
  const gchar *id = NULL;

  id = udisks_block_get_id (data->block);
  if (id == NULL || strlen (id) == 0)
    goto out;

  bench_dir = g_strdup_printf ("%s/gnome-disks/benchmarks", g_get_user_cache_dir ());
  if (g_mkdir_with_parents (bench_dir, 0777) != 0)
    {
      g_warning ("Error creating directory %s: %m", bench_dir);
      goto out;
    }

  ret = g_strdup_printf ("%s/%s.gnome-disks-benchmark", bench_dir, id);

 out:
  g_free (bench_dir);
  return ret;
}

static void
update_dialog (DialogData *data)
{
  GError *error = NULL;
  GdkWindow *window = NULL;
  gdouble read_avg = 0.0;
  gdouble write_avg = 0.0;
  gdouble access_time_avg = 0.0;
  gchar *s = NULL;
  UDisksDrive *drive = NULL;
  UDisksObjectInfo *info = NULL;

  G_LOCK (bm_lock);
  if (data->bm_error != NULL)
    {
      error = data->bm_error;
      data->bm_error = NULL;
    }
  G_UNLOCK (bm_lock);

  /* first of all, present an error if something went wrong */
  if (error != NULL)
    {
      if (!data->closed)
        {
          if (!(error->domain == G_IO_ERROR && error->code == G_IO_ERROR_CANCELLED))
            gdu_utils_show_error (GTK_WINDOW (data->window), C_("benchmarking", "An error occurred"), error);
        }
      g_clear_error (&error);

      /* and reload old data */
      if (!maybe_load_data (data, &error))
        {
          /* not worth complaining in dialog about */
          g_warning ("Error loading cached data: %s (%s, %d)",
                     error->message, g_quark_to_string (error->domain), error->code);
          g_clear_error (&error);
        }
    }

  update_updated_label (data);

  /* disk / device label */
  drive = udisks_client_get_drive_for_block (gdu_window_get_client (data->window), data->block);
  info = udisks_client_get_object_info (gdu_window_get_client (data->window), data->object);
  gtk_label_set_text (GTK_LABEL (data->device_label), udisks_object_info_get_one_liner (info));
  g_free (s);

  G_LOCK (bm_lock);

  if (data->bm_in_progress)
    {

      gtk_widget_hide (data->start_benchmark_button);
      gtk_widget_show (data->stop_benchmark_button);
    }
  else
    {
      gtk_widget_show (data->start_benchmark_button);
      gtk_widget_hide (data->stop_benchmark_button);
    }

  get_max_min_avg (data->bm_read_samples,
                   NULL, NULL, &read_avg);
  get_max_min_avg (data->bm_write_samples,
                   NULL, NULL, &write_avg);
  get_max_min_avg (data->bm_access_time_samples,
                   NULL, NULL, &access_time_avg);

  G_UNLOCK (bm_lock);

  if (data->bm_sample_size == 0)
    s = g_strdup ("–");
  else
    s = g_format_size_full (data->bm_sample_size, G_FORMAT_SIZE_IEC_UNITS | G_FORMAT_SIZE_LONG_FORMAT);
  gtk_label_set_markup (GTK_LABEL (data->sample_size_label), s);
  g_free (s);

  if (read_avg == 0.0)
    s = g_strdup ("–");
  else
    s = format_transfer_rate_and_num_samples (read_avg, data->bm_read_samples->len);
  gtk_label_set_markup (GTK_LABEL (data->read_rate_label), s);
  g_free (s);

  if (write_avg == 0.0)
    s = g_strdup ("–");
  else
    s = format_transfer_rate_and_num_samples (write_avg, data->bm_write_samples->len);
  gtk_label_set_markup (GTK_LABEL (data->write_rate_label), s);
  g_free (s);

  if (access_time_avg == 0.0)
    {
      s = g_strdup ("–");
    }
  else
    {
      gchar *s2;
      gchar *s3;
      /* Translators: %d is number of milliseconds and msec means "milli-second" */
      s2 = g_strdup_printf (C_("benchmark-access-time", "%.2f msec"), access_time_avg * 1000.0);
      s3 = g_strdup_printf (g_dngettext (GETTEXT_PACKAGE,
                                         "%d sample",
                                         "%d samples",
                                         data->bm_access_time_samples->len),
                            data->bm_access_time_samples->len);
      s = g_strdup_printf ("%s <small>(%s)</small>", s2, s3);
      g_free (s3);
      g_free (s2);
    }
  gtk_label_set_markup (GTK_LABEL (data->access_time_label), s);
  g_free (s);


  window = gtk_widget_get_window (data->graph_drawing_area);
  if (window != NULL)
    gdk_window_invalidate_rect (window, NULL, TRUE);

  g_clear_object (&drive);
  g_clear_object (&info);
}


/* called every second (on the second) */
static gboolean
on_timeout (gpointer user_data)
{
  DialogData *data = user_data;
  update_updated_label (data);
  return TRUE; /* keep timeout around */
}

/* ---------------------------------------------------------------------------------------------------- */

static void
samples_from_gvariant (GArray   *array,
                       GVariant *variant)
{
  GVariantIter iter;
  BMSample sample;

  g_array_set_size (array, 0);

  g_variant_iter_init (&iter, variant);
  while (g_variant_iter_next (&iter, "(td)", &sample.offset, &sample.value))
    {
      g_array_append_val (array, sample);
    }
}

static gboolean
maybe_load_data (DialogData  *data,
                 GError     **error)
{
  gboolean ret = FALSE;
  gchar *filename = NULL;
  GVariant *value = NULL;
  gchar *variant_data = NULL;
  gsize variant_size;
  GError *local_error = NULL;
  GVariant *read_samples_variant = NULL;
  GVariant *write_samples_variant = NULL;
  GVariant *access_time_samples_variant = NULL;
  gint32 version;
  gint64 timestamp_usec;
  guint64 device_size;
  guint64 sample_size;

  filename = get_bm_filename (data);
  if (filename == NULL)
    {
      /* all good since we don't want to load data for this device */
      ret = TRUE;
      goto out;
    }

  if (!g_file_get_contents (filename,
                            &variant_data,
                            &variant_size,
                            &local_error))
    {
      if (local_error->domain == G_FILE_ERROR && local_error->code == G_FILE_ERROR_NOENT)
        {
          /* don't complain about a missing file */
          g_clear_error (&local_error);
          ret = TRUE;
          goto out;
        }
      g_propagate_error (error, local_error);
      goto out;
    }

  value = g_variant_new_from_data (G_VARIANT_TYPE_VARDICT,
                                   variant_data,
                                   variant_size,
                                   FALSE,
                                   NULL, NULL);

  if (!g_variant_lookup (value, "version", "i", &version))
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "No version key");
      goto out;
    }
  if (version != 1)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Cannot decode version %d data", version);
      goto out;
    }

  if (!g_variant_lookup (value, "timestamp-usec", "x", &timestamp_usec))
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "No timestamp-usec");
      goto out;
    }

  if (!g_variant_lookup (value, "device-size", "t", &device_size))
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "No device-size");
      goto out;
    }

  if (!g_variant_lookup (value, "read-samples", "@a(td)", &read_samples_variant))
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "No read-samples");
      goto out;
    }

  if (!g_variant_lookup (value, "write-samples", "@a(td)", &write_samples_variant))
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "No write-samples");
      goto out;
    }

  if (!g_variant_lookup (value, "access-time-samples", "@a(td)", &access_time_samples_variant))
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "No access-time-samples");
      goto out;
    }

  if (!g_variant_lookup (value, "sample-size", "t", &sample_size))
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "No sample-size");
      goto out;
    }

  data->bm_time_benchmarked_usec = timestamp_usec;
  data->bm_size = device_size;
  data->bm_sample_size = sample_size;
  samples_from_gvariant (data->bm_read_samples, read_samples_variant);
  samples_from_gvariant (data->bm_write_samples, write_samples_variant);
  samples_from_gvariant (data->bm_access_time_samples, access_time_samples_variant);

  ret = TRUE;

 out:
  if (read_samples_variant != NULL)
    g_variant_unref (read_samples_variant);
  if (write_samples_variant != NULL)
    g_variant_unref (write_samples_variant);
  if (access_time_samples_variant != NULL)
    g_variant_unref (access_time_samples_variant);
  if (value != NULL)
    g_variant_unref (value);
  g_free (variant_data);
  g_free (filename);
  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

static GVariant *
samples_to_gvariant (GArray *array)
{
  guint n;
  GVariantBuilder builder;

  g_variant_builder_init (&builder, G_VARIANT_TYPE ("a(td)"));
  for (n = 0; n < array->len; n++)
    {
      BMSample *s = &g_array_index (array, BMSample, n);
      g_variant_builder_add (&builder, "(td)", s->offset, s->value);
    }

  return g_variant_builder_end (&builder);
}


static gboolean
maybe_save_data (DialogData  *data,
                 GError     **error)
{
  gboolean ret = FALSE;
  gchar *filename = NULL;
  GVariantBuilder builder;
  GVariant *value = NULL;
  gconstpointer variant_data;
  gsize variant_size;

  filename = get_bm_filename (data);
  if (filename == NULL)
    {
      /* all good since we don't want to save data for this device */
      ret = TRUE;
      goto out;
    }

  g_variant_builder_init (&builder, G_VARIANT_TYPE_VARDICT);
  g_variant_builder_add (&builder, "{sv}", "version", g_variant_new_int32 (1));
  g_variant_builder_add (&builder, "{sv}", "timestamp-usec", g_variant_new_int64 (data->bm_time_benchmarked_usec));
  g_variant_builder_add (&builder, "{sv}", "device-size", g_variant_new_uint64 (data->bm_size));
  g_variant_builder_add (&builder, "{sv}", "sample-size", g_variant_new_uint64 (data->bm_sample_size));
  g_variant_builder_add (&builder, "{sv}", "read-samples", samples_to_gvariant (data->bm_read_samples));
  g_variant_builder_add (&builder, "{sv}", "write-samples", samples_to_gvariant (data->bm_write_samples));
  g_variant_builder_add (&builder, "{sv}", "access-time-samples", samples_to_gvariant (data->bm_access_time_samples));
  value = g_variant_builder_end (&builder);

  variant_data = g_variant_get_data (value);
  variant_size = g_variant_get_size (value);

  if (!g_file_set_contents (filename,
                            variant_data,
                            variant_size,
                            error))
    goto out;

 out:
  if (value != NULL)
    g_variant_unref (value);
  g_free (filename);
  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

/* called on main / UI thread */
static gboolean
bmt_on_timeout (gpointer user_data)
{
  DialogData *data = user_data;
  update_dialog (data);
  G_LOCK (bm_lock);
  data->bm_update_timeout_pending = FALSE;
  G_UNLOCK (bm_lock);
  dialog_data_unref (data);
  return FALSE; /* don't run again */
}

static void
bmt_schedule_update (DialogData *data)
{
  /* rate-limit updates */
  G_LOCK (bm_lock);
  if (!data->bm_update_timeout_pending)
    {
      g_timeout_add (200, /* ms */
                     bmt_on_timeout,
                     dialog_data_ref (data));
      data->bm_update_timeout_pending = TRUE;
    }
  G_UNLOCK (bm_lock);
}

static gpointer
benchmark_thread (gpointer user_data)
{
  DialogData *data = user_data;
  GVariant *fd_index = NULL;
  GUnixFDList *fd_list = NULL;
  GError *error = NULL;
  guchar *buffer_unaligned = NULL;
  guchar *buffer = NULL;
  GRand *rand = NULL;
  int fd = -1;
  gint n;
  long page_size;
  guint64 disk_size;
  GVariantBuilder options_builder;

  //g_print ("bm thread start\n");

  g_variant_builder_init (&options_builder, G_VARIANT_TYPE_VARDICT);
  g_variant_builder_add (&options_builder, "{sv}", "writable", g_variant_new_boolean (data->bm_do_write));

  if (!udisks_block_call_open_for_benchmark_sync (data->block,
                                                  g_variant_builder_end (&options_builder),
                                                  NULL, /* fd_list */
                                                  &fd_index,
                                                  &fd_list,
                                                  data->bm_cancellable,
                                                  &error))
    goto out;

  fd = g_unix_fd_list_get (fd_list, g_variant_get_handle (fd_index), NULL);
  g_clear_object (&fd_list);

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

  buffer_unaligned = g_new0 (guchar, data->bm_sample_size_mib*1024*1024 + page_size);
  buffer = (guchar*) (((gintptr) (buffer_unaligned + page_size)) & (~(page_size - 1)));

  /* transfer rate... */
  G_LOCK (bm_lock);
  data->bm_size = disk_size;
  data->bm_sample_size = data->bm_sample_size_mib*1024*1024;
  data->bm_state = BM_STATE_TRANSFER_RATE;
  G_UNLOCK (bm_lock);
  for (n = 0; n < data->bm_num_samples; n++)
    {
      gchar *s, *s2;
      gint64 begin_usec;
      gint64 end_usec;
      gint64 offset;
      ssize_t num_read;
      BMSample sample = {0};

      if (g_cancellable_set_error_if_cancelled (data->bm_cancellable, &error))
        goto out;

      /* figure out offset and align to page-size */
      offset = n * disk_size / data->bm_num_samples;
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
      num_read = read (fd, buffer, data->bm_sample_size_mib*1024*1024);
      if (G_UNLIKELY (num_read < 0))
        {
          s = g_format_size_full (data->bm_sample_size_mib * 1024 * 1024, G_FORMAT_SIZE_LONG_FORMAT);
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
      g_array_append_val (data->bm_read_samples, sample);
      G_UNLOCK (bm_lock);

      bmt_schedule_update (data);

      if (data->bm_do_write)
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
          g_array_append_val (data->bm_write_samples, sample);
          G_UNLOCK (bm_lock);

          bmt_schedule_update (data);
        }
    }

  /* access time... */
  G_LOCK (bm_lock);
  data->bm_state = BM_STATE_ACCESS_TIME;
  G_UNLOCK (bm_lock);
  rand = g_rand_new_with_seed (42); /* want this to be deterministic (per size) so it's repeatable */
  for (n = 0; n < data->bm_num_access_samples; n++)
    {
      gint64 begin_usec;
      gint64 end_usec;
      gint64 offset;
      ssize_t num_read;
      BMSample sample = {0};

      if (g_cancellable_set_error_if_cancelled (data->bm_cancellable, &error))
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
      g_array_append_val (data->bm_access_time_samples, sample);
      G_UNLOCK (bm_lock);

      bmt_schedule_update (data);
    }

  G_LOCK (bm_lock);
  data->bm_time_benchmarked_usec = g_get_real_time ();
  G_UNLOCK (bm_lock);
  if (!maybe_save_data (data, &error))
    goto out;

 out:
  if (rand != NULL)
    g_rand_free (rand);
  g_clear_object (&fd_list);

  if (fd_index != NULL)
    g_variant_unref (fd_index);
  if (fd != -1)
    close (fd);
  g_free (buffer_unaligned);
  data->bm_in_progress = FALSE;
  data->bm_thread = NULL;
  data->bm_state = BM_STATE_NONE;

  if (error != NULL)
    {
      G_LOCK (bm_lock);
      data->bm_error = error;
      g_array_set_size (data->bm_read_samples, 0);
      g_array_set_size (data->bm_write_samples, 0);
      g_array_set_size (data->bm_access_time_samples, 0);
      data->bm_time_benchmarked_usec = 0;
      data->bm_sample_size = 0;
      data->bm_size = 0;
      G_UNLOCK (bm_lock);
    }

  bmt_schedule_update (data);

  dialog_data_unref (data);

  //g_print ("bm thread end\n");
  return NULL;
}

static void
abort_benchmark (DialogData *data)
{
  g_cancellable_cancel (data->bm_cancellable);
}

static void
start_benchmark2 (DialogData *data)
{
  data->bm_in_progress = TRUE;
  data->bm_state = BM_STATE_OPENING_DEVICE;
  g_clear_error (&data->bm_error);
  g_array_set_size (data->bm_read_samples, 0);
  g_array_set_size (data->bm_write_samples, 0);
  g_array_set_size (data->bm_access_time_samples, 0);
  data->bm_time_benchmarked_usec = 0;
  g_cancellable_reset (data->bm_cancellable);

  data->bm_thread = g_thread_new ("benchmark-thread",
                                  benchmark_thread,
                                  dialog_data_ref (data));
}

static void
ensure_unused_cb (GduWindow     *window,
                  GAsyncResult  *res,
                  gpointer       user_data)
{
  DialogData *data = user_data;
  if (gdu_window_ensure_unused_finish (window, res, NULL))
    {
      start_benchmark2 (data);
    }
  dialog_data_unref (data);
}

static void
start_benchmark (DialogData *data)
{
  GtkWidget *dialog;
  GtkBuilder *builder = NULL;
  GtkWidget *num_samples_spinbutton;
  GtkWidget *sample_size_spinbutton;
  GtkWidget *write_checkbutton;
  GtkWidget *num_access_samples_spinbutton;
  gint response;

  g_assert (!data->bm_in_progress);
  g_assert (data->bm_thread == NULL);
  g_assert_cmpint (data->bm_state, ==, BM_STATE_NONE);

  dialog = GTK_WIDGET (gdu_application_new_widget (gdu_window_get_application (data->window),
                                                   "benchmark-dialog.ui",
                                                   "dialog2",
                                                   &builder));
  gtk_window_set_transient_for (GTK_WINDOW (dialog), GTK_WINDOW (data->dialog));
  gtk_dialog_set_default_response (GTK_DIALOG (dialog), GTK_RESPONSE_OK);

  num_samples_spinbutton = GTK_WIDGET (gtk_builder_get_object (builder, "num-samples-spinbutton"));
  sample_size_spinbutton = GTK_WIDGET (gtk_builder_get_object (builder, "sample-size-spinbutton"));
  write_checkbutton = GTK_WIDGET (gtk_builder_get_object (builder, "write-checkbutton"));
  num_access_samples_spinbutton = GTK_WIDGET (gtk_builder_get_object (builder, "num-access-samples-spinbutton"));

  /* if device is read-only, uncheck the "perform write-test"
   * check-button and also make it insensitive
   */
  if (udisks_block_get_read_only (data->block))
    {
      gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (write_checkbutton), FALSE);
      gtk_widget_set_sensitive (write_checkbutton, FALSE);
    }

  /* If the device is currently in use, uncheck the "perform write-test" check-button */
  if (gdu_utils_is_in_use (gdu_window_get_client (data->window), data->object))
    {
      gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (write_checkbutton), FALSE);
    }

  /* and scene... */
  response = gtk_dialog_run (GTK_DIALOG (dialog));

  gtk_widget_hide (dialog);

  if (response != GTK_RESPONSE_OK)
    goto out;

  data->bm_num_samples = gtk_spin_button_get_value (GTK_SPIN_BUTTON (num_samples_spinbutton));
  data->bm_sample_size_mib = gtk_spin_button_get_value (GTK_SPIN_BUTTON (sample_size_spinbutton));
  data->bm_do_write = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (write_checkbutton));
  data->bm_num_access_samples = gtk_spin_button_get_value (GTK_SPIN_BUTTON (num_access_samples_spinbutton));

  //g_print ("num_samples=%d\n", data->bm_num_samples);
  //g_print ("sample_size=%d MB\n", data->bm_sample_size_mib);
  //g_print ("do_write=%d\n", data->bm_do_write);
  //g_print ("num_access_samples=%d\n", data->bm_num_access_samples);

  if (data->bm_do_write)
    {
      /* ensure the device is unused (e.g. unmounted) before formatting it... */
      gdu_window_ensure_unused (data->window,
                                data->object,
                                (GAsyncReadyCallback) ensure_unused_cb,
                                NULL, /* GCancellable */
                                dialog_data_ref (data));
    }
  else
    {
      start_benchmark2 (data);
    }

 out:
  gtk_widget_destroy (dialog);
  g_clear_object (&builder);
  update_dialog (data);
}

/* ---------------------------------------------------------------------------------------------------- */

void
gdu_benchmark_dialog_show (GduWindow    *window,
                           UDisksObject *object)
{
  DialogData *data;
  guint n;
  guint timeout_id;
  GError *error = NULL;

  data = g_new0 (DialogData, 1);
  data->ref_count = 1;
  data->object = g_object_ref (object);
  data->block = udisks_object_peek_block (data->object);
  data->window = g_object_ref (window);
  data->bm_cancellable = g_cancellable_new ();

  data->bm_read_samples = g_array_new (FALSE, /* zero-terminated */
                                       FALSE, /* clear */
                                       sizeof (BMSample));
  data->bm_write_samples = g_array_new (FALSE, /* zero-terminated */
                                        FALSE, /* clear */
                                        sizeof (BMSample));
  data->bm_access_time_samples = g_array_new (FALSE, /* zero-terminated */
                                              FALSE, /* clear */
                                              sizeof (BMSample));

  data->dialog = GTK_WIDGET (gdu_application_new_widget (gdu_window_get_application (window),
                                                         "benchmark-dialog.ui",
                                                         "dialog1",
                                                         &data->builder));
  for (n = 0; widget_mapping[n].name != NULL; n++)
    {
      gpointer *p = (gpointer *) ((char *) data + widget_mapping[n].offset);
      *p = GTK_WIDGET (gtk_builder_get_object (data->builder, widget_mapping[n].name));
    }


  gtk_window_set_transient_for (GTK_WINDOW (data->dialog), GTK_WINDOW (window));

  data->start_benchmark_button = gtk_dialog_get_widget_for_response (GTK_DIALOG (data->dialog), 0);
  data->stop_benchmark_button = gtk_dialog_get_widget_for_response (GTK_DIALOG (data->dialog), 1);

  g_signal_connect (data->graph_drawing_area,
                    "draw",
                    G_CALLBACK (on_drawing_area_draw),
                    data);

  /* set minimum size for the graph */
  gtk_widget_set_size_request (data->graph_drawing_area,
                               600,
                               300);

  /* need this to update the "Updated" value */
  timeout_id = g_timeout_add_seconds (1, on_timeout, data);

  /* see if we have cached data */
  if (!maybe_load_data (data, &error))
    {
      /* not worth complaining in dialog about */
      g_warning ("Error loading cached data: %s (%s, %d)",
                 error->message, g_quark_to_string (error->domain), error->code);
      g_clear_error (&error);
    }

  update_dialog (data);

  while (TRUE)
    {
      gint response;
      response = gtk_dialog_run (GTK_DIALOG (data->dialog));
      /* Keep in sync with .ui file */
      switch (response)
        {
        case 0: /* start benchmark */
          start_benchmark (data);
          break;

        case 1: /* abort benchmark */
          abort_benchmark (data);
          break;
        }

      if (response < 0)
        break;
    }

  g_source_remove (timeout_id);
  dialog_data_close (data);
}
