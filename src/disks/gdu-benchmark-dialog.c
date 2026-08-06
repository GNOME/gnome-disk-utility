/*
 * Copyright (C) 2008-2013 Red Hat, Inc.
 *
 * Licensed under GPL version 2 or later.
 *
 * Author: David Zeuthen <zeuthen@gmail.com>
 */

#include "config.h"

#include "gdu-benchmark-dialog.h"

#include <math.h>

#include <glib/gi18n.h>
#include <glib/gstdio.h>
#include <linux/fs.h>
#include <sys/ioctl.h>

#include "gdk/gdk.h"
#include "gdu-application.h"
#include "gdu-job-manager.h"
#include "gdulocaljob.h"
#include "gio/gio.h"
#include "glib-object.h"
#include "glib.h"
#include "glibconfig.h"
#include "graphene.h"
#include "gsk/gsk.h"
#include "gtk/gtk.h"

/* Taken from
 * https://gitlab.gnome.org/GNOME/gnome-control-center/-/blob/f9c4cbe62c3d456a08c35fddd8666af7c0f2884e/panels/wellbeing/cc-bar-chart.c#L553
 * Adapted for Disks
 */
static const guint GRID_LINE_WIDTH = 1;
static const GdkRGBA GRID_LINE_COLOR = { .red = 0, .green = 0, .blue = 0, .alpha = 0.15 };
static const GdkRGBA GRID_LINE_COLOR_DARK = { .red = 1, .green = 1, .blue = 1, .alpha = 0.15 };
static const GdkRGBA GRID_LINE_COLOR_HC = { .red = 0, .green = 0, .blue = 0, .alpha = 0.5 };
static const GdkRGBA GRID_LINE_COLOR_HC_DARK = { .red = 1, .green = 1, .blue = 1, .alpha = 0.5 };
static const gfloat GRID_LINE_DASH[] = { 4, 2 };

static const GdkRGBA READ_CURVE_COLOR = {
    .red = 53.0 / 255.0, .green = 132.0 / 255.0, .blue = 228.0 / 255.0, .alpha = 1
};
static const GdkRGBA WRITE_CURVE_COLOR = {
    .red = 230.0 / 255.0, .green = 45.0 / 255.0, .blue = 66.0 / 255.0, .alpha = 1
};
static const GdkRGBA ATIME_DOT_COLOR = {
    .red = 58.0 / 255.0, .green = 148.0 / 255.0, .blue = 74.0 / 255.0, .alpha = 0.5
};

static const GdkRGBA GRAPH_BG_COLOR = { .red = 1.0, .green = 1.0, .blue = 1.0, .alpha = 1 };
static const GdkRGBA GRAPH_BG_COLOR_DARK = {
    .red = 52.0 / 255.0, .green = 52.0 / 255.0, .blue = 55.0 / 255.0, .alpha = 1
};

static const GdkRGBA LABEL_COLOR = { .red = 0.0, .green = 0.0, .blue = 0.0, .alpha = 1 };
static const GdkRGBA LABEL_COLOR_DARK = { .red = 1.0, .green = 1.0, .blue = 1.0, .alpha = 1 };

typedef enum {
    BENCHMARK_SAMPLE_READ,
    BENCHMARK_SAMPLE_WRITE,
    BENCHMARK_SAMPLE_ACCESS_TIME,
} BenchmarkSampleKind;

typedef struct {
    BenchmarkSampleKind kind;
    guint64 offset;
    gdouble value;
} BenchmarkSample;

typedef struct {
    gdouble max;
    gdouble avg;
} BenchmarkStats;

typedef struct {
    GWeakRef dialog;
    UDisksBlock *block;
    GAsyncQueue *pending_samples;
    guint64 benchmark_size;
    guint num_samples;
    guint sample_size_mib;
    guint num_access_samples;
    gint completed_samples;
    guint inhibit_cookie;
    gboolean write_benchmark;
} BenchmarkJobData;

#define GDU_TYPE_BENCHMARK_GRAPH (gdu_benchmark_graph_get_type ())
G_DECLARE_FINAL_TYPE (GduBenchmarkGraph, gdu_benchmark_graph, GDU, BENCHMARK_GRAPH, AdwBin)

struct _GduBenchmarkGraph {
    AdwBin parent_instance;

    guint64 benchmark_size;
    guint total_transfer_samples;
    guint total_atime_samples;
    GArray *read_samples;
    GArray *write_samples;
    GArray *atime_samples;
};

G_DEFINE_FINAL_TYPE (GduBenchmarkGraph, gdu_benchmark_graph, ADW_TYPE_BIN)

struct _GduBenchmarkDialog {
    AdwDialog parent_instance;

    GtkWidget *close_button;
    GtkWidget *cancel_button;
    GtkWidget *window_title;

    GtkWidget *pages_stack;

    /* Configuration Page */
    GtkWidget *sample_row;
    GtkWidget *sample_size_row;
    GtkWidget *access_samples_row;
    GtkWidget *write_bench_switch;

    /* Results Page */
    GtkWidget *benchmark_graph;
    GtkWidget *sample_size_action_row;
    GtkWidget *read_rate_row;
    GtkWidget *write_rate_row;
    GtkWidget *access_time_row;

    GduLocalJob *job;

    GSettings *settings;
    UDisksClient *client;
    UDisksObject *object;
    UDisksBlock *block;

    GtkWindow *parent_window;
};

G_DEFINE_FINAL_TYPE (GduBenchmarkDialog, gdu_benchmark_dialog, ADW_TYPE_DIALOG)

static void
benchmark_job_data_free (BenchmarkJobData *data)
{
    if (data->inhibit_cookie != 0)
        gtk_application_uninhibit ((gpointer) g_application_get_default (), data->inhibit_cookie);

    g_weak_ref_clear (&data->dialog);
    g_clear_object (&data->block);
    g_clear_pointer (&data->pending_samples, g_async_queue_unref);
    g_free (data);
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC (BenchmarkJobData, benchmark_job_data_free)

static BenchmarkJobData *
benchmark_job_data_new (GduBenchmarkDialog *self)
{
    BenchmarkJobData *data = g_new0 (BenchmarkJobData, 1);

    g_weak_ref_init (&data->dialog, self);
    data->block = g_object_ref (self->block);
    data->pending_samples = g_async_queue_new_full (g_free);
    data->num_samples = g_settings_get_int (self->settings, "num-samples");
    data->sample_size_mib = g_settings_get_int (self->settings, "sample-size-mib");
    data->num_access_samples = g_settings_get_int (self->settings, "num-access-samples");
    data->write_benchmark = g_settings_get_boolean (self->settings, "do-write");
    data->inhibit_cookie = gtk_application_inhibit ((gpointer) g_application_get_default (), self->parent_window,
                                                    GTK_APPLICATION_INHIBIT_SUSPEND | GTK_APPLICATION_INHIBIT_LOGOUT,
                                                    /* Translators: Reason why suspend/logout is being inhibited */
                                                    "Benchmark in progress");

    return data;
}

static GtkWindow *
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

static BenchmarkStats
get_max_avg (GArray *samples)
{
    gdouble sum = 0;
    BenchmarkStats ret = { 0 };

    if (samples->len == 0)
        return ret;

    for (guint n = 0; n < samples->len; n++) {
        BenchmarkSample *sample = &g_array_index (samples, BenchmarkSample, n);
        ret.max = MAX (ret.max, sample->value);
        sum += sample->value;
    }

    ret.avg = sum / samples->len;

    return ret;
}

static gdouble
get_max_speed (GduBenchmarkGraph *self)
{
    return MAX (get_max_avg (self->read_samples).max, get_max_avg (self->write_samples).max);
}

static gdouble
get_max_time (GduBenchmarkGraph *self)
{
    return get_max_avg (self->atime_samples).max;
}

typedef struct {
    gint width;
    gint height;
    gint graph_width;
    gint graph_height;
    gint graph_x;
    gint graph_y;
    const GdkRGBA *color;
    GArray *samples;
    guint total_samples;
    guint64 benchmark_size;
    gdouble max_speed;
    gdouble max_time;
} GraphData;

static const GdkRGBA *
get_color_hc (GtkWidget *widget, const GdkRGBA *color_light, const GdkRGBA *color_dark, const GdkRGBA *color_hc_light,
              const GdkRGBA *color_hc_dark)
{
    AdwStyleManager *style_manager;

    style_manager = adw_style_manager_get_for_display (gtk_widget_get_display (GTK_WIDGET (widget)));

    if (adw_style_manager_get_dark (style_manager) && adw_style_manager_get_high_contrast (style_manager))
        return color_hc_dark;
    else if (adw_style_manager_get_dark (style_manager))
        return color_dark;
    else if (adw_style_manager_get_high_contrast (style_manager))
        return color_hc_light;
    else
        return color_light;
}

#define get_color(widget, color_light, color_dark)                                                                     \
    get_color_hc (widget, color_light, color_dark, color_light, color_dark)

static void
gdu_benchmark_graph_draw_box (GtkWidget *widget, GtkSnapshot *snapshot, GraphData *graph_data)
{
    g_autoptr(GskPathBuilder) builder = NULL;
    g_autoptr(GskStroke) stroke = NULL;
    g_autoptr(GskPath) path = NULL;
    GskRoundedRect rect;
    const GdkRGBA *grid_line_color;
    const GdkRGBA *bg_color;
    graphene_rect_t bounds = { .size.height = graph_data->graph_height,
                               .size.width = graph_data->graph_width,
                               .origin.x = graph_data->graph_x,
                               .origin.y = graph_data->graph_y };

    grid_line_color =
        get_color_hc (widget, &GRID_LINE_COLOR, &GRID_LINE_COLOR_DARK, &GRID_LINE_COLOR_HC, &GRID_LINE_COLOR_HC_DARK);

    bg_color = get_color (widget, &GRAPH_BG_COLOR, &GRAPH_BG_COLOR_DARK);

    builder = gsk_path_builder_new ();
    gsk_rounded_rect_init_from_rect (&rect, &bounds, 10);
    gsk_path_builder_add_rounded_rect (builder, &rect);

    path = gsk_path_builder_free_to_path (g_steal_pointer (&builder));
    stroke = gsk_stroke_new (GRID_LINE_WIDTH);
    gtk_snapshot_append_stroke (snapshot, path, stroke, grid_line_color);
    gtk_snapshot_append_fill (snapshot, path, GSK_FILL_RULE_WINDING, bg_color);
}

static void
draw_horizontal_axis_and_labels (GtkWidget *widget, GtkSnapshot *snapshot, GraphData *graph_data)
{
    g_autoptr(GskPath) path = NULL;
    g_autoptr(GskStroke) stroke = NULL;
    g_autoptr(GskPathBuilder) builder = NULL;
    g_autoptr(PangoLayout) layout = NULL;
    g_autoptr(PangoFontDescription) label_font_desc = NULL;
    g_autoptr(PangoFontDescription) axis_title_font_desc = NULL;
    PangoContext *pango_context = NULL;
    gint font_size;
    g_autofree gchar *label = NULL;
    const GdkRGBA *text_color;
    const GdkRGBA *grid_line_color;
    gint text_width, text_height;
    gint max_left_label_width = 0, max_right_label_width = 0;
    gdouble max_visible_speed, max_speed, max_time;
    gdouble speed_step, time_step;
    guint num_hlines;
    gdouble padding = 6;

    grid_line_color =
        get_color_hc (widget, &GRID_LINE_COLOR, &GRID_LINE_COLOR_DARK, &GRID_LINE_COLOR_HC, &GRID_LINE_COLOR_HC_DARK);

    pango_context = gtk_widget_get_pango_context (widget);
    label_font_desc = pango_font_description_copy (pango_context_get_font_description (pango_context));
    axis_title_font_desc = pango_font_description_copy (pango_context_get_font_description (pango_context));
    font_size = pango_font_description_get_size (label_font_desc);
    pango_font_description_set_absolute_size (label_font_desc, PANGO_SCALE_X_SMALL * font_size);
    pango_font_description_set_absolute_size (axis_title_font_desc, PANGO_SCALE_SMALL * font_size);

    label = g_strdup ("100");
    layout = gtk_widget_create_pango_layout (widget, label);
    pango_layout_set_font_description (layout, label_font_desc);
    pango_layout_get_pixel_size (layout, &text_width, &text_height);
    g_clear_pointer (&label, g_free);

    graph_data->graph_height -= (text_height + 2 * padding);

    text_color = get_color (widget, &LABEL_COLOR, &LABEL_COLOR_DARK);

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
    max_visible_speed = ceil (max_speed / (10 * 1000 * 1000)) * 10 * 1000 * 1000;
    speed_step = max_visible_speed / num_hlines;

    graph_data->max_time = max_time;
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
    for (guint j = 0; j <= num_hlines; j++) {
        gdouble x, y;

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
        g_clear_pointer (&label, g_free);

        x = graph_data->graph_width;
        label = g_strdup_printf ("%-3g", j * time_step * 1000);
        layout = gtk_widget_create_pango_layout (widget, label);
        pango_layout_set_font_description (layout, label_font_desc);
        pango_layout_get_pixel_size (layout, &text_width, &text_height);
        max_right_label_width = fmax (max_right_label_width, text_width);

        gtk_snapshot_save (snapshot);
        gtk_snapshot_translate (snapshot, &GRAPHENE_POINT_INIT (x - text_width, y - (text_height / 2.0)));
        gtk_snapshot_append_layout (snapshot, layout, text_color);
        gtk_snapshot_restore (snapshot);

        g_clear_pointer (&label, g_free);
    }

    graph_data->graph_width -= (max_left_label_width + max_right_label_width + 2 * padding);
    graph_data->graph_x += (max_left_label_width + padding);

    // draw box before the grid lines are drawn
    gdu_benchmark_graph_draw_box (widget, snapshot, graph_data);

    for (guint j = 0; j <= num_hlines; j++) {
        gdouble x, y;

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

    label = g_strdup (_("Read/Write Speed (MB/s)"));
    layout = gtk_widget_create_pango_layout (widget, label);
    pango_layout_set_font_description (layout, axis_title_font_desc);
    pango_layout_get_pixel_size (layout, &text_width, &text_height);

    gtk_snapshot_save (snapshot);
    gtk_snapshot_translate (snapshot,
                            &GRAPHENE_POINT_INIT (0.0 - (text_height + padding),
                                                  (graph_data->graph_height / 2.0) + ((float) text_width / 2)));
    gtk_snapshot_rotate (snapshot, -90.0);
    gtk_snapshot_append_layout (snapshot, layout, text_color);
    gtk_snapshot_restore (snapshot);
    g_set_str (&label, _("Access Time (ms)"));
    layout = gtk_widget_create_pango_layout (widget, label);
    pango_layout_set_font_description (layout, axis_title_font_desc);
    pango_layout_get_pixel_size (layout, &text_width, &text_height);

    gtk_snapshot_save (snapshot);
    gtk_snapshot_translate (snapshot,
                            &GRAPHENE_POINT_INIT (graph_data->width + padding + max_right_label_width,
                                                  (graph_data->graph_height / 2.0) - ((float) text_width / 2)));
    gtk_snapshot_rotate (snapshot, 90.0);
    gtk_snapshot_append_layout (snapshot, layout, text_color);
    gtk_snapshot_restore (snapshot);
}

static void
draw_vertical_axis_and_labels (GtkWidget *widget, GtkSnapshot *snapshot, GraphData *graph_data)
{
    g_autoptr(GskPath) path = NULL;
    g_autoptr(GskStroke) stroke = NULL;
    g_autoptr(GskPathBuilder) builder = NULL;
    g_autoptr(PangoLayout) layout = NULL;
    g_autoptr(PangoFontDescription) label_font_desc = NULL;
    g_autoptr(PangoFontDescription) axis_title_font_desc = NULL;
    PangoContext *pango_context = NULL;
    gint font_size;
    g_autofree char *label = NULL;
    const GdkRGBA *text_color;
    const GdkRGBA *grid_line_color;
    gint text_width, text_height;

    grid_line_color =
        get_color_hc (widget, &GRID_LINE_COLOR, &GRID_LINE_COLOR_DARK, &GRID_LINE_COLOR_HC, &GRID_LINE_COLOR_HC_DARK);

    text_color = get_color (widget, &LABEL_COLOR, &LABEL_COLOR_DARK);

    pango_context = gtk_widget_get_pango_context (widget);
    label_font_desc = pango_font_description_copy (pango_context_get_font_description (pango_context));
    axis_title_font_desc = pango_font_description_copy (pango_context_get_font_description (pango_context));
    font_size = pango_font_description_get_size (label_font_desc);
    pango_font_description_set_absolute_size (label_font_desc, font_size * PANGO_SCALE_X_SMALL);
    pango_font_description_set_absolute_size (axis_title_font_desc, font_size * PANGO_SCALE_SMALL);

    builder = gsk_path_builder_new ();
    for (gint i = 0; i <= 10; i++) {
        gdouble x = graph_data->graph_x + (i * graph_data->graph_width / 10.0);
        gdouble y = graph_data->height;

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

        g_clear_pointer (&label, g_free);
    }

    label = g_strdup_printf (_("Speed: Location Within Disk (%%) / Access Time: Location Delta (%%)"));
    layout = gtk_widget_create_pango_layout (widget, label);
    pango_layout_set_font_description (layout, axis_title_font_desc);
    pango_layout_get_pixel_size (layout, &text_width, &text_height);

    gtk_snapshot_save (snapshot);
    gtk_snapshot_translate (snapshot,
                            &GRAPHENE_POINT_INIT ((graph_data->width - text_width) / 2.0, graph_data->height));
    gtk_snapshot_append_layout (snapshot, layout, text_color);
    gtk_snapshot_restore (snapshot);

    path = gsk_path_builder_free_to_path (g_steal_pointer (&builder));

    stroke = gsk_stroke_new (GRID_LINE_WIDTH);
    gsk_stroke_set_dash (stroke, GRID_LINE_DASH, 2);
    gtk_snapshot_append_stroke (snapshot, path, stroke, grid_line_color);
}

static void
gdu_benchmark_graph_draw_grid (GduBenchmarkGraph *self, GtkSnapshot *snapshot, GraphData *graph_data)
{
    draw_horizontal_axis_and_labels (GTK_WIDGET (self), snapshot, graph_data);
    draw_vertical_axis_and_labels (GTK_WIDGET (self), snapshot, graph_data);
}

static void
draw_scatterplot (GdkSnapshot *snapshot, GraphData *graph_data)
{
    guint n_samples;
    gdouble maximum_value = graph_data->max_time;
    g_autoptr(GskPathBuilder) builder = gsk_path_builder_new ();
    g_autoptr(GskStroke) stroke = gsk_stroke_new (GRID_LINE_WIDTH);
    g_autoptr(GskPath) path = NULL;
    guint64 max_offset;

    if (graph_data->samples == NULL)
        return;

    n_samples = graph_data->samples->len;
    if (n_samples == 0)
        return;

    max_offset = graph_data->benchmark_size;

    g_assert (max_offset != 0);

    for (guint n = 0; n < n_samples; n++) {
        BenchmarkSample *sample = &g_array_index (graph_data->samples, BenchmarkSample, n);
        graphene_point_t p;

        p.x = graph_data->graph_x + (((double) sample->offset / max_offset) * graph_data->graph_width);
        p.y = graph_data->graph_y
              + (graph_data->graph_height - (sample->value / maximum_value * graph_data->graph_height));

        gsk_path_builder_add_circle (builder, &p, 2);
    }

    path = gsk_path_builder_free_to_path (g_steal_pointer (&builder));
    gtk_snapshot_append_stroke (snapshot, path, stroke, graph_data->color);
    gtk_snapshot_append_fill (snapshot, path, GSK_FILL_RULE_WINDING, graph_data->color);
}

static void
draw_curve (GdkSnapshot *snapshot, GraphData *graph_data)
{
    g_autoptr(GskPath) path = NULL;
    g_autoptr(GskStroke) stroke = NULL;
    g_autoptr(GskPathBuilder) builder = gsk_path_builder_new ();
    guint n_samples;
    guint total_samples;
    gdouble maximum_value = graph_data->max_speed;
    gdouble previous_slope = 0;
    gdouble previous_tangent = 0;

    if (graph_data->samples == NULL)
        return;

    n_samples = graph_data->samples->len;
    if (n_samples == 0)
        return;

    total_samples = graph_data->total_samples - 1;

    {
        BenchmarkSample *sample = &g_array_index (graph_data->samples, BenchmarkSample, 0);
        gdouble y = graph_data->graph_y
                    + (graph_data->graph_height - (sample->value / maximum_value * graph_data->graph_height));

        gsk_path_builder_move_to (builder, graph_data->graph_x, y);
    }

    /* Monotonic cubic interpolation avoids overshooting measured values. */
    for (guint n = 0; n + 1 < n_samples; n++) {
        BenchmarkSample *sample1 = &g_array_index (graph_data->samples, BenchmarkSample, n);
        BenchmarkSample *sample2 = &g_array_index (graph_data->samples, BenchmarkSample, n + 1);
        gdouble x0 = graph_data->graph_x + ((gdouble) n / total_samples * graph_data->graph_width);
        gdouble x3 = graph_data->graph_x + ((gdouble) (n + 1) / total_samples * graph_data->graph_width);
        gdouble y0 = graph_data->graph_y
                     + (graph_data->graph_height - (sample1->value / maximum_value * graph_data->graph_height));
        gdouble y3 = graph_data->graph_y
                     + (graph_data->graph_height - (sample2->value / maximum_value * graph_data->graph_height));
        gdouble slope = (y3 - y0) / (x3 - x0);
        gdouble tangent = slope;

        if (n > 0) {
            tangent = previous_slope * slope <= 0 ? 0 : (previous_slope + slope) / 2.0;

            if (previous_slope != 0) {
                gdouble alpha = previous_tangent / previous_slope;
                gdouble beta = tangent / previous_slope;
                gdouble magnitude = alpha * alpha + beta * beta;

                if (magnitude > 9) {
                    gdouble scale = 3 / sqrt (magnitude);

                    previous_tangent = scale * alpha * previous_slope;
                    tangent = scale * beta * previous_slope;
                }
            }
        }

        gsk_path_builder_cubic_to (builder, x0 + (x3 - x0) / 3, MAX (0, y0 + (x3 - x0) * previous_tangent / 3),
                                   x3 - (x3 - x0) / 3, MAX (0, y3 - (x3 - x0) * tangent / 3), x3, MAX (0, y3));

        previous_slope = slope;
        previous_tangent = tangent;
    }

    path = gsk_path_builder_free_to_path (g_steal_pointer (&builder));
    stroke = gsk_stroke_new (GRID_LINE_WIDTH);
    gtk_snapshot_append_stroke (snapshot, path, stroke, graph_data->color);
}

static void
gdu_benchmark_graph_snapshot (GtkWidget *widget, GtkSnapshot *snapshot)
{
    GduBenchmarkGraph *self = GDU_BENCHMARK_GRAPH (widget);
    GraphData graph_data = { 0 };

    graph_data.benchmark_size = self->benchmark_size;
    graph_data.width = gtk_widget_get_width (GTK_WIDGET (self));
    graph_data.height = gtk_widget_get_height (GTK_WIDGET (self));
    graph_data.graph_width = graph_data.width;
    graph_data.graph_height = graph_data.height;

    gdu_benchmark_graph_draw_grid (self, snapshot, &graph_data);

    graph_data.samples = self->read_samples;
    graph_data.total_samples = self->total_transfer_samples;
    graph_data.color = &READ_CURVE_COLOR;
    draw_curve (snapshot, &graph_data);

    graph_data.samples = self->write_samples;
    graph_data.color = &WRITE_CURVE_COLOR;
    draw_curve (snapshot, &graph_data);

    graph_data.samples = self->atime_samples;
    graph_data.total_samples = self->total_atime_samples;
    graph_data.color = &ATIME_DOT_COLOR;
    draw_scatterplot (snapshot, &graph_data);
}

static gchar *
format_stats (gdouble stat, guint num_samples, gboolean is_atime)
{
    g_autofree char *s;
    g_autofree char *s2;

    s = is_atime ? g_strdup_printf ("%.2f msec", stat * 1000.0)
                 : g_strdup_printf ("%s/s", g_format_size ((guint64) stat));
    s2 = g_strdup_printf (g_dngettext (GETTEXT_PACKAGE, "%u sample", "%u samples", num_samples), num_samples);

    return g_strdup_printf ("%s <small>(%s)</small>", s, s2);
}

static BenchmarkSample *
benchmark_sample_new (BenchmarkSampleKind kind, guint64 offset, gdouble value)
{
    BenchmarkSample *sample = g_new (BenchmarkSample, 1);

    *sample = (BenchmarkSample){ .kind = kind, .offset = offset, .value = value };

    return sample;
}

static void
append_pending_samples (BenchmarkJobData *data, GduBenchmarkGraph *graph)
{
    BenchmarkSample *sample;
    GArray *samples;

    while ((sample = g_async_queue_try_pop (data->pending_samples)) != NULL) {
        switch (sample->kind) {
        case BENCHMARK_SAMPLE_READ:
            samples = graph->read_samples;
            break;
        case BENCHMARK_SAMPLE_WRITE:
            samples = graph->write_samples;
            break;
        case BENCHMARK_SAMPLE_ACCESS_TIME:
            samples = graph->atime_samples;
            break;
        default:
            g_assert_not_reached ();
        }

        g_array_append_val (samples, *sample);
        g_free (sample);
    }
}

static void
update_dialog (GduBenchmarkDialog *self, BenchmarkJobData *data)
{
    GduBenchmarkGraph *graph = GDU_BENCHMARK_GRAPH (self->benchmark_graph);
    BenchmarkStats read_stats;
    BenchmarkStats write_stats;
    BenchmarkStats atime_stats;
    g_autofree gchar *s = NULL;

    append_pending_samples (data, graph);

    graph->benchmark_size = data->benchmark_size;

    read_stats = get_max_avg (graph->read_samples);
    write_stats = get_max_avg (graph->write_samples);
    atime_stats = get_max_avg (graph->atime_samples);

    if (read_stats.avg != 0.0) {
        s = format_stats (read_stats.avg, graph->read_samples->len, FALSE);
        adw_action_row_set_subtitle (ADW_ACTION_ROW (self->read_rate_row), s);
        g_clear_pointer (&s, g_free);
    }

    if (write_stats.avg != 0.0) {
        s = format_stats (write_stats.avg, graph->write_samples->len, FALSE);
        adw_action_row_set_subtitle (ADW_ACTION_ROW (self->write_rate_row), s);
        g_clear_pointer (&s, g_free);
    }

    if (atime_stats.avg != 0.0) {
        s = format_stats (atime_stats.avg, graph->atime_samples->len, TRUE);
        adw_action_row_set_subtitle (ADW_ACTION_ROW (self->access_time_row), s);
        g_clear_pointer (&s, g_free);
    }

    gtk_widget_queue_draw (GTK_WIDGET (graph));
}

static void
benchmark_job_update (GduLocalJob *job)
{
    BenchmarkJobData *data = gdu_local_job_get_user_data (job);
    g_autoptr(GduBenchmarkDialog) self = NULL;
    guint total_samples = data->num_samples + data->num_access_samples;

    gdu_local_job_set_progress (job, (gdouble) g_atomic_int_get (&data->completed_samples) / total_samples);

    self = g_weak_ref_get (&data->dialog);
    if (self != NULL)
        update_dialog (self, data);
}

static void
queue_job_update_if_due (GduLocalJob *job, gint64 *last_update_usec)
{
    gint64 now_usec = g_get_monotonic_time ();

    if (now_usec - *last_update_usec < 200 * 1000)
        return;

    *last_update_usec = now_usec;
    gdu_local_job_queue_update (job);
}

static GError *
open_for_benchmark (BenchmarkJobData *data, GCancellable *cancellable, gint *fd)
{
    GVariantBuilder options_builder;
    GError *error = NULL;
    g_autoptr (GVariant) fd_index = NULL;
    g_autoptr (GUnixFDList) fd_list = NULL;

    g_assert (fd != NULL);

    g_variant_builder_init (&options_builder, G_VARIANT_TYPE_VARDICT);
    g_variant_builder_add (&options_builder, "{sv}", "writable", g_variant_new_boolean (data->write_benchmark));

    if (!udisks_block_call_open_for_benchmark_sync (data->block, g_variant_builder_end (&options_builder),
                                                    NULL, /* fd_list */
                                                    &fd_index, &fd_list, cancellable, &error))
        return error;

    *fd = g_unix_fd_list_get (fd_list, g_variant_get_handle (fd_index), &error);

    return error;
}

static GError *
benchmark_transfer_rate (BenchmarkJobData *data, GduLocalJob *job, GCancellable *cancellable, guchar *buffer, gint fd,
                         glong page_size, guint64 disk_size, gint64 *last_update_usec)
{
    guint n;
    gint sample_size;
    GError *error = NULL;

    g_assert (fd != -1);
    g_assert (buffer != NULL);

    sample_size = data->sample_size_mib * 1024 * 1024;

    for (n = 0; n < data->num_samples; n++) {
        g_autofree char *s = NULL;
        g_autofree char *s2 = NULL;
        gint64 begin_usec;
        gint64 end_usec;
        gint64 offset;
        gssize num_read;
        g_autofree BenchmarkSample *sample = NULL;

        if (g_cancellable_set_error_if_cancelled (cancellable, &error))
            return error;

        /* figure out offset and align to page-size */
        offset = n * disk_size / data->num_samples;
        offset &= ~(page_size - 1);

        if (lseek (fd, offset, SEEK_SET) != offset) {
            g_set_error (&error, G_IO_ERROR, g_io_error_from_errno (errno), "Error seeking to offset %lld",
                         (long long int) offset);
            return error;
        }

        if (read (fd, buffer, page_size) != page_size) {
            s = g_format_size_full (page_size, G_FORMAT_SIZE_LONG_FORMAT);
            s2 = g_format_size_full (offset, G_FORMAT_SIZE_LONG_FORMAT);
            g_set_error (&error, G_IO_ERROR, g_io_error_from_errno (errno), "Error pre-reading %s from offset %s", s,
                         s2);
            return error;
        }

        if (lseek (fd, offset, SEEK_SET) != offset) {
            s = g_format_size_full (offset, G_FORMAT_SIZE_LONG_FORMAT);
            g_set_error (&error, G_IO_ERROR, g_io_error_from_errno (errno), "Error seeking to offset %s", s);
            return error;
        }

        begin_usec = g_get_monotonic_time ();
        num_read = read (fd, buffer, sample_size);
        if (G_UNLIKELY (num_read < 0)) {
            s = g_format_size_full (sample_size, G_FORMAT_SIZE_LONG_FORMAT);
            s2 = g_format_size_full (offset, G_FORMAT_SIZE_LONG_FORMAT);
            g_set_error (&error, G_IO_ERROR, g_io_error_from_errno (errno), "Error reading %s from offset %s", s, s2);
            return error;
        }
        end_usec = g_get_monotonic_time ();

        sample = benchmark_sample_new (BENCHMARK_SAMPLE_READ, offset,
                                       ((gdouble) G_USEC_PER_SEC) * num_read / (end_usec - begin_usec));

        g_async_queue_push (data->pending_samples, g_steal_pointer (&sample));

        if (data->write_benchmark) {
            gssize num_written;

            /* and now write the same block again... */
            if (lseek (fd, offset, SEEK_SET) != offset) {
                g_set_error (&error, G_IO_ERROR, g_io_error_from_errno (errno), "Error seeking to offset %lld",
                             (long long int) offset);
                return error;
            }
            if (read (fd, buffer, page_size) != page_size) {
                g_set_error (&error, G_IO_ERROR, g_io_error_from_errno (errno),
                             "Error pre-reading %lld bytes from offset %lld", (long long int) page_size,
                             (long long int) offset);
                return error;
            }
            if (lseek (fd, offset, SEEK_SET) != offset) {
                g_set_error (&error, G_IO_ERROR, g_io_error_from_errno (errno), "Error seeking to offset %lld",
                             (long long int) offset);
                return error;
            }

            begin_usec = g_get_monotonic_time ();
            num_written = write (fd, buffer, num_read);
            if (G_UNLIKELY (num_written < 0)) {
                g_set_error (&error, G_IO_ERROR, g_io_error_from_errno (errno),
                             "Error writing %lld bytes at offset %lld: %m", (long long int) num_read,
                             (long long int) offset);
                return error;
            }

            if (num_written != num_read) {
                g_set_error (&error, G_IO_ERROR, g_io_error_from_errno (errno),
                             "Expected to write %lld bytes, only wrote %lld: %m", (long long int) num_read,
                             (long long int) num_written);
                return error;
            }

            if (fsync (fd) != 0) {
                g_set_error (&error, G_IO_ERROR, g_io_error_from_errno (errno), "Error syncing (at offset %lld): %m",
                             (long long int) offset);
                return error;
            }
            end_usec = g_get_monotonic_time ();

            sample = benchmark_sample_new (BENCHMARK_SAMPLE_WRITE, offset,
                                           ((gdouble) G_USEC_PER_SEC) * num_written / (end_usec - begin_usec));

            g_async_queue_push (data->pending_samples, g_steal_pointer (&sample));
        }
        g_atomic_int_inc (&data->completed_samples);
        queue_job_update_if_due (job, last_update_usec);
    }

    return NULL;
}

static GError *
benchmark_access_time (BenchmarkJobData *data, GduLocalJob *job, GCancellable *cancellable, guchar *buffer, gint fd,
                       glong page_size, guint64 disk_size, gint64 *last_update_usec)
{
    guint n;
    GError *error = NULL;
    gdouble prev_offset = 0;
    g_autoptr (GRand) rand = NULL;

    g_assert (buffer != NULL);
    g_assert (fd != -1);

    rand = g_rand_new_with_seed (42); /* want this to be deterministic (per size) so it's repeatable */

    for (n = 0; n < data->num_access_samples; n++) {
        gint64 begin_usec;
        gint64 end_usec;
        gint64 offset;
        gssize num_read;
        g_autofree BenchmarkSample *sample = NULL;

        if (g_cancellable_set_error_if_cancelled (cancellable, &error)) {
            return error;
        }

        offset = (guint64) g_rand_double_range (rand, 0, (gdouble) disk_size);
        offset &= ~(page_size - 1);

        if (lseek (fd, offset, SEEK_SET) != offset) {
            g_set_error (&error, G_IO_ERROR, g_io_error_from_errno (errno),
                         C_("benchmarking", "Error seeking to offset %lld: %m"), (long long int) offset);
            return error;
        }

        begin_usec = g_get_monotonic_time ();
        num_read = read (fd, buffer, page_size);
        if (G_UNLIKELY (num_read < 0)) {
            g_set_error (&error, G_IO_ERROR, g_io_error_from_errno (errno),
                         C_("benchmarking", "Error reading %lld bytes from offset %lld"), (long long int) page_size,
                            (long long int) offset);
            return error;
        }
        end_usec = g_get_monotonic_time ();

        sample = benchmark_sample_new (BENCHMARK_SAMPLE_ACCESS_TIME, offset,
                                       (end_usec - begin_usec) / ((gdouble) G_USEC_PER_SEC));

        {
            gdouble sample_offset = sample->offset;
            if (n != 0) {
                sample->offset = fabs (sample->offset - prev_offset);
                g_async_queue_push (data->pending_samples, g_steal_pointer (&sample));
            }
            prev_offset = sample_offset;
        }

        g_atomic_int_inc (&data->completed_samples);
        queue_job_update_if_due (job, last_update_usec);
    }

    return NULL;
}

static GduLocalJobResult
benchmark_job_result (GError *error, GError **out_error)
{
    g_autoptr(GError) owned_error = error;

    if (owned_error == NULL)
        return GDU_LOCAL_JOB_RESULT_SUCCESS;

    if (g_error_matches (owned_error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
        return GDU_LOCAL_JOB_RESULT_CANCELLED;

    *out_error = g_steal_pointer (&owned_error);
    return GDU_LOCAL_JOB_RESULT_ERROR;
}

static GduLocalJobResult
benchmark_job_run (GduLocalJob *job, GCancellable *cancellable, GError **out_error)
{
    BenchmarkJobData *data = gdu_local_job_get_user_data (job);
    g_autoptr(GError) error = NULL;
    guchar *buffer = NULL;
    g_autofree guchar *buffer_unaligned = NULL;
    g_autofd gint fd = -1;
    gint64 last_update_usec = 0;
    glong page_size;
    guint64 disk_size;

    error = open_for_benchmark (data, cancellable, &fd);
    if (error != NULL)
        return benchmark_job_result (g_steal_pointer (&error), out_error);

    /* We can't use udisks_block_get_size() because the media may have
     * changed and udisks may not have noticed. TODO: maybe have a
     * Block.GetSize() method instead...
     */
    if (ioctl (fd, BLKGETSIZE64, &disk_size) != 0) {
        g_set_error (&error, G_IO_ERROR, g_io_error_from_errno (errno), "Error getting size of device: %m");
        return benchmark_job_result (g_steal_pointer (&error), out_error);
    }

    page_size = sysconf (_SC_PAGESIZE);
    if (page_size < 1) {
        g_set_error (&error, G_IO_ERROR, g_io_error_from_errno (errno), "Error getting page size: %m\n");
        return benchmark_job_result (g_steal_pointer (&error), out_error);
    }

    buffer_unaligned = g_new0 (guchar, data->sample_size_mib * 1024 * 1024 + page_size);
    buffer = (guchar *) (((gintptr) (buffer_unaligned + page_size)) & (~(page_size - 1)));

    data->benchmark_size = disk_size;
    gdu_local_job_queue_update (job);

    error = benchmark_transfer_rate (data, job, cancellable, buffer, fd, page_size, disk_size, &last_update_usec);
    if (error != NULL)
        return benchmark_job_result (g_steal_pointer (&error), out_error);

    error = benchmark_access_time (data, job, cancellable, buffer, fd, page_size, disk_size, &last_update_usec);

    return benchmark_job_result (g_steal_pointer (&error), out_error);
}

static void
benchmark_job_completed (GduLocalJob *job, GduLocalJobResult result, GError *error)
{
    BenchmarkJobData *data = gdu_local_job_get_user_data (job);
    g_autoptr(GduBenchmarkDialog) self = NULL;

    if (data->inhibit_cookie != 0) {
        gtk_application_uninhibit ((gpointer) g_application_get_default (), data->inhibit_cookie);
        data->inhibit_cookie = 0;
    }

    self = g_weak_ref_get (&data->dialog);
    if (self == NULL)
        return;

    update_dialog (self, data);
    gtk_widget_set_visible (self->cancel_button, FALSE);

    if (result == GDU_LOCAL_JOB_RESULT_ERROR) {
        gdu_utils_show_error (gdu_benchmark_dialog_get_window (self), _("An error occurred"), error);
        adw_action_row_set_subtitle (ADW_ACTION_ROW (self->sample_size_action_row), "–");
        adw_action_row_set_subtitle (ADW_ACTION_ROW (self->read_rate_row), "–");
        adw_action_row_set_subtitle (ADW_ACTION_ROW (self->write_rate_row), "–");
        adw_action_row_set_subtitle (ADW_ACTION_ROW (self->access_time_row), "–");
    }

    g_clear_object (&self->job);
}

static void
on_cancel_clicked_cb (GduBenchmarkDialog *self)
{
    if (self->job != NULL)
        gdu_local_job_request_cancel (self->job);
}

static void
start_benchmark (GduBenchmarkDialog *self)
{
    GduBenchmarkGraph *graph = GDU_BENCHMARK_GRAPH (self->benchmark_graph);
    GduJobManager *job_manager;
    g_autoptr(BenchmarkJobData) data = NULL;
    g_autoptr(GduLocalJob) job = NULL;
    gint sample_size = 0;
    g_autofree char *s = NULL;

    data = benchmark_job_data_new (self);

    graph->total_transfer_samples = data->num_samples;
    graph->total_atime_samples = data->num_access_samples;

    sample_size = data->sample_size_mib * 1024 * 1024;

    if (sample_size != 0) {
        s = g_format_size_full (sample_size, G_FORMAT_SIZE_IEC_UNITS | G_FORMAT_SIZE_LONG_FORMAT);
        adw_action_row_set_subtitle (ADW_ACTION_ROW (self->sample_size_action_row), s);
    }

    job = gdu_local_job_new (self->object, "x-gdu-benchmark",
                             _("Benchmarking"), benchmark_job_run, benchmark_job_update, benchmark_job_completed,
                               g_steal_pointer (&data), (GDestroyNotify) benchmark_job_data_free);
    gdu_local_job_set_cancelable (job, TRUE);
    gdu_local_job_set_progress_valid (job, TRUE);
    self->job = g_object_ref (job);

    job_manager = gdu_application_get_job_manager ();
    if (!gdu_job_manager_enqueue (job_manager, g_steal_pointer (&job))) {
        g_warning ("Failed to enqueue benchmark job");
        g_clear_object (&self->job);
        gtk_widget_set_visible (self->cancel_button, FALSE);
    }
}

static void
ensure_unused_cb (GtkWindow *window, GAsyncResult *res, gpointer user_data)
{
    g_autoptr(GduBenchmarkDialog) self = user_data;

    if (gdu_utils_ensure_unused_finish (self->client, res, NULL))
        start_benchmark (self);
    else
        gtk_widget_set_visible (self->cancel_button, FALSE);
}

static void
on_start_clicked_cb (GduBenchmarkDialog *self, GtkButton *button)
{
    gboolean write_benchmark;

    g_assert (self->job == NULL);

    gdu_benchmark_dialog_save_options (self);

    write_benchmark = g_settings_get_boolean (self->settings, "do-write");

    /* ensure the device is unused (e.g. unmounted) before formatting it... */
    if (write_benchmark)
        gdu_utils_ensure_unused (self->client, gdu_benchmark_dialog_get_window (self), self->object,
                                 (GAsyncReadyCallback) ensure_unused_cb, NULL, /* GCancellable */
                                 g_object_ref (self));
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
    adw_window_title_set_subtitle (ADW_WINDOW_TITLE (self->window_title), udisks_object_info_get_one_liner (info));
}

static gboolean
set_sample_size_unit_cb (AdwSpinRow *spin_row, gpointer *user_data)
{
    GtkAdjustment *adjustment;
    g_autofree char *unit = NULL;

    adjustment = adw_spin_row_get_adjustment (spin_row);
    unit = g_strdup_printf ("%.2f MiB", gtk_adjustment_get_value (adjustment));
    gtk_editable_set_text (GTK_EDITABLE (spin_row), unit);

    return TRUE;
}

static void
gdu_benchmark_graph_dispose (GObject *object)
{
    GduBenchmarkGraph *self = GDU_BENCHMARK_GRAPH (object);

    g_clear_pointer (&self->read_samples, g_array_unref);
    g_clear_pointer (&self->write_samples, g_array_unref);
    g_clear_pointer (&self->atime_samples, g_array_unref);

    G_OBJECT_CLASS (gdu_benchmark_graph_parent_class)->dispose (object);
}

static void
gdu_benchmark_graph_init (GduBenchmarkGraph *self)
{
    self->read_samples = g_array_new (FALSE, FALSE, sizeof (BenchmarkSample));
    self->write_samples = g_array_new (FALSE, FALSE, sizeof (BenchmarkSample));
    self->atime_samples = g_array_new (FALSE, FALSE, sizeof (BenchmarkSample));

    gtk_widget_set_size_request (GTK_WIDGET (self), -1, 279);
}

static void
gdu_benchmark_graph_class_init (GduBenchmarkGraphClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

    object_class->dispose = gdu_benchmark_graph_dispose;

    widget_class->snapshot = gdu_benchmark_graph_snapshot;
}

static void
gdu_benchmark_dialog_finalize (GObject *object)
{
    GduBenchmarkDialog *self = GDU_BENCHMARK_DIALOG (object);

    g_clear_object (&self->job);
    g_clear_object (&self->settings);
    g_clear_object (&self->object);
    g_clear_object (&self->parent_window);

    G_OBJECT_CLASS (gdu_benchmark_dialog_parent_class)->finalize (object);
}

void
gdu_benchmark_dialog_class_init (GduBenchmarkDialogClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

    object_class->finalize = gdu_benchmark_dialog_finalize;

    gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/DiskUtility/ui/"
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
}

void
gdu_benchmark_dialog_show (GtkWindow *parent_window, UDisksObject *object, UDisksClient *client)
{
    GduBenchmarkDialog *self;

    self = g_object_new (GDU_TYPE_BENCHMARK_DIALOG, NULL);
    self->object = g_object_ref (object);
    self->parent_window = g_object_ref (parent_window);
    self->block = udisks_object_peek_block (self->object);
    self->client = client;

    gdu_benchmark_dialog_set_title (self);
    gdu_benchmark_dialog_load_options (self);

    /* if device is read-only, uncheck the "perform write-test"
     * check-button and also make it insensitive
     */
    if (udisks_block_get_read_only (self->block)) {
        adw_switch_row_set_active (ADW_SWITCH_ROW (self->write_bench_switch), FALSE);
        gtk_widget_set_sensitive (self->write_bench_switch, FALSE);
    }

    /* If the device is currently in use, uncheck the "perform write-test" check-button */
    if (gdu_utils_is_in_use (self->client, self->object))
        adw_switch_row_set_active (ADW_SWITCH_ROW (self->write_bench_switch), FALSE);

    adw_dialog_present (ADW_DIALOG (self), GTK_WIDGET (parent_window));
}
