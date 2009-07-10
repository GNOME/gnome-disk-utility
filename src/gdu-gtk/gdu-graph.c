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
        GHashTable *curves;

        gint64 window_end_usec;
        gint64 window_size_usec;
};

G_DEFINE_TYPE (GduGraph, gdu_graph, GTK_TYPE_DRAWING_AREA)

static gboolean gdu_graph_expose_event (GtkWidget      *widget,
                                        GdkEventExpose *event);

enum
{
        PROP_0,
        PROP_WINDOW_END_USEC,
        PROP_WINDOW_SIZE_USEC,
};

static void
gdu_graph_set_property (GObject      *object,
                        guint         prop_id,
                        const GValue *value,
                        GParamSpec   *pspec)
{
        GduGraph *graph = GDU_GRAPH (object);

        switch (prop_id) {
        case PROP_WINDOW_END_USEC:
                gdu_graph_set_window_end_usec (graph, g_value_get_int64 (value));
                break;

        case PROP_WINDOW_SIZE_USEC:
                gdu_graph_set_window_size_usec (graph, g_value_get_int64 (value));
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
        case PROP_WINDOW_END_USEC:
                g_value_set_int64 (value, graph->priv->window_end_usec);
                break;

        case PROP_WINDOW_SIZE_USEC:
                g_value_set_int64 (value, graph->priv->window_size_usec);
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

        widget_class->expose_event       = gdu_graph_expose_event;

        g_type_class_add_private (klass, sizeof (GduGraphPrivate));

        g_object_class_install_property (gobject_class,
                                         PROP_WINDOW_END_USEC,
                                         g_param_spec_int64 ("window-end-usec",
                                                             _("Window End Microseconds"),
                                                             _("The end of the graph window, in micro-seconds since epoch"),
                                                             G_MININT64,
                                                             G_MAXINT64,
                                                             G_MAXINT64,
                                                             G_PARAM_READABLE |
                                                             G_PARAM_WRITABLE |
                                                             G_PARAM_CONSTRUCT));


        g_object_class_install_property (gobject_class,
                                         PROP_WINDOW_SIZE_USEC,
                                         g_param_spec_int64 ("window-size-usec",
                                                             _("Window Size Microseconds"),
                                                             _("Size of graph window, in microseconds"),
                                                             G_MININT64,
                                                             G_MAXINT64,
                                                             12L * 60 * 60 * G_USEC_PER_SEC,
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

gint64
gdu_graph_get_window_end_usec  (GduGraph *graph)
{
        g_return_val_if_fail (GDU_IS_GRAPH (graph), 0);
        return graph->priv->window_end_usec;
}

gint64
gdu_graph_get_window_size_usec (GduGraph *graph)
{
        g_return_val_if_fail (GDU_IS_GRAPH (graph), 0);
        return graph->priv->window_size_usec;
}

void
gdu_graph_set_window_end_usec  (GduGraph *graph,
                                gint64    time_usec)
{
        g_return_if_fail (GDU_IS_GRAPH (graph));

        graph->priv->window_end_usec = time_usec;

        if (GTK_WIDGET (graph)->window != NULL)
                gdk_window_invalidate_rect (GTK_WIDGET (graph)->window, NULL, TRUE);
}

void
gdu_graph_set_window_size_usec (GduGraph *graph,
                                gint64    period_usec)
{
        g_return_if_fail (GDU_IS_GRAPH (graph));

        graph->priv->window_size_usec = period_usec;

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
str_fits_width (cairo_t *cr,
                gdouble width_pixels,
                const gchar *str)
{
        if (measure_width (cr, str) < width_pixels)
                return TRUE;
        return FALSE;
}


#if 0
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
#endif

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
curve_z_order_sort (GduCurve *a,
                    GduCurve *b)
{
        return gdu_curve_get_z_order (a) - gdu_curve_get_z_order (b);
}

typedef enum
{
        TIME_MARKER_TEXT_ALIGN_CENTER,
        TIME_MARKER_TEXT_ALIGN_LEFT
} TimeMarkerTextAlign;

typedef enum
{
        TEXT_MARKER_GRANULARITY_1_HOUR,
        TEXT_MARKER_GRANULARITY_3_HOUR,
        TEXT_MARKER_GRANULARITY_1_DAY,
        TEXT_MARKER_GRANULARITY_1_WEEK,
        TEXT_MARKER_GRANULARITY_1_MONTH,
        TEXT_MARKER_GRANULARITY_1_YEAR,
} TextMarkerGranularity;

typedef enum
{
        TIME_MARKER_FLAGS_NONE         = 0,      /* No flags set */
        TIME_MARKER_FLAGS_TEXT_AREA    = (1<<0), /* Make the line cut through the text area */
        TIME_MARKER_FLAGS_EMPHASIS     = (1<<1), /* Emphasize the line */
} TimeMarkerFlags;

typedef struct
{
        gint64 time_usec;               /* position in time, in localtime, usec since Epoch */

        TimeMarkerFlags flags;          /* Flags affecting presentation */

        gchar *text;                    /* Text to print, NULL for no text */
        TimeMarkerTextAlign text_align; /* How to align the text */
        gdouble text_width_pixels;      /* width of the area for text, in pixels */
} TimeMarker;

static void
time_marker_free (TimeMarker *marker)
{
        g_free (marker->text);
        g_free (marker);
}

static gchar *
time_marker_format_time_hour (cairo_t *cr,
                              gdouble width_pixels,
                              struct tm *t,
                              struct tm *now)
{
        gchar buf[512];

        if (now->tm_year == t->tm_year &&
            now->tm_yday == t->tm_yday) {
                /* today */
                strftime (buf, sizeof buf, _("%l %P"), t);
        } else if (now->tm_year == t->tm_year &&
                   now->tm_mon  == t->tm_mon &&
                   (now->tm_mday - t->tm_mday) < 7) {
                /* this week */

                strftime (buf, sizeof buf, _("%A %l %P"), t);
                if (str_fits_width (cr, width_pixels, buf))
                        goto out;

                strftime (buf, sizeof buf, _("%a %l %P"), t);

        } else if (now->tm_year == t->tm_year) {
                /* this year */

                strftime (buf, sizeof buf, _("%B %e, %l %P"), t);
                if (str_fits_width (cr, width_pixels, buf))
                        goto out;

                strftime (buf, sizeof buf, _("%b %e, %l %P"), t);
                if (str_fits_width (cr, width_pixels, buf))
                        goto out;

                strftime (buf, sizeof buf, _("%b %e, %l %P"), t);
        } else {
                /* other year */

                strftime (buf, sizeof buf, _("%B %e, %l %P, %Y"), t);
                if (str_fits_width (cr, width_pixels, buf))
                        goto out;

                strftime (buf, sizeof buf, _("%b %e, %l %P, %Y"), t);
                if (str_fits_width (cr, width_pixels, buf))
                        goto out;

                strftime (buf, sizeof buf, _("%b %e, %l %P, %y"), t);
        }

 out:
        return g_strdup (buf);
}

static gchar *
time_marker_format_time_day (cairo_t *cr,
                             gdouble width_pixels,
                             struct tm *t,
                             struct tm *now)
{
        gchar buf[512];

        if (now->tm_year == t->tm_year) {
                strftime (buf, sizeof buf, _("%A %B %e"), t);
                if (str_fits_width (cr, width_pixels, buf))
                        goto out;

                strftime (buf, sizeof buf, _("%a %B %e"), t);
                if (str_fits_width (cr, width_pixels, buf))
                        goto out;

                strftime (buf, sizeof buf, _("%b %e"), t);
                if (str_fits_width (cr, width_pixels, buf))
                        goto out;

                if (now->tm_mon == t->tm_mon) {
                        strftime (buf, sizeof buf, _("%e"), t);
                }

        } else {
                strftime (buf, sizeof buf, _("%A %B %e, %Y"), t);
                if (str_fits_width (cr, width_pixels, buf))
                        goto out;

                strftime (buf, sizeof buf, _("%a %B %e, %Y"), t);
                if (str_fits_width (cr, width_pixels, buf))
                        goto out;

                strftime (buf, sizeof buf, _("%b %e, %y"), t);
                if (str_fits_width (cr, width_pixels, buf))
                        goto out;
        }

 out:
        return g_strdup (buf);
}

static gchar *
time_marker_format_time_week (cairo_t *cr,
                              gdouble width_pixels,
                              struct tm *t,
                              struct tm *now)
{
        /* for now, just use the day format - we could do week numbers but... */
        return time_marker_format_time_day (cr, width_pixels, t, now);
}

static gchar *
time_marker_format_time_month (cairo_t *cr,
                               gdouble width_pixels,
                               struct tm *t,
                               struct tm *now)
{
        gchar buf[512];

        strftime (buf, sizeof buf, _("%B %Y"), t);
        if (str_fits_width (cr, width_pixels, buf))
                goto out;

         strftime (buf, sizeof buf, _("%b %y"), t);
        if (str_fits_width (cr, width_pixels, buf))
                goto out;

        if (now->tm_year == t->tm_year) {
                strftime (buf, sizeof buf, _("%b"), t);
        }

 out:
        return g_strdup (buf);
}

static gchar *
time_marker_format_time_year (cairo_t *cr,
                              gdouble width_pixels,
                              struct tm *t,
                              struct tm *now)
{
        gchar buf[512];
        strftime (buf, sizeof buf, _("%Y"), t);
        return g_strdup (buf);
}

/* returns a list of time markers to draw */
static GPtrArray *
time_markers_get (cairo_t *cr,
                  gint64   since_usec,
                  gint64   until_usec,
                  gdouble  width_pixels)
{
        GPtrArray *markers;
        gint64 size_usec;
        gdouble secs_per_pixel;
        gdouble minimum_distance_pixels;
        gdouble secs_per_minimum_distance;
        guint64 distance_usec;
        gdouble distance_pixels;
        guint64 t_usec;
        gint64 timezone_offset_usec;
        TextMarkerGranularity granularity;
        gdouble margin_pixels;
        guint n;

        /* minimum distance between markers */
        minimum_distance_pixels = 70;

        /* margin on left/right of marker text */
        margin_pixels = 3;

        size_usec = until_usec - since_usec;

        secs_per_pixel = size_usec / ((gdouble) G_USEC_PER_SEC) / width_pixels;
        secs_per_minimum_distance = minimum_distance_pixels * secs_per_pixel;

        markers = g_ptr_array_new_with_free_func ((GDestroyNotify) time_marker_free);

        if (secs_per_minimum_distance < 60*60) {
                /* 1 hour granularity */
                distance_usec = 60*60L * G_USEC_PER_SEC;
                granularity = TEXT_MARKER_GRANULARITY_1_HOUR;
        } else if (secs_per_minimum_distance < 3*60*60) {
                /* 3 hour granularity */
                distance_usec = 3*60*60L * G_USEC_PER_SEC;
                granularity = TEXT_MARKER_GRANULARITY_3_HOUR;
        } else if (secs_per_minimum_distance < 24*60*60) {
                /* day granularity */
                distance_usec = 24*60*60L * G_USEC_PER_SEC;
                granularity = TEXT_MARKER_GRANULARITY_1_DAY;
        } else if (secs_per_minimum_distance < 7*24*60*60) {
                /* week granularity */
                distance_usec = 7*24*60*60L * G_USEC_PER_SEC;
                granularity = TEXT_MARKER_GRANULARITY_1_WEEK;
        } else if (secs_per_minimum_distance < 30*24*60*60) {
                /* month granularity */
                distance_usec = 30*24*60*60L * G_USEC_PER_SEC;
                granularity = TEXT_MARKER_GRANULARITY_1_MONTH;
        } else /* if (secs_per_minimum_distance < 365*24*60*60)*/ {
                /* year granularity */
                distance_usec = 365*24*60*60L * G_USEC_PER_SEC;
                granularity = TEXT_MARKER_GRANULARITY_1_YEAR;
        }
        distance_pixels = distance_usec / secs_per_pixel / G_USEC_PER_SEC;

        /* TODO: figure this out from somewhere */
        timezone_offset_usec = - 4 * 60 * 60 * 1L * G_USEC_PER_SEC;

        struct tm tm_now;
        time_t time_now_utc;

        time_now_utc = time (NULL);
        localtime_r (&time_now_utc, &tm_now);

        /* we use different loops according to the granularity used */
        if (granularity < TEXT_MARKER_GRANULARITY_1_DAY) {
                /* Now, for the distance D, we sweep over the interval [since - 2*D; until + 2*D] and
                 * create a marker for each point T in said interval where T is divisible by D.
                 *
                 * We also adjust for the local timezone to get the markers to land correctly, e.g.
                 * at 12am when using a 3-hour granularity.
                 */
                for (t_usec = since_usec - 2 * distance_usec - (since_usec % distance_usec) -
                             timezone_offset_usec - 24*60*60*1L*G_USEC_PER_SEC;
                     t_usec < until_usec + 2 * distance_usec;
                     t_usec += distance_usec) {
                        TimeMarker *marker;
                        time_t time_utc_sec;
                        struct tm tm;
                        gchar *s;

                        time_utc_sec = t_usec / G_USEC_PER_SEC;
                        localtime_r (&time_utc_sec, &tm);

                        if (tm.tm_hour == 0) {
                                s = time_marker_format_time_day (cr, distance_pixels - 2*margin_pixels, &tm, &tm_now);
                        } else {
                                s = time_marker_format_time_hour (cr, distance_pixels - 2*margin_pixels, &tm, &tm_now);
                        }

                        marker = g_new0 (TimeMarker, 1);
                        marker->text = s;
                        marker->time_usec = t_usec;
                        marker->flags = TIME_MARKER_FLAGS_TEXT_AREA | TIME_MARKER_FLAGS_EMPHASIS;
                        marker->text_align = TIME_MARKER_TEXT_ALIGN_LEFT;
                        marker->text_width_pixels = distance_pixels;
                        g_ptr_array_add (markers, marker);

                        /* add extra markers */
                        switch (granularity) {
                        case TEXT_MARKER_GRANULARITY_1_HOUR:
                                /* four extra markers => every 15 minutes */
                                for (n = 0; n < 4; n++) {
                                        marker = g_new0 (TimeMarker, 1);
                                        marker->time_usec = t_usec + n * distance_usec / 4;
                                        marker->flags = TIME_MARKER_FLAGS_NONE;
                                        g_ptr_array_add (markers, marker);
                                }
                                break;
                        case TEXT_MARKER_GRANULARITY_3_HOUR:
                                /* three extra markers => every hour */
                                for (n = 0; n < 3; n++) {
                                        marker = g_new0 (TimeMarker, 1);
                                        marker->time_usec = t_usec + n * distance_usec / 3;
                                        marker->flags = TIME_MARKER_FLAGS_NONE;
                                        g_ptr_array_add (markers, marker);
                                }
                                break;
                        default:
                                g_assert_not_reached ();
                                break;
                        }

                }
        } else {
                GDate date_iter;
                GDate date_until;
                GDate date_next;

                g_date_clear (&date_iter, 1);
                g_date_clear (&date_until, 1);
                g_date_clear (&date_next, 1);

                g_date_set_time_t (&date_iter,  (time_t) ((since_usec - timezone_offset_usec) / G_USEC_PER_SEC));
                g_date_set_time_t (&date_until, (time_t) ((until_usec - timezone_offset_usec) / G_USEC_PER_SEC));

                /* adjust so we start from beginning of the specified period */
                switch (granularity) {
                case TEXT_MARKER_GRANULARITY_1_DAY:
                        g_date_subtract_days (&date_iter, 1);
                        g_date_add_days (&date_until, 1);
                        break;
                case TEXT_MARKER_GRANULARITY_1_WEEK:
                        g_date_add_days (&date_iter, -g_date_get_weekday (&date_iter));
                        g_date_subtract_days (&date_iter, 7);
                        g_date_add_days (&date_until, 7);
                        break;
                case TEXT_MARKER_GRANULARITY_1_MONTH:
                        g_date_set_day (&date_iter, 1);
                        g_date_subtract_months (&date_iter, 1);
                        g_date_add_months (&date_until, 1);
                        break;
                case TEXT_MARKER_GRANULARITY_1_YEAR:
                        g_date_set_day (&date_iter, 1);
                        g_date_set_month (&date_iter, 1);
                        g_date_subtract_years (&date_iter, 1);
                        g_date_add_years (&date_until, 1);
                        break;
                default:
                        g_assert_not_reached ();
                        break;
                }

                while (g_date_compare (&date_iter, &date_until) < 0) {
                        TimeMarker *marker;
                        gint64 t_usec;
                        struct tm tm;
                        gchar *s;

                        g_date_to_struct_tm (&date_iter, &tm);
                        tm.tm_sec = tm.tm_min = tm.tm_hour = 0;

                        date_next = date_iter;
                        switch (granularity) {
                        case TEXT_MARKER_GRANULARITY_1_DAY:
                                g_date_add_days (&date_next, 1);
                                break;
                        case TEXT_MARKER_GRANULARITY_1_WEEK:
                                g_date_add_days (&date_next, 7);
                                break;
                        case TEXT_MARKER_GRANULARITY_1_MONTH:
                                g_date_add_months (&date_next, 1);
                                break;
                        case TEXT_MARKER_GRANULARITY_1_YEAR:
                                g_date_add_years (&date_next, 1);
                                break;
                        default:
                                g_assert_not_reached ();
                                break;
                        }

                        /* distance in pixels varies since each month/year isn't the same */
                        distance_usec = g_date_days_between (&date_iter, &date_next) * 24*60*60*1L*G_USEC_PER_SEC;
                        distance_pixels = distance_usec / secs_per_pixel / G_USEC_PER_SEC;

                        switch (granularity) {
                        case TEXT_MARKER_GRANULARITY_1_DAY:
                                s = time_marker_format_time_day (cr, distance_pixels - 2*margin_pixels, &tm, &tm_now);
                                break;
                        case TEXT_MARKER_GRANULARITY_1_WEEK:
                                s = time_marker_format_time_week (cr, distance_pixels - 2*margin_pixels, &tm, &tm_now);
                                break;
                        case TEXT_MARKER_GRANULARITY_1_MONTH:
                                s = time_marker_format_time_month (cr, distance_pixels - 2*margin_pixels, &tm, &tm_now);
                                break;
                        case TEXT_MARKER_GRANULARITY_1_YEAR:
                                s = time_marker_format_time_year (cr, distance_pixels - 2*margin_pixels, &tm, &tm_now);
                                break;
                        default:
                                g_assert_not_reached ();
                                break;
                        }

#if 0
                        g_debug ("Using `%s' for sec=%d min=%d hour=%d mday=%d mon=%d year=%d wday=%d yday=%d isdst=%d",
                                 s,
                                 tm.tm_sec,
                                 tm.tm_min,
                                 tm.tm_hour,
                                 tm.tm_mday,
                                 tm.tm_mon,
                                 tm.tm_year,
                                 tm.tm_wday,
                                 tm.tm_yday,
                                 tm.tm_isdst);
#endif

                        t_usec = timelocal (&tm) * 1L * G_USEC_PER_SEC;

                        /* TODO: Maybe want to center some markers, like months */
                        marker = g_new0 (TimeMarker, 1);
                        marker->text = s;
                        marker->time_usec = t_usec;
                        marker->flags = TIME_MARKER_FLAGS_TEXT_AREA | TIME_MARKER_FLAGS_EMPHASIS;
                        marker->text_align = TIME_MARKER_TEXT_ALIGN_LEFT;
                        marker->text_width_pixels = distance_pixels;
                        g_ptr_array_add (markers, marker);

                        /* add extra markers */
                        switch (granularity) {
                        case TEXT_MARKER_GRANULARITY_1_DAY:
                                /* four extra markers => every 6 hours */
                                for (n = 0; n < 4; n++) {
                                        marker = g_new0 (TimeMarker, 1);
                                        marker->time_usec = t_usec + n * distance_usec / 4;
                                        marker->flags = TIME_MARKER_FLAGS_NONE;
                                        g_ptr_array_add (markers, marker);
                                }
                                break;
                        case TEXT_MARKER_GRANULARITY_1_WEEK:
                                /* seven extra markers => every day */
                                for (n = 0; n < 7; n++) {
                                        marker = g_new0 (TimeMarker, 1);
                                        marker->time_usec = t_usec + n * distance_usec / 7;
                                        marker->flags = TIME_MARKER_FLAGS_NONE;
                                        g_ptr_array_add (markers, marker);
                                }
                                break;
                        case TEXT_MARKER_GRANULARITY_1_MONTH:
                                /* four extra markers => every week */
                                for (n = 0; n < 4; n++) {
                                        marker = g_new0 (TimeMarker, 1);
                                        marker->time_usec = t_usec + n * distance_usec / 4;
                                        marker->flags = TIME_MARKER_FLAGS_NONE;
                                        g_ptr_array_add (markers, marker);
                                }
                                break;
                        case TEXT_MARKER_GRANULARITY_1_YEAR:
                                /* twelve extra markers => every month */
                                for (n = 0; n < 12; n++) {
                                        marker = g_new0 (TimeMarker, 1);
                                        marker->time_usec = t_usec + n * distance_usec / 12;
                                        marker->flags = TIME_MARKER_FLAGS_NONE;
                                        g_ptr_array_add (markers, marker);
                                }
                                break;
                        default:
                                g_assert_not_reached ();
                                break;
                        }

                        date_iter = date_next;
                }
        }

        return markers;
}

typedef enum {
        TIMEBAR_STYLE_SEPARATE,
        TIMEBAR_STYLE_EMBEDDED
} TimeBarStyle;

static void
draw_curves (cairo_t *cr,
             gdouble gx, gdouble gy, gdouble gw, gdouble gh,
             TimeBarStyle timebar_style,
             GList *curve_list,
             gint64 window_since_usec, gint64 window_until_usec)
{
        GTimeVal now;
        GList *l;
        gint64 window_size_usec;
        guint n;
        gdouble x, y;
        guint64 t_left;
        guint64 t_right;
        guint timebar_height;

        if (timebar_style == TIMEBAR_STYLE_SEPARATE)
                timebar_height = 12;
        else
                timebar_height = 0;

        window_size_usec = window_until_usec - window_since_usec;

        cairo_reset_clip (cr);

        /* draw the box to draw in */
        cairo_set_source_rgb (cr, 1, 1, 1);
        cairo_rectangle (cr, gx, gy, gw, gh - timebar_height);
        cairo_fill_preserve (cr);
        cairo_set_source_rgb (cr, 0.0, 0.0, 0.0);
        cairo_set_line_width (cr, 1.0);
        cairo_set_dash (cr, NULL, 0, 0.0);
        cairo_stroke (cr);

        g_get_current_time (&now);
        t_left = now.tv_sec - 6 * 24 * 60 * 60;
        t_right = now.tv_sec;

        /* draw time bar */
        if (timebar_height > 0) {
                cairo_new_path (cr);
                cairo_rectangle (cr, gx, gy + gh - timebar_height, gw, timebar_height);
                cairo_set_source_rgb (cr, 0x72 / 255.0, 0x9f / 255.0, 0xcf / 255.0);
                cairo_fill_preserve (cr);
                cairo_set_source_rgb (cr, 0.0, 0.0, 0.0);
                cairo_set_line_width (cr, 1.0);
                cairo_set_dash (cr, NULL, 0, 0.0);
                cairo_stroke (cr);
        }

        /* clip to the graph + timebar area */
        cairo_rectangle (cr, gx, gy, gw, gh);
        cairo_clip (cr);

        /* ---------------------------------------------------------------------------------------------------- */

        GPtrArray *time_markers;
        time_markers = time_markers_get (cr,
                                         window_since_usec,
                                         window_until_usec,
                                         gw);
        for (n = 0; n < time_markers->len; n++) {
                TimeMarker *marker;
                cairo_text_extents_t te;

                marker = time_markers->pdata[n];

                x = ceil (gx + gw * (marker->time_usec- window_since_usec) / window_size_usec) + 0.5;

                /* draw the vertical line */
                cairo_new_path (cr);
                cairo_move_to (cr, x, gy);
                if (marker->flags & TIME_MARKER_FLAGS_TEXT_AREA) {
                        cairo_line_to (cr, x, gy + gh);
                } else {
                        cairo_line_to (cr, x, gy + gh - timebar_height);
                }
                if (marker->flags & TIME_MARKER_FLAGS_EMPHASIS) {
                        cairo_set_source_rgba (cr, 0.0, 0.0, 0.0, 1.0);
                        cairo_set_line_width (cr, 1.0);
                } else {
                        cairo_set_source_rgba (cr, 0.0, 0.0, 0.0, 0.75);
                        cairo_set_line_width (cr, 0.5);
                }
                cairo_set_dash (cr, NULL, 0, 0.0);
                cairo_stroke (cr);

                cairo_select_font_face (cr, "sans",
                                        CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
                cairo_set_font_size (cr, 8.0);
                cairo_text_extents (cr, marker->text, &te);

                if (timebar_style == TIMEBAR_STYLE_SEPARATE) {
                        y = gy + gh - timebar_height/2 - 8.0/2 - te.y_bearing;
                } else {
                        y = gy + gh - 8.0 - 2.0 - te.y_bearing;
                }

                if (marker->text_align == TIME_MARKER_TEXT_ALIGN_CENTER) {

                        cairo_move_to (cr,
                                       x  + te.x_bearing + 3 + marker->text_width_pixels/2 - te.width/2,
                                       y);

                } else if (marker->text_align == TIME_MARKER_TEXT_ALIGN_LEFT) {

                        cairo_move_to (cr,
                                       x  + te.x_bearing + 3,
                                       y);
                }

                cairo_show_text (cr, marker->text);
        }
        g_ptr_array_unref (time_markers);

        /* clip to the graph area */
        cairo_rectangle (cr, gx, gy, gw, gh - timebar_height);
        cairo_reset_clip (cr);
        cairo_clip (cr);

        for (l = curve_list; l != NULL; l = l->next) {
                GduCurve *c = GDU_CURVE (l->data);
                GduColor *color;
                GduColor *fill_color;
                gdouble width;
                GduCurveFlags flags;
                GArray *samples;
                guint m;
                gdouble sample_value_min;
                gdouble sample_value_max;

                color = gdu_curve_get_color (c);
                fill_color = gdu_curve_get_fill_color (c);
                if (fill_color == NULL)
                        fill_color = color;
                width = gdu_curve_get_width (c);
                flags = gdu_curve_get_flags (c);
                samples = gdu_curve_get_samples (c);

                /* if normalization is requested, find min/max for sample values in curve on window */
                if (flags & GDU_CURVE_FLAGS_NORMALIZE) {
                        sample_value_min = G_MAXDOUBLE;
                        sample_value_max = -G_MAXDOUBLE;

                        for (m = 0; m < samples->len; m++) {
                                GduSample *sample;

                                sample = &g_array_index (samples, GduSample, m);

                                /* ignore break points */
                                if (sample->time_usec == G_MAXINT64 && sample->value == G_MAXDOUBLE)
                                        continue;

                                /* ignore samples outside window */
                                x = gx + gw * (sample->time_usec - window_since_usec) / window_size_usec;
                                if (x < gx)
                                        continue;
                                if (x > gx + gw)
                                        continue;

                                if (sample->value < sample_value_min)
                                        sample_value_min = sample->value;
                                if (sample->value > sample_value_max)
                                        sample_value_max = sample->value;
                        }

                        if (sample_value_max - sample_value_min < 0.001) {
                                sample_value_min -= 1.0;
                                sample_value_max += 1.0;
                        }
                } else {
                        sample_value_min = 0.0;
                        sample_value_max = 1.0;
                }

                /* draw the curve */
                m = 0;
                while (m < samples->len) {
                        gint first_sample_index;

                        cairo_new_path (cr);

                        first_sample_index = -1;
                        for (; m < samples->len; m++) {
                                GduSample *sample;
                                gdouble sample_value_normalized;

                                sample = &g_array_index (samples, GduSample, m);

                                if (sample->time_usec == G_MAXINT64 && sample->value == G_MAXDOUBLE) {
                                        m++;
                                        break;
                                }

                                x = gx + gw * (sample->time_usec - window_since_usec) / window_size_usec;

                                if (x < gx) {
                                        if (m + 1 < samples->len) {
                                                GduSample *next_sample;
                                                gdouble next_x;

                                                next_sample = &g_array_index (samples, GduSample, m + 1);
                                                next_x = gx + gw * (next_sample->time_usec - window_since_usec) / window_size_usec;
                                                if (next_x < gx)
                                                        continue;
                                        } else {
                                                continue;
                                        }
                                }

                                sample_value_normalized = (sample->value - sample_value_min) /
                                                          (sample_value_max - sample_value_min);

                                y = gy + (gh - timebar_height) * (1.0f - sample_value_normalized);

                                if (y < gy + 1.0)
                                        y = gy;

                                if (y > gy + (gh - timebar_height) - 1.0)
                                        y = gy + (gh - timebar_height) - 1.0;

                                cairo_line_to (cr, x, y);

                                if (first_sample_index == -1)
                                        first_sample_index = m;
                        }

                        /* then draw the curve */
                        cairo_set_dash (cr, NULL, 0, 0.0);
                        cairo_set_line_width (cr, width);
                        cairo_set_source_rgba (cr,
                                               color->red,
                                               color->green,
                                               color->blue,
                                               color->alpha);

                        cairo_stroke_preserve (cr);

                        /* fill if requested */
                        if (flags & GDU_CURVE_FLAGS_FILLED && first_sample_index != -1) {
                                GduSample *sample;
                                gdouble first_x;

                                /* first, close the path */
                                cairo_line_to (cr, x, gy + (gh - timebar_height));
                                sample = &g_array_index (samples, GduSample, first_sample_index);
                                first_x = gx + gw * (sample->time_usec - window_since_usec) / window_size_usec;
                                cairo_line_to (cr, first_x, gy + (gh - timebar_height));
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
                                cairo_fill (cr);
                        }

                } /* process more samples */
        }
}

static gboolean
gdu_graph_expose_event (GtkWidget      *widget,
                        GdkEventExpose *event)
{
        GduGraph *graph = GDU_GRAPH (widget);
        cairo_t *cr;
        gdouble width, height;
        guint left_margin;
        guint right_margin;
        guint top_margin;
        guint bottom_margin;
        gdouble gx, gy, gw, gh;
        gint64 window_since_usec;
        gint64 window_until_usec;
        gint64 small_graph_window_since_usec;
        gint64 small_graph_window_until_usec;
        GList *curve_list;
        GList *l;
        gint64 now_usec;

        curve_list = g_hash_table_get_values (graph->priv->curves);
        curve_list = g_list_sort (curve_list,
                                  (GCompareFunc) curve_z_order_sort);

        width = widget->allocation.width;
        height = widget->allocation.height;

        cr = gdk_cairo_create (widget->window);

        cairo_rectangle (cr,
                         event->area.x, event->area.y,
                         event->area.width, event->area.height);
        cairo_clip (cr);

        gx = 0.5;
        gy = 0.5;
        gw = width;
        gh = height;

        top_margin = 10;
        bottom_margin = 10;
        left_margin = 10;
        right_margin = 10;

        guint small_graph_height;
        small_graph_height = 60;
        bottom_margin += small_graph_height;

        /* adjust drawing area */
        gy += top_margin;
        gh -= top_margin;
        gh -= bottom_margin;
        gx += left_margin;
        gw -= left_margin;
        gw -= right_margin;

        now_usec = time (NULL) * G_USEC_PER_SEC;
        window_until_usec = graph->priv->window_end_usec;
        if (graph->priv->window_end_usec > now_usec)
                window_until_usec = now_usec;
        window_since_usec = window_until_usec - graph->priv->window_size_usec;

        draw_curves (cr,
                     gx, gy, gw, gh,
                     TIMEBAR_STYLE_SEPARATE,
                     curve_list,
                     window_since_usec, window_until_usec);


        small_graph_window_since_usec = G_MAXINT64;
        small_graph_window_until_usec = G_MININT64;
        for (l = curve_list; l != NULL; l = l->next) {
                GduCurve *curve;
                GArray *samples;
                GduSample *sample;
                gint64 first, last;

                curve = GDU_CURVE (l->data);
                samples = gdu_curve_get_samples (curve);

                if (samples == NULL || samples->len == 0)
                        continue;

                sample = &g_array_index (samples, GduSample, 0);
                first = sample->time_usec;

                sample = &g_array_index (samples, GduSample, samples->len - 1);
                last = sample->time_usec;

                if (first != G_MAXINT64) {
                        if (first < small_graph_window_since_usec)
                                small_graph_window_since_usec = first;
                }

                if (last != G_MAXINT64) {
                        if (last > small_graph_window_until_usec)
                                small_graph_window_until_usec = last;
                }
        }
        if (small_graph_window_since_usec != G_MAXINT64 && small_graph_window_until_usec != G_MININT64) {

                if (small_graph_window_until_usec - small_graph_window_since_usec < 21 * 24 * 3600 * 1L * G_USEC_PER_SEC)
                        small_graph_window_since_usec = small_graph_window_until_usec - 21 * 24 * 3600 * 1L * G_USEC_PER_SEC;

                draw_curves (cr,
                             gx, gy + gh, gw, small_graph_height,
                             TIMEBAR_STYLE_EMBEDDED,
                             curve_list,
                             small_graph_window_since_usec, small_graph_window_until_usec);

                gdouble zm_x0;
                gdouble zm_x1;
                gint64 small_window_size_usec;

                small_window_size_usec = small_graph_window_until_usec - small_graph_window_since_usec;

                zm_x0 = gx + gw * (window_since_usec - small_graph_window_since_usec) / small_window_size_usec;
                zm_x1 = gx + gw * (window_until_usec - small_graph_window_since_usec) / small_window_size_usec;

                cairo_reset_clip (cr);
                cairo_set_source_rgba (cr, 0.0, 0.0, 0.0, 0.5);

#if 1
                cairo_rectangle (cr,
                                 gx, gy + gh,
                                 zm_x0 - gx, small_graph_height);
                cairo_fill (cr);
#endif

#if 1
                cairo_rectangle (cr,
                                 zm_x1, gy + gh,
                                 gx + gw - zm_x1, small_graph_height);
                cairo_fill (cr);
#endif

        }


        g_list_free (curve_list);

        cairo_destroy (cr);

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
