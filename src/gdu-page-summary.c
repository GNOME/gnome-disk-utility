/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*- */
/* gdu-page-summary.c
 *
 * Copyright (C) 2007 David Zeuthen
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
 * USA
 */

#include <config.h>
#include <glib-object.h>
#include <string.h>
#include <glib/gi18n.h>
#include <polkit-gnome/polkit-gnome.h>

#include "gdu-page.h"
#include "gdu-page-summary.h"
#include "gdu-util.h"

struct _GduPageSummaryPrivate
{
        GduShell *shell;

        GtkWidget *notebook;
        GList *drive_labels;
        GList *volume_labels;
        GList *unallocated_labels;

        GtkWidget *job_description_label;
        GtkWidget *job_progress_bar;
        GtkWidget *job_task_label;
        GtkWidget *job_cancel_button;
        guint job_progress_pulse_timer_id;

        GtkWidget *job_failed_reason_label;
        GtkWidget *job_failed_dismiss_button;
};

static GObjectClass *parent_class = NULL;

static void gdu_page_summary_page_iface_init (GduPageIface *iface);
G_DEFINE_TYPE_WITH_CODE (GduPageSummary, gdu_page_summary, G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (GDU_TYPE_PAGE,
                                                gdu_page_summary_page_iface_init))

enum {
        PROP_0,
        PROP_SHELL,
};

static void
gdu_page_summary_finalize (GduPageSummary *page)
{
        if (page->priv->shell != NULL)
                g_object_unref (page->priv->shell);

        g_list_free (page->priv->drive_labels);
        g_list_free (page->priv->volume_labels);
        g_list_free (page->priv->unallocated_labels);

        if (G_OBJECT_CLASS (parent_class)->finalize)
                (* G_OBJECT_CLASS (parent_class)->finalize) (G_OBJECT (page));
}

static void
gdu_page_summary_set_property (GObject      *object,
                             guint         prop_id,
                             const GValue *value,
                             GParamSpec   *pspec)
{
        GduPageSummary *page = GDU_PAGE_SUMMARY (object);

        switch (prop_id) {
        case PROP_SHELL:
                if (page->priv->shell != NULL)
                        g_object_unref (page->priv->shell);
                page->priv->shell = g_object_ref (g_value_get_object (value));
                break;

        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static void
gdu_page_summary_get_property (GObject     *object,
                             guint        prop_id,
                             GValue      *value,
                             GParamSpec  *pspec)
{
        GduPageSummary *page = GDU_PAGE_SUMMARY (object);

        switch (prop_id) {
        case PROP_SHELL:
                g_value_set_object (value, page->priv->shell);
                break;

        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
    }
}

static void
gdu_page_summary_class_init (GduPageSummaryClass *klass)
{
        GObjectClass *obj_class = (GObjectClass *) klass;

        parent_class = g_type_class_peek_parent (klass);

        obj_class->finalize = (GObjectFinalizeFunc) gdu_page_summary_finalize;
        obj_class->set_property = gdu_page_summary_set_property;
        obj_class->get_property = gdu_page_summary_get_property;

        /**
         * GduPageSummary:shell:
         *
         * The #GduShell instance hosting this page.
         */
        g_object_class_install_property (obj_class,
                                         PROP_SHELL,
                                         g_param_spec_object ("shell",
                                                              NULL,
                                                              NULL,
                                                              GDU_TYPE_SHELL,
                                                              G_PARAM_CONSTRUCT_ONLY |
                                                              G_PARAM_WRITABLE |
                                                              G_PARAM_READABLE));
}

static gboolean
job_progress_pulse_timeout_handler (gpointer user_data)
{
        GduPageSummary *page = GDU_PAGE_SUMMARY (user_data);

        gtk_progress_bar_pulse (GTK_PROGRESS_BAR (page->priv->job_progress_bar));
        return TRUE;
}


static void
job_cancel_button_clicked (GtkWidget *button, gpointer user_data)
{
        GduDevice *device;
        GduPageSummary *page = GDU_PAGE_SUMMARY (user_data);

        device = gdu_presentable_get_device (gdu_shell_get_selected_presentable (page->priv->shell));
        if (device != NULL) {
                gdu_device_op_cancel_job (device);
                g_object_unref (device);
        }
}

static void
job_failed_dismiss_button_clicked (GtkWidget *button, gpointer user_data)
{
        GduDevice *device;
        GduPageSummary *page = GDU_PAGE_SUMMARY (user_data);

        device = gdu_presentable_get_device (gdu_shell_get_selected_presentable (page->priv->shell));
        if (device != NULL) {
                gdu_device_job_clear_last_error_message (device);
                gtk_label_set_markup (GTK_LABEL (page->priv->job_failed_reason_label), "");
                gdu_shell_update (page->priv->shell);
        }
}

static void
job_update (GduPageSummary *page, GduDevice *device)
{
        char *s;
        char *job_description;
        char *task_description;
        double percentage;

        if (device != NULL && gdu_device_job_in_progress (device)) {
                job_description = gdu_get_job_description (gdu_device_job_get_id (device));
                task_description = gdu_get_task_description (gdu_device_job_get_cur_task_id (device));

                s = g_strdup_printf ("<b>%s</b>", job_description);
                gtk_label_set_markup (GTK_LABEL (page->priv->job_description_label), s);
                g_free (s);

                s = g_strdup_printf (_("%s (task %d of %d)"),
                                     task_description,
                                     gdu_device_job_get_cur_task (device) + 1,
                                     gdu_device_job_get_num_tasks (device));
                gtk_label_set_markup (GTK_LABEL (page->priv->job_task_label), s);
                g_free (s);

                percentage = gdu_device_job_get_cur_task_percentage (device);
                if (percentage < 0) {
                        gtk_progress_bar_set_pulse_step (GTK_PROGRESS_BAR (page->priv->job_progress_bar), 2.0 / 50);
                        gtk_progress_bar_pulse (GTK_PROGRESS_BAR (page->priv->job_progress_bar));
                        if (page->priv->job_progress_pulse_timer_id == 0) {
                                page->priv->job_progress_pulse_timer_id = g_timeout_add (
                                        1000 / 50,
                                        job_progress_pulse_timeout_handler,
                                        page);
                        }
                } else {
                        gtk_progress_bar_set_fraction (GTK_PROGRESS_BAR (page->priv->job_progress_bar),
                                                       percentage / 100.0);
                        if (page->priv->job_progress_pulse_timer_id > 0) {
                                g_source_remove (page->priv->job_progress_pulse_timer_id);
                                page->priv->job_progress_pulse_timer_id = 0;
                        }
                }

                g_free (job_description);
                g_free (task_description);

                gtk_widget_set_sensitive (page->priv->job_cancel_button, gdu_device_job_is_cancellable (device));
        } else {
                if (page->priv->job_progress_pulse_timer_id > 0) {
                        g_source_remove (page->priv->job_progress_pulse_timer_id);
                        page->priv->job_progress_pulse_timer_id = 0;
                }
        }

}


static void
gdu_page_summary_init (GduPageSummary *page)
{
        int n;
        int row, column;
        GtkWidget *table;
        GtkWidget *align;
        GtkWidget *progress_bar;
        GtkWidget *label;
        GtkWidget *button_box;
        GtkWidget *button;
        GtkWidget *vbox;
        GtkWidget *vbox2;

        page->priv = g_new0 (GduPageSummaryPrivate, 1);

        page->priv->notebook = gtk_notebook_new ();
        gtk_container_set_border_width (GTK_CONTAINER (page->priv->notebook), 8);
        gtk_notebook_set_show_tabs (GTK_NOTEBOOK (page->priv->notebook), FALSE);
        gtk_notebook_set_show_border (GTK_NOTEBOOK (page->priv->notebook), FALSE);

        /* Add 5x2 summary labels for: drive, volume, unallocated space */
        for (n = 0; n < 3; n++) {
                GList **labels;

                switch (n) {
                case 0:
                        labels = &page->priv->drive_labels;
                        break;
                case 1:
                        labels = &page->priv->volume_labels;
                        break;
                case 2:
                        labels = &page->priv->unallocated_labels;
                        break;
                default:
                        g_assert_not_reached ();
                        break;
                }

                *labels = NULL;

                table = gtk_table_new (5, 4, FALSE);
                gtk_table_set_col_spacings (GTK_TABLE (table), 8);
                gtk_table_set_row_spacings (GTK_TABLE (table), 4);
                for (row = 0; row < 5; row++) {
                        for (column = 0; column < 2; column++) {
                                GtkWidget *key_label;
                                GtkWidget *value_label;

                                key_label = gtk_label_new (NULL);
                                gtk_misc_set_alignment (GTK_MISC (key_label), 1.0, 0.5);
                                gtk_label_set_markup (GTK_LABEL (key_label), "<b>Key:</b>");

                                value_label = gtk_label_new (NULL);
                                gtk_label_set_markup (GTK_LABEL (value_label), "Value");
                                gtk_misc_set_alignment (GTK_MISC (value_label), 0.0, 0.5);
                                gtk_label_set_selectable (GTK_LABEL (value_label), TRUE);
                                gtk_label_set_ellipsize (GTK_LABEL (value_label), PANGO_ELLIPSIZE_END);

                                gtk_table_attach (GTK_TABLE (table), key_label,   column + 0, column + 1, row, row + 1,
                                                  GTK_FILL, GTK_EXPAND | GTK_FILL, 0, 0);
                                gtk_table_attach (GTK_TABLE (table), value_label, column + 1, column + 2, row, row + 1,
                                                  GTK_EXPAND | GTK_FILL, GTK_EXPAND | GTK_FILL, 0, 0);

                                *labels = g_list_append (*labels, key_label);
                                *labels = g_list_append (*labels, value_label);
                        }
                }
                gtk_notebook_append_page (GTK_NOTEBOOK (page->priv->notebook), table, NULL);
        }

        /* job progress page */
        vbox = gtk_vbox_new (FALSE, 5);

        align = gtk_alignment_new (0.5, 0.5, 0.15, 0.15);
        vbox2 = gtk_vbox_new (FALSE, 5);
        gtk_container_add (GTK_CONTAINER (align), vbox2);

        label = gtk_label_new (NULL);
        gtk_label_set_markup (GTK_LABEL (label), "<b>Job Name</b>");
        gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
        gtk_box_pack_start (GTK_BOX (vbox2), label, TRUE, TRUE, 0);
        page->priv->job_description_label = label;

        progress_bar = gtk_progress_bar_new ();
        gtk_box_pack_start (GTK_BOX (vbox2), progress_bar, TRUE, TRUE, 0);
        page->priv->job_progress_bar = progress_bar;

        label = gtk_label_new (NULL);
        gtk_label_set_markup (GTK_LABEL (label), "Task Name (task 1 of 3)");
        gtk_misc_set_alignment (GTK_MISC (label), 0.5, 0.5);
        gtk_box_pack_start (GTK_BOX (vbox2), label, TRUE, TRUE, 0);
        page->priv->job_task_label = label;


        button_box = gtk_hbutton_box_new ();
        gtk_button_box_set_layout (GTK_BUTTON_BOX (button_box), GTK_BUTTONBOX_END);
        gtk_box_set_spacing (GTK_BOX (button_box), 6);
        button = gtk_button_new_from_stock (GTK_STOCK_CANCEL);
        page->priv->job_cancel_button = button;
        gtk_container_add (GTK_CONTAINER (button_box), button);

        gtk_box_pack_start (GTK_BOX (vbox), align, FALSE, FALSE, 0);
        gtk_box_pack_start (GTK_BOX (vbox), button_box, FALSE, FALSE, 0);

        g_signal_connect (page->priv->job_cancel_button, "clicked",
                          G_CALLBACK (job_cancel_button_clicked), page);

        gtk_notebook_append_page (GTK_NOTEBOOK (page->priv->notebook), vbox, NULL);


        /* job failure page */

        GtkWidget *image;
        GtkWidget *hbox;

        vbox = gtk_vbox_new (FALSE, 5);

        hbox = gtk_hbox_new (FALSE, 5);
        vbox2 = gtk_vbox_new (FALSE, 5);
        image = gtk_image_new_from_stock (GTK_STOCK_DIALOG_ERROR, GTK_ICON_SIZE_BUTTON);
        gtk_box_pack_start (GTK_BOX (hbox), image, FALSE, TRUE, 0);
        gtk_box_pack_start (GTK_BOX (hbox), vbox2, FALSE, TRUE, 0);

        align = gtk_alignment_new (0.5, 0.5, 0.0, 0.0);
        gtk_container_add (GTK_CONTAINER (align), hbox);

        label = gtk_label_new (NULL);
        gtk_label_set_markup (GTK_LABEL (label), _("<b>Job Failed</b>"));
        gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
        gtk_box_pack_start (GTK_BOX (vbox2), label, FALSE, TRUE, 0);

        label = gtk_label_new (NULL);
        gtk_label_set_markup (GTK_LABEL (label), "Reason the job failed");
        gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
        gtk_label_set_line_wrap (GTK_LABEL (label), TRUE);
        gtk_box_pack_start (GTK_BOX (vbox2), label, FALSE, TRUE, 0);
        page->priv->job_failed_reason_label = label;

        button_box = gtk_hbutton_box_new ();
        gtk_button_box_set_layout (GTK_BUTTON_BOX (button_box), GTK_BUTTONBOX_END);
        gtk_box_set_spacing (GTK_BOX (button_box), 6);
        button = gtk_button_new_with_mnemonic (_("_Dismiss"));
        page->priv->job_failed_dismiss_button = button;
        gtk_container_add (GTK_CONTAINER (button_box), button);

        gtk_box_pack_start (GTK_BOX (vbox), align, TRUE, FALSE, 0);
        gtk_box_pack_start (GTK_BOX (vbox), button_box, FALSE, FALSE, 0);

        g_signal_connect (page->priv->job_failed_dismiss_button, "clicked",
                          G_CALLBACK (job_failed_dismiss_button_clicked), page);

        gtk_notebook_append_page (GTK_NOTEBOOK (page->priv->notebook), vbox, NULL);

}


GduPageSummary *
gdu_page_summary_new (GduShell *shell)
{
        return GDU_PAGE_SUMMARY (g_object_new (GDU_TYPE_PAGE_SUMMARY, "shell", shell, NULL));
}

static gboolean
gdu_page_summary_update (GduPage *_page, GduPresentable *presentable)
{
        int page_to_show;
        GList *labels;
        GList *i;
        GList *j;
        GList *kv_pairs;
        GduDevice *device;
        GduPageSummary *page = GDU_PAGE_SUMMARY (_page);

        labels = NULL;
        device = gdu_presentable_get_device (presentable);
        if (device == NULL) {
                page_to_show = 2;
                labels = page->priv->unallocated_labels;
        } else {
                if (gdu_device_job_in_progress (device)) {
                        page_to_show = 3;
                } else if (gdu_device_job_get_last_error_message (device) != NULL) {
                        gtk_label_set_markup (GTK_LABEL (page->priv->job_failed_reason_label),
                                              gdu_device_job_get_last_error_message (device));
                        page_to_show = 4;
                } else if (gdu_device_is_partition (device)) {
                        page_to_show = 1;
                        labels = page->priv->volume_labels;
                } else {
                        page_to_show = 0;
                        labels = page->priv->drive_labels;
                }
        }

        job_update (page, device);

        if (labels != NULL) {
                /* update key/value pairs on summary tabs */
                kv_pairs = gdu_presentable_get_info (presentable);
                for (i = kv_pairs, j = labels; i != NULL && j != NULL; i = i->next, j = j->next) {
                        char *key;
                        char *key2;
                        char *value;
                        char *value2;
                        GtkWidget *key_label;
                        GtkWidget *value_label;

                        key = i->data;
                        key_label = j->data;
                        i = i->next;
                        j = j->next;
                        if (i == NULL || j == NULL) {
                                g_free (key);
                                break;
                        }
                        value = i->data;
                        value_label = j->data;

                        key2 = g_strdup_printf ("<small><b>%s:</b></small>", key);
                        value2 = g_strdup_printf ("<small>%s</small>", value);
                        gtk_label_set_markup (GTK_LABEL (key_label), key2);
                        gtk_label_set_markup (GTK_LABEL (value_label), value2);
                        g_free (key2);
                        g_free (value2);
                }
                g_list_foreach (kv_pairs, (GFunc) g_free, NULL);
                g_list_free (kv_pairs);

                /* clear remaining labels */
                for ( ; j != NULL; j = j->next) {
                        GtkWidget *label = j->data;
                        gtk_label_set_markup (GTK_LABEL (label), "");
                }
        }

        gtk_notebook_set_current_page (GTK_NOTEBOOK (page->priv->notebook), page_to_show);

        if (device != NULL) {
                g_object_unref (device);
        }
        return TRUE;
}

static GtkWidget *
gdu_page_summary_get_widget (GduPage *_page)
{
        GduPageSummary *page = GDU_PAGE_SUMMARY (_page);
        return page->priv->notebook;
}

static char *
gdu_page_summary_get_name (GduPage *page)
{
        return g_strdup (_("Summary"));
}

static void
gdu_page_summary_page_iface_init (GduPageIface *iface)
{
        iface->get_widget = gdu_page_summary_get_widget;
        iface->get_name = gdu_page_summary_get_name;
        iface->update = gdu_page_summary_update;
}
