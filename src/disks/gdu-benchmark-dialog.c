/*
 * Copyright (C) 2008-2013 Red Hat, Inc.
 *
 * Licensed under GPL version 2 or later.
 *
 * Author: David Zeuthen <zeuthen@gmail.com>
 */

#include "config.h"
#include "gio/gio.h"
#include "glib-object.h"
#include "glib.h"
#include "glibconfig.h"
#include "gsk/gsk.h"
#include "gtk/gtk.h"

#include <glib/gi18n.h>

#include <linux/fs.h>
#include <math.h>
#include <sys/ioctl.h>

#include "gdu-benchmark-dialog.h"

struct _GduBMSample
{
  GObject parent_instance;

  guint64 offset;
  gdouble value;
};

G_DEFINE_TYPE (GduBMSample, gdu_bm_sample, g_object_get_type())

typedef struct
{
  gdouble max;
  gdouble min;
  gdouble avg;
} BMStats;

struct _GduBenchmarkGraph
{
  AdwBin         parent_instance;

  guint64        bm_size;
  guint          total_transfer_samples;
  guint          total_atime_samples;
  GListStore     *read_samples;
  GListStore     *write_samples;
  GListStore     *atime_samples;
};

G_DEFINE_TYPE (GduBenchmarkGraph, gdu_benchmark_graph, ADW_TYPE_BIN)

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
  GtkWidget     *benchmark_graph;
  GtkWidget     *sample_size_action_row;
  GtkWidget     *read_rate_row;
  GtkWidget     *write_rate_row;
  GtkWidget     *access_time_row;

  /* must hold bm_lock when reading/writing these */
  GError        *bm_error;
  GCancellable  *bm_cancellable;
  gboolean       bm_in_progress;
  gboolean       bm_update_timeout_pending;

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
gdu_benchmark_dialog_load_options (GduBenchmarkDialog *self)
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
get_max_min_avg (GListStore *list)
{
  guint n;
  guint n_items;
  gdouble sum;
  BMStats ret = { 0 };

  n_items = g_list_model_get_n_items (G_LIST_MODEL (list));
  if (n_items == 0)
    return ret;

  ret.max = G_MINDOUBLE;
  ret.min = G_MAXDOUBLE;
  sum = 0;


  for (n = 0; n < n_items; n++)
    {
      GduBMSample *s = g_list_model_get_item (G_LIST_MODEL (list), n);
      ret.max = MAX (ret.max, s->value);
      ret.min = MIN (ret.min, s->value);
      sum += s->value;
    }

  ret.avg = sum / n_items;

  return ret;
}

static gdouble
get_max_speed(GduBenchmarkGraph *self) {
  gdouble max_val = 0.0;
  BMStats stats;

  if (self->read_samples && g_list_model_get_n_items (G_LIST_MODEL (self->read_samples)) > 0)
    {
      stats = get_max_min_avg (self->read_samples);
      max_val = MAX (max_val, stats.max);
    }

  if (self->write_samples && g_list_model_get_n_items (G_LIST_MODEL (self->write_samples)) > 0)
    {
      stats = get_max_min_avg (self->write_samples);
      max_val = MAX (max_val, stats.max);
    }

  return (max_val <= 0) ? 1 : max_val;
}

static gdouble
get_max_time (GduBenchmarkGraph *self)
{
  gdouble max_val = 0.0;
  BMStats stats;

  if (self->atime_samples && g_list_model_get_n_items (G_LIST_MODEL (self->atime_samples)) > 0)
    {
      stats = get_max_min_avg (self->atime_samples);
      max_val = MAX (max_val, stats.max);
    }

  return (max_val <= 0) ? 1 : max_val;
}

typedef struct
{
  int width;
  int height;
  int graph_width;
  int graph_height;
  int graph_x;
  int graph_y;
  gboolean is_time;
  const GdkRGBA *color;
  GListStore *samples;
  guint total_samples;
  gdouble max_speed;
  gdouble max_time;
} GraphData;

static void
gdu_benchmark_graph_draw_box (GtkWidget         *widget,
                              GtkSnapshot       *snapshot,
                              GraphData         *graph_data)
{
  AdwStyleManager *style_manager;
  g_autoptr(GskPathBuilder) builder = NULL;
  g_autoptr(GskStroke) stroke = NULL;
  g_autoptr(GskPath) path = NULL;
  GskRoundedRect rect;
  const GdkRGBA *grid_line_color;
  const GdkRGBA *bg_color;
  graphene_rect_t bounds = {.size.height = graph_data->graph_height, 
                            .size.width = graph_data->graph_width, 
                            .origin.x = graph_data->graph_x, 
                            .origin.y = graph_data->graph_y};

  style_manager = adw_style_manager_get_for_display (gtk_widget_get_display (GTK_WIDGET (widget)));

  if (adw_style_manager_get_dark (style_manager) && adw_style_manager_get_high_contrast (style_manager))
    grid_line_color = &GRID_LINE_COLOR_HC_DARK;
  else if (adw_style_manager_get_dark (style_manager))
    grid_line_color = &GRID_LINE_COLOR_DARK;
  else if (adw_style_manager_get_high_contrast (style_manager))
    grid_line_color = &GRID_LINE_COLOR_HC;
  else
    grid_line_color = &GRID_LINE_COLOR;

  if (adw_style_manager_get_dark (style_manager))
    bg_color = &GRAPH_BG_COLOR_DARK;
  else
    bg_color = &GRAPH_BG_COLOR;

  builder = gsk_path_builder_new ();
  gsk_rounded_rect_init_from_rect (&rect, &bounds, 10);
  gsk_path_builder_add_rounded_rect (builder, &rect);
  
  path = gsk_path_builder_free_to_path (g_steal_pointer (&builder));
  stroke = gsk_stroke_new (GRID_LINE_WIDTH);
  gtk_snapshot_append_stroke (snapshot, path, stroke, grid_line_color);
  gtk_snapshot_append_fill(snapshot, path, GSK_FILL_RULE_WINDING, bg_color);
}


static void
draw_horizontal_axis_and_labels (GtkWidget   *widget,
                                 GtkSnapshot *snapshot,
                                 GraphData   *graph_data)
{
  AdwStyleManager *style_manager;
  g_autoptr(GskPath) path = NULL;
  g_autoptr(GskStroke) stroke = NULL;
  g_autoptr(GskPathBuilder) builder = NULL;
  g_autoptr(PangoLayout) layout = NULL;
  g_autoptr(PangoFontDescription) label_font_desc = NULL;
  g_autoptr(PangoFontDescription) axis_title_font_desc = NULL;
  PangoContext* pango_context = NULL;
  gint font_size;
  char* label;
  const GdkRGBA *text_color;
  const GdkRGBA *grid_line_color;
  int text_width, text_height;
  int max_left_label_width = 0, max_right_label_width = 0;
  gdouble max_visible_speed, max_speed, max_time;
  gdouble speed_step, time_step;
  guint num_hlines;
  gdouble padding = 5;

  style_manager = adw_style_manager_get_for_display (gtk_widget_get_display (widget));

  if (adw_style_manager_get_dark (style_manager) && adw_style_manager_get_high_contrast (style_manager))
    grid_line_color = &GRID_LINE_COLOR_HC_DARK;
  else if (adw_style_manager_get_dark (style_manager))
    grid_line_color = &GRID_LINE_COLOR_DARK;
  else if (adw_style_manager_get_high_contrast (style_manager))
    grid_line_color = &GRID_LINE_COLOR_HC;
  else
    grid_line_color = &GRID_LINE_COLOR;

  pango_context = gtk_widget_get_pango_context (widget);
  label_font_desc = pango_font_description_copy (pango_context_get_font_description (pango_context));
  axis_title_font_desc = pango_font_description_copy (pango_context_get_font_description (pango_context));
  font_size = pango_font_description_get_size (label_font_desc);
  pango_font_description_set_absolute_size (label_font_desc, PANGO_SCALE_X_SMALL * font_size);
  pango_font_description_set_absolute_size (axis_title_font_desc, PANGO_SCALE_SMALL * font_size);

  label = g_strdup_printf ("100");
  layout = gtk_widget_create_pango_layout (widget, label);
  pango_layout_set_font_description (layout, label_font_desc);
  pango_layout_get_pixel_size (layout, &text_width, &text_height);
  g_free (label);

  graph_data->graph_height -= (text_height + padding);

  if (adw_style_manager_get_dark (style_manager))
    text_color = &LABEL_COLOR_DARK;
  else
    text_color = &LABEL_COLOR;

  /* TODO: Calculate this based on some maximum time or speed
   * TODO: Usually time (ms) is going to be really small compared to speed.
   * Try scaling the time data so that the graph height is equal for time and speed
   */

  num_hlines = 10;
  max_speed = get_max_speed (GDU_BENCHMARK_GRAPH (widget));
  max_time = get_max_time (GDU_BENCHMARK_GRAPH (widget));
  if (max_speed == 0)
    max_speed = 100 * 1000 * 1000;

  if (max_time == 0)
    max_time = 50 / 1000.0;

  time_step = max_time / num_hlines;

  /* round up to next multiple of 10 MB/s */
  max_visible_speed = ceil (max_speed / (10*1000*1000)) * 10*1000*1000;
  speed_step = max_visible_speed / num_hlines;

  graph_data->max_time =  max_time;
  graph_data->max_speed = max_visible_speed;

  if (time_step < 0.0001)
    time_step = 0.0001;
  else if (time_step < 0.0005)
    time_step = 0.0005;
  else if (time_step < 0.001)
    time_step = 0.001;
  else if (time_step < 0.0025)
    time_step = 0.0025;
  else if (time_step < 0.005)
    time_step = 0.005;
  else
    time_step = ceil (((gdouble) time_step) / 0.005) * 0.005;

  graph_data->max_time = (time_step * num_hlines);

  builder = gsk_path_builder_new ();
  for (guint j = 0; j <= num_hlines; j++) 
    {
      double x, y;

      y = graph_data->graph_height - ((float) j * graph_data->graph_height / num_hlines);

      x = 0.0;
      label = g_strdup_printf ("%ld", (gulong) (j * speed_step) / (1000 * 1000));
      layout = gtk_widget_create_pango_layout (widget, label);
      pango_layout_set_font_description (layout, label_font_desc);
      pango_layout_get_pixel_size (layout, &text_width, &text_height);
      max_left_label_width = fmax (max_left_label_width, text_width);

      gtk_snapshot_save (snapshot);
      gtk_snapshot_translate (snapshot, &GRAPHENE_POINT_INIT (x, y - (text_height / 2.0)));
      gtk_snapshot_append_layout (snapshot, layout, text_color);
      gtk_snapshot_restore (snapshot);
      g_free (label);

      x = graph_data->graph_width;
      label = g_strdup_printf ("%3g", j * time_step * 1000);
      layout = gtk_widget_create_pango_layout (widget, label);
      pango_layout_set_font_description (layout, label_font_desc);
      pango_layout_get_pixel_size (layout, &text_width, &text_height);
      max_right_label_width = fmax (max_right_label_width, text_width);

      gtk_snapshot_save (snapshot);
      gtk_snapshot_translate (snapshot, &GRAPHENE_POINT_INIT (x - text_width, y - (text_height / 2.0)));
      gtk_snapshot_append_layout (snapshot, layout, text_color);
      gtk_snapshot_restore (snapshot);

      g_free (label);
    }

  graph_data->graph_width -= (max_left_label_width + max_right_label_width + 2 * padding);
  graph_data->graph_x += (max_left_label_width + padding);

  // draw box before the grid lines are drawn
  gdu_benchmark_graph_draw_box  (widget, snapshot, graph_data);
  
  for (guint j = 0; j <= num_hlines; j++)
    {
      double x,  y;

      y = graph_data->graph_height - ((float) j * graph_data->graph_height / num_hlines);
      x = graph_data->graph_x;

      gsk_path_builder_move_to (builder, x, y);
      if (j != 0 && j != num_hlines) {
        gsk_path_builder_line_to (builder, graph_data->graph_x + graph_data->graph_width, y);
      }
    }

  path = gsk_path_builder_free_to_path (g_steal_pointer (&builder));
  stroke = gsk_stroke_new (GRID_LINE_WIDTH);
  gtk_snapshot_append_stroke (snapshot, path, stroke, grid_line_color);


  label = g_strdup_printf ("Read/Write Speed (MB/s)");
  layout = gtk_widget_create_pango_layout (widget, label);
  pango_layout_set_font_description (layout, axis_title_font_desc);
  pango_layout_get_pixel_size (layout, &text_width, &text_height);

  gtk_snapshot_save (snapshot);
  gtk_snapshot_translate (snapshot, &GRAPHENE_POINT_INIT (0.0 - (text_height + padding), (graph_data->graph_height / 2.0) + ((float) text_width / 2)));
  gtk_snapshot_rotate (snapshot, -90.0);
  gtk_snapshot_append_layout (snapshot, layout, text_color);
  gtk_snapshot_restore (snapshot);
  g_free (label);

  label = g_strdup_printf ("Access Time (ms)");
  layout = gtk_widget_create_pango_layout (widget, label);
  pango_layout_set_font_description (layout, axis_title_font_desc);
  pango_layout_get_pixel_size (layout, &text_width, &text_height);

  gtk_snapshot_save (snapshot);
  gtk_snapshot_translate (snapshot, &GRAPHENE_POINT_INIT (graph_data->width + padding + max_right_label_width, 
                                                         (graph_data->graph_height / 2.0) - ((float) text_width / 2)));
  gtk_snapshot_rotate (snapshot, 90.0);
  gtk_snapshot_append_layout (snapshot, layout, text_color);
  gtk_snapshot_restore (snapshot);
}

static void
draw_vertical_axis_and_labels (GtkWidget   *widget,
                               GtkSnapshot *snapshot,
                               GraphData   *graph_data)
{
  AdwStyleManager *style_manager;
  g_autoptr(GskPath) path = NULL;
  g_autoptr(GskStroke) stroke = NULL;
  g_autoptr(GskPathBuilder) builder = NULL;
  g_autoptr(PangoLayout) layout = NULL;
  g_autoptr(PangoFontDescription) label_font_desc = NULL;
  g_autoptr(PangoFontDescription) axis_title_font_desc = NULL;
  PangoContext* pango_context = NULL;
  gint font_size;
  g_autofree char* label;
  const GdkRGBA *text_color;
  const GdkRGBA *grid_line_color;
  int text_width, text_height;
  gdouble padding = 5;

  style_manager = adw_style_manager_get_for_display (gtk_widget_get_display (widget));

  if (adw_style_manager_get_dark (style_manager) && adw_style_manager_get_high_contrast (style_manager))
    grid_line_color = &GRID_LINE_COLOR_HC_DARK;
  else if (adw_style_manager_get_dark (style_manager))
    grid_line_color = &GRID_LINE_COLOR_DARK;
  else if (adw_style_manager_get_high_contrast (style_manager))
    grid_line_color = &GRID_LINE_COLOR_HC;
  else
    grid_line_color = &GRID_LINE_COLOR;

  if (adw_style_manager_get_dark (style_manager))
    text_color = &LABEL_COLOR_DARK;
  else
    text_color = &LABEL_COLOR;

  pango_context = gtk_widget_get_pango_context (widget);
  label_font_desc = pango_font_description_copy (pango_context_get_font_description (pango_context));
  axis_title_font_desc = pango_font_description_copy (pango_context_get_font_description (pango_context));
  font_size = pango_font_description_get_size (label_font_desc);
  pango_font_description_set_absolute_size (label_font_desc, font_size * PANGO_SCALE_X_SMALL);
  pango_font_description_set_absolute_size (axis_title_font_desc, font_size * PANGO_SCALE_SMALL);

  builder = gsk_path_builder_new ();
  for (int i = 0; i <= 10; i++)
    {
      double x = graph_data->graph_x + (i * graph_data->graph_width / 10.0);
      double y = graph_data->height;

      gsk_path_builder_move_to (builder, x, 0);
      if (i != 0 && i != 10)
        gsk_path_builder_line_to (builder, x, graph_data->graph_height);

      label = g_strdup_printf ("%d", i * 10);
      layout = gtk_widget_create_pango_layout (widget, label);
      pango_layout_set_font_description (layout, label_font_desc);
      pango_layout_get_pixel_size (layout, &text_width, &text_height);

      gtk_snapshot_save (snapshot);
      gtk_snapshot_translate (snapshot, &GRAPHENE_POINT_INIT (x - (text_width / 2.0), y - text_height));
      gtk_snapshot_append_layout (snapshot, layout, text_color);
      gtk_snapshot_restore (snapshot);

      g_free (label);
    }

    label = g_strdup_printf ("Progress (%%)");
    layout = gtk_widget_create_pango_layout (widget, label);
    pango_layout_set_font_description (layout, axis_title_font_desc);
    pango_layout_get_pixel_size (layout, &text_width, &text_height);

    gtk_snapshot_save (snapshot);
    gtk_snapshot_translate (snapshot, 
      &GRAPHENE_POINT_INIT ((graph_data->width - text_width) / 2.0, 
      graph_data->height + padding));
    gtk_snapshot_append_layout (snapshot, layout, text_color);
    gtk_snapshot_restore (snapshot);

    path = gsk_path_builder_free_to_path (g_steal_pointer (&builder));

    stroke = gsk_stroke_new (GRID_LINE_WIDTH);
    gsk_stroke_set_dash (stroke, GRID_LINE_DASH, 2);
    gtk_snapshot_append_stroke (snapshot, path, stroke, grid_line_color);
}

static void
gdu_benchmark_graph_draw_grid (GduBenchmarkGraph *self,
                               GtkSnapshot       *snapshot,
                               GraphData         *graph_data)
{
  draw_horizontal_axis_and_labels (GTK_WIDGET (self), snapshot, graph_data);
  draw_vertical_axis_and_labels (GTK_WIDGET (self), snapshot, graph_data);
}

static void
draw_curve (GdkSnapshot    *snapshot,
            GraphData      *graph_data)
{
  g_autoptr(GskPath) path = NULL;
  g_autoptr(GskStroke) stroke = NULL;
  g_autoptr(GskPathBuilder) builder = NULL;
  double x, y;
  guint n, n_samples, total_samples;
  gdouble maximum_value = graph_data->is_time ?
                          graph_data->max_time :
                          graph_data->max_speed;
  gdouble prev_slope = 0, prev_m = 0;

  if (graph_data->samples == NULL)
    return;

  n_samples = g_list_model_get_n_items (G_LIST_MODEL (graph_data->samples));
  if (n_samples == 0)
    return;

  total_samples = graph_data->total_samples - 1;

  builder = gsk_path_builder_new ();

  /*
   * For smoothing, use monotonic cubic interpolation
   *
   */

  {
    GduBMSample *sample = g_list_model_get_item (G_LIST_MODEL (graph_data->samples), 0);
    x = graph_data->graph_x + ((0.0 / total_samples) * graph_data->graph_width);
    y = graph_data->graph_y + (graph_data->graph_height - (sample->value / maximum_value * graph_data->graph_height));
    gsk_path_builder_move_to (builder, x, y);
  }


  for (n = 0; n < n_samples - 1; n++)
    {
      GduBMSample *sample1 = g_list_model_get_item (G_LIST_MODEL (graph_data->samples), n);
      GduBMSample *sample2 = g_list_model_get_item (G_LIST_MODEL (graph_data->samples), n+1);
      gdouble x0, x1, x2, x3, y0, y1, y2, y3;
      gdouble slope, m;
      gdouble a, b, r;

      x0 = graph_data->graph_x + (((double)  n       / total_samples) * graph_data->graph_width);
      x3 = graph_data->graph_x + ((((double)(n + 1)) / total_samples) * graph_data->graph_width);
      y0 = graph_data->graph_y + (graph_data->graph_height - (sample1->value / maximum_value * graph_data->graph_height));
      y3 = graph_data->graph_y + (graph_data->graph_height - (sample2->value / maximum_value * graph_data->graph_height));

      slope = (y3 - y0) / (x3 - x0);

      // initialize tangents as average of slopes
      if (n == 0 || n == n_samples - 1) {
        m = slope;
      }
      else {
        if ((prev_slope > 0 && slope < 0) || (prev_slope < 0 && slope > 0))
          m = 0;
        else
          m = (prev_slope + slope) / 2.0;
      }

      if (slope == 0) {
        prev_slope = 0;
        m = 0;
      }

      a = prev_m / prev_slope;
      b = m / prev_slope;

      // scale down tangents to ensure monotonicity
      if ((a * a + b * b) > 9) {
        r = 3 / sqrt (a * a + b * b);
        prev_m = r * a * prev_slope;
        m = r * b * prev_slope;
      }

      // calculate bezier control points
      x1 = x0 + (x3 - x0) / 3;
      x2 = x3 - (x3 - x0) / 3;

      y1 = y0 + ((1.0/3.0) * (x3 - x0) * prev_m);
      y2 = y3 - ((1.0/3.0) * (x3 - x0) * m);

      // cap it to graph height if it overflows
      y1 = fmax (0.0, y1);
      y2 = fmax (0.0, y2);
      y3 = fmax (0.0, y3);

      prev_m = m;
      prev_slope = slope;

      gsk_path_builder_cubic_to (builder,
                                 x1, y1,
                                 x2, y2,
                                 x3,  y3);
    }

  path = gsk_path_builder_free_to_path (g_steal_pointer (&builder));

  stroke = gsk_stroke_new (GRID_LINE_WIDTH);
  gtk_snapshot_append_stroke (snapshot, path, stroke, graph_data->color);
}

static void
gdu_benchmark_graph_snapshot (GtkWidget   *widget,
                              GtkSnapshot *snapshot)
{
  GduBenchmarkGraph *self = GDU_BENCHMARK_GRAPH (widget);
  GraphData graph_data = {0};

  graph_data.width = gtk_widget_get_width (GTK_WIDGET (self));
  graph_data.height = gtk_widget_get_height (GTK_WIDGET (self));
  graph_data.graph_width = graph_data.width;
  graph_data.graph_height = graph_data.height;

  gdu_benchmark_graph_draw_grid (self, snapshot, &graph_data);

  graph_data.samples = self->read_samples;
  graph_data.total_samples = self->total_transfer_samples;
  graph_data.is_time = FALSE;
  graph_data.color = &READ_CURVE_COLOR;
  draw_curve (snapshot, &graph_data);

  graph_data.samples = self->write_samples;
  graph_data.color = &WRITE_CURVE_COLOR;
  draw_curve (snapshot, &graph_data);

  graph_data.samples = self->atime_samples;
  graph_data.total_samples = self->total_atime_samples;
  graph_data.is_time = TRUE;
  graph_data.color = &ATIME_CURVE_COLOR;
  draw_curve (snapshot, &graph_data);
}

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
    error = g_steal_pointer (&self->bm_error);
  G_UNLOCK (bm_lock);

  /* present an error if something went wrong */
  if (error != NULL && (error->domain != G_IO_ERROR || error->code != G_IO_ERROR_CANCELLED))
    {
      gdu_utils_show_error (gdu_benchmark_dialog_get_window (self),
                            _("An error occurred"), error);

      s = g_strdup ("–");
      adw_action_row_set_subtitle (ADW_ACTION_ROW (self->sample_size_action_row), s);
      adw_action_row_set_subtitle (ADW_ACTION_ROW (self->read_rate_row), s);
      adw_action_row_set_subtitle (ADW_ACTION_ROW (self->write_rate_row), s);
      adw_action_row_set_subtitle (ADW_ACTION_ROW (self->access_time_row), s);
      g_free (s);
      return;
    }

  G_LOCK (bm_lock);
  read_stats = get_max_min_avg (GDU_BENCHMARK_GRAPH (self->benchmark_graph)->read_samples);
  write_stats = get_max_min_avg (GDU_BENCHMARK_GRAPH (self->benchmark_graph)->write_samples);
  atime_stats = get_max_min_avg (GDU_BENCHMARK_GRAPH (self->benchmark_graph)->atime_samples);
  G_UNLOCK (bm_lock);

  if (read_stats.avg != 0.0)
    {
      s = format_stats (read_stats.avg, 
        g_list_model_get_n_items (G_LIST_MODEL (GDU_BENCHMARK_GRAPH (self->benchmark_graph)->read_samples)), 
        FALSE);
      adw_action_row_set_subtitle (ADW_ACTION_ROW (self->read_rate_row), s);
      g_free (s);
    }

  if (write_stats.avg != 0.0)
    {
      s = format_stats (write_stats.avg, 
        g_list_model_get_n_items (G_LIST_MODEL (GDU_BENCHMARK_GRAPH (self->benchmark_graph)->write_samples)), 
        FALSE);
      adw_action_row_set_subtitle (ADW_ACTION_ROW (self->write_rate_row), s);
      g_free (s);
    }

  if (atime_stats.avg != 0.0)
    {
      s = format_stats (atime_stats.avg, 
        g_list_model_get_n_items (G_LIST_MODEL (GDU_BENCHMARK_GRAPH (self->benchmark_graph)->atime_samples)), 
        TRUE);
      adw_action_row_set_subtitle (ADW_ACTION_ROW (self->access_time_row), s);
      g_free (s);
    }

  gtk_widget_queue_draw (GTK_WIDGET (GDU_BENCHMARK_GRAPH (self->benchmark_graph)));
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
                     bmt_on_timeout, self);
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
    close (fd);
  self->bm_in_progress = FALSE;
  gtk_widget_set_visible (self->cancel_button, FALSE);

  if (inhibit_cookie != 0)
    gtk_application_uninhibit ((gpointer) g_application_get_default (),
                                          inhibit_cookie);

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
                    int                *fd)
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
    return error;

  *fd = g_unix_fd_list_get (fd_list, g_variant_get_handle (fd_index), NULL);

  return NULL;
}

static GError *
benchmark_transfer_rate (GduBenchmarkDialog *self,
                         guchar             *buffer,
                         int                 fd,
                         long                page_size,
                         guint64             disk_size)
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
      GduBMSample *sample;

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

      sample = gdu_bm_sample_new (offset, 
        ((gdouble)G_USEC_PER_SEC) * num_read / (end_usec - begin_usec));

      G_LOCK (bm_lock);
      g_list_store_append (GDU_BENCHMARK_GRAPH (self->benchmark_graph)->read_samples, sample);
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

          sample = gdu_bm_sample_new (offset, 
            ((gdouble)G_USEC_PER_SEC) * num_written
                         / (end_usec - begin_usec));

          G_LOCK (bm_lock);
          g_list_store_append (GDU_BENCHMARK_GRAPH (self->benchmark_graph)->write_samples, sample);
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
      GduBMSample *sample;

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

      sample = gdu_bm_sample_new (offset, 
        (end_usec - begin_usec) / ((gdouble)G_USEC_PER_SEC));

      G_LOCK (bm_lock);
      g_list_store_append (GDU_BENCHMARK_GRAPH (self->benchmark_graph)->atime_samples, sample);
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
  GDU_BENCHMARK_GRAPH (self->benchmark_graph)->bm_size = disk_size;
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
  g_cancellable_reset (self->bm_cancellable);

  GDU_BENCHMARK_GRAPH(self->benchmark_graph)->total_transfer_samples = (guint)g_settings_get_int (self->settings, "num-samples");
  GDU_BENCHMARK_GRAPH(self->benchmark_graph)->total_atime_samples = (guint)g_settings_get_int (self->settings, "num-access-samples");

  sample_size = g_settings_get_int (self->settings, "sample-size-mib");
  sample_size = sample_size * 1024 * 1024;

  if (sample_size != 0)
  {
    s = g_format_size_full (sample_size, G_FORMAT_SIZE_IEC_UNITS |
                                         G_FORMAT_SIZE_LONG_FORMAT);
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
    start_benchmark (self);
}

static void
on_start_clicked_cb (GduBenchmarkDialog *self,
                     GtkButton          *button)
{
  gboolean write_benchmark;

  g_assert (!self->bm_in_progress);

  gdu_benchmark_dialog_save_options (self);

  write_benchmark = g_settings_get_boolean (self->settings, "do-write");

  /* ensure the device is unused (e.g. unmounted) before formatting it... */
  if (write_benchmark)
    gdu_utils_ensure_unused (self->client,
                              gdu_benchmark_dialog_get_window (self),
                              self->object,
                              (GAsyncReadyCallback) ensure_unused_cb,
                              NULL, /* GCancellable */
                              self);
  else
    start_benchmark (self);

  gtk_stack_set_visible_child_name (GTK_STACK (self->pages_stack), "results");
  gtk_widget_set_visible (self->close_button, FALSE);
}

static void
gdu_benchmark_dialog_set_title (GduBenchmarkDialog *self)
{
  g_autoptr(UDisksObjectInfo) info = NULL;

  info = udisks_client_get_object_info (self->client, self->object);
  adw_window_title_set_subtitle (ADW_WINDOW_TITLE (self->window_title),
                                 udisks_object_info_get_one_liner (info));
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

GduBMSample *
gdu_bm_sample_new (guint64  offset,
                   gdouble  value)
{
  GduBMSample *self;

  self = g_object_new (GDU_TYPE_BM_SAMPLE, NULL);

  self->offset = offset;
  self->value = value;

  return self;
}

static void
gdu_bm_sample_init (GduBMSample *self)
{}

static void
gdu_bm_sample_class_init (GduBMSampleClass *self)
{}

static void
gdu_benchmark_graph_dispose (GObject *object)
{
  G_OBJECT_CLASS (gdu_benchmark_graph_parent_class)->dispose (object);
}

static void
gdu_benchmark_graph_init (GduBenchmarkGraph *self)
{
  self->read_samples = g_list_store_new (GDU_TYPE_BM_SAMPLE);
  self->write_samples = g_list_store_new (GDU_TYPE_BM_SAMPLE);
  self->atime_samples = g_list_store_new (GDU_TYPE_BM_SAMPLE);

  gtk_widget_set_size_request (GTK_WIDGET (self), -1, 279);
}

static void
gdu_benchmark_graph_class_init(GduBenchmarkGraphClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->dispose = gdu_benchmark_graph_dispose;

  widget_class->snapshot = gdu_benchmark_graph_snapshot;
}

static void
gdu_benchmark_dialog_finalize (GObject *object)
{
  GduBenchmarkGraph *self = GDU_BENCHMARK_GRAPH (object);

  g_clear_object (&self->read_samples);
  g_clear_object (&self->write_samples);
  g_clear_object (&self->atime_samples);

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

  gtk_widget_class_bind_template_child (widget_class, GduBenchmarkDialog, benchmark_graph);
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

  g_signal_connect_swapped (GDU_BENCHMARK_GRAPH (self->benchmark_graph)->read_samples,
                            "notify::n-items",
                            G_CALLBACK (gtk_widget_queue_draw),
                            GTK_WIDGET (GDU_BENCHMARK_GRAPH (self->benchmark_graph)));

  g_signal_connect_swapped (GDU_BENCHMARK_GRAPH (self->benchmark_graph)->write_samples,
                            "notify::n-items",
                            G_CALLBACK (gtk_widget_queue_draw),
                            GTK_WIDGET (GDU_BENCHMARK_GRAPH (self->benchmark_graph)));

  g_signal_connect_swapped (GDU_BENCHMARK_GRAPH (self->benchmark_graph)->atime_samples,
                            "notify::n-items",
                            G_CALLBACK (gtk_widget_queue_draw),
                            GTK_WIDGET (GDU_BENCHMARK_GRAPH (self->benchmark_graph)));

  gdu_benchmark_dialog_set_title (self);
  gdu_benchmark_dialog_load_options (self);

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
    adw_switch_row_set_active (ADW_SWITCH_ROW (self->write_bench_switch),
                               FALSE);

  adw_dialog_present (ADW_DIALOG (self), GTK_WIDGET (parent_window));
}
