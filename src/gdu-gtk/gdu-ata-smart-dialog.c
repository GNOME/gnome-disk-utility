/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*- */
/* gdu-ata-smart-dialog.c
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

#include "gdu-time-label.h"
#include "gdu-curve.h"
#include "gdu-graph.h"
#include "gdu-ata-smart-dialog.h"

struct GduAtaSmartDialogPrivate
{
        GduDevice *device;

        guint64 last_updated;
        gulong device_changed_signal_handler_id;

        GtkWidget *power_on_hours_label;
        GtkWidget *temperature_label;
        GtkWidget *last_self_test_result_label;
        GtkWidget *updated_label;
        GtkWidget *assessment_label;
        GtkWidget *sectors_label;
        GtkWidget *attributes_label;

        GtkWidget *tree_view;
        GtkListStore *attr_list_store;

        GtkWidget *graph;

        GList *historical_data;
};

enum
{
        PROP_0,
        PROP_DEVICE,
};


enum
{
        ATTR_NAME_COLUMN,
        ATTR_ID_INT_COLUMN,
        ATTR_ID_COLUMN,
        ATTR_DESC_COLUMN,
        ATTR_CURRENT_COLUMN,
        ATTR_WORST_COLUMN,
        ATTR_THRESHOLD_COLUMN,
        ATTR_VALUE_COLUMN,
        ATTR_STATUS_PIXBUF_COLUMN,
        ATTR_STATUS_TEXT_COLUMN,
        ATTR_TYPE_COLUMN,
        ATTR_UPDATES_COLUMN,
        ATTR_TOOLTIP_COLUMN,
        ATTR_N_COLUMNS,
};

G_DEFINE_TYPE (GduAtaSmartDialog, gdu_ata_smart_dialog, GTK_TYPE_DIALOG)

static void update_dialog (GduAtaSmartDialog *dialog);
static void device_changed (GduDevice *device, gpointer user_data);

static gchar *pretty_to_string (guint64                  pretty_value,
                                GduAtaSmartAttributeUnit pretty_unit,
                                gboolean                 long_string);

static void
gdu_ata_smart_dialog_finalize (GObject *object)
{
        GduAtaSmartDialog *dialog = GDU_ATA_SMART_DIALOG (object);

        g_signal_handler_disconnect (dialog->priv->device, dialog->priv->device_changed_signal_handler_id);
        g_object_unref (dialog->priv->device);
        g_object_unref (dialog->priv->attr_list_store);

        if (dialog->priv->historical_data != NULL) {
                g_list_foreach (dialog->priv->historical_data, (GFunc) g_object_unref, NULL);
                g_list_free (dialog->priv->historical_data);
        }

        if (G_OBJECT_CLASS (gdu_ata_smart_dialog_parent_class)->finalize != NULL)
                G_OBJECT_CLASS (gdu_ata_smart_dialog_parent_class)->finalize (object);
}

static void
gdu_ata_smart_dialog_get_property (GObject    *object,
                                   guint       property_id,
                                   GValue     *value,
                                   GParamSpec *pspec)
{
        GduAtaSmartDialog *dialog = GDU_ATA_SMART_DIALOG (object);

        switch (property_id) {
        case PROP_DEVICE:
                g_value_set_object (value, dialog->priv->device);
                break;

        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
        }
}

static void
gdu_ata_smart_dialog_set_property (GObject      *object,
                                   guint         property_id,
                                   const GValue *value,
                                   GParamSpec   *pspec)
{
        GduAtaSmartDialog *dialog = GDU_ATA_SMART_DIALOG (object);

        switch (property_id) {
        case PROP_DEVICE:
                dialog->priv->device = g_value_dup_object (value);
                break;

        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
        }
}

static void
selection_changed (GtkTreeSelection *tree_selection,
                   gpointer          user_data)
{
        GduAtaSmartDialog *dialog = GDU_ATA_SMART_DIALOG (user_data);
        GtkTreeIter iter;
        gchar *attr_name;
        guint n;

        attr_name = NULL;

        if (dialog->priv->historical_data == NULL)
                goto out;

        if (!gtk_tree_selection_get_selected (tree_selection,
                                              NULL,
                                              &iter))
                goto out;

        gtk_tree_model_get (GTK_TREE_MODEL (dialog->priv->attr_list_store),
                            &iter,
                            ATTR_NAME_COLUMN,
                            &attr_name,
                            -1);

        g_debug ("selected %s", attr_name);

        GArray *cur_points;
        GArray *worst_points;
        GArray *threshold_points;
        GArray *raw_points;
        GArray *band_points;
        GList *l;
        guint64 now;
        cur_points = g_array_new (FALSE, FALSE, sizeof (GduPoint));
        worst_points = g_array_new (FALSE, FALSE, sizeof (GduPoint));
        threshold_points = g_array_new (FALSE, FALSE, sizeof (GduPoint));
        raw_points = g_array_new (FALSE, FALSE, sizeof (GduPoint));
        band_points = g_array_new (FALSE, FALSE, sizeof (GduPoint));

        guint64 raw_min;
        guint64 raw_max;
        GduAtaSmartAttributeUnit raw_unit;
        raw_min = G_MAXUINT64;
        raw_max = 0;
        for (l = dialog->priv->historical_data; l != NULL; l = l->next) {
                GduAtaSmartHistoricalData *data = GDU_ATA_SMART_HISTORICAL_DATA (l->data);
                GduAtaSmartAttribute *attr;

                attr = gdu_ata_smart_historical_data_get_attribute (data, attr_name);
                if (attr != NULL) {
                        guint64 raw;

                        raw = gdu_ata_smart_attribute_get_pretty_value (attr);
                        raw_unit = gdu_ata_smart_attribute_get_pretty_unit (attr);
                        if (raw < raw_min)
                                raw_min = raw;
                        if (raw > raw_max)
                                raw_max = raw;
                        g_object_unref (attr);
                }
        }

        guint64 time_factor;
        switch (raw_unit) {
        case GDU_ATA_SMART_ATTRIBUTE_UNIT_MSECONDS:
                if (raw_min > 1000 * 60 * 60 * 24) {
                        time_factor = 1000 * 60 * 60 * 24;
                } else if (raw_min > 1000 * 60 * 60) {
                        time_factor = 1000 * 60 * 60;
                } else if (raw_min > 1000 * 60) {
                        time_factor = 1000 * 60;
                } else if (raw_min > 1000) {
                        time_factor = 1000;
                } else {
                        time_factor = 1;
                }

                if (raw_max - raw_min < 5 * time_factor) {
                        raw_min -= (raw_min % time_factor);
                        raw_max = raw_min + 5 * time_factor;
                }
                break;
        case GDU_ATA_SMART_ATTRIBUTE_UNIT_MKELVIN:
                if (raw_max - raw_min < 5000) {
                        raw_min -= (raw_min % 1000);
                        raw_max = raw_min + 5000;
                }
                break;
        case GDU_ATA_SMART_ATTRIBUTE_UNIT_SECTORS:
        case GDU_ATA_SMART_ATTRIBUTE_UNIT_NONE:
        case GDU_ATA_SMART_ATTRIBUTE_UNIT_UNKNOWN:
                if (raw_min - raw_max < 5) {
                        raw_max = raw_min + 5;
                }
                break;
        }


        gchar **y_axis_left;
        y_axis_left = g_new0 (gchar *, 6);
        for (n = 0; n < 5; n++) {
                guint64 raw_marker_value;
                gchar *s;

                raw_marker_value = raw_min + n * ((gdouble) (raw_max - raw_min)) / (5 - 1);

                s = pretty_to_string (raw_marker_value, raw_unit, FALSE);
                y_axis_left[n] = s;
        }
        y_axis_left[n] = NULL;
        gdu_graph_set_y_markers_left (GDU_GRAPH (dialog->priv->graph), (const gchar* const *) y_axis_left);
        g_strfreev (y_axis_left);

        guint64 tolerance;
        guint64 timespan;

        timespan = 5 * 24 * 60 * 60;
        tolerance = 2 * 60 * 60;

        guint64 last_age;
        now = (guint64) time (NULL);
        last_age = timespan;

        /* oldest points first */
        for (l = dialog->priv->historical_data; l != NULL; l = l->next) {
                GduAtaSmartHistoricalData *data = GDU_ATA_SMART_HISTORICAL_DATA (l->data);
                GduAtaSmartAttribute *attr;
                guint64 time_collected;
                GduPoint point;
                guint64 age;
                gboolean use_point;

                memset (&point, '\0', sizeof (GduPoint));

                time_collected = gdu_ata_smart_historical_data_get_time_collected (data);
                age = now - time_collected;

                /* skip old points, except if the following point is not too old */
                use_point = FALSE;
                if (age < timespan) {
                        use_point = TRUE;
                } else {
                        if (l->next != NULL) {
                                GduAtaSmartHistoricalData *next_data = GDU_ATA_SMART_HISTORICAL_DATA (l->next->data);
                                guint64 next_age;
                                next_age = now - gdu_ata_smart_historical_data_get_time_collected (next_data);
                                if (next_age < timespan) {
                                        use_point = TRUE;
                                }
                        }
                }

                if (use_point) {

                        point.x = 1.0 - ((gdouble) age) / ((gdouble) timespan);

                        attr = gdu_ata_smart_historical_data_get_attribute (data, attr_name);
                        if (attr != NULL) {
                                guint current;
                                guint worst;
                                guint threshold;
                                guint64 raw;

                                current = gdu_ata_smart_attribute_get_current (attr);
                                worst = gdu_ata_smart_attribute_get_worst (attr);
                                threshold = gdu_ata_smart_attribute_get_threshold (attr);
                                raw = gdu_ata_smart_attribute_get_pretty_value (attr);

                                point.y = current / 255.0f;
                                g_array_append_val (cur_points, point);

                                point.y = worst / 255.0f;
                                g_array_append_val (worst_points, point);

                                point.y = threshold / 255.0f;
                                g_array_append_val (threshold_points, point);

                                point.y = ((gdouble) (raw - raw_min)) / ((gdouble) (raw_max - raw_min));
                                g_array_append_val (raw_points, point);

                                g_object_unref (attr);
                        }

                        /* draw a band if there's a discontinuity; e.g. no samples for an hour or more */
                        if (last_age - age >= tolerance) {
                                guint64 band_start;
                                guint64 band_end;

                                band_start = last_age - tolerance;
                                band_end = age + tolerance;

                                point.x = 1.0 - band_start / ((gdouble) timespan);
                                point.y = 1.0;
                                g_array_append_val (band_points, point);

                                point.x = 1.0 - band_end / ((gdouble) timespan);
                                point.y = 1.0;
                                g_array_append_val (band_points, point);

                                /* close the segment */
                                point.x = G_MAXDOUBLE;
                                point.y = G_MAXDOUBLE;
                                g_array_append_val (band_points, point);
                        }
                }

                last_age = age;
        }

#define GDU_COLOR_FROM_HEX(argb_hex, alpha)             \
        {                                               \
                (((argb_hex) >> 16)&0xff) / 255.0,      \
                (((argb_hex) >>  8)&0xff) / 255.0,      \
                (((argb_hex) >>  0)&0xff) / 255.0,      \
                alpha                                   \
        }

        /* see http://tango.freedesktop.org/Tango_Icon_Theme_Guidelines for colors
         */
        GduColor raw_color       = GDU_COLOR_FROM_HEX (0xfcaf3e, 255.0); /* orange */
        GduColor cur_color       = GDU_COLOR_FROM_HEX (0x729fcf, 255.0); /* sky blue */
        GduColor worst_color     = GDU_COLOR_FROM_HEX (0xad7fa8, 255.0); /* plum */
        GduColor threshold_color = GDU_COLOR_FROM_HEX (0xef2929, 255.0); /* scarlet red */
        GduColor band_color      = { 0.00, 0.00, 0.00, 0.0 };
        GduColor band_fill_color = { 0.85, 0.85, 0.85, 0.5 };
        GduCurve *c;

        /* add graphs in order */
        gint z_order = 0;

        /* first the bands representing no data */
        c = gdu_curve_new ();
        gdu_curve_set_legend (c, _("No data"));
        gdu_curve_set_z_order (c, z_order++);
        gdu_curve_set_points (c, band_points);
        gdu_curve_set_color (c, &band_color);
        gdu_curve_set_fill_color (c, &band_fill_color);
        gdu_curve_set_flags (c, GDU_CURVE_FLAGS_FILLED | GDU_CURVE_FLAGS_FADE_EDGES);
        gdu_graph_add_curve (GDU_GRAPH (dialog->priv->graph),
                             "band",
                             c);
        g_object_unref (c);

        /* then worst */
        c = gdu_curve_new ();
        gdu_curve_set_legend (c, _("Worst"));
        gdu_curve_set_z_order (c, z_order++);
        gdu_curve_set_points (c, worst_points);
        gdu_curve_set_color (c, &worst_color);
        gdu_curve_set_width (c, 1.0);
        gdu_graph_add_curve (GDU_GRAPH (dialog->priv->graph),
                             "worst",
                             c);
        g_object_unref (c);

        /* then threshold */
        c = gdu_curve_new ();
        gdu_curve_set_legend (c, _("Treshold"));
        gdu_curve_set_z_order (c, z_order++);
        gdu_curve_set_points (c, threshold_points);
        gdu_curve_set_color (c, &threshold_color);
        gdu_curve_set_width (c, 1.0);
        gdu_graph_add_curve (GDU_GRAPH (dialog->priv->graph),
                             "threshold",
                             c);
        g_object_unref (c);

        /* then current */
        c = gdu_curve_new ();
        gdu_curve_set_legend (c, _("Current"));
        gdu_curve_set_z_order (c, z_order++);
        gdu_curve_set_points (c, cur_points);
        gdu_curve_set_color (c, &cur_color);
        gdu_curve_set_width (c, 2.0);
        gdu_graph_add_curve (GDU_GRAPH (dialog->priv->graph),
                             "current",
                             c);
        g_object_unref (c);


        /* then raw */
        c = gdu_curve_new ();
        gdu_curve_set_legend (c, _("Raw")); /* TODO: units? */
        gdu_curve_set_z_order (c, z_order++);
        gdu_curve_set_points (c, raw_points);
        gdu_curve_set_color (c, &raw_color);
        gdu_curve_set_width (c, 2.0);
        gdu_graph_add_curve (GDU_GRAPH (dialog->priv->graph),
                             "raw",
                             c);
        g_object_unref (c);

        g_array_unref (cur_points);
        g_array_unref (worst_points);
        g_array_unref (threshold_points);
        g_array_unref (raw_points);
        g_array_unref (band_points);

 out:
        g_free (attr_name);
}

static void
gdu_ata_smart_dialog_constructed (GObject *object)
{
        GduAtaSmartDialog *dialog = GDU_ATA_SMART_DIALOG (object);
        GtkWidget *content_area;
        GtkWidget *align;
        GtkWidget *vbox;
        GtkWidget *hbox;
        GtkWidget *table;
        GtkWidget *label;
        GtkWidget *tree_view;
        GtkWidget *scrolled_window;
        GtkWidget *graph;
        GtkCellRenderer *renderer;
        GtkTreeViewColumn *column;
        gint row;
        GduPresentable *drive;
        GduPool *pool;
        gchar *title;
        gchar *drive_name;
        GtkTreeSelection *selection;

        gtk_dialog_set_has_separator (GTK_DIALOG (dialog), FALSE);

        pool = gdu_device_get_pool (dialog->priv->device);
        drive = gdu_pool_get_drive_by_device (pool, dialog->priv->device);
        drive_name = gdu_presentable_get_name (drive);
        /* Translators: %s is the drive name */
        title = g_strdup_printf (_("ATA SMART data for %s"), drive_name);
        gtk_window_set_title (GTK_WINDOW (dialog), title);
        g_object_unref (pool);
        g_object_unref (drive);
        g_free (title);
        g_free (drive_name);

        content_area = gtk_dialog_get_content_area (GTK_DIALOG (dialog));
        //gtk_dialog_add_button (GTK_DIALOG (dialog),
        //                       GTK_STOCK_CLOSE,
        //                       GTK_RESPONSE_CLOSE);

        align = gtk_alignment_new (0.5, 0.5, 1.0, 1.0);
        gtk_alignment_set_padding (GTK_ALIGNMENT (align), 12, 12, 12, 12);
        gtk_box_pack_start (GTK_BOX (content_area), align, TRUE, TRUE, 0);

        vbox = gtk_vbox_new (FALSE, 6);
        gtk_container_add (GTK_CONTAINER (align), vbox);

        hbox = gtk_hbox_new (FALSE, 6);
        gtk_box_pack_start (GTK_BOX (vbox), hbox, FALSE, FALSE, 0);

        table = gtk_table_new (4, 2, FALSE);
        gtk_table_set_col_spacings (GTK_TABLE (table), 12);
        gtk_table_set_row_spacings (GTK_TABLE (table), 4);
        gtk_box_pack_start (GTK_BOX (hbox), table, FALSE, FALSE, 0);

        graph = gdu_graph_new ();
        dialog->priv->graph = graph;
        gtk_widget_set_size_request (graph, 480, 250);
        gtk_box_pack_start (GTK_BOX (hbox), graph, TRUE, TRUE, 0);

        const gchar *time_axis[7];
        time_axis[0] = C_("ATA SMART graph label", "five days ago");
        time_axis[1] = C_("ATA SMART graph label", "four days ago");
        time_axis[2] = C_("ATA SMART graph label", "three days ago");
        time_axis[3] = C_("ATA SMART graph label", "two days ago");
        time_axis[4] = C_("ATA SMART graph label", "one day ago");
        time_axis[5] = C_("ATA SMART graph label", "now");
        time_axis[6] = NULL;
        gdu_graph_set_x_markers (GDU_GRAPH (graph), time_axis);

        const gchar *y_axis_right[6];
        y_axis_right[0] = C_("ATA SMART graph label", "0");
        y_axis_right[1] = C_("ATA SMART graph label", "64");
        y_axis_right[2] = C_("ATA SMART graph label", "128");
        y_axis_right[3] = C_("ATA SMART graph label", "192");
        y_axis_right[4] = C_("ATA SMART graph label", "255");
        y_axis_right[5] = NULL;
        gdu_graph_set_y_markers_right (GDU_GRAPH (graph), y_axis_right);

        row = 0;

        /* power on hours */
        label = gtk_label_new (NULL);
        gtk_misc_set_alignment (GTK_MISC (label), 1.0, 0.5);
        gtk_label_set_markup_with_mnemonic (GTK_LABEL (label), _("<b>Powered On:</b>"));
        gtk_table_attach (GTK_TABLE (table), label, 0, 1, row, row + 1,
                          GTK_FILL, GTK_FILL, 0, 0);

        label = gtk_label_new (NULL);
        gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
        dialog->priv->power_on_hours_label = label;

        gtk_table_attach (GTK_TABLE (table), label, 1, 2, row, row + 1,
                          GTK_FILL, GTK_FILL, 0, 0);

        row++;

        /* temperature */
        label = gtk_label_new (NULL);
        gtk_misc_set_alignment (GTK_MISC (label), 1.0, 0.5);
        gtk_label_set_markup_with_mnemonic (GTK_LABEL (label), _("<b>Temperature:</b>"));
        gtk_table_attach (GTK_TABLE (table), label, 0, 1, row, row + 1,
                          GTK_FILL, GTK_FILL, 0, 0);

        label = gtk_label_new (NULL);
        gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
        dialog->priv->temperature_label = label;

        gtk_table_attach (GTK_TABLE (table), label, 1, 2, row, row + 1,
                          GTK_FILL, GTK_FILL, 0, 0);

        row++;

        /* last test */
        label = gtk_label_new (NULL);
        gtk_misc_set_alignment (GTK_MISC (label), 1.0, 0.5);
        gtk_label_set_markup_with_mnemonic (GTK_LABEL (label), _("<b>Last Test:</b>"));
        gtk_table_attach (GTK_TABLE (table), label, 0, 1, row, row + 1,
                          GTK_FILL, GTK_FILL, 0, 0);

        label = gtk_label_new (NULL);
        gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
        dialog->priv->last_self_test_result_label = label;

        gtk_table_attach (GTK_TABLE (table), label, 1, 2, row, row + 1,
                          GTK_FILL, GTK_FILL, 0, 0);

        row++;

        /* updated */
        label = gtk_label_new (NULL);
        gtk_misc_set_alignment (GTK_MISC (label), 1.0, 0.5);
        gtk_label_set_markup_with_mnemonic (GTK_LABEL (label), _("<b>Updated:</b>"));
        gtk_table_attach (GTK_TABLE (table), label, 0, 1, row, row + 1,
                          GTK_FILL, GTK_FILL, 0, 0);

        label = gdu_time_label_new (NULL);
        gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
        dialog->priv->updated_label = label;

        gtk_table_attach (GTK_TABLE (table), label, 1, 2, row, row + 1,
                          GTK_FILL, GTK_FILL, 0, 0);

        row++;

        /* bad sectors */
        label = gtk_label_new (NULL);
        gtk_misc_set_alignment (GTK_MISC (label), 1.0, 0.5);
        gtk_label_set_markup_with_mnemonic (GTK_LABEL (label), _("<b>Sectors:</b>"));
        gtk_table_attach (GTK_TABLE (table), label, 0, 1, row, row + 1,
                          GTK_FILL, GTK_FILL, 0, 0);

        label = gtk_label_new (NULL);
        gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
        dialog->priv->sectors_label = label;

        gtk_table_attach (GTK_TABLE (table), label, 1, 2, row, row + 1,
                          GTK_FILL, GTK_FILL, 0, 0);

        row++;

        /* attributes */
        label = gtk_label_new (NULL);
        gtk_misc_set_alignment (GTK_MISC (label), 1.0, 0.5);
        gtk_label_set_markup_with_mnemonic (GTK_LABEL (label), _("<b>Attributes:</b>"));
        gtk_table_attach (GTK_TABLE (table), label, 0, 1, row, row + 1,
                          GTK_FILL, GTK_FILL, 0, 0);

        label = gtk_label_new (NULL);
        gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
        dialog->priv->attributes_label = label;

        gtk_table_attach (GTK_TABLE (table), label, 1, 2, row, row + 1,
                          GTK_FILL, GTK_FILL, 0, 0);

        row++;

        /* assessment */
        label = gtk_label_new (NULL);
        gtk_misc_set_alignment (GTK_MISC (label), 1.0, 0.5);
        gtk_label_set_markup_with_mnemonic (GTK_LABEL (label), _("<b>Assessment:</b>"));
        gtk_table_attach (GTK_TABLE (table), label, 0, 1, row, row + 1,
                          GTK_FILL, GTK_FILL, 0, 0);

        label = gtk_label_new (NULL);
        gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
        dialog->priv->assessment_label = label;

        gtk_table_attach (GTK_TABLE (table), label, 1, 2, row, row + 1,
                          GTK_FILL, GTK_FILL, 0, 0);

        row++;

        /* ---------------------------------------------------------------------------------------------------- */
        /* attributes in a tree view */

        dialog->priv->attr_list_store = gtk_list_store_new (ATTR_N_COLUMNS,
                                                            G_TYPE_STRING,
                                                            G_TYPE_INT,
                                                            G_TYPE_STRING,
                                                            G_TYPE_STRING,
                                                            G_TYPE_STRING,
                                                            G_TYPE_STRING,
                                                            G_TYPE_STRING,
                                                            G_TYPE_STRING,
                                                            GDK_TYPE_PIXBUF,
                                                            G_TYPE_STRING,
                                                            G_TYPE_STRING,
                                                            G_TYPE_STRING,
                                                            G_TYPE_STRING);

        tree_view = gtk_tree_view_new_with_model (GTK_TREE_MODEL (dialog->priv->attr_list_store));
        gtk_tree_view_set_rules_hint (GTK_TREE_VIEW (tree_view), TRUE);
        gtk_tree_view_set_tooltip_column (GTK_TREE_VIEW (tree_view), ATTR_TOOLTIP_COLUMN);
        gtk_tree_sortable_set_sort_column_id (GTK_TREE_SORTABLE (dialog->priv->attr_list_store),
                                              ATTR_ID_INT_COLUMN,
                                              GTK_SORT_ASCENDING);
        dialog->priv->tree_view = tree_view;

        selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (tree_view));
        g_signal_connect (selection,
                          "changed",
                          G_CALLBACK (selection_changed),
                          dialog);

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
        gtk_tree_view_column_set_title (column, "Current");
        renderer = gtk_cell_renderer_text_new ();
        g_object_set (renderer, "xalign", 1.0, NULL);
        gtk_tree_view_column_pack_start (column, renderer, TRUE);
        gtk_tree_view_column_set_attributes (column, renderer,
                                             "text", ATTR_CURRENT_COLUMN,
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
        gtk_tree_view_column_set_title (column, "Value");
        renderer = gtk_cell_renderer_text_new ();
        g_object_set (renderer, "xalign", 1.0, NULL);
        gtk_tree_view_column_pack_start (column, renderer, TRUE);
        gtk_tree_view_column_set_attributes (column, renderer,
                                             "text", ATTR_VALUE_COLUMN,
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

        column = gtk_tree_view_column_new ();
        gtk_tree_view_column_set_title (column, "Type");
        renderer = gtk_cell_renderer_text_new ();
        g_object_set (renderer, "xalign", 1.0, NULL);
        gtk_tree_view_column_pack_start (column, renderer, TRUE);
        gtk_tree_view_column_set_attributes (column, renderer,
                                             "text", ATTR_TYPE_COLUMN,
                                             NULL);
        gtk_tree_view_append_column (GTK_TREE_VIEW (tree_view), column);

        column = gtk_tree_view_column_new ();
        gtk_tree_view_column_set_title (column, "Updates");
        renderer = gtk_cell_renderer_text_new ();
        g_object_set (renderer, "xalign", 1.0, NULL);
        gtk_tree_view_column_pack_start (column, renderer, TRUE);
        gtk_tree_view_column_set_attributes (column, renderer,
                                             "text", ATTR_UPDATES_COLUMN,
                                             NULL);
        gtk_tree_view_append_column (GTK_TREE_VIEW (tree_view), column);


        scrolled_window = gtk_scrolled_window_new (NULL, NULL);
        gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scrolled_window),
                                        GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
        gtk_scrolled_window_set_shadow_type (GTK_SCROLLED_WINDOW (scrolled_window),
                                             GTK_SHADOW_IN);
        gtk_container_add (GTK_CONTAINER (scrolled_window), tree_view);

	gtk_box_pack_start (GTK_BOX (vbox), scrolled_window, TRUE, TRUE, 0);

        gtk_window_set_default_size (GTK_WINDOW (dialog), 500, 500);

        update_dialog (dialog);

        dialog->priv->device_changed_signal_handler_id = g_signal_connect (dialog->priv->device,
                                                                           "changed",
                                                                           G_CALLBACK (device_changed),
                                                                           dialog);

        if (G_OBJECT_CLASS (gdu_ata_smart_dialog_parent_class)->constructed != NULL)
                G_OBJECT_CLASS (gdu_ata_smart_dialog_parent_class)->constructed (object);
}

static void
gdu_ata_smart_dialog_class_init (GduAtaSmartDialogClass *klass)
{
        GObjectClass *object_class = G_OBJECT_CLASS (klass);

        g_type_class_add_private (klass, sizeof (GduAtaSmartDialogPrivate));

        object_class->get_property = gdu_ata_smart_dialog_get_property;
        object_class->set_property = gdu_ata_smart_dialog_set_property;
        object_class->constructed  = gdu_ata_smart_dialog_constructed;
        object_class->finalize     = gdu_ata_smart_dialog_finalize;

        g_object_class_install_property (object_class,
                                         PROP_DEVICE,
                                         g_param_spec_object ("device",
                                                              _("Device"),
                                                              _("The device to show ATA SMART data for"),
                                                              GDU_TYPE_DEVICE,
                                                              G_PARAM_READABLE |
                                                              G_PARAM_WRITABLE |
                                                              G_PARAM_CONSTRUCT_ONLY));
}

static void
gdu_ata_smart_dialog_init (GduAtaSmartDialog *dialog)
{
        dialog->priv = G_TYPE_INSTANCE_GET_PRIVATE (dialog, GDU_TYPE_ATA_SMART_DIALOG, GduAtaSmartDialogPrivate);
}

GtkWidget *
gdu_ata_smart_dialog_new (GtkWindow *parent,
                          GduDevice *device)
{
        return GTK_WIDGET (g_object_new (GDU_TYPE_ATA_SMART_DIALOG,
                                         "transient-for", parent,
                                         "device", device,
                                         NULL));
}

/* ---------------------------------------------------------------------------------------------------- */

static gchar *
pretty_to_string (guint64                  pretty_value,
                  GduAtaSmartAttributeUnit pretty_unit,
                  gboolean                 long_string)
{
        gchar *ret;
        gdouble celcius;
        gdouble fahrenheit;

        switch (pretty_unit) {

        case GDU_ATA_SMART_ATTRIBUTE_UNIT_MSECONDS:
                if (long_string) {
                        if (pretty_value > 1000 * 60 * 60 * 24) {
                                ret = g_strdup_printf (_("%.3f days"), pretty_value / 1000.0 / 60.0 / 60.0 / 24.0);
                        } else if (pretty_value > 1000 * 60 * 60) {
                                ret = g_strdup_printf (_("%.3f hours"), pretty_value / 1000.0 / 60.0 / 60.0);
                        } else if (pretty_value > 1000 * 60) {
                                ret = g_strdup_printf (_("%.3f minutes"), pretty_value / 1000.0 / 60.0);
                        } else if (pretty_value > 1000) {
                                ret = g_strdup_printf (_("%.3f seconds"), pretty_value / 1000.0);
                        } else {
                                ret = g_strdup_printf (_("%" G_GUINT64_FORMAT " msec"), pretty_value);
                        }
                } else {
                        if (pretty_value > 1000 * 60 * 60 * 24) {
                                ret = g_strdup_printf (_("%.0f d"), pretty_value / 1000.0 / 60.0 / 60.0 / 24.0);
                        } else if (pretty_value > 1000 * 60 * 60) {
                                ret = g_strdup_printf (_("%.0f h"), pretty_value / 1000.0 / 60.0 / 60.0);
                        } else if (pretty_value > 1000 * 60) {
                                ret = g_strdup_printf (_("%.0f m"), pretty_value / 1000.0 / 60.0);
                        } else if (pretty_value > 1000) {
                                ret = g_strdup_printf (_("%.0f s"), pretty_value / 1000.0);
                        } else {
                                ret = g_strdup_printf (_("%" G_GUINT64_FORMAT " msec"), pretty_value);
                        }
                }
                break;

        case GDU_ATA_SMART_ATTRIBUTE_UNIT_SECTORS:
                if (long_string) {
                        if (pretty_value == 1)
                                ret = g_strdup (_("1 Sector"));
                        else
                                ret = g_strdup_printf (_("%" G_GUINT64_FORMAT " Sectors"), pretty_value);
                } else {
                        ret = g_strdup_printf (_("%" G_GUINT64_FORMAT), pretty_value);
                }
                break;

        case GDU_ATA_SMART_ATTRIBUTE_UNIT_MKELVIN:
                if (long_string) {
                        celcius = pretty_value / 1000.0 - 273.15;
                        fahrenheit = 9.0 * celcius / 5.0 + 32.0;
                        ret = g_strdup_printf (_("%.3f\302\260 C / %.3f\302\260 F"), celcius, fahrenheit);
                } else {
                        /* We could choose kelvin here to treat C and F camps equally. But
                         * that would be lame.
                         */
                        celcius = pretty_value / 1000.0 - 273.15;
                        ret = g_strdup_printf (_("%.0f\302\260 C"), celcius);
                }
                break;

        default:
        case GDU_ATA_SMART_ATTRIBUTE_UNIT_NONE:
        case GDU_ATA_SMART_ATTRIBUTE_UNIT_UNKNOWN:
                ret = g_strdup_printf (_("%" G_GUINT64_FORMAT), pretty_value);
                break;
        }

        return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

static void
get_historical_data_cb (GduDevice *device,
                        GList     *smart_data,
                        GError    *error,
                        gpointer   user_data)
{
        GduAtaSmartDialog *dialog = GDU_ATA_SMART_DIALOG (user_data);

        if (error != NULL) {
                g_warning ("Error getting historical data: %s", error->message);
                g_error_free (error);
        } else {
                if (dialog->priv->historical_data != NULL) {
                        g_list_foreach (dialog->priv->historical_data, (GFunc) g_object_unref, NULL);
                        g_list_free (dialog->priv->historical_data);
                }
                dialog->priv->historical_data = smart_data;

                g_debug ("got historical data (%d elems)", g_list_length (smart_data));

                update_dialog (dialog);
        }

        g_object_unref (dialog);
}


static void
update_dialog (GduAtaSmartDialog *dialog)
{
        gchar *assessment_text;
        gchar *bad_sectors_text;
        gchar *attributes_text;
        gchar *powered_on_text;
        gchar *temperature_text;
        gchar *selftest_text;
        guint64 temperature_mkelvin;
        guint64 power_on_msec;
        GTimeVal updated;
        gboolean is_failing;
        gboolean is_failing_valid;
        gboolean has_bad_sectors;
        gboolean has_bad_attributes;
        GduAtaSmartSelfTestExecutionStatus self_test_status;
        GList *attributes;
        GList *l;

        if (!gdu_device_drive_ata_smart_get_is_available (dialog->priv->device)) {
                assessment_text = g_strdup (_("ATA SMART Not Supported"));
                bad_sectors_text = g_strdup ("-");
                attributes_text = g_strdup ("-");
                powered_on_text = g_strdup ("-");
                temperature_text = g_strdup ("-");
                selftest_text = g_strdup ("-");

                updated.tv_sec = 0;

                attributes = NULL;

                //polkit_gnome_action_set_sensitive (dialog->priv->refresh_action, FALSE);
                //polkit_gnome_action_set_sensitive (dialog->priv->details_action, FALSE);
                //polkit_gnome_action_set_sensitive (dialog->priv->selftest_action, FALSE);
                dialog->priv->last_updated = 0;
                goto out;
        }

        attributes = gdu_device_drive_ata_smart_get_attributes (dialog->priv->device);

        is_failing = gdu_device_drive_ata_smart_get_is_failing (dialog->priv->device);
        is_failing_valid = gdu_device_drive_ata_smart_get_is_failing_valid (dialog->priv->device);

        self_test_status = gdu_device_drive_ata_smart_get_self_test_execution_status (dialog->priv->device);

        power_on_msec = 1000 * gdu_device_drive_ata_smart_get_power_on_seconds (dialog->priv->device);
        temperature_mkelvin = (guint64) (gdu_device_drive_ata_smart_get_temperature_kelvin (dialog->priv->device) * 1000.0);

        has_bad_sectors = gdu_device_drive_ata_smart_get_has_bad_sectors (dialog->priv->device);
        has_bad_attributes = gdu_device_drive_ata_smart_get_has_bad_attributes (dialog->priv->device);

        //polkit_gnome_action_set_sensitive (dialog->priv->refresh_action, TRUE);
        //polkit_gnome_action_set_sensitive (dialog->priv->details_action, TRUE);
        //polkit_gnome_action_set_sensitive (dialog->priv->selftest_action, TRUE);

        if (is_failing_valid) {
                if (!is_failing) {
                        assessment_text = g_strdup (_("Passed"));
                } else {
                        assessment_text = g_strdup (_("<span foreground='red'><b>FAILING</b></span>"));
                }
        } else {
                assessment_text = g_strdup (_("Unknown"));
        }

        if (has_bad_sectors)
                bad_sectors_text = g_strdup (_("<span foreground='red'><b>BAD SECTORS DETECTED</b></span>"));
        else
                bad_sectors_text = g_strdup (_("No bad sectors detected"));

        if (has_bad_attributes)
                attributes_text = g_strdup (_("<span foreground='red'><b>EXCEEDS THRESHOLD</b></span>"));
        else
                attributes_text = g_strdup (_("Within threshold"));

        if (power_on_msec == 0) {
                powered_on_text = g_strdup (_("Unknown"));
        } else {
                powered_on_text = pretty_to_string (power_on_msec, GDU_ATA_SMART_ATTRIBUTE_UNIT_MSECONDS, TRUE);
        }

        if (temperature_mkelvin == 0) {
                temperature_text = g_strdup (_("Unknown"));
        } else {
                temperature_text = pretty_to_string (temperature_mkelvin, GDU_ATA_SMART_ATTRIBUTE_UNIT_MKELVIN, TRUE);
        }

        dialog->priv->last_updated = updated.tv_sec = gdu_device_drive_ata_smart_get_time_collected (dialog->priv->device);
        updated.tv_usec = 0;

        switch (self_test_status) {
        case GDU_ATA_SMART_SELF_TEST_EXECUTION_STATUS_SUCCESS_OR_NEVER:
                selftest_text = g_strdup (_("Completed OK"));
                break;
        case GDU_ATA_SMART_SELF_TEST_EXECUTION_STATUS_ABORTED:
                selftest_text = g_strdup (_("Cancelled"));
                break;
        case GDU_ATA_SMART_SELF_TEST_EXECUTION_STATUS_INTERRUPTED:
                selftest_text = g_strdup (_("Cancelled (with hard or soft reset)"));
                break;
        case GDU_ATA_SMART_SELF_TEST_EXECUTION_STATUS_FATAL:
                selftest_text = g_strdup (_("Not completed (a fatal error might have occured)"));
                break;
        case GDU_ATA_SMART_SELF_TEST_EXECUTION_STATUS_ERROR_ELECTRICAL:
                selftest_text = g_strdup (_("<span foreground='red'><b>FAILED</b></span> (Electrical)"));
                break;
        case GDU_ATA_SMART_SELF_TEST_EXECUTION_STATUS_ERROR_SERVO:
                selftest_text = g_strdup (_("<span foreground='red'><b>FAILED</b></span> (Servo)"));
                break;
        case GDU_ATA_SMART_SELF_TEST_EXECUTION_STATUS_ERROR_READ:
                selftest_text = g_strdup (_("<span foreground='red'><b>FAILED</b></span> (Read)"));
                break;
        case GDU_ATA_SMART_SELF_TEST_EXECUTION_STATUS_ERROR_HANDLING:
                selftest_text = g_strdup (_("<span foreground='red'><b>FAILED</b></span> (Suspected of having handled damage"));
                break;
        case GDU_ATA_SMART_SELF_TEST_EXECUTION_STATUS_INPROGRESS:
                selftest_text = g_strdup (_("In progress"));
                break;

        default:
        case GDU_ATA_SMART_SELF_TEST_EXECUTION_STATUS_ERROR_UNKNOWN:
                selftest_text = g_strdup (_("Unknown"));
                break;
        }

 out:
        gtk_label_set_markup (GTK_LABEL (dialog->priv->assessment_label), assessment_text);
        gtk_label_set_markup (GTK_LABEL (dialog->priv->sectors_label), bad_sectors_text);
        gtk_label_set_markup (GTK_LABEL (dialog->priv->attributes_label), attributes_text);
        gtk_label_set_markup (GTK_LABEL (dialog->priv->power_on_hours_label), powered_on_text);
        gtk_label_set_markup (GTK_LABEL (dialog->priv->temperature_label), temperature_text);
        if (updated.tv_sec == 0) {
                gdu_time_label_set_time (GDU_TIME_LABEL (dialog->priv->updated_label), NULL);
                gtk_label_set_markup (GTK_LABEL (dialog->priv->updated_label), "-");
        } else {
                gdu_time_label_set_time (GDU_TIME_LABEL (dialog->priv->updated_label), &updated);
        }
        gtk_label_set_markup (GTK_LABEL (dialog->priv->last_self_test_result_label), selftest_text);

        gtk_list_store_clear (dialog->priv->attr_list_store);
        for (l = attributes; l != NULL; l = l->next) {
                GduAtaSmartAttribute *a = GDU_ATA_SMART_ATTRIBUTE (l->data);
                GtkTreeIter iter;
                char *col_str;
                char *name_str;
                char *current_str;
                char *worst_str;
                char *threshold_str;
                char *pretty_str;
                char *status_str;
                GdkPixbuf *status_pixbuf;
                char *tooltip_str;
                int icon_width, icon_height;
                char *desc_str;
                const gchar *type_str;
                const gchar *updates_str;
                const gchar *tip_type_str;
                const gchar *tip_updates_str;
                gboolean is_good;
                gboolean is_good_valid;
                guint64 pretty_value;
                GduAtaSmartAttributeUnit pretty_unit;

                col_str = g_strdup_printf ("%d", gdu_ata_smart_attribute_get_id (a));

                name_str = gdu_ata_smart_attribute_get_localized_name (a);
                desc_str = gdu_ata_smart_attribute_get_localized_description (a);

                if (desc_str == NULL) {
                        desc_str = g_strdup_printf (_("No description for attribute %d."),
                                                    gdu_ata_smart_attribute_get_id (a));
                }

                if (gdu_ata_smart_attribute_get_flags (a) & 0x0001) {
                        tip_type_str = _("Failure is a sign of imminent disk failure.");
                } else {
                        tip_type_str = _("Failure is a sign of old age.");
                }

                if (gdu_ata_smart_attribute_get_flags (a) & 0x0002) {
                        tip_updates_str = _("Every time data is collected.");
                } else {
                        tip_updates_str = _("Only when performing a self-test.");
                }

                tooltip_str = g_strdup_printf (_("<b>Type:</b> %s\n"
                                                 "<b>Updates:</b> %s\n"
                                                 "<b>Description</b>: %s"),
                                               tip_type_str,
                                               tip_updates_str,
                                               desc_str);

                current_str = g_strdup_printf ("%d", gdu_ata_smart_attribute_get_current (a));
                worst_str = g_strdup_printf ("%d", gdu_ata_smart_attribute_get_worst (a));
                threshold_str = g_strdup_printf ("%d", gdu_ata_smart_attribute_get_threshold (a));

                if (gdu_ata_smart_attribute_get_flags (a) & 0x0002)
                        updates_str = _("Online");
                else
                        updates_str = _("Offline");

                if (gdu_ata_smart_attribute_get_flags (a) & 0x0001)
                        type_str = _("Pre-fail");
                else
                        type_str = _("Old-age");

                pretty_value = gdu_ata_smart_attribute_get_pretty_value (a);
                pretty_unit = gdu_ata_smart_attribute_get_pretty_unit (a);
                pretty_str = pretty_to_string (pretty_value, pretty_unit, TRUE);

                is_good = gdu_ata_smart_attribute_get_good (a);
                is_good_valid = gdu_ata_smart_attribute_get_good_valid (a);

                if (!gtk_icon_size_lookup (GTK_ICON_SIZE_MENU, &icon_width, &icon_height))
                        icon_height = 48;

                if (!is_good_valid) {
                                status_pixbuf = gtk_icon_theme_load_icon (gtk_icon_theme_get_default (),
                                                                          "gdu-smart-unknown",
                                                                          icon_height,
                                                                          GTK_ICON_LOOKUP_GENERIC_FALLBACK,
                                                                          NULL);
                                status_str = g_strdup (_("N/A"));
                } else {
                        if (is_good) {
                                status_pixbuf = gtk_icon_theme_load_icon (gtk_icon_theme_get_default (),
                                                                          "gdu-smart-healthy",
                                                                          icon_height,
                                                                          GTK_ICON_LOOKUP_GENERIC_FALLBACK,
                                                                          NULL);
                                status_str = g_strdup (_("OK"));
                        } else {
                                status_pixbuf = gtk_icon_theme_load_icon (gtk_icon_theme_get_default (),
                                                                          "gdu-smart-failing",
                                                                          icon_height,
                                                                          GTK_ICON_LOOKUP_GENERIC_FALLBACK,
                                                                          NULL);
                                status_str = g_strdup (_("<span foreground='red'><b>FAILING</b></span>"));
                        }
                }

                gtk_list_store_append (dialog->priv->attr_list_store, &iter);
                gtk_list_store_set (dialog->priv->attr_list_store, &iter,
                                    ATTR_NAME_COLUMN, gdu_ata_smart_attribute_get_name (a),
                                    ATTR_ID_INT_COLUMN, gdu_ata_smart_attribute_get_id (a),
                                    ATTR_ID_COLUMN, col_str,
                                    ATTR_DESC_COLUMN, name_str,
                                    ATTR_CURRENT_COLUMN, current_str,
                                    ATTR_WORST_COLUMN, worst_str,
                                    ATTR_THRESHOLD_COLUMN, threshold_str,
                                    ATTR_VALUE_COLUMN, pretty_str,
                                    ATTR_STATUS_PIXBUF_COLUMN, status_pixbuf,
                                    ATTR_STATUS_TEXT_COLUMN, status_str,
                                    ATTR_TYPE_COLUMN, type_str,
                                    ATTR_UPDATES_COLUMN, updates_str,
                                    ATTR_TOOLTIP_COLUMN, tooltip_str,
                                    -1);
                g_free (col_str);
                g_free (name_str);
                g_free (current_str);
                g_free (worst_str);
                g_free (threshold_str);
                g_free (pretty_str);
                g_object_unref (status_pixbuf);
                g_free (status_str);
                g_free (tooltip_str);
                g_free (desc_str);

        }

        g_free (assessment_text);
        g_free (powered_on_text);
        g_free (temperature_text);
        g_free (selftest_text);

        /* TODO: also fetch new data if current data is out of date */
        if (dialog->priv->historical_data == NULL) {
                gdu_device_drive_ata_smart_get_historical_data (dialog->priv->device,
                                                                0, /* since */
                                                                0, /* until */
                                                                0, /* spacing */
                                                                get_historical_data_cb,
                                                                g_object_ref (dialog));
        }
}

static void
device_changed (GduDevice *device,
                gpointer   user_data)
{
        GduAtaSmartDialog *dialog = GDU_ATA_SMART_DIALOG (user_data);

        if (gdu_device_drive_ata_smart_get_time_collected (dialog->priv->device) != dialog->priv->last_updated) {
                update_dialog (dialog);
        }

}
