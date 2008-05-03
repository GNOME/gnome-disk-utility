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

#include "gdu-pool.h"
#include "gdu-util.h"
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
        GtkWidget *health_refresh_button;
        GtkWidget *health_details_button;
        GtkWidget *health_selftest_button;
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

        gtk_widget_set_sensitive (section->priv->health_refresh_button, FALSE);
        gtk_widget_set_sensitive (section->priv->health_details_button, FALSE);
        gtk_widget_set_sensitive (section->priv->health_selftest_button, FALSE);
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

        gtk_widget_set_sensitive (section->priv->health_refresh_button, FALSE);
        gtk_widget_set_sensitive (section->priv->health_details_button, FALSE);
        gtk_widget_set_sensitive (section->priv->health_selftest_button, FALSE);
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
        const char *last_self_test_result;
        GduDevice *device;
        gboolean attr_warn;
        gboolean attr_fail;

        device = gdu_presentable_get_device (gdu_section_get_presentable (GDU_SECTION (section)));
        if (device == NULL) {
                g_warning ("%s: device is not supposed to be NULL", __FUNCTION__);
                goto out;
        }

        passed = ! gdu_device_drive_smart_get_is_failing (device, &attr_warn, &attr_fail);
        power_on_hours = gdu_device_drive_smart_get_time_powered_on (device) / 3600;
        temperature = gdu_device_drive_smart_get_temperature (device);
        last_self_test_result = gdu_device_drive_smart_get_last_self_test_result (device);

        gtk_widget_set_sensitive (section->priv->health_refresh_button, TRUE);
        gtk_widget_set_sensitive (section->priv->health_details_button, TRUE);
        gtk_widget_set_sensitive (section->priv->health_selftest_button, TRUE);
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

        GTimeVal now;
        guint64 collect_time;
        int age;
        g_get_current_time (&now);
        collect_time = gdu_device_drive_smart_get_time_collected (device);
        age = (int) (now.tv_sec - collect_time);
        if (age < 60) {
                s = g_strdup_printf (_("A moment ago"));
        } else if (age < 60 * 60) {
                if (age / 60 == 1) {
                        s = g_strdup_printf (_("1 minute ago"));
                } else {
                        s = g_strdup_printf (_("%d minutes ago"), age / 60);
                }
        } else {
                if (age / 60 / 60 == 1) {
                        s = g_strdup_printf (_("1 hour ago"));
                } else {
                        s = g_strdup_printf (_("%d hours ago"), age / 60 / 60);
                }
        }
        gtk_label_set_text (GTK_LABEL (section->priv->health_updated_label), s);
        g_free (s);

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
        if (device != NULL)
                g_object_unref (device);
}

static void
retrieve_smart_data_cb (GduDevice  *device,
                        gboolean    result,
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
health_refresh_button_clicked (GtkWidget *button, gpointer user_data)
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
        ATTR_ID_COLUMN,
        ATTR_DESC_COLUMN,
        ATTR_FLAGS_COLUMN,
        ATTR_VALUE_COLUMN,
        ATTR_WORST_COLUMN,
        ATTR_THRESHOLD_COLUMN,
        ATTR_TYPE_COLUMN,
        ATTR_UPDATED_COLUMN,
        ATTR_RAW_COLUMN,
        ATTR_STATUS_PIXBUF_COLUMN,
        ATTR_STATUS_TEXT_COLUMN,
        ATTR_TOOLTIP_COLUMN,
        ATTR_N_COLUMNS,
};

static void
health_details_button_clicked (GtkWidget *button, gpointer user_data)
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
        GduDeviceSmartAttribute *attrs;
        int num_attrs;
        int n;

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

	vbox = gtk_hbox_new (FALSE, 12);
	gtk_container_set_border_width (GTK_CONTAINER (vbox), 5);
	gtk_box_pack_start (GTK_BOX (GTK_DIALOG (dialog)->vbox), vbox, TRUE, TRUE, 0);


        list_store = gtk_list_store_new (ATTR_N_COLUMNS,
                                         G_TYPE_STRING,
                                         G_TYPE_STRING,
                                         G_TYPE_STRING,
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
        gtk_tree_view_column_set_title (column, "Flags");
        renderer = gtk_cell_renderer_text_new ();
        gtk_tree_view_column_pack_start (column, renderer, TRUE);
        gtk_tree_view_column_set_attributes (column, renderer,
                                             "text", ATTR_FLAGS_COLUMN,
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
        gtk_tree_view_column_set_title (column, "Type");
        renderer = gtk_cell_renderer_text_new ();
        gtk_tree_view_column_pack_start (column, renderer, TRUE);
        gtk_tree_view_column_set_attributes (column, renderer,
                                             "text", ATTR_TYPE_COLUMN,
                                             NULL);
        gtk_tree_view_append_column (GTK_TREE_VIEW (tree_view), column);

        column = gtk_tree_view_column_new ();
        gtk_tree_view_column_set_title (column, "Updated");
        renderer = gtk_cell_renderer_text_new ();
        gtk_tree_view_column_pack_start (column, renderer, TRUE);
        gtk_tree_view_column_set_attributes (column, renderer,
                                             "text", ATTR_UPDATED_COLUMN,
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


        attrs = gdu_device_drive_smart_get_attributes (device, &num_attrs);
        for (n = 0; n < num_attrs; n++) {
                GduDeviceSmartAttribute *a = attrs + n;
                GtkTreeIter iter;
                char *col_str;
                char *desc_str;
                char *flags_str;
                char *value_str;
                char *worst_str;
                char *threshold_str;
                char *type_str;
                char *updated_str;
                char *raw_str;
                char *status_str;
                GdkPixbuf *status_pixbuf;
                char *tooltip_str;
                int icon_width, icon_height;
                gboolean threshold_exceeded;
                gboolean threshold_exceeded_in_the_past;
                gboolean should_warn;

                col_str = g_strdup_printf ("%d", a->id);

                gdu_device_smart_attribute_get_details (a, &desc_str, &tooltip_str, &should_warn);

                flags_str = g_strdup_printf ("0x%04x", a->flags);
                value_str = g_strdup_printf ("%d", a->value);
                worst_str = g_strdup_printf ("%d", a->worst);
                threshold_str = g_strdup_printf ("%d", a->threshold);
                type_str = g_strdup ((a->flags & 0x0001) ? _("Pre-Fail") : _("Old-Age"));
                updated_str = g_strdup ((a->flags & 0x0002) ? _("Always") : _("Offline"));
                raw_str = g_strdup (a->raw);

                threshold_exceeded = (a->value < a->threshold);
                threshold_exceeded_in_the_past = (a->worst < a->threshold);

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
                                    ATTR_ID_COLUMN, col_str,
                                    ATTR_DESC_COLUMN, desc_str,
                                    ATTR_FLAGS_COLUMN, flags_str,
                                    ATTR_VALUE_COLUMN, value_str,
                                    ATTR_WORST_COLUMN, worst_str,
                                    ATTR_THRESHOLD_COLUMN, threshold_str,
                                    ATTR_TYPE_COLUMN, type_str,
                                    ATTR_UPDATED_COLUMN, updated_str,
                                    ATTR_RAW_COLUMN, raw_str,
                                    ATTR_STATUS_PIXBUF_COLUMN, status_pixbuf,
                                    ATTR_STATUS_TEXT_COLUMN, status_str,
                                    ATTR_TOOLTIP_COLUMN, tooltip_str,
                                    -1);
                g_free (col_str);
                g_free (desc_str);
                g_free (flags_str);
                g_free (value_str);
                g_free (worst_str);
                g_free (threshold_str);
                g_free (type_str);
                g_free (updated_str);
                g_free (raw_str);
                g_object_unref (status_pixbuf);
                g_free (status_str);
                g_free (tooltip_str);
        }

        g_object_unref (list_store);


        gtk_widget_show_all (dialog);
        gtk_dialog_run (GTK_DIALOG (dialog));
        gtk_widget_destroy (dialog);

        //smart_data_set_pending (section);
        //gdu_device_drive_smart_refresh_data (device, retrieve_smart_data_cb, g_object_ref (section));

out:
        if (device != NULL)
                g_object_unref (device);
}

static void
health_selftest_button_clicked (GtkWidget *button, gpointer user_data)
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


        dialog = gtk_dialog_new_with_buttons (_("S.M.A.R.T. Selftest"),
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
        gtk_dialog_add_button (GTK_DIALOG (dialog), _("_Initiate Selftest"), 0);
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
        gdu_device_op_run_smart_selftest (device, test, FALSE);
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
        collect_time = gdu_device_drive_smart_get_time_collected (device);
        if (collect_time == 0 || (now.tv_sec - collect_time) > 60 * 60) {
                smart_data_set_pending (section);
                gdu_device_drive_smart_refresh_data (device, retrieve_smart_data_cb, g_object_ref (section));
        } else {
                smart_data_set (section);
        }

out:
        if (device != NULL)
                g_object_unref (device);
}

/* ---------------------------------------------------------------------------------------------------- */

static void
gdu_section_health_finalize (GduSectionHealth *section_health)
{
        if (G_OBJECT_CLASS (parent_class)->finalize)
                (* G_OBJECT_CLASS (parent_class)->finalize) (G_OBJECT (section_health));
}

static void
gdu_section_health_class_init (GduSectionHealthClass *klass)
{
        GObjectClass *obj_class = (GObjectClass *) klass;
        GduSectionClass *section_class = (GduSectionClass *) klass;

        parent_class = g_type_class_peek_parent (klass);

        obj_class->finalize = (GObjectFinalizeFunc) gdu_section_health_finalize;
        section_class->update = (gpointer) update;
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

        section->priv = g_new0 (GduSectionHealthPrivate, 1);

        label = gtk_label_new (NULL);
        gtk_label_set_markup (GTK_LABEL (label), _("<b>Health</b>"));
        gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
        gtk_box_pack_start (GTK_BOX (section), label, FALSE, FALSE, 0);
        vbox2 = gtk_vbox_new (FALSE, 5);
        align = gtk_alignment_new (0.5, 0.5, 1.0, 1.0);
        gtk_alignment_set_padding (GTK_ALIGNMENT (align), 0, 0, 24, 0);
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

        label = gtk_label_new (NULL);
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

        button = gtk_button_new_with_mnemonic (_("Refre_sh"));
        gtk_button_set_image (GTK_BUTTON (button),
                              gtk_image_new_from_stock (GTK_STOCK_REFRESH, GTK_ICON_SIZE_BUTTON));
        gtk_container_add (GTK_CONTAINER (button_box), button);
        g_signal_connect (button, "clicked", G_CALLBACK (health_refresh_button_clicked), section);
        section->priv->health_refresh_button = button;

        button = gtk_button_new_with_mnemonic (_("_Details..."));
        gtk_button_set_image (GTK_BUTTON (button),
                              gtk_image_new_from_stock (GTK_STOCK_DIALOG_INFO, GTK_ICON_SIZE_BUTTON));
        gtk_container_add (GTK_CONTAINER (button_box), button);
        g_signal_connect (button, "clicked", G_CALLBACK (health_details_button_clicked), section);
        section->priv->health_details_button = button;

        button = gtk_button_new_with_mnemonic (_("Se_lftest..."));
        gtk_button_set_image (GTK_BUTTON (button),
                              gtk_image_new_from_stock (GTK_STOCK_EXECUTE, GTK_ICON_SIZE_BUTTON));
        gtk_container_add (GTK_CONTAINER (button_box), button);
        g_signal_connect (button, "clicked", G_CALLBACK (health_selftest_button_clicked), section);
        section->priv->health_selftest_button = button;
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
