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

#include "gdu-curve.h"
#include "gdu-graph.h"

/* ---------------------------------------------------------------------------------------------------- */

struct GduGraphPrivate
{
        guint foo;

        gchar **x_markers;
        gchar **y_markers_left;
        gchar **y_markers_right;

        GHashTable *curves;
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

        g_hash_table_unref (graph->priv->curves);

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
        graph->priv->curves = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_object_unref);
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

static gdouble
measure_height (cairo_t     *cr,
                const gchar *s)
{
        cairo_text_extents_t te;
        cairo_select_font_face (cr,
                                "sans",
                                CAIRO_FONT_SLANT_NORMAL,
                                CAIRO_FONT_WEIGHT_NORMAL);
        cairo_set_font_size (cr, 8.0);
        cairo_text_extents (cr, s, &te);
        return te.height;
}

static void
set_fade_edges_pattern (cairo_t   *cr,
                        gdouble    x0,
                        gdouble    x1,
                        GduColor  *color)
{
        cairo_pattern_t *pattern;

        pattern = cairo_pattern_create_linear (x0, 0,
                                               x1, 0);
        cairo_pattern_add_color_stop_rgba (pattern, 0.00,
                                           1.00,
                                           1.00,
                                           1.00,
                                           0.00);
        cairo_pattern_add_color_stop_rgba (pattern, 0.35,
                                           color->red,
                                           color->green,
                                           color->blue,
                                           color->alpha);
        cairo_pattern_add_color_stop_rgba (pattern, 0.65,
                                           color->red,
                                           color->green,
                                           color->blue,
                                           color->alpha);
        cairo_pattern_add_color_stop_rgba (pattern, 1.00,
                                           1.00,
                                           1.00,
                                           1.00,
                                           0.00);
        cairo_set_source (cr, pattern);
        cairo_pattern_destroy (pattern);
}

static gint
compute_all_legends_width (cairo_t *cr,
                           gdouble  lb_width,
                           GList   *curve_list)
{
        GList *l;
        gint width;

        width = 0;
        for (l = curve_list; l != NULL; l = l->next) {
                GduCurve *c = GDU_CURVE (l->data);
                cairo_text_extents_t te;
                const gchar *text;

                text = gdu_curve_get_legend (c);

                if (text == NULL)
                        continue;

                cairo_select_font_face (cr, "sans",
                                        CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
                cairo_set_font_size (cr, 8.0);
                cairo_text_extents (cr, text, &te);

                width += lb_width + 3 + ceil (te.width) + 12;
        }

        return width;
}

static gint
curve_z_order_sort (GduCurve *a,
                    GduCurve *b)
{
        return gdu_curve_get_z_order (a) - gdu_curve_get_z_order (b);
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
        gdouble x, y;
        guint twidth;
        guint theight;
        guint left_margin;
        guint right_margin;
        guint top_margin;
        guint bottom_margin;
        guint x_markers_height;
        double gx, gy, gw, gh;
        guint64 t_left;
        guint64 t_right;
        GTimeVal now;
        GList *curve_list;
        GList *l;
        gdouble lb_width;
        gdouble lb_height;
        gdouble lb_padding;
        gdouble lb_xpos;
        gdouble lb_ypos;

        curve_list = g_hash_table_get_values (graph->priv->curves);
        curve_list = g_list_sort (curve_list,
                                  (GCompareFunc) curve_z_order_sort);

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

        gx = 0;
        gy = 0;
        gw = width;
        gh = height;

        top_margin = 10;

        /* measure text of all x markers */
        bottom_margin = 0;
        for (n = 0; n < n_x_markers; n++) {
                theight = ceil (measure_height (cr, graph->priv->x_markers[n]));
                if (theight > bottom_margin)
                        bottom_margin = theight;
        }
        bottom_margin += 12; /* padding */
        x_markers_height = bottom_margin;

        /* compute how much size we need for legends */
        bottom_margin += 6; /* padding */
        lb_height = 14;
        lb_width = 23; /* golden ratio */
        lb_padding = 6;
        bottom_margin += lb_height + lb_padding;

        /* adjust drawing area */
        gy += top_margin;
        gh -= top_margin;
        gh -= bottom_margin;

        gint all_legends_width;
        all_legends_width = compute_all_legends_width (cr,
                                                       lb_width,
                                                       curve_list);

        /* draw legends */
        lb_ypos = gy + gh + x_markers_height + 6; /* padding */
        lb_xpos = 10 + ceil (((width - 20) - all_legends_width) / 2);
        for (l = curve_list; l != NULL; l = l->next) {
                GduCurve *c = GDU_CURVE (l->data);
                GduColor *color;
                GduColor *fill_color;
                gdouble width;
                GduCurveFlags flags;
                const gchar *text;
                gdouble x, y;
                cairo_text_extents_t te;

                color = gdu_curve_get_color (c);
                fill_color = gdu_curve_get_fill_color (c);
                if (fill_color == NULL)
                        fill_color = color;
                width = gdu_curve_get_width (c);
                flags = gdu_curve_get_flags (c);
                text = gdu_curve_get_legend (c);

                if (text == NULL)
                        continue;

                x = lb_xpos + 0.5;
                y = lb_ypos + 0.5;

                cairo_new_path (cr);
                cairo_set_dash (cr, NULL, 0, 0.0);
                cairo_set_line_width (cr, 1.0);
                cairo_rectangle (cr, x, y, lb_width, lb_height);
                cairo_close_path (cr);
                cairo_set_source_rgba (cr, 1, 1, 1, 1);
                cairo_fill_preserve (cr);
                cairo_set_source_rgba (cr, 0, 0, 0, 1);
                cairo_stroke (cr);

                cairo_new_path (cr);

                if (flags * GDU_CURVE_FLAGS_FILLED) {
                        if (flags & GDU_CURVE_FLAGS_FADE_EDGES) {
                                set_fade_edges_pattern (cr, x, x + lb_width, fill_color);
                        } else {
                                cairo_set_source_rgba (cr,
                                                       fill_color->red,
                                                       fill_color->green,
                                                       fill_color->blue,
                                                       fill_color->alpha);
                        }
                        cairo_rectangle (cr, x + 1, y + 1, lb_width - 2, lb_height - 2);
                        cairo_fill (cr);
                } else {
                        cairo_move_to (cr, x, y + lb_height/2.0);
                        cairo_line_to (cr, x + lb_width / 2.0, y + lb_height/3.0);
                        cairo_line_to (cr, x + lb_width, y + 2.0 * lb_height/3.0);

                        cairo_set_line_width (cr, width);
                        cairo_set_source_rgba (cr,
                                               color->red,
                                               color->green,
                                               color->blue,
                                               color->alpha);
                        cairo_stroke (cr);
                }

                /* and now show the text */

                cairo_set_source_rgb (cr, 0.0, 0.0, 0.0);
                cairo_select_font_face (cr, "sans",
                                        CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
                cairo_set_font_size (cr, 8.0);
                cairo_text_extents (cr, text, &te);
                cairo_move_to (cr,
                               x + lb_width + 3 - te.x_bearing,
                               y + lb_height/2.0 - te.height/2 - te.y_bearing);

                cairo_show_text (cr, text);

                lb_xpos += lb_width + 3 + ceil (te.width) + 12;
        }

        /* measure text on the left y-axis */
        left_margin = 0;
        for (n = 0; n < n_y_markers_left; n++) {
                twidth = ceil (measure_width (cr, graph->priv->y_markers_left[n]));
                if (twidth > left_margin)
                        left_margin = twidth;
        }
        /* include half width of first xmarker label */
        if (n_x_markers > 0) {
                twidth = ceil (measure_width (cr, graph->priv->x_markers[0]));
                if (twidth/2 > left_margin)
                        left_margin = twidth/2;
        }
        left_margin += 6; /* padding */
        gx += left_margin;
        gw -= left_margin;

        /* measure text on the right y-axis */
        right_margin = 0;
        for (n = 0; n < n_y_markers_right; n++) {
                twidth = ceil (measure_width (cr, graph->priv->y_markers_right[n]));
                if (twidth/2 > right_margin)
                        right_margin = twidth/2;
        }
        /* include half width of last xmarker label */
        if (n_x_markers > 0) {
                twidth = ceil (measure_width (cr, graph->priv->x_markers[n_x_markers - 1]));
                right_margin += twidth/2;
        }
        right_margin += 6; /* padding */
        gw -= right_margin;

        /* draw the box to draw in */
        cairo_set_source_rgb (cr, 1, 1, 1);
        cairo_rectangle (cr, gx, gy, gw, gh);
        cairo_set_line_width (cr, 0.0);
	cairo_fill (cr);

        /* draw markers on the left y-axis */
        for (n = 0; n < n_y_markers_left; n++) {
                gdouble pos;
                gdouble dashes[1] = {2.0};
                const gchar *s;
                cairo_text_extents_t te;

                pos = ceil (gy + gh / (n_y_markers_left - 1) * n);

                s = graph->priv->y_markers_left[n_y_markers_left - 1 - n];

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
                gdouble pos;
                const gchar *s;
                cairo_text_extents_t te;

                pos = ceil (gy + gh / (n_y_markers_right - 1) * n);

                s = graph->priv->y_markers_right[n_y_markers_right - 1 - n];

                cairo_set_source_rgb (cr, 0.0, 0.0, 0.0);
                cairo_select_font_face (cr, "sans",
                                        CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
                cairo_set_font_size (cr, 8.0);
                cairo_text_extents (cr, s, &te);
                cairo_move_to (cr,
                               gx + gw + right_margin/2.0 + 3 - te.width/2  - te.x_bearing,
                               pos - te.height/2 - te.y_bearing);

                cairo_show_text (cr, s);
        }

        g_get_current_time (&now);
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
                               gy + gh + x_markers_height/2.0 - te.y_bearing);

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

        for (l = curve_list; l != NULL; l = l->next) {
                GduCurve *c = GDU_CURVE (l->data);
                GduColor *color;
                GduColor *fill_color;
                gdouble width;
                GduCurveFlags flags;
                GArray *points;
                guint m;

                color = gdu_curve_get_color (c);
                fill_color = gdu_curve_get_fill_color (c);
                if (fill_color == NULL)
                        fill_color = color;
                width = gdu_curve_get_width (c);
                flags = gdu_curve_get_flags (c);
                points = gdu_curve_get_points (c);

                m = 0;
                while (m < points->len) {
                        guint first_point_index;

                        cairo_new_path (cr);

                        first_point_index = m;
                        for (; m < points->len; m++) {
                                GduPoint *point;

                                point = &g_array_index (points, GduPoint, m);

                                if (point->x == G_MAXDOUBLE &&
                                    point->y == G_MAXDOUBLE) {
                                        m++;
                                        break;
                                }

                                x = gx + gw * point->x;
                                y = gy + gh * (1.0f - point->y);

                                if (y < gy + 1.0)
                                        y = gy;

                                if (y > gy + gh - 1.0)
                                        y = gy + gh - 1.0;

                                cairo_line_to (cr, x, y);
                        }

                        /* fill if requested */
                        if (flags & GDU_CURVE_FLAGS_FILLED) {
                                GduPoint *point;
                                gdouble first_x;

                                /* first, close the path */
                                cairo_line_to (cr, x, gy + gh);
                                point = &g_array_index (points, GduPoint, first_point_index);
                                first_x = gx + gw * point->x;
                                cairo_line_to (cr, first_x, gy + gh);
                                cairo_close_path (cr);

                                if (flags & GDU_CURVE_FLAGS_FADE_EDGES) {
                                        set_fade_edges_pattern (cr, first_x, x, fill_color);
                                } else {
                                        cairo_set_source_rgba (cr,
                                                               fill_color->red,
                                                               fill_color->green,
                                                               fill_color->blue,
                                                               fill_color->alpha);
                                }
                                cairo_fill_preserve (cr);
                        }

                        /* then draw the curve */
                        cairo_set_dash (cr, NULL, 0, 0.0);
                        cairo_set_line_width (cr, width);
                        cairo_set_source_rgba (cr,
                                               color->red,
                                               color->green,
                                               color->blue,
                                               color->alpha);

                        cairo_stroke (cr);

                } /* process more points */
        }

        g_list_free (curve_list);

        /* propagate event further */
        return FALSE;
}

gboolean
gdu_graph_remove_curve (GduGraph           *graph,
                        const gchar        *curve_id)
{
        gboolean ret;

        ret = g_hash_table_remove (graph->priv->curves, curve_id);

        if (GTK_WIDGET (graph)->window != NULL)
                gdk_window_invalidate_rect (GTK_WIDGET (graph)->window, NULL, TRUE);

        return ret;
}

void
gdu_graph_add_curve (GduGraph           *graph,
                     const gchar        *curve_id,
                     GduCurve           *curve)
{
        g_return_if_fail (GDU_IS_GRAPH (graph));
        g_return_if_fail (curve_id != NULL);
        g_return_if_fail (curve != NULL);

        g_hash_table_insert (graph->priv->curves, g_strdup (curve_id), g_object_ref (curve));

        if (GTK_WIDGET (graph)->window != NULL)
                gdk_window_invalidate_rect (GTK_WIDGET (graph)->window, NULL, TRUE);
}
