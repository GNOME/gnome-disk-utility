/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*- */
/* gdu-graph.c
 *
 * Copyright (C) 2009 David Zeuthen
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 */

#include <config.h>
#include <glib/gi18n.h>
#include <string.h>
#include <math.h>

#include "gdu-graph.h"

/* ---------------------------------------------------------------------------------------------------- */

typedef struct
{
        gchar    *id;
        GdkColor *color;
        GArray   *points;
} Curve;

static Curve *
curve_new (const gchar *curve_id,
           GdkColor    *color,
           GArray      *points)
{
        Curve *c;

        c = g_new0 (Curve, 1);
        c->id = g_strdup (curve_id);
        c->color = gdk_color_copy (color);
        c->points = g_array_ref (points);

        return c;
}

static void
curve_free (Curve *c)
{
        g_free (c->id);
        gdk_color_free (c->color);
        g_array_unref (c->points);
        g_free (c);
}

/* ---------------------------------------------------------------------------------------------------- */

struct GduGraphPrivate
{
        guint foo;

        gchar **x_markers;
        gchar **y_markers_left;
        gchar **y_markers_right;

        GPtrArray *curves;
        GPtrArray *bands;
};

G_DEFINE_TYPE (GduGraph, gdu_graph, GTK_TYPE_DRAWING_AREA)

static gboolean gdu_graph_expose_event (GtkWidget      *widget,
                                        GdkEventExpose *event);

enum
{
        PROP_0,
        PROP_X_MARKERS,
        PROP_Y_MARKERS_LEFT,
        PROP_Y_MARKERS_RIGHT,
};

static void
gdu_graph_set_property (GObject      *object,
                        guint         prop_id,
                        const GValue *value,
                        GParamSpec   *pspec)
{
        GduGraph *graph = GDU_GRAPH (object);

        switch (prop_id) {
        case PROP_X_MARKERS:
                gdu_graph_set_x_markers (graph, g_value_get_boxed (value));
                break;

        case PROP_Y_MARKERS_LEFT:
                gdu_graph_set_y_markers_left (graph, g_value_get_boxed (value));
                break;

        case PROP_Y_MARKERS_RIGHT:
                gdu_graph_set_y_markers_right (graph, g_value_get_boxed (value));
                break;

        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static void
gdu_graph_get_property (GObject     *object,
                        guint        prop_id,
                        GValue      *value,
                        GParamSpec  *pspec)
{
        GduGraph *graph = GDU_GRAPH (object);

        switch (prop_id) {
        case PROP_X_MARKERS:
                g_value_set_boxed (value, graph->priv->x_markers);
                break;

        case PROP_Y_MARKERS_LEFT:
                g_value_set_boxed (value, graph->priv->y_markers_left);
                break;

        case PROP_Y_MARKERS_RIGHT:
                g_value_set_boxed (value, graph->priv->y_markers_right);
                break;

        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
    }
}

static void
gdu_graph_finalize (GObject *object)
{
        GduGraph *graph = GDU_GRAPH (object);

        g_strfreev (graph->priv->x_markers);
        g_strfreev (graph->priv->y_markers_left);
        g_strfreev (graph->priv->y_markers_right);

        g_ptr_array_unref (graph->priv->curves);
        g_ptr_array_unref (graph->priv->bands);

        if (G_OBJECT_CLASS (gdu_graph_parent_class)->finalize != NULL)
                G_OBJECT_CLASS (gdu_graph_parent_class)->finalize (object);
}

static void
gdu_graph_class_init (GduGraphClass *klass)
{
        GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
        GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

        gobject_class->finalize     = gdu_graph_finalize;
        gobject_class->set_property = gdu_graph_set_property;
        gobject_class->get_property = gdu_graph_get_property;

        widget_class->expose_event  = gdu_graph_expose_event;

        g_type_class_add_private (klass, sizeof (GduGraphPrivate));

        g_object_class_install_property (gobject_class,
                                         PROP_X_MARKERS,
                                         g_param_spec_boxed ("x-markers",
                                                             _("X Markers"),
                                                             _("Markers to print on the X axis"),
                                                             G_TYPE_STRV,
                                                             G_PARAM_READABLE |
                                                             G_PARAM_WRITABLE |
                                                             G_PARAM_CONSTRUCT));

        g_object_class_install_property (gobject_class,
                                         PROP_Y_MARKERS_LEFT,
                                         g_param_spec_boxed ("y-markers-left",
                                                             _("Left Y Markers"),
                                                             _("Markers to print on the left Y axis"),
                                                             G_TYPE_STRV,
                                                             G_PARAM_READABLE |
                                                             G_PARAM_WRITABLE |
                                                             G_PARAM_CONSTRUCT));

        g_object_class_install_property (gobject_class,
                                         PROP_Y_MARKERS_RIGHT,
                                         g_param_spec_boxed ("y-markers-right",
                                                             _("Right Y Markers"),
                                                             _("Markers to print on the right Y axis"),
                                                             G_TYPE_STRV,
                                                             G_PARAM_READABLE |
                                                             G_PARAM_WRITABLE |
                                                             G_PARAM_CONSTRUCT));
}

static void
gdu_graph_init (GduGraph *graph)
{
        graph->priv = G_TYPE_INSTANCE_GET_PRIVATE (graph, GDU_TYPE_GRAPH, GduGraphPrivate);
        graph->priv->curves = g_ptr_array_new_with_free_func ((GDestroyNotify) curve_free);
        graph->priv->bands  = g_ptr_array_new_with_free_func ((GDestroyNotify) curve_free);
}

GtkWidget *
gdu_graph_new (void)
{
        return GTK_WIDGET (g_object_new (GDU_TYPE_GRAPH, NULL));
}

gchar **
gdu_graph_get_x_markers (GduGraph *graph)
{
        g_return_val_if_fail (GDU_IS_GRAPH (graph), NULL);
        return g_strdupv (graph->priv->x_markers);
}

gchar **
gdu_graph_get_y_markers_left (GduGraph *graph)
{
        g_return_val_if_fail (GDU_IS_GRAPH (graph), NULL);
        return g_strdupv (graph->priv->y_markers_left);
}

gchar **
gdu_graph_get_y_markers_right (GduGraph *graph)
{
        g_return_val_if_fail (GDU_IS_GRAPH (graph), NULL);
        return g_strdupv (graph->priv->y_markers_right);
}

void
gdu_graph_set_x_markers (GduGraph           *graph,
                         const gchar* const *markers)
{
        g_return_if_fail (GDU_IS_GRAPH (graph));
        g_strfreev (graph->priv->x_markers);
        graph->priv->x_markers = g_strdupv ((gchar **) markers);
        if (GTK_WIDGET (graph)->window != NULL)
                gdk_window_invalidate_rect (GTK_WIDGET (graph)->window, NULL, TRUE);
}

void
gdu_graph_set_y_markers_left (GduGraph           *graph,
                              const gchar* const *markers)
{
        g_return_if_fail (GDU_IS_GRAPH (graph));
        g_strfreev (graph->priv->y_markers_left);
        graph->priv->y_markers_left = g_strdupv ((gchar **) markers);
        if (GTK_WIDGET (graph)->window != NULL)
                gdk_window_invalidate_rect (GTK_WIDGET (graph)->window, NULL, TRUE);
}

void
gdu_graph_set_y_markers_right (GduGraph           *graph,
                               const gchar* const *markers)
{
        g_return_if_fail (GDU_IS_GRAPH (graph));
        g_strfreev (graph->priv->y_markers_right);
        graph->priv->y_markers_right = g_strdupv ((gchar **) markers);
        if (GTK_WIDGET (graph)->window != NULL)
                gdk_window_invalidate_rect (GTK_WIDGET (graph)->window, NULL, TRUE);
}

static gdouble
measure_width (cairo_t     *cr,
               const gchar *s)
{
        cairo_text_extents_t te;
        cairo_select_font_face (cr,
                                "sans",
                                CAIRO_FONT_SLANT_NORMAL,
                                CAIRO_FONT_WEIGHT_NORMAL);
        cairo_set_font_size (cr, 8.0);
        cairo_text_extents (cr, s, &te);
        return te.width;
}

static gboolean
gdu_graph_expose_event (GtkWidget      *widget,
                        GdkEventExpose *event)
{
        GduGraph *graph = GDU_GRAPH (widget);
        guint n_x_markers;
        guint n_y_markers_left;
        guint n_y_markers_right;
        cairo_t *cr;
        gdouble width, height;
        guint n;

        n_x_markers = graph->priv->x_markers != NULL ? g_strv_length (graph->priv->x_markers) : 0;
        n_y_markers_left = graph->priv->y_markers_left != NULL ? g_strv_length (graph->priv->y_markers_left) : 0;
        n_y_markers_right = graph->priv->y_markers_right != NULL ? g_strv_length (graph->priv->y_markers_right) : 0;

        width = widget->allocation.width;
        height = widget->allocation.height;

        cr = gdk_cairo_create (widget->window);
        cairo_rectangle (cr,
                         event->area.x, event->area.y,
                         event->area.width, event->area.height);
        cairo_clip (cr);

        double gx, gy, gw, gh;
        gx = 0;
        gy = 10;
        gw = width - 10;
        gh = height - gy - 30;

        guint twidth;

        /* measure text on the left y-axis */
        guint y_left_max_width;
        y_left_max_width = 0;
        for (n = 0; n < n_y_markers_left; n++) {
                twidth = ceil (measure_width (cr, graph->priv->y_markers_left[n]));
                if (twidth > y_left_max_width)
                        y_left_max_width = twidth;
        }
        /* include half width of first xmarker label */
        if (n_x_markers > 0) {
                twidth = ceil (measure_width (cr, graph->priv->x_markers[0]));
                if (twidth/2 > y_left_max_width)
                        y_left_max_width = twidth/2;
        }
        y_left_max_width += 6; /* padding */
        gx += y_left_max_width;
        gw -= y_left_max_width;

        /* measure text on the right y-axis */
        guint y_right_max_width;
        y_right_max_width = 0;
        for (n = 0; n < n_y_markers_right; n++) {
                twidth = ceil (measure_width (cr, graph->priv->y_markers_right[n]));
                if (twidth/2 > y_right_max_width)
                        y_right_max_width = twidth/2;
        }
        /* include half width of last xmarker label */
        if (n_x_markers > 0) {
                twidth = ceil (measure_width (cr, graph->priv->x_markers[n_x_markers - 1]));
                y_right_max_width += twidth/2;
        }
        y_right_max_width += 6; /* padding */
        gw -= y_right_max_width;

        /* draw the box to draw in */
        cairo_set_source_rgb (cr, 1, 1, 1);
        cairo_rectangle (cr, gx, gy, gw, gh);
        cairo_set_line_width (cr, 0.0);
	cairo_fill (cr);

        /* draw markers on the left y-axis */
        for (n = 0; n < n_y_markers_left; n++) {
                double pos;

                pos = ceil (gy + gh / (n_y_markers_left - 1) * n);

                const char *s;
                s = graph->priv->y_markers_left[n_y_markers_left - 1 - n];

                cairo_text_extents_t te;
                cairo_set_source_rgb (cr, 0.0, 0.0, 0.0);
                cairo_select_font_face (cr, "sans",
                                        CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
                cairo_set_font_size (cr, 8.0);
                cairo_text_extents (cr, s, &te);
                cairo_move_to (cr,
                               gx/2.0 - 3 - te.width/2  - te.x_bearing,
                               pos - te.height/2 - te.y_bearing);

                cairo_show_text (cr, s);

                cairo_set_line_width (cr, 1.0);
                double dashes[1] = {2.0};
                cairo_set_dash (cr, dashes, 1, 0.0);
                cairo_move_to (cr,
                               gx - 0.5,
                               pos - 0.5);
                cairo_line_to (cr,
                               gx - 0.5 + gw,
                               pos - 0.5);
                cairo_stroke (cr);
        }

        /* draw markers on the right y-axis */
        for (n = 0; n < n_y_markers_right; n++) {
                double pos;

                pos = ceil (gy + gh / (n_y_markers_right - 1) * n);

                const char *s;
                s = graph->priv->y_markers_right[n_y_markers_right - 1 - n];

                cairo_text_extents_t te;
                cairo_set_source_rgb (cr, 0.0, 0.0, 0.0);
                cairo_select_font_face (cr, "sans",
                                        CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
                cairo_set_font_size (cr, 8.0);
                cairo_text_extents (cr, s, &te);
                cairo_move_to (cr,
                               gx + gw + y_right_max_width/2.0 + 3 - te.width/2  - te.x_bearing,
                               pos - te.height/2 - te.y_bearing);

                cairo_show_text (cr, s);
        }

        guint64 t_left;
        guint64 t_right;
        GTimeVal now;

        g_get_current_time (&now);
        /*
        switch (0) {
        default:
        case 0:
                t_left = now.tv_sec - 6 * 60 * 60;
                break;
        case 1:
                t_left = now.tv_sec - 24 * 60 * 60;
                break;
        case 2:
                t_left = now.tv_sec - 3 * 24 * 60 * 60;
                break;
        case 3:
                t_left = now.tv_sec - 12 * 24 * 60 * 60;
                break;
        case 4:
                t_left = now.tv_sec - 36 * 24 * 60 * 60;
                break;
        case 5:
                t_left = now.tv_sec - 96 * 24 * 60 * 60;
                break;
        }
        t_right = now.tv_sec;
        */
        t_left = now.tv_sec - 6 * 24 * 60 * 60;
        t_right = now.tv_sec;

        /* draw time markers on x-axis */
        for (n = 0; n < n_x_markers; n++) {
                double pos;
                guint64 val;
                const gchar *s;
                cairo_text_extents_t te;
                double dashes[1] = {2.0};

                s = graph->priv->x_markers[n];

                pos = ceil (gx + gw / (n_x_markers - 1) * n);
                val = t_left + (t_right - t_left) * n / (n_x_markers - 1);

                cairo_set_source_rgb (cr, 0.0, 0.0, 0.0);
                cairo_select_font_face (cr, "sans",
                                        CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
                cairo_set_font_size (cr, 8.0);
                cairo_text_extents (cr, s, &te);
                cairo_move_to (cr,
                               pos - te.width/2  - te.x_bearing,
                               height - 30.0/2  - te.height/2 - te.y_bearing); /* TODO */

                cairo_show_text (cr, s);

                cairo_set_line_width (cr, 1.0);
                cairo_set_dash (cr, dashes, 1, 0.0);
                cairo_move_to (cr,
                               pos - 0.5,
                               gy - 0.5);
                cairo_line_to (cr,
                               pos - 0.5,
                               gy - 0.5 + gh);
                cairo_stroke (cr);
        }

        /* clip to the graph area */
        cairo_rectangle (cr, gx, gy, gw, gh);
        cairo_clip (cr);

        /* draw all bands */
        for (n = 0; n < graph->priv->bands->len; n++) {
                Curve *c = (Curve *) graph->priv->bands->pdata[n];
                guint m;

                cairo_new_path (cr);
                cairo_set_line_width (cr, 0.0);
                cairo_set_source_rgba (cr, 0.5, 0.5, 0.5, 0.5);

                for (m = 0; m < c->points->len; m+= 2) {
                        GduGraphPoint *point;
                        gdouble x0, x1;
                        gdouble x, width;
                        cairo_pattern_t *pat;

                        point = &g_array_index (c->points, GduGraphPoint, m);
                        x0 = gx + gw * point->x;

                        point = &g_array_index (c->points, GduGraphPoint, m + 1);
                        x1 = gx + gw * point->x;

                        x = x0;
                        if (x1 < x0)
                                x = x1;
                        width = fabs (x1 - x0);

                        g_debug ("band: %f to %f", x0, x1);

                        pat = cairo_pattern_create_linear (x, gy,
                                                           x + width, gy);
                        cairo_pattern_add_color_stop_rgba (pat, 0.00, 1.00, 1.00, 1.00, 0.00);
                        cairo_pattern_add_color_stop_rgba (pat, 0.35, 0.85, 0.85, 0.85, 0.50);
                        cairo_pattern_add_color_stop_rgba (pat, 0.65, 0.85, 0.85, 0.85, 0.50);
                        cairo_pattern_add_color_stop_rgba (pat, 1.00, 1.00, 1.00, 1.00, 0.00);
                        cairo_set_source (cr, pat);
                        cairo_pattern_destroy (pat);

                        cairo_rectangle (cr, x, gy, width, gh);
                        cairo_fill (cr);
                }
        }

        /* draw all curves */
        for (n = 0; n < graph->priv->curves->len; n++) {
                Curve *c = (Curve *) graph->priv->curves->pdata[n];
                guint m;

                cairo_new_path (cr);
                cairo_set_dash (cr, NULL, 0, 0.0);
                cairo_set_line_width (cr, 1.0);
                gdk_cairo_set_source_color (cr, c->color);

                for (m = 0; m < c->points->len; m++) {
                        GduGraphPoint *point;
                        gdouble x, y;

                        point = &g_array_index (c->points, GduGraphPoint, m);

                        x = gx + gw * point->x;
                        y = gy + gh * (1.0f - point->y);

                        if (y < gy + 1.0)
                                y = gy;

                        if (y > gy + gh - 1.0)
                                y = gy + gh - 1.0;

                        cairo_line_to (cr, x, y);
                }
                cairo_stroke (cr);
        }

        /* propagate event further */
        return FALSE;
}

gboolean
gdu_graph_remove_curve (GduGraph           *graph,
                        const gchar        *curve_id)
{
        guint n;
        gboolean found;

        found = FALSE;
        for (n = 0; n < graph->priv->curves->len; n++) {
                Curve *c = (Curve *) graph->priv->curves->pdata[n];
                if (g_strcmp0 (curve_id, c->id) == 0) {
                        g_ptr_array_remove_index (graph->priv->curves, n);
                        found = TRUE;
                        break;
                }
        }

        return found;
}

void
gdu_graph_set_curve (GduGraph           *graph,
                     const gchar        *curve_id,
                     GdkColor           *color,
                     GArray             *points)
{
        g_return_if_fail (GDU_IS_GRAPH (graph));
        g_return_if_fail (curve_id != NULL);
        g_return_if_fail (color != NULL);

        if (points == NULL) {
                gdu_graph_remove_curve (graph, curve_id);
        } else {
                gdu_graph_remove_curve (graph, curve_id);
                g_ptr_array_add (graph->priv->curves,
                                 curve_new (curve_id,
                                            color,
                                            points));
        }

        if (GTK_WIDGET (graph)->window != NULL)
                gdk_window_invalidate_rect (GTK_WIDGET (graph)->window, NULL, TRUE);
}

gboolean
gdu_graph_remove_band (GduGraph           *graph,
                       const gchar        *band_id)
{
        guint n;
        gboolean found;

        found = FALSE;
        for (n = 0; n < graph->priv->bands->len; n++) {
                Curve *c = (Curve *) graph->priv->bands->pdata[n];
                if (g_strcmp0 (band_id, c->id) == 0) {
                        g_ptr_array_remove_index (graph->priv->bands, n);
                        found = TRUE;
                        break;
                }
        }

        return found;
}

void
gdu_graph_set_band (GduGraph           *graph,
                    const gchar        *band_id,
                    GdkColor           *color,
                    GArray             *points)
{
        g_return_if_fail (GDU_IS_GRAPH (graph));
        g_return_if_fail (band_id != NULL);
        g_return_if_fail (color != NULL);

        if (points == NULL) {
                gdu_graph_remove_band (graph, band_id);
        } else {
                gdu_graph_remove_band (graph, band_id);
                g_ptr_array_add (graph->priv->bands,
                                 curve_new (band_id,
                                            color,
                                            points));
        }

        if (GTK_WIDGET (graph)->window != NULL)
                gdk_window_invalidate_rect (GTK_WIDGET (graph)->window, NULL, TRUE);
}
