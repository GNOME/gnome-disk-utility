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
#include <polkit-gnome/polkit-gnome.h>

#include <gdu/gdu.h>
#include "gdu-section-job.h"

struct _GduSectionJobPrivate
{
        GtkWidget *job_description_label;
        GtkWidget *job_progress_bar;
        GtkWidget *job_task_label;

        PolKitAction *pk_cancel_job_others_action;
        PolKitGnomeAction *cancel_action;

        guint job_progress_pulse_timer_id;
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
cancel_action_callback (GtkAction *action, gpointer user_data)
{
        GduDevice *device;
        GduSectionJob *section = GDU_SECTION_JOB (user_data);

        device = gdu_presentable_get_device (gdu_section_get_presentable (GDU_SECTION (section)));
        if (device != NULL) {
                gdu_device_op_cancel_job (device, NULL, NULL);
                g_object_unref (device);
        }
}

static void
job_update (GduSectionJob *section, GduDevice *device)
{
        char *s;
        uid_t job_initiator;
        char *job_description;
        char *task_description;
        double percentage;

        if (device != NULL && gdu_device_job_in_progress (device)) {
                PolKitAction *pk_action;

                job_initiator = gdu_device_job_get_initiated_by_uid (device);

                pk_action = NULL;
                if (job_initiator != getuid ())
                        pk_action = section->priv->pk_cancel_job_others_action;
                g_object_set (section->priv->cancel_action,
                              "polkit-action",
                              pk_action,
                              NULL);

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

                polkit_gnome_action_set_sensitive (section->priv->cancel_action,
                                                   gdu_device_job_is_cancellable (device));
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
        GList *labels;
        GduPresentable *presentable;
        GduDevice *device;

        labels = NULL;
        presentable = gdu_section_get_presentable (GDU_SECTION (section));
        device = gdu_presentable_get_device (presentable);
        if (device == NULL)
                goto out;

        if (!gdu_device_job_in_progress (device)) {
                g_warning ("showing job but no job is in progress");
                goto out;
        }

        job_update (section, device);

out:
        if (device != NULL) {
                g_object_unref (device);
        }
}

/* ---------------------------------------------------------------------------------------------------- */

static void
gdu_section_job_finalize (GduSectionJob *section)
{
        polkit_action_unref (section->priv->pk_cancel_job_others_action);

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

        g_type_class_add_private (klass, sizeof (GduSectionJobPrivate));
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

        section->priv = G_TYPE_INSTANCE_GET_PRIVATE (section, GDU_TYPE_SECTION_JOB, GduSectionJobPrivate);

        section->priv->pk_cancel_job_others_action = polkit_action_new ();
        polkit_action_set_action_id (section->priv->pk_cancel_job_others_action,
                                     "org.freedesktop.devicekit.disks.cancel-job-others");

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

        section->priv->cancel_action = polkit_gnome_action_new_default (
                "cancel",
                NULL,
                _("_Cancel"),
                _("Cancel Job"));
        g_object_set (section->priv->cancel_action,
                      "auth-label", _("_Cancel..."),
                      "yes-icon-name", GTK_STOCK_CANCEL,
                      "no-icon-name", GTK_STOCK_CANCEL,
                      "auth-icon-name", GTK_STOCK_CANCEL,
                      "self-blocked-icon-name", GTK_STOCK_CANCEL,
                      NULL);
        g_signal_connect (section->priv->cancel_action, "activate", G_CALLBACK (cancel_action_callback), section);

        button = polkit_gnome_action_create_button (section->priv->cancel_action);
        gtk_container_add (GTK_CONTAINER (button_box), button);

        gtk_box_pack_start (GTK_BOX (vbox), align, FALSE, FALSE, 0);
        gtk_box_pack_start (GTK_BOX (vbox), button_box, FALSE, FALSE, 0);

        align = gtk_alignment_new (0.5, 0.5, 1.0, 0.0);
        gtk_container_add (GTK_CONTAINER (align), vbox);

        gtk_box_pack_start (GTK_BOX (section), align, TRUE, TRUE, 0);
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
