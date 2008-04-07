/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*- */
/* gdu-section-job.c
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
#include "gdu-section-job.h"

struct _GduSectionJobPrivate
{
        GtkWidget *notebook;

        GtkWidget *job_description_label;
        GtkWidget *job_progress_bar;
        GtkWidget *job_task_label;
        GtkWidget *job_cancel_button;
        guint job_progress_pulse_timer_id;

        GtkWidget *job_failed_reason_label;
        GtkWidget *job_failed_dismiss_button;
};

static GObjectClass *parent_class = NULL;

G_DEFINE_TYPE (GduSectionJob, gdu_section_job, GDU_TYPE_SECTION)

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
job_progress_pulse_timeout_handler (gpointer user_data)
{
        GduSectionJob *section = GDU_SECTION_JOB (user_data);

        gtk_progress_bar_pulse (GTK_PROGRESS_BAR (section->priv->job_progress_bar));
        return TRUE;
}


static void
job_cancel_button_clicked (GtkWidget *button, gpointer user_data)
{
        GduDevice *device;
        GduSectionJob *section = GDU_SECTION_JOB (user_data);

        device = gdu_presentable_get_device (gdu_section_get_presentable (GDU_SECTION (section)));
        if (device != NULL) {
                gdu_device_op_cancel_job (device);
                g_object_unref (device);
        }
}

static void
job_failed_dismiss_button_clicked (GtkWidget *button, gpointer user_data)
{
        GduDevice *device;
        GduSectionJob *section = GDU_SECTION_JOB (user_data);

        device = gdu_presentable_get_device (gdu_section_get_presentable (GDU_SECTION (section)));
        if (device != NULL) {
                gdu_device_job_clear_last_error_message (device);
                gtk_label_set_markup (GTK_LABEL (section->priv->job_failed_reason_label), "");
                gdu_shell_update (gdu_section_get_shell (GDU_SECTION (section)));
        }
}

static void
job_update (GduSectionJob *section, GduDevice *device)
{
        char *s;
        char *job_description;
        char *task_description;
        double percentage;

        if (device != NULL && gdu_device_job_in_progress (device)) {
                job_description = gdu_get_job_description (gdu_device_job_get_id (device));
                task_description = gdu_get_task_description (gdu_device_job_get_cur_task_id (device));

                s = g_strdup_printf ("<b>%s</b>", job_description);
                gtk_label_set_markup (GTK_LABEL (section->priv->job_description_label), s);
                g_free (s);

                s = g_strdup_printf (_("%s (task %d of %d)"),
                                     task_description,
                                     gdu_device_job_get_cur_task (device) + 1,
                                     gdu_device_job_get_num_tasks (device));
                gtk_label_set_markup (GTK_LABEL (section->priv->job_task_label), s);
                g_free (s);

                percentage = gdu_device_job_get_cur_task_percentage (device);
                if (percentage < 0) {
                        gtk_progress_bar_set_pulse_step (GTK_PROGRESS_BAR (section->priv->job_progress_bar), 2.0 / 50);
                        gtk_progress_bar_pulse (GTK_PROGRESS_BAR (section->priv->job_progress_bar));
                        if (section->priv->job_progress_pulse_timer_id == 0) {
                                section->priv->job_progress_pulse_timer_id = g_timeout_add (
                                        1000 / 50,
                                        job_progress_pulse_timeout_handler,
                                        section);
                        }
                } else {
                        gtk_progress_bar_set_fraction (GTK_PROGRESS_BAR (section->priv->job_progress_bar),
                                                       percentage / 100.0);
                        if (section->priv->job_progress_pulse_timer_id > 0) {
                                g_source_remove (section->priv->job_progress_pulse_timer_id);
                                section->priv->job_progress_pulse_timer_id = 0;
                        }
                }

                g_free (job_description);
                g_free (task_description);

                gtk_widget_set_sensitive (section->priv->job_cancel_button, gdu_device_job_is_cancellable (device));
        } else {
                if (section->priv->job_progress_pulse_timer_id > 0) {
                        g_source_remove (section->priv->job_progress_pulse_timer_id);
                        section->priv->job_progress_pulse_timer_id = 0;
                }
        }

}

static void
update (GduSectionJob *section)
{
        int section_to_show;
        GList *labels;
        GduPresentable *presentable;
        GduDevice *device;

        labels = NULL;
        presentable = gdu_section_get_presentable (GDU_SECTION (section));
        device = gdu_presentable_get_device (presentable);
        if (device == NULL)
                goto out;

        if (gdu_device_job_in_progress (device)) {
                section_to_show = 0;
        } else if (gdu_device_job_get_last_error_message (device) != NULL) {
                gtk_label_set_markup (GTK_LABEL (section->priv->job_failed_reason_label),
                                      gdu_device_job_get_last_error_message (device));
                section_to_show = 1;
        } else {
                g_warning ("showing job but no job is in progress / no job failure");
                goto out;
        }

        job_update (section, device);

        gtk_notebook_set_current_page (GTK_NOTEBOOK (section->priv->notebook), section_to_show);

out:
        if (device != NULL) {
                g_object_unref (device);
        }
}

/* ---------------------------------------------------------------------------------------------------- */

static void
gdu_section_job_finalize (GduSectionJob *section)
{
        if (section->priv->job_progress_pulse_timer_id > 0) {
                g_source_remove (section->priv->job_progress_pulse_timer_id);
        }

        if (G_OBJECT_CLASS (parent_class)->finalize)
                (* G_OBJECT_CLASS (parent_class)->finalize) (G_OBJECT (section));
}

static void
gdu_section_job_class_init (GduSectionJobClass *klass)
{
        GObjectClass *obj_class = (GObjectClass *) klass;
        GduSectionClass *section_class = (GduSectionClass *) klass;

        parent_class = g_type_class_peek_parent (klass);

        obj_class->finalize = (GObjectFinalizeFunc) gdu_section_job_finalize;
        section_class->update = (gpointer) update;
}

static void
gdu_section_job_init (GduSectionJob *section)
{
        GtkWidget *align;
        GtkWidget *progress_bar;
        GtkWidget *label;
        GtkWidget *button_box;
        GtkWidget *button;
        GtkWidget *vbox;
        GtkWidget *vbox2;
        GtkWidget *image;
        GtkWidget *hbox;

        section->priv = g_new0 (GduSectionJobPrivate, 1);

        section->priv->notebook = gtk_notebook_new ();
        gtk_box_pack_start (GTK_BOX (section), section->priv->notebook, TRUE, TRUE, 0);
        gtk_container_set_border_width (GTK_CONTAINER (section->priv->notebook), 8);
        gtk_notebook_set_show_tabs (GTK_NOTEBOOK (section->priv->notebook), FALSE);
        gtk_notebook_set_show_border (GTK_NOTEBOOK (section->priv->notebook), FALSE);

        /* job progress section */
        vbox = gtk_vbox_new (FALSE, 5);

        align = gtk_alignment_new (0.5, 0.5, 0.15, 0.15);
        vbox2 = gtk_vbox_new (FALSE, 5);
        gtk_container_add (GTK_CONTAINER (align), vbox2);

        label = gtk_label_new (NULL);
        gtk_label_set_markup (GTK_LABEL (label), "<b>Job Name</b>");
        gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
        gtk_box_pack_start (GTK_BOX (vbox2), label, TRUE, TRUE, 0);
        section->priv->job_description_label = label;

        progress_bar = gtk_progress_bar_new ();
        gtk_box_pack_start (GTK_BOX (vbox2), progress_bar, TRUE, TRUE, 0);
        section->priv->job_progress_bar = progress_bar;

        label = gtk_label_new (NULL);
        gtk_label_set_markup (GTK_LABEL (label), "Task Name (task 1 of 3)");
        gtk_misc_set_alignment (GTK_MISC (label), 0.5, 0.5);
        gtk_box_pack_start (GTK_BOX (vbox2), label, TRUE, TRUE, 0);
        section->priv->job_task_label = label;


        button_box = gtk_hbutton_box_new ();
        gtk_button_box_set_layout (GTK_BUTTON_BOX (button_box), GTK_BUTTONBOX_END);
        gtk_box_set_spacing (GTK_BOX (button_box), 6);
        button = gtk_button_new_from_stock (GTK_STOCK_CANCEL);
        section->priv->job_cancel_button = button;
        gtk_container_add (GTK_CONTAINER (button_box), button);

        gtk_box_pack_start (GTK_BOX (vbox), align, FALSE, FALSE, 0);
        gtk_box_pack_start (GTK_BOX (vbox), button_box, FALSE, FALSE, 0);

        g_signal_connect (section->priv->job_cancel_button, "clicked",
                          G_CALLBACK (job_cancel_button_clicked), section);

        align = gtk_alignment_new (0.5, 0.5, 1.0, 0.0);
        gtk_container_add (GTK_CONTAINER (align), vbox);

        gtk_notebook_append_page (GTK_NOTEBOOK (section->priv->notebook), align, NULL);

        /* job failure section */
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
        section->priv->job_failed_reason_label = label;

        button_box = gtk_hbutton_box_new ();
        gtk_button_box_set_layout (GTK_BUTTON_BOX (button_box), GTK_BUTTONBOX_END);
        gtk_box_set_spacing (GTK_BOX (button_box), 6);
        button = gtk_button_new_with_mnemonic (_("_Dismiss"));
        section->priv->job_failed_dismiss_button = button;
        gtk_container_add (GTK_CONTAINER (button_box), button);

        gtk_box_pack_start (GTK_BOX (vbox), align, TRUE, FALSE, 0);
        gtk_box_pack_start (GTK_BOX (vbox), button_box, FALSE, FALSE, 0);

        g_signal_connect (section->priv->job_failed_dismiss_button, "clicked",
                          G_CALLBACK (job_failed_dismiss_button_clicked), section);

        align = gtk_alignment_new (0.5, 0.5, 1.0, 0.0);
        gtk_container_add (GTK_CONTAINER (align), vbox);

        gtk_notebook_append_page (GTK_NOTEBOOK (section->priv->notebook), align, NULL);
}

GtkWidget *
gdu_section_job_new (GduShell       *shell,
                     GduPresentable *presentable)
{
        return GTK_WIDGET (g_object_new (GDU_TYPE_SECTION_JOB,
                                         "shell", shell,
                                         "presentable", presentable,
                                         NULL));
}
