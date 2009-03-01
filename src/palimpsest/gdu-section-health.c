/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*- */
/* gdu-section-health.c
 *
 * Copyright (C) 2007 David Zeuthen
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
#include <string.h>
#include <glib/gi18n.h>
#include <dbus/dbus-glib.h>
#include <stdlib.h>
#include <math.h>
#include <polkit-gnome/polkit-gnome.h>

#include <gdu/gdu.h>
#include "gdu-time-label.h"
#include "gdu-section-health.h"

struct _GduSectionHealthPrivate
{
        GtkWidget *health_status_image;
        GtkWidget *health_status_label;
        GtkWidget *health_status_explanation_label;
        GtkWidget *health_last_self_test_result_label;
        GtkWidget *health_power_on_hours_label;
        GtkWidget *health_temperature_label;
        GtkWidget *health_updated_label;

        PolKitAction *pk_smart_refresh_action;
        PolKitAction *pk_smart_retrieve_historical_data_action;
        PolKitAction *pk_smart_selftest_action;
        PolKitGnomeAction *health_refresh_action;
        PolKitGnomeAction *health_details_action;
        PolKitGnomeAction *health_selftest_action;
};

static GObjectClass *parent_class = NULL;

G_DEFINE_TYPE (GduSectionHealth, gdu_section_health, GDU_TYPE_SECTION)

/* ---------------------------------------------------------------------------------------------------- */

static void
smart_data_set_pending (GduSectionHealth *section)
{
        gtk_image_set_from_icon_name (GTK_IMAGE (section->priv->health_status_image),
                                      "gdu-smart-unknown",
                                      GTK_ICON_SIZE_MENU);
        gtk_label_set_markup (GTK_LABEL (section->priv->health_status_label), _("<i>Retrieving...</i>"));
        gtk_label_set_text (GTK_LABEL (section->priv->health_power_on_hours_label), _("-"));
        gtk_label_set_text (GTK_LABEL (section->priv->health_temperature_label), _("-"));
        gtk_label_set_text (GTK_LABEL (section->priv->health_updated_label), _("-"));
        gtk_label_set_markup (GTK_LABEL (section->priv->health_last_self_test_result_label), _("-"));

        polkit_gnome_action_set_sensitive (section->priv->health_refresh_action, FALSE);
        polkit_gnome_action_set_sensitive (section->priv->health_details_action, FALSE);
        polkit_gnome_action_set_sensitive (section->priv->health_selftest_action, FALSE);
        gtk_widget_hide (section->priv->health_status_explanation_label);
}

static void
smart_data_set_not_supported (GduSectionHealth *section)
{
        gtk_image_set_from_icon_name (GTK_IMAGE (section->priv->health_status_image),
                                      "gdu-smart-unknown",
                                      GTK_ICON_SIZE_MENU);
        gtk_label_set_markup (GTK_LABEL (section->priv->health_status_label), _("<i>S.M.A.R.T. Not Supported</i>"));
        gtk_label_set_text (GTK_LABEL (section->priv->health_power_on_hours_label), _("-"));
        gtk_label_set_text (GTK_LABEL (section->priv->health_temperature_label), _("-"));
        gtk_label_set_text (GTK_LABEL (section->priv->health_updated_label), _("-"));
        gtk_label_set_markup (GTK_LABEL (section->priv->health_last_self_test_result_label), _("-"));

        polkit_gnome_action_set_sensitive (section->priv->health_refresh_action, FALSE);
        polkit_gnome_action_set_sensitive (section->priv->health_details_action, FALSE);
        polkit_gnome_action_set_sensitive (section->priv->health_selftest_action, FALSE);
        gtk_widget_hide (section->priv->health_status_explanation_label);
}

static void
smart_data_set (GduSectionHealth *section)
{
        char *s;
        double fahrenheit;
        const char *last;
        gboolean passed;
        int power_on_hours;
        double temperature;
        char *last_self_test_result;
        GduDevice *device;
        gboolean attr_warn;
        gboolean attr_fail;
        GTimeVal updated;
        GduSmartData *sd;

        sd = NULL;
        last_self_test_result = NULL;

        device = gdu_presentable_get_device (gdu_section_get_presentable (GDU_SECTION (section)));
        if (device == NULL) {
                g_warning ("%s: device is not supposed to be NULL", __FUNCTION__);
                goto out;
        }

        sd = gdu_device_get_smart_data (device);
        if (sd == NULL) {
                g_warning ("%s: no smart data for device", __FUNCTION__);
                goto out;
        }

        passed = ! gdu_smart_data_get_is_failing (sd);
        attr_warn = gdu_smart_data_get_attribute_warning (sd);
        attr_fail = gdu_smart_data_get_attribute_failing (sd);
        power_on_hours = gdu_smart_data_get_time_powered_on (sd) / 3600;
        temperature = gdu_smart_data_get_temperature (sd);
        last_self_test_result = gdu_smart_data_get_last_self_test_result (sd);

        polkit_gnome_action_set_sensitive (section->priv->health_refresh_action, TRUE);
        polkit_gnome_action_set_sensitive (section->priv->health_details_action, TRUE);
        polkit_gnome_action_set_sensitive (section->priv->health_selftest_action, TRUE);
        gtk_widget_show (section->priv->health_status_image);

        if (passed) {
                if (attr_fail) {
                        gtk_image_set_from_icon_name (GTK_IMAGE (section->priv->health_status_image),
                                                      "gdu-smart-threshold",
                                                      GTK_ICON_SIZE_MENU);
                        gtk_label_set_text (GTK_LABEL (section->priv->health_status_label),
                                            _("Passed"));
                        gtk_label_set_markup (GTK_LABEL (section->priv->health_status_explanation_label),
                                              _("<small><i><b>"
                                                "One or more attributes failing."
                                                "</b></i></small>"));
                } else if (attr_warn) {
                        gtk_image_set_from_icon_name (GTK_IMAGE (section->priv->health_status_image),
                                                      "gdu-smart-threshold",
                                                      GTK_ICON_SIZE_MENU);
                        gtk_label_set_text (GTK_LABEL (section->priv->health_status_label),
                                            _("Passed"));
                        gtk_label_set_markup (GTK_LABEL (section->priv->health_status_explanation_label),
                                              _("<small><i><b>"
                                                "One or more attributes non-zero but within threshold."
                                                "</b></i></small>"));
                } else {
                        gtk_image_set_from_icon_name (GTK_IMAGE (section->priv->health_status_image),
                                                      "gdu-smart-healthy",
                                                      GTK_ICON_SIZE_MENU);
                        gtk_label_set_text (GTK_LABEL (section->priv->health_status_label),
                                            _("Passed"));
                        gtk_widget_hide (section->priv->health_status_explanation_label);
                }
        } else {
                gtk_image_set_from_icon_name (GTK_IMAGE (section->priv->health_status_image),
                                              "gdu-smart-failing",
                                              GTK_ICON_SIZE_MENU);
                gtk_label_set_markup (GTK_LABEL (section->priv->health_status_label), _("<span foreground='red'><b>FAILING</b></span>"));
                gtk_label_set_markup (GTK_LABEL (section->priv->health_status_explanation_label),
                                      _("<small><i><b>"
                                        "Drive failure expected in less than 24 hours. "
                                        "Save all data immediately."
                                        "</b></i></small>"));
        }
        /* TODO: use gdu-smart-threshold if one or more attributes exceeds threshold */

        if (power_on_hours < 24)
                s = g_strdup_printf (_("%d hours"), power_on_hours);
        else {
                int d;
                int h;

                d = power_on_hours / 24;
                h = power_on_hours - d * 24;

                if (d == 0)
                        s = g_strdup_printf (_("%d days"), d);
                else if (d == 1)
                        s = g_strdup_printf (_("%d days, 1 hour"), d);
                else
                        s = g_strdup_printf (_("%d days, %d hours"), d, h);
        }
        gtk_label_set_text (GTK_LABEL (section->priv->health_power_on_hours_label), s);
        g_free (s);

        fahrenheit = 9.0 * temperature / 5.0 + 32.0;
        s = g_strdup_printf (_("%g° C / %g° F"), temperature, fahrenheit);
        gtk_label_set_text (GTK_LABEL (section->priv->health_temperature_label), s);
        g_free (s);

        updated.tv_sec = gdu_smart_data_get_time_collected (sd);
        updated.tv_usec = 0;
        gdu_time_label_set_time (GDU_TIME_LABEL (section->priv->health_updated_label), &updated);

        last = _("Unknown");
        if (strcmp (last_self_test_result, "completed_ok") == 0) {
                last = _("Completed OK");
        } else if (strcmp (last_self_test_result, "not_completed_aborted") == 0) {
                last = _("Cancelled");
        } else if (strcmp (last_self_test_result, "not_completed_aborted_reset") == 0) {
                last = _("Cancelled (with hard or soft reset)");
        } else if (strcmp (last_self_test_result, "not_completed_unknown_reason") == 0) {
                last = _("Not completed (a fatal error might have occured)");
        } else if (strcmp (last_self_test_result, "completed_failed_electrical") == 0) {
                last = _("<span foreground='red'><b>FAILED</b></span> (electrical test)");
        } else if (strcmp (last_self_test_result, "completed_failed_servo") == 0) {
                last = _("<span foreground='red'><b>FAILED</b></span> (servo/seek test)");
        } else if (strcmp (last_self_test_result, "completed_failed_read") == 0) {
                last = _("<span foreground='red'><b>FAILED</b></span> (read test)");
        } else if (strcmp (last_self_test_result, "completed_failed_damage") == 0) {
                last = _("<span foreground='red'><b>FAILED</b></span> (device is suspected of having handled damage");
        }
        gtk_label_set_markup (GTK_LABEL (section->priv->health_last_self_test_result_label), last);

out:
        g_free (last_self_test_result);
        if (device != NULL)
                g_object_unref (device);
        if (sd != NULL)
                g_object_unref (sd);
}

static void
retrieve_smart_data_cb (GduDevice  *device,
                        GError     *error,
                        gpointer    user_data)
{
        GduSectionHealth *section = GDU_SECTION_HEALTH (user_data);

        if (error != NULL) {
                smart_data_set_not_supported (section);
                g_error_free (error);
                goto out;
        }

        smart_data_set (section);

out:
        g_object_unref (section);
}

static void
health_refresh_action_callback (GtkAction *action, gpointer user_data)
{
        GduSectionHealth *section = GDU_SECTION_HEALTH (user_data);
        GduDevice *device;

        device = gdu_presentable_get_device (gdu_section_get_presentable (GDU_SECTION (section)));
        if (device == NULL) {
                g_warning ("%s: device is not supposed to be NULL", __FUNCTION__);
                goto out;
        }

        smart_data_set_pending (section);
        gdu_device_drive_smart_refresh_data (device, retrieve_smart_data_cb, g_object_ref (section));

out:
        if (device != NULL)
                g_object_unref (device);
}

enum
{
        ATTR_ID_INT_COLUMN,
        ATTR_ID_COLUMN,
        ATTR_DESC_COLUMN,
        ATTR_VALUE_COLUMN,
        ATTR_WORST_COLUMN,
        ATTR_THRESHOLD_COLUMN,
        ATTR_RAW_COLUMN,
        ATTR_STATUS_PIXBUF_COLUMN,
        ATTR_STATUS_TEXT_COLUMN,
        ATTR_TOOLTIP_COLUMN,
        ATTR_N_COLUMNS,
};

typedef struct
{
        GList *history;
        GtkWidget *drawing_area;
        GtkWidget *history_combo_box;
        GtkWidget *tree_view;
} HealthGraphData;

typedef struct
{
        GArray *points;
} Segment;

static Segment *
segment_new (void)
{
        Segment *s;
        s = g_new0 (Segment, 1);
        s->points = g_array_new (FALSE, FALSE, sizeof (double));
        return s;
}

static void
segment_add_point (Segment *s, double x, double y)
{
        g_array_append_val (s->points, x);
        g_array_append_val (s->points, y);
}

static void
segment_free (Segment *s)
{
        g_array_free (s->points, TRUE);
        g_free (s);
}

static void
segment_draw (Segment *s, cairo_t *cr)
{
        double x, y;
        int num_points;
        double *p;
        int n;

        p = (double *) s->points->data;

        num_points = s->points->len / 2;
        if (num_points == 1) {
                x = p[0];
                y = p[1];
                cairo_arc (cr, x, y, cairo_get_line_width (cr), 0, 2 * M_PI);
                cairo_fill (cr);

        } else {
                /* TODO: fit a smooth curve */

                cairo_new_path (cr);
                for (n = 0; n < num_points; n++) {
                        x = p[n * 2 + 0];
                        y = p[n * 2 + 1];
                        cairo_line_to (cr, x, y);
                }
                cairo_stroke (cr);
        }
}

static double
segment_normalize_y (double y, double min_y, double max_y, double ypos, double height)
{
        double ny;
        ny = ypos + (y - min_y) * height / (max_y - min_y);
        return ny;
}

static void
segment_draw_normalized (Segment *s, cairo_t *cr, double min_y, double max_y, double ypos, double height)
{
        double x, y, ny;
        int num_points;
        double *p;
        int n;

        p = (double *) s->points->data;

        num_points = s->points->len / 2;
        if (num_points == 1) {
                x = p[0];
                y = p[1];
                ny = segment_normalize_y (y, min_y, max_y, ypos, height);
                cairo_arc (cr, x, ny, cairo_get_line_width (cr), 0, 2 * M_PI);
                cairo_fill (cr);

        } else {
                /* TODO: fit a smooth curve */
                cairo_new_path (cr);
                for (n = 0; n < num_points; n++) {
                        x = p[n * 2 + 0];
                        y = p[n * 2 + 1];
                        ny = segment_normalize_y (y, min_y, max_y, ypos, height);
                        cairo_line_to (cr, x, ny);
                }
                cairo_stroke (cr);
        }
}

typedef struct
{
        GPtrArray *segments;
} SegmentSet;

static SegmentSet *
segment_set_new (void)
{
        SegmentSet *ss;
        ss = g_new0 (SegmentSet, 1);
        ss->segments = g_ptr_array_new ();
        return ss;
}

static void
segment_set_free (SegmentSet *ss)
{
        g_ptr_array_foreach (ss->segments, (GFunc) segment_free, NULL);
        g_ptr_array_free (ss->segments, TRUE);
        g_free (ss);
}

static void
segment_set_add_point (SegmentSet *ss, double x, double y)
{
        if (ss->segments->len == 0) {
                g_ptr_array_add (ss->segments, segment_new ());
        }
        segment_add_point (ss->segments->pdata[ss->segments->len - 1], x, y);
}

static void
segment_set_close (SegmentSet *ss)
{
        g_ptr_array_add (ss->segments, segment_new ());
}

static void
segment_set_draw (SegmentSet *ss, cairo_t *cr)
{
        g_ptr_array_foreach (ss->segments, (GFunc) segment_draw, cr);
}

static void
segment_set_draw_normalized (SegmentSet *ss, double ypos, double height,
                             double *out_min_y, double *out_max_y,
                             cairo_t *cr)
{
        unsigned int n, m;
        double min_y;
        double max_y;

        max_y = -G_MAXDOUBLE;
        min_y = G_MAXDOUBLE;
        for (n = 0; n < ss->segments->len; n++) {
                Segment *s = ss->segments->pdata[n];
                for (m = 0; m < s->points->len; m += 2) {
                        double y;
                        y = g_array_index (s->points, double, m + 1);
                        if (y < min_y)
                                min_y = y;
                        if (y > max_y)
                                max_y = y;
                }
        }

        for (n = 0; n < ss->segments->len; n++) {
                Segment *s = ss->segments->pdata[n];
                segment_draw_normalized (s, cr, min_y, max_y, ypos, height);
        }

        if (out_min_y != NULL)
                *out_min_y = min_y;

        if (out_max_y != NULL)
                *out_max_y = max_y;
}


static gboolean
expose_event_callback (GtkWidget *widget, GdkEventExpose *event, gpointer user_data)
{
        HealthGraphData *data = (HealthGraphData *) user_data;
        cairo_t *cr;
        double width, height;

        width = widget->allocation.width;
        height = widget->allocation.height;

        cr = gdk_cairo_create (widget->window);
        cairo_rectangle (cr,
                         event->area.x, event->area.y,
                         event->area.width, event->area.height);
        cairo_clip (cr);

        double gx, gy, gw, gh;
        gx = 30;
        gy = 10;
        gw = width - gx - 10;
        gh = height - gy - 30;

        cairo_set_source_rgb (cr, 1, 1, 1);
        cairo_rectangle (cr, gx, gy, gw, gh);
        cairo_set_line_width (cr, 0.0);
	cairo_fill (cr);

        int n;
        int num_y_markers;
        double val_y_top;
        double val_y_bottom;

        /* draw temperature markers on y-axis */
        num_y_markers = 5;
        val_y_top = 75.0;
        val_y_bottom = 15.0;
        for (n = 0; n < num_y_markers; n++) {
                double pos;
                double val;

                pos = ceil (gy + gh / (num_y_markers - 1) * n);

                val = val_y_top - (val_y_top - val_y_bottom) * n / (num_y_markers - 1);

                char *s;
                s = g_strdup_printf (_("%g°"), ceil (val));

                cairo_text_extents_t te;
                cairo_set_source_rgb (cr, 0.0, 0.0, 0.0);
                cairo_select_font_face (cr, "sans",
                                        CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
                cairo_set_font_size (cr, 8.0);
                cairo_text_extents (cr, s, &te);
                cairo_move_to (cr,
                               gx / 2 - te.width/2  - te.x_bearing,
                               pos - te.height/2 - te.y_bearing);

                cairo_show_text (cr, s);
                g_free (s);

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

        int num_x_markers;
        guint64 t_left;
        guint64 t_right;
        GTimeVal now;

        g_get_current_time (&now);
        switch (gtk_combo_box_get_active (GTK_COMBO_BOX (data->history_combo_box))) {
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

        /* draw time markers on x-axis */
        num_x_markers = 13;
        for (n = 0; n < num_x_markers; n++) {
                double pos;
                guint64 val;

                pos = ceil (gx + gw / (num_x_markers - 1) * n);
                val = t_left + (t_right - t_left) * n / (num_x_markers - 1);

                int age;
                age = (int) (now.tv_sec - val);

                char *s;
                if (age == 0) {
                        s = g_strdup_printf (_("now"));
                } else if (age < 3600) {
                        s = g_strdup_printf ("%dm", age / 60);
                } else if (age < 24 * 3600) {
                        int h = age/3600;
                        int m = (age%3600) / 60;
                        if (m == 0)
                                s = g_strdup_printf ("%dh", h);
                        else
                                s = g_strdup_printf ("%dh %dm", h, m);
                } else {
                        int d = age/(24*3600);
                        int h = (age%(24*3600)) / 3600;
                        int m = (age%3600) / 60;
                        if (h == 0 && m == 0)
                                s = g_strdup_printf ("%dd", d);
                        else if (m == 0)
                                s = g_strdup_printf ("%dd %dh", d, h);
                        else
                                s = g_strdup_printf ("%dd %dh %dm", d, h, m);
                }

                cairo_text_extents_t te;
                cairo_set_source_rgb (cr, 0.0, 0.0, 0.0);
                cairo_select_font_face (cr, "sans",
                                        CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
                cairo_set_font_size (cr, 8.0);
                cairo_text_extents (cr, s, &te);
                cairo_move_to (cr,
                               pos - te.width/2  - te.x_bearing,
                               height - 30.0/2  - te.height/2 - te.y_bearing); /* TODO */

                cairo_show_text (cr, s);
                g_free (s);

                cairo_set_line_width (cr, 1.0);
                double dashes[1] = {2.0};
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

        GtkTreeSelection *tree_selection;
        GtkTreeModel *tree_model;
        GtkTreeIter iter;
        int selected_attr_id;

        selected_attr_id = -1;

        tree_selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (data->tree_view));
        if (gtk_tree_selection_get_selected (tree_selection, &tree_model, &iter)) {
                gtk_tree_model_get (tree_model, &iter,
                                    ATTR_ID_INT_COLUMN,
                                    &selected_attr_id,
                                    -1);
        }

        SegmentSet *temperature_segset;
        SegmentSet *attr_value_segset;
        SegmentSet *attr_thres_segset;
        SegmentSet *attr_raw_segset;
        temperature_segset = segment_set_new ();
        attr_value_segset = segment_set_new ();
        attr_thres_segset = segment_set_new ();
        attr_raw_segset = segment_set_new ();

        /* draw temperature graph (TODO: draw a smooth curve) */
        GList *l;

        cairo_new_path (cr);
        cairo_set_dash (cr, NULL, 0, 0.0);

        int num_segments;
        guint64 prev_time_collected;
        double last_segment_xpos;
        prev_time_collected = 0;
        num_segments = 0;
        last_segment_xpos = 0;
        for (l = data->history; l != NULL; l = l->next) {
                double x;
                double temperature_y;
                GduSmartData *sd = GDU_SMART_DATA (l->data);

                x = gx + gw * ((double) gdu_smart_data_get_time_collected (sd) - (double) t_left) /
                        ((double) t_right - (double) t_left);

                if (x < gx) {
                        /* point is not in graph.. but do consider it if the *following* point is */

                        if (l->next != NULL) {
                                GduSmartData *nsd = GDU_SMART_DATA (l->next->data);
                                double nx;
                                nx = gx + gw * ((double) gdu_smart_data_get_time_collected (nsd) - (double) t_left) /
                                        ((double) t_right - (double) t_left);
                                if (nx < gx)
                                        continue;
                        } else {
                                continue;
                        }
                }

                /* If there's a discontinuity in the samples (more than 30 minutes between consecutive
                 * samples), draw a grey rectangle to convey this
                 */
                if (prev_time_collected != 0 && (gdu_smart_data_get_time_collected (sd) -
                                                 prev_time_collected) > 30 * 60) {
                        cairo_pattern_t *pat;
                        double stop_size;

                        segment_set_close (temperature_segset);
                        segment_set_close (attr_value_segset);
                        segment_set_close (attr_thres_segset);
                        segment_set_close (attr_raw_segset);

                        /* make sure the gradient looks similar and that it doesn't
                         * depend on the width of the rectangle
                         */
                        if (x - last_segment_xpos <= 60) {
                                stop_size = 0.3;
                        } else {
                                stop_size = 0.3 * 60.0 / (x - last_segment_xpos);
                        }
                        pat = cairo_pattern_create_linear (last_segment_xpos, gy,
                                                           x, gy);
                        cairo_pattern_add_color_stop_rgba (pat, 0.0, 1.0, 1.0, 1.0, 0.5);
                        cairo_pattern_add_color_stop_rgba (pat, stop_size, 0.9, 0.9, 0.9, 0.5);
                        cairo_pattern_add_color_stop_rgba (pat, 1.0 - stop_size, 0.9, 0.9, 0.9, 0.5);
                        cairo_pattern_add_color_stop_rgba (pat, 1.0, 1.0, 1.0, 1.0, 0.5);
                        cairo_set_source (cr, pat);
                        cairo_rectangle (cr,
                                         last_segment_xpos, gy,
                                         x - last_segment_xpos, gh);
                        cairo_fill (cr);
                        cairo_pattern_destroy (pat);
                }

                temperature_y = gy + gh - gh * (gdu_smart_data_get_temperature (sd) - val_y_bottom) /
                        (val_y_top - val_y_bottom);
                segment_set_add_point (temperature_segset, x, temperature_y);

                if (selected_attr_id != -1) {
                        GduSmartDataAttribute *a;

                        a = gdu_smart_data_get_attribute (sd, selected_attr_id);
                        if (a != NULL) {
                                double attr_value_y;
                                double attr_thres_y;
                                double attr_raw_y;
                                int attr_value;
                                int attr_thres;

                                attr_value = gdu_smart_data_attribute_get_value (a);
                                attr_thres = gdu_smart_data_attribute_get_threshold (a);

                                attr_value_y = gy + (256 - attr_value) * gh / 256.0;
                                attr_thres_y = gy + (256 - attr_thres) * gh / 256.0;

                                segment_set_add_point (attr_value_segset, x, attr_value_y);
                                segment_set_add_point (attr_thres_segset, x, attr_thres_y);

                                attr_raw_y = atof (gdu_smart_data_attribute_get_raw (a));
                                segment_set_add_point (attr_raw_segset, x, attr_raw_y);

                                g_object_unref (a);
                        }
                }

                prev_time_collected = gdu_smart_data_get_time_collected (sd);
                last_segment_xpos = x;
        }

        cairo_set_line_width (cr, 1.0);
        cairo_set_source_rgb (cr, 1.0, 0.64, 0.0);
        segment_set_draw (temperature_segset, cr);

        if (selected_attr_id != -1) {
                double min_y, max_y;
                double dashes[1] = {1.0};
                cairo_set_dash (cr, dashes, 1, 0.0);
                cairo_set_line_width (cr, 1.0);
                cairo_set_source_rgb (cr, 0.5, 0.5, 1.0);
                segment_set_draw_normalized (attr_raw_segset, gy + gh, -gh, &min_y, &max_y, cr);
                //g_warning ("min_y = %g, max_y = %g", min_y, max_y);

                cairo_set_line_width (cr, 1.5);
                cairo_set_dash (cr, NULL, 0, 0.0);
                cairo_set_source_rgb (cr, 0.0, 0.7, 0.0);
                segment_set_draw (attr_value_segset, cr);

                cairo_set_source_rgb (cr, 1.0, 0.2, 0.2);
                segment_set_draw (attr_thres_segset, cr);
        }

        segment_set_free (temperature_segset);
        segment_set_free (attr_value_segset);
        segment_set_free (attr_thres_segset);

        cairo_destroy (cr);
        return TRUE;
}

static void
history_combo_box_changed (GtkWidget *combo_box, gpointer user_data)
{
        HealthGraphData *data = (HealthGraphData *) user_data;
        gtk_widget_queue_draw_area (data->drawing_area,
                                    0,
                                    0,
                                    data->drawing_area->allocation.width,
                                    data->drawing_area->allocation.height);
}

static void
smart_attr_tree_selection_changed (GtkTreeSelection *treeselection, gpointer user_data)
{
        HealthGraphData *data = (HealthGraphData *) user_data;
        gtk_widget_queue_draw_area (data->drawing_area,
                                    0,
                                    0,
                                    data->drawing_area->allocation.width,
                                    data->drawing_area->allocation.height);
}

static void
health_details_action_callback (GtkAction *action, gpointer user_data)
{
        GduSectionHealth *section = GDU_SECTION_HEALTH (user_data);
        GduDevice *device;
        GtkWidget *dialog;
        GtkWidget *vbox;
        GtkWidget *scrolled_window;
        GtkWidget *tree_view;
        GtkListStore *list_store;
        GtkCellRenderer *renderer;
        GtkTreeViewColumn *column;
        GduSmartData *sd;
        GList *attrs;
        GList *l;

        sd = NULL;
        attrs = NULL;

        device = gdu_presentable_get_device (gdu_section_get_presentable (GDU_SECTION (section)));
        if (device == NULL) {
                g_warning ("%s: device is not supposed to be NULL", __FUNCTION__);
                goto out;
        }

        dialog = gtk_dialog_new_with_buttons (_("S.M.A.R.T. Attributes"),
                                              GTK_WINDOW (gdu_shell_get_toplevel (gdu_section_get_shell (GDU_SECTION (section)))),
                                              GTK_DIALOG_MODAL|GTK_DIALOG_DESTROY_WITH_PARENT|GTK_DIALOG_NO_SEPARATOR,
                                              GTK_STOCK_CLOSE,
                                              GTK_RESPONSE_CLOSE,
                                              NULL);

	gtk_container_set_border_width (GTK_CONTAINER (dialog), 5);
	gtk_box_set_spacing (GTK_BOX (GTK_DIALOG (dialog)->vbox), 2);
	gtk_container_set_border_width (GTK_CONTAINER (GTK_DIALOG (dialog)->action_area), 5);
	gtk_box_set_spacing (GTK_BOX (GTK_DIALOG (dialog)->action_area), 6);
	gtk_window_set_resizable (GTK_WINDOW (dialog), TRUE);
        gtk_window_set_default_size (GTK_WINDOW (dialog), 400, 400);

	vbox = gtk_vbox_new (FALSE, 12);
	gtk_container_set_border_width (GTK_CONTAINER (vbox), 5);
	gtk_box_pack_start (GTK_BOX (GTK_DIALOG (dialog)->vbox), vbox, TRUE, TRUE, 0);

        HealthGraphData *data;
        data = g_new0 (HealthGraphData, 1);
        data->history = gdu_device_drive_smart_get_historical_data_sync (device, NULL);

        GtkWidget *history_label;
        history_label = gtk_label_new (_("View:"));

        GtkWidget *history_combo_box;
        history_combo_box = gtk_combo_box_new_text ();
        gtk_combo_box_append_text (GTK_COMBO_BOX (history_combo_box), _("6 hours"));
        gtk_combo_box_append_text (GTK_COMBO_BOX (history_combo_box), _("24 hours"));
        gtk_combo_box_append_text (GTK_COMBO_BOX (history_combo_box), _("3 days"));
        gtk_combo_box_append_text (GTK_COMBO_BOX (history_combo_box), _("12 days"));
        gtk_combo_box_append_text (GTK_COMBO_BOX (history_combo_box), _("36 days"));
        gtk_combo_box_append_text (GTK_COMBO_BOX (history_combo_box), _("96 days"));
        gtk_combo_box_set_active (GTK_COMBO_BOX (history_combo_box), 0);
        data->history_combo_box = history_combo_box;

        GtkWidget *drawing_area;
        drawing_area = gtk_drawing_area_new ();
        gtk_widget_set_size_request (drawing_area, 100, 100);
        g_signal_connect (drawing_area, "expose-event", G_CALLBACK (expose_event_callback), data);
	gtk_box_pack_start (GTK_BOX (vbox), drawing_area, TRUE, TRUE, 0);
        data->drawing_area = drawing_area;

        GtkWidget *hbox;
        hbox = gtk_hbox_new (FALSE, 12);
	gtk_box_pack_start (GTK_BOX (hbox), history_label, FALSE, TRUE, 0);
	gtk_box_pack_start (GTK_BOX (hbox), history_combo_box, FALSE, TRUE, 0);
	gtk_box_pack_start (GTK_BOX (vbox), hbox, FALSE, TRUE, 0);
        g_signal_connect (history_combo_box, "changed", G_CALLBACK (history_combo_box_changed), data);

        list_store = gtk_list_store_new (ATTR_N_COLUMNS,
                                         G_TYPE_INT,
                                         G_TYPE_STRING,
                                         G_TYPE_STRING,
                                         G_TYPE_STRING,
                                         G_TYPE_STRING,
                                         G_TYPE_STRING,
                                         G_TYPE_STRING,
                                         GDK_TYPE_PIXBUF,
                                         G_TYPE_STRING,
                                         G_TYPE_STRING);

        tree_view = gtk_tree_view_new_with_model (GTK_TREE_MODEL (list_store));
        gtk_tree_view_set_rules_hint (GTK_TREE_VIEW (tree_view), TRUE);
        gtk_tree_view_set_tooltip_column (GTK_TREE_VIEW (tree_view), ATTR_TOOLTIP_COLUMN);
        data->tree_view = tree_view;
        g_signal_connect (gtk_tree_view_get_selection (GTK_TREE_VIEW (data->tree_view)),
                          "changed",
                          G_CALLBACK (smart_attr_tree_selection_changed), data);

        column = gtk_tree_view_column_new ();
        gtk_tree_view_column_set_title (column, "ID");
        renderer = gtk_cell_renderer_text_new ();
        gtk_tree_view_column_pack_start (column, renderer, TRUE);
        gtk_tree_view_column_set_attributes (column, renderer,
                                             "text", ATTR_ID_COLUMN,
                                             NULL);
        gtk_tree_view_append_column (GTK_TREE_VIEW (tree_view), column);

        column = gtk_tree_view_column_new ();
        gtk_tree_view_column_set_title (column, "Attribute");
        renderer = gtk_cell_renderer_text_new ();
        gtk_tree_view_column_pack_start (column, renderer, TRUE);
        gtk_tree_view_column_set_attributes (column, renderer,
                                             "text", ATTR_DESC_COLUMN,
                                             NULL);
        gtk_tree_view_append_column (GTK_TREE_VIEW (tree_view), column);

        column = gtk_tree_view_column_new ();
        gtk_tree_view_column_set_title (column, "Value");
        renderer = gtk_cell_renderer_text_new ();
        g_object_set (renderer, "xalign", 1.0, NULL);
        gtk_tree_view_column_pack_start (column, renderer, TRUE);
        gtk_tree_view_column_set_attributes (column, renderer,
                                             "text", ATTR_VALUE_COLUMN,
                                             NULL);
        gtk_tree_view_append_column (GTK_TREE_VIEW (tree_view), column);

        column = gtk_tree_view_column_new ();
        gtk_tree_view_column_set_title (column, "Worst");
        renderer = gtk_cell_renderer_text_new ();
        g_object_set (renderer, "xalign", 1.0, NULL);
        gtk_tree_view_column_pack_start (column, renderer, TRUE);
        gtk_tree_view_column_set_attributes (column, renderer,
                                             "text", ATTR_WORST_COLUMN,
                                             NULL);
        gtk_tree_view_append_column (GTK_TREE_VIEW (tree_view), column);


        column = gtk_tree_view_column_new ();
        gtk_tree_view_column_set_title (column, "Threshold");
        renderer = gtk_cell_renderer_text_new ();
        g_object_set (renderer, "xalign", 1.0, NULL);
        gtk_tree_view_column_pack_start (column, renderer, TRUE);
        gtk_tree_view_column_set_attributes (column, renderer,
                                             "text", ATTR_THRESHOLD_COLUMN,
                                             NULL);
        gtk_tree_view_append_column (GTK_TREE_VIEW (tree_view), column);

        column = gtk_tree_view_column_new ();
        gtk_tree_view_column_set_title (column, "Raw Value");
        renderer = gtk_cell_renderer_text_new ();
        g_object_set (renderer, "xalign", 1.0, NULL);
        gtk_tree_view_column_pack_start (column, renderer, TRUE);
        gtk_tree_view_column_set_attributes (column, renderer,
                                             "text", ATTR_RAW_COLUMN,
                                             NULL);
        gtk_tree_view_append_column (GTK_TREE_VIEW (tree_view), column);

        column = gtk_tree_view_column_new ();
        gtk_tree_view_column_set_title (column, "Status");
        renderer = gtk_cell_renderer_pixbuf_new ();
        gtk_tree_view_column_pack_start (column, renderer, FALSE);
        gtk_tree_view_column_set_attributes (column, renderer,
                                             "pixbuf", ATTR_STATUS_PIXBUF_COLUMN,
                                             NULL);
        renderer = gtk_cell_renderer_text_new ();
        gtk_tree_view_column_pack_start (column, renderer, TRUE);
        gtk_tree_view_column_set_attributes (column, renderer,
                                             "markup", ATTR_STATUS_TEXT_COLUMN,
                                             NULL);
        gtk_tree_view_append_column (GTK_TREE_VIEW (tree_view), column);


        scrolled_window = gtk_scrolled_window_new (NULL, NULL);
        gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scrolled_window),
                                        GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
        gtk_scrolled_window_set_shadow_type (GTK_SCROLLED_WINDOW (scrolled_window),
                                             GTK_SHADOW_IN);
        gtk_container_add (GTK_CONTAINER (scrolled_window), tree_view);

	gtk_box_pack_start (GTK_BOX (vbox), scrolled_window, TRUE, TRUE, 0);

        sd = gdu_device_get_smart_data (device);
        if (sd != NULL)
                attrs = gdu_smart_data_get_attributes (sd);
        for (l = attrs; l != NULL; l = l->next) {
                GduSmartDataAttribute *a = l->data;
                GtkTreeIter iter;
                char *col_str;
                char *name_str;
                char *value_str;
                char *worst_str;
                char *threshold_str;
                char *raw_str;
                char *status_str;
                GdkPixbuf *status_pixbuf;
                char *tooltip_str;
                int icon_width, icon_height;
                gboolean threshold_exceeded;
                gboolean threshold_exceeded_in_the_past;
                gboolean should_warn;
                char *desc_str;

                col_str = g_strdup_printf ("%d", gdu_smart_data_attribute_get_id (a));

                name_str = gdu_smart_data_attribute_get_name (a);
                desc_str = gdu_smart_data_attribute_get_description (a);
                should_warn = gdu_smart_data_attribute_is_warning (a);

                if (desc_str == NULL) {
                        desc_str = g_strdup_printf (_("No description for attribute %d."),
                                                    gdu_smart_data_attribute_get_id (a));
                }

                tooltip_str = g_strdup_printf (_("<b>Flags:</b> 0x%04x\n"
                                                 "<b>Type:</b> %s\n"
                                                 "<b>Updated:</b> %s\n"
                                                 "<b>Description</b>: %s"),
                                               gdu_smart_data_attribute_get_flags (a),
                                               (gdu_smart_data_attribute_get_flags (a) & 0x0001) ? _("Pre-Fail") : _("Old-Age"),
                                               (gdu_smart_data_attribute_get_flags (a) & 0x0002) ? _("Always") : _("Offline"),
                                               desc_str);

                value_str = g_strdup_printf ("%d", gdu_smart_data_attribute_get_value (a));
                worst_str = g_strdup_printf ("%d", gdu_smart_data_attribute_get_worst (a));
                threshold_str = g_strdup_printf ("%d", gdu_smart_data_attribute_get_threshold (a));
                raw_str = gdu_smart_data_attribute_get_raw (a);

                threshold_exceeded = (gdu_smart_data_attribute_get_value (a) < gdu_smart_data_attribute_get_threshold (a));
                threshold_exceeded_in_the_past = (gdu_smart_data_attribute_get_worst (a) < gdu_smart_data_attribute_get_threshold (a));

                if (!gtk_icon_size_lookup (GTK_ICON_SIZE_MENU, &icon_width, &icon_height))
                        icon_height = 48;

                if (threshold_exceeded) {
                        status_pixbuf = gtk_icon_theme_load_icon (gtk_icon_theme_get_default (),
                                                                  "gdu-smart-failing",
                                                                  icon_height,
                                                                  GTK_ICON_LOOKUP_GENERIC_FALLBACK,
                                                                  NULL);
                        status_str = g_strdup (_("<span foreground='red'><b>FAILING NOW</b></span>"));
                } else if (threshold_exceeded_in_the_past) {
                        status_pixbuf = gtk_icon_theme_load_icon (gtk_icon_theme_get_default (),
                                                                  "gdu-smart-threshold",
                                                                  icon_height,
                                                                  GTK_ICON_LOOKUP_GENERIC_FALLBACK,
                                                                  NULL);
                        status_str = g_strdup (_("OK (Failed in the past)"));
                } else {
                        if (should_warn) {
                                status_pixbuf = gtk_icon_theme_load_icon (gtk_icon_theme_get_default (),
                                                                          "gdu-smart-threshold",
                                                                          icon_height,
                                                                          GTK_ICON_LOOKUP_GENERIC_FALLBACK,
                                                                          NULL);
                                status_str = g_strdup (_("OK (Non-zero)"));
                        } else {
                                status_pixbuf = gtk_icon_theme_load_icon (gtk_icon_theme_get_default (),
                                                                          "gdu-smart-healthy",
                                                                          icon_height,
                                                                          GTK_ICON_LOOKUP_GENERIC_FALLBACK,
                                                                          NULL);
                                status_str = g_strdup (_("OK"));
                        }
                }

                gtk_list_store_append (list_store, &iter);
                gtk_list_store_set (list_store, &iter,
                                    ATTR_ID_INT_COLUMN, gdu_smart_data_attribute_get_id (a),
                                    ATTR_ID_COLUMN, col_str,
                                    ATTR_DESC_COLUMN, name_str,
                                    ATTR_VALUE_COLUMN, value_str,
                                    ATTR_WORST_COLUMN, worst_str,
                                    ATTR_THRESHOLD_COLUMN, threshold_str,
                                    ATTR_RAW_COLUMN, raw_str,
                                    ATTR_STATUS_PIXBUF_COLUMN, status_pixbuf,
                                    ATTR_STATUS_TEXT_COLUMN, status_str,
                                    ATTR_TOOLTIP_COLUMN, tooltip_str,
                                    -1);
                g_free (col_str);
                g_free (name_str);
                g_free (value_str);
                g_free (worst_str);
                g_free (threshold_str);
                g_free (raw_str);
                g_object_unref (status_pixbuf);
                g_free (status_str);
                g_free (tooltip_str);
                g_free (desc_str);
        }

        g_object_unref (list_store);


        gtk_widget_show_all (dialog);
        gtk_dialog_run (GTK_DIALOG (dialog));
        gtk_widget_destroy (dialog);

        //smart_data_set_pending (section);
        //gdu_device_drive_smart_refresh_data (device, retrieve_smart_data_cb, g_object_ref (section));

        g_list_foreach (data->history, (GFunc) g_object_unref, NULL);
        g_list_free (data->history);
        g_free (data);

out:
        if (device != NULL)
                g_object_unref (device);
        if (attrs != NULL) {
                g_list_foreach (attrs, (GFunc) g_object_unref, NULL);
                g_list_free (attrs);
        }
        if (sd != NULL)
                g_object_unref (sd);
}

static void
run_smart_selftest_callback (GduDevice *device,
                             GError *error,
                             gpointer user_data)
{
        GduSection *section = GDU_SECTION (user_data);
        if (error != NULL) {
                gdu_shell_raise_error (gdu_section_get_shell (section),
                                       gdu_section_get_presentable (section),
                                       error,
                                       _("Error initiating S.M.A.R.T. Self Test"));
        }
        g_object_unref (section);
}

static void
health_selftest_action_callback (GtkAction *action, gpointer user_data)
{
        int response;
        GtkWidget *dialog;
        GduSectionHealth *section = GDU_SECTION_HEALTH (user_data);
        GduDevice *device;
        GtkWidget *hbox;
        GtkWidget *image;
        GtkWidget *main_vbox;
        GtkWidget *label;
        GtkWidget *radio0;
        GtkWidget *radio1;
        const char *test;

        test = NULL;

        device = gdu_presentable_get_device (gdu_section_get_presentable (GDU_SECTION (section)));
        if (device == NULL) {
                g_warning ("%s: device is not supposed to be NULL", __FUNCTION__);
                goto out;
        }


        dialog = gtk_dialog_new_with_buttons (_("S.M.A.R.T. Self Test"),
                                              GTK_WINDOW (gdu_shell_get_toplevel (gdu_section_get_shell (GDU_SECTION (section)))),
                                              GTK_DIALOG_MODAL|GTK_DIALOG_DESTROY_WITH_PARENT|GTK_DIALOG_NO_SEPARATOR,
                                              NULL);

	gtk_container_set_border_width (GTK_CONTAINER (dialog), 5);
	gtk_box_set_spacing (GTK_BOX (GTK_DIALOG (dialog)->vbox), 2);
	gtk_container_set_border_width (GTK_CONTAINER (GTK_DIALOG (dialog)->action_area), 5);
	gtk_box_set_spacing (GTK_BOX (GTK_DIALOG (dialog)->action_area), 6);
	gtk_window_set_resizable (GTK_WINDOW (dialog), FALSE);

	hbox = gtk_hbox_new (FALSE, 12);
	gtk_container_set_border_width (GTK_CONTAINER (hbox), 5);
	gtk_box_pack_start (GTK_BOX (GTK_DIALOG (dialog)->vbox), hbox, TRUE, TRUE, 0);

	image = gtk_image_new_from_stock (GTK_STOCK_DIALOG_QUESTION, GTK_ICON_SIZE_DIALOG);
	gtk_misc_set_alignment (GTK_MISC (image), 0.5, 0.0);
	gtk_box_pack_start (GTK_BOX (hbox), image, FALSE, FALSE, 0);

	main_vbox = gtk_vbox_new (FALSE, 10);
	gtk_box_pack_start (GTK_BOX (hbox), main_vbox, TRUE, TRUE, 0);

	label = gtk_label_new (NULL);
        gtk_label_set_markup (GTK_LABEL (label), _("<big><b>Select what S.M.A.R.T. test to run on the drive.</b></big>"));
	gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
	gtk_label_set_line_wrap (GTK_LABEL (label), TRUE);
	gtk_box_pack_start (GTK_BOX (main_vbox), GTK_WIDGET (label), FALSE, FALSE, 0);

	label = gtk_label_new (NULL);
        gtk_label_set_markup (GTK_LABEL (label), _("The tests may take a very long time to complete depending "
                                                   "on the speed and size of the disk. You can continue using "
                                                   "your system while the test is running."));
	gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
	gtk_label_set_line_wrap (GTK_LABEL (label), TRUE);
	gtk_box_pack_start (GTK_BOX (main_vbox), GTK_WIDGET (label), FALSE, FALSE, 0);

        radio0 = gtk_radio_button_new_with_mnemonic_from_widget (NULL,
                                                                 _("_Short (usually less than ten minutes)"));
        radio1 = gtk_radio_button_new_with_mnemonic_from_widget (GTK_RADIO_BUTTON (radio0),
                                                                 _("_Long (usually tens of minutes)"));

	gtk_box_pack_start (GTK_BOX (main_vbox), GTK_WIDGET (radio0), FALSE, FALSE, 0);
	gtk_box_pack_start (GTK_BOX (main_vbox), GTK_WIDGET (radio1), FALSE, FALSE, 0);

        gtk_dialog_add_button (GTK_DIALOG (dialog), GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL);
        gtk_dialog_add_button (GTK_DIALOG (dialog), _("_Initiate Self Test"), 0);
        gtk_dialog_set_default_response (GTK_DIALOG (dialog), 0);

        gtk_widget_show_all (dialog);
        response = gtk_dialog_run (GTK_DIALOG (dialog));

        if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (radio0))) {
                test = "short";
        } else if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (radio1))) {
                test = "long";
        }

        gtk_widget_destroy (dialog);
        if (response != 0)
                goto out;

        /* TODO: option for captive */
        gdu_device_op_drive_smart_initiate_selftest (device,
                                                     test,
                                                     FALSE,
                                                     run_smart_selftest_callback,
                                                     g_object_ref (section));
out:
        if (device != NULL)
                g_object_unref (device);
}

/* ---------------------------------------------------------------------------------------------------- */

static void
update (GduSectionHealth *section)
{
        GduDevice *device;
        guint64 collect_time;
        GTimeVal now;
        GduSmartData *sd;

        sd = NULL;
        device = gdu_presentable_get_device (gdu_section_get_presentable (GDU_SECTION (section)));
        if (device == NULL) {
                g_warning ("%s: device is not supposed to be NULL", __FUNCTION__);
                goto out;
        }

        if (!gdu_device_drive_smart_get_is_capable (device)) {
                smart_data_set_not_supported (section);
                goto out;
        }

        /* refresh if data is more than an hour old */
        g_get_current_time (&now);
        sd = gdu_device_get_smart_data (device);
        collect_time = 0;
        if (sd != NULL)
                collect_time = gdu_smart_data_get_time_collected (sd);
        if (sd == NULL || collect_time == 0 || (now.tv_sec - collect_time) > 60 * 60) {
                smart_data_set_pending (section);
                gdu_device_drive_smart_refresh_data (device, retrieve_smart_data_cb, g_object_ref (section));
        } else {
                smart_data_set (section);
        }

out:
        if (device != NULL)
                g_object_unref (device);
        if (sd != NULL)
                g_object_unref (sd);
}

/* ---------------------------------------------------------------------------------------------------- */

static void
gdu_section_health_finalize (GduSectionHealth *section)
{
        polkit_action_unref (section->priv->pk_smart_refresh_action);
        polkit_action_unref (section->priv->pk_smart_retrieve_historical_data_action);
        polkit_action_unref (section->priv->pk_smart_selftest_action);
        g_object_unref (section->priv->health_refresh_action);
        g_object_unref (section->priv->health_details_action);
        g_object_unref (section->priv->health_selftest_action);
        if (G_OBJECT_CLASS (parent_class)->finalize)
                (* G_OBJECT_CLASS (parent_class)->finalize) (G_OBJECT (section));
}

static void
gdu_section_health_class_init (GduSectionHealthClass *klass)
{
        GObjectClass *obj_class = (GObjectClass *) klass;
        GduSectionClass *section_class = (GduSectionClass *) klass;

        parent_class = g_type_class_peek_parent (klass);

        obj_class->finalize = (GObjectFinalizeFunc) gdu_section_health_finalize;
        section_class->update = (gpointer) update;

        g_type_class_add_private (klass, sizeof (GduSectionHealthPrivate));
}

static void
gdu_section_health_init (GduSectionHealth *section)
{
        int row;
        GtkWidget *hbox;
        GtkWidget *vbox2;
        GtkWidget *label;
        GtkWidget *align;
        GtkWidget *table;
        GtkWidget *button;
        GtkWidget *button_box;
        GtkWidget *image;

        section->priv = G_TYPE_INSTANCE_GET_PRIVATE (section, GDU_TYPE_SECTION_HEALTH, GduSectionHealthPrivate);

        section->priv->pk_smart_refresh_action = polkit_action_new ();
        polkit_action_set_action_id (section->priv->pk_smart_refresh_action,
                                     "org.freedesktop.devicekit.disks.drive-smart-refresh");

        section->priv->pk_smart_retrieve_historical_data_action = polkit_action_new ();
        polkit_action_set_action_id (section->priv->pk_smart_retrieve_historical_data_action,
                                     "org.freedesktop.devicekit.disks.drive-smart-retrieve-historical-data");

        section->priv->pk_smart_selftest_action = polkit_action_new ();
        polkit_action_set_action_id (section->priv->pk_smart_selftest_action,
                                     "org.freedesktop.devicekit.disks.drive-smart-selftest");

        label = gtk_label_new (NULL);
        gtk_label_set_markup (GTK_LABEL (label), _("<b>Health</b>"));
        gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
        gtk_box_pack_start (GTK_BOX (section), label, FALSE, FALSE, 6);
        vbox2 = gtk_vbox_new (FALSE, 5);
        align = gtk_alignment_new (0.5, 0.5, 1.0, 1.0);
        gtk_alignment_set_padding (GTK_ALIGNMENT (align), 0, 0, 12, 0);
        gtk_container_add (GTK_CONTAINER (align), vbox2);
        gtk_box_pack_start (GTK_BOX (section), align, FALSE, TRUE, 0);

        /* explanatory text */
        label = gtk_label_new (NULL);
        gtk_label_set_markup (GTK_LABEL (label), _("Some disks supports S.M.A.R.T., a monitoring system for "
                                                   "disks to detect and report on various indicators of "
                                                   "reliability, in the hope of anticipating failures."));
        gtk_label_set_line_wrap (GTK_LABEL (label), TRUE);
        gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
        gtk_box_pack_start (GTK_BOX (vbox2), label, FALSE, TRUE, 0);

        table = gtk_table_new (4, 2, FALSE);
        gtk_box_pack_start (GTK_BOX (vbox2), table, FALSE, FALSE, 0);

        row = 0;


        /* power on hours */
        label = gtk_label_new (NULL);
        gtk_misc_set_alignment (GTK_MISC (label), 1.0, 0.5);
        gtk_label_set_markup_with_mnemonic (GTK_LABEL (label), _("<b>Powered On:</b>"));
        gtk_table_attach (GTK_TABLE (table), label, 0, 1, row, row + 1,
                          GTK_FILL, GTK_EXPAND | GTK_FILL, 2, 2);

        label = gtk_label_new (NULL);
        gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
        section->priv->health_power_on_hours_label = label;

        gtk_table_attach (GTK_TABLE (table), label, 1, 2, row, row + 1,
                          GTK_FILL, GTK_EXPAND | GTK_FILL, 2, 2);

        row++;

        /* temperature */
        label = gtk_label_new (NULL);
        gtk_misc_set_alignment (GTK_MISC (label), 1.0, 0.5);
        gtk_label_set_markup_with_mnemonic (GTK_LABEL (label), _("<b>Temperature:</b>"));
        gtk_table_attach (GTK_TABLE (table), label, 0, 1, row, row + 1,
                          GTK_FILL, GTK_EXPAND | GTK_FILL, 2, 2);

        label = gtk_label_new (NULL);
        gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
        section->priv->health_temperature_label = label;

        gtk_table_attach (GTK_TABLE (table), label, 1, 2, row, row + 1,
                          GTK_FILL, GTK_EXPAND | GTK_FILL, 2, 2);

        row++;

        /* last test */
        label = gtk_label_new (NULL);
        gtk_misc_set_alignment (GTK_MISC (label), 1.0, 0.5);
        gtk_label_set_markup_with_mnemonic (GTK_LABEL (label), _("<b>Last Test:</b>"));
        gtk_table_attach (GTK_TABLE (table), label, 0, 1, row, row + 1,
                          GTK_FILL, GTK_EXPAND | GTK_FILL, 2, 2);

        label = gtk_label_new (NULL);
        gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
        section->priv->health_last_self_test_result_label = label;

        gtk_table_attach (GTK_TABLE (table), label, 1, 2, row, row + 1,
                          GTK_FILL, GTK_EXPAND | GTK_FILL, 2, 2);

        row++;

        /* updated */
        label = gtk_label_new (NULL);
        gtk_misc_set_alignment (GTK_MISC (label), 1.0, 0.5);
        gtk_label_set_markup_with_mnemonic (GTK_LABEL (label), _("<b>Updated:</b>"));
        gtk_table_attach (GTK_TABLE (table), label, 0, 1, row, row + 1,
                          GTK_FILL, GTK_EXPAND | GTK_FILL, 2, 2);

        label = gdu_time_label_new (NULL);
        gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
        section->priv->health_updated_label = label;

        gtk_table_attach (GTK_TABLE (table), label, 1, 2, row, row + 1,
                          GTK_FILL, GTK_EXPAND | GTK_FILL, 2, 2);

        row++;

        /* assessment */
        label = gtk_label_new (NULL);
        gtk_misc_set_alignment (GTK_MISC (label), 1.0, 0.5);
        gtk_label_set_markup_with_mnemonic (GTK_LABEL (label), _("<b>Assessment:</b>"));
        gtk_table_attach (GTK_TABLE (table), label, 0, 1, row, row + 1,
                          GTK_FILL, GTK_EXPAND | GTK_FILL, 2, 2);

        hbox = gtk_hbox_new (FALSE, 5);
        image = gtk_image_new ();
        gtk_box_pack_start (GTK_BOX (hbox), image, FALSE, TRUE, 0);
        label = gtk_label_new (NULL);
        gtk_box_pack_start (GTK_BOX (hbox), label, FALSE, TRUE, 0);
        gtk_table_attach (GTK_TABLE (table), hbox, 1, 2, row, row + 1,
                          GTK_FILL, GTK_EXPAND | GTK_FILL, 2, 2);
        section->priv->health_status_image = image;
        section->priv->health_status_label = label;

        row++;

        label = gtk_label_new (NULL);
        gtk_label_set_line_wrap (GTK_LABEL (label), TRUE);
        gtk_label_set_width_chars (GTK_LABEL (label), 40);
        gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
        gtk_table_attach (GTK_TABLE (table), label, 1, 2, row, row + 1,
                          GTK_FILL, GTK_EXPAND | GTK_FILL, 2, 2);
        section->priv->health_status_explanation_label = label;

        /* health buttons */
        button_box = gtk_hbutton_box_new ();
        gtk_button_box_set_layout (GTK_BUTTON_BOX (button_box), GTK_BUTTONBOX_START);
        gtk_box_set_spacing (GTK_BOX (button_box), 6);
        gtk_box_pack_start (GTK_BOX (vbox2), button_box, TRUE, TRUE, 0);

        section->priv->health_refresh_action = polkit_gnome_action_new_default (
                "refresh",
                section->priv->pk_smart_refresh_action,
                _("Refre_sh"),
                _("Collect S.M.A.R.T. data from the device"));
        g_object_set (section->priv->health_refresh_action,
                      "auth-label", _("Refre_sh..."),
                      "yes-icon-name", GTK_STOCK_REFRESH,
                      "no-icon-name", GTK_STOCK_REFRESH,
                      "auth-icon-name", GTK_STOCK_REFRESH,
                      "self-blocked-icon-name", GTK_STOCK_REFRESH,
                      NULL);
        g_signal_connect (section->priv->health_refresh_action,
                          "activate", G_CALLBACK (health_refresh_action_callback), section);
        button = polkit_gnome_action_create_button (section->priv->health_refresh_action);
        gtk_container_add (GTK_CONTAINER (button_box), button);

        section->priv->health_details_action = polkit_gnome_action_new_default (
                "details",
                section->priv->pk_smart_retrieve_historical_data_action,
                _("_Details..."),
                _("Show S.M.A.R.T. Historical Data"));
        g_object_set (section->priv->health_details_action,
                      "auth-label", _("_Details..."),
                      "yes-icon-name", GTK_STOCK_DIALOG_INFO,
                      "no-icon-name", GTK_STOCK_DIALOG_INFO,
                      "auth-icon-name", GTK_STOCK_DIALOG_INFO,
                      "self-blocked-icon-name", GTK_STOCK_DIALOG_INFO,
                      NULL);
        g_signal_connect (section->priv->health_details_action,
                          "activate", G_CALLBACK (health_details_action_callback), section);
        button = polkit_gnome_action_create_button (section->priv->health_details_action);
        gtk_container_add (GTK_CONTAINER (button_box), button);

        section->priv->health_selftest_action = polkit_gnome_action_new_default (
                "selftest",
                section->priv->pk_smart_selftest_action,
                _("Se_lf Test..."),
                _("Run a S.M.A.R.T. Self Test"));
        g_object_set (section->priv->health_selftest_action,
                      "auth-label", _("Se_lf Test..."),
                      "yes-icon-name", GTK_STOCK_EXECUTE,
                      "no-icon-name", GTK_STOCK_EXECUTE,
                      "auth-icon-name", GTK_STOCK_EXECUTE,
                      "self-blocked-icon-name", GTK_STOCK_EXECUTE,
                      NULL);
        g_signal_connect (section->priv->health_selftest_action,
                          "activate", G_CALLBACK (health_selftest_action_callback), section);
        button = polkit_gnome_action_create_button (section->priv->health_selftest_action);
        gtk_container_add (GTK_CONTAINER (button_box), button);
}

GtkWidget *
gdu_section_health_new (GduShell       *shell,
                        GduPresentable *presentable)
{
        return GTK_WIDGET (g_object_new (GDU_TYPE_SECTION_HEALTH,
                                         "shell", shell,
                                         "presentable", presentable,
                                         NULL));
}
