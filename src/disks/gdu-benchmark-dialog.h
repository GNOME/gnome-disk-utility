/*
 * Copyright (C) 2008-2013 Red Hat, Inc.
 *
 * Licensed under GPL version 2 or later.
 *
 * Author: David Zeuthen <zeuthen@gmail.com>
 */

#pragma once

#include <gtk/gtk.h>

#include "gdutypes.h"

G_BEGIN_DECLS

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

static const guint GRAPH_CURVE_WIDTH = 2;
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

#define GDU_TYPE_BENCHMARK_SAMPLE (gdu_benchmark_sample_get_type ())
G_DECLARE_FINAL_TYPE (GduBenchmarkSample, gdu_benchmark_sample, GDU, BENCHMARK_SAMPLE, GObject)

#define GDU_TYPE_BENCHMARK_GRAPH (gdu_benchmark_graph_get_type ())
G_DECLARE_FINAL_TYPE (GduBenchmarkGraph, gdu_benchmark_graph, GDU, BENCHMARK_GRAPH, AdwBin)

#define GDU_TYPE_BENCHMARK_DIALOG (gdu_benchmark_dialog_get_type ())
G_DECLARE_FINAL_TYPE (GduBenchmarkDialog, gdu_benchmark_dialog, GDU, BENCHMARK_DIALOG, AdwDialog)

void gdu_benchmark_dialog_show (GtkWindow *window, UDisksObject *object, UDisksClient *client);

G_END_DECLS
