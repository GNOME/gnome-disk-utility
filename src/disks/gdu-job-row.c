/* gdu-job-row.c
 *
 * Copyright 2026 The GNOME Project
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#define G_LOG_DOMAIN "gdu-job-row"

#include "config.h"

#include "gdu-job-row.h"

#include <glib/gi18n.h>
#include <gtk/gtk.h>

#include "gduutils.h"

struct _GduJobRow {
    AdwPreferencesRow parent_instance;

    GtkProgressBar *progress_bar;
    GtkLabel *status_label;

    GduLocalJob *job;
    GduJobManager *job_manager;
    gulong job_notify_id;
};

G_DEFINE_FINAL_TYPE (GduJobRow, gdu_job_row, ADW_TYPE_PREFERENCES_ROW)

static gchar *
format_job_status (GduJobRow *self)
{
    GduLocalJobState state;

    g_assert (GDU_IS_JOB_ROW (self));
    g_assert (GDU_IS_LOCAL_JOB (self->job));

    state = gdu_local_job_get_state (self->job);

    switch (state) {
    case GDU_LOCAL_JOB_STATE_QUEUED:
        return g_strdup (_("Queued"));

    case GDU_LOCAL_JOB_STATE_CANCELING:
        return g_strdup (_("Canceling"));

    case GDU_LOCAL_JOB_STATE_FINISHED:
        return g_strdup (_("Finished"));

    case GDU_LOCAL_JOB_STATE_RUNNING:
        break;

    default:
        g_assert_not_reached ();
    }

    if (gdu_local_job_get_progress_valid (self->job)) {
        g_autofree gchar *remaining = NULL;
        guint64 expected_end_time;
        guint percent;
        gdouble progress;
        gint64 now;

        progress = CLAMP (gdu_local_job_get_progress (self->job), 0.0, 1.0);
        percent = (guint) (progress * 100.0 + 0.5);
        expected_end_time = gdu_local_job_get_expected_end_time (self->job);
        now = g_get_real_time ();

        if (expected_end_time > (guint64) now) {
            remaining = gdu_utils_format_duration_usec (expected_end_time - (guint64) now,
                                                        GDU_FORMAT_DURATION_FLAGS_NO_SECONDS);
            /* Translators: Shown for an active job. The first placeholder is the
             * percentage complete, the second is a duration such as "1 minute". */
            return g_strdup_printf (_("%u%% - %s left"), percent, remaining);
        }

        return g_strdup_printf (_("%u%%"), percent);
    }

    return g_strdup (_("Running"));
}

static void
gdu_job_row_set_status_label (GduJobRow *self, const gchar *status)
{
    const gchar *extra_markup;

    extra_markup = gdu_local_job_get_extra_markup (self->job);
    if (extra_markup != NULL && *extra_markup != '\0') {
        g_autofree gchar *escaped_status = NULL;
        g_autofree gchar *markup = NULL;

        escaped_status = g_markup_escape_text (status, -1);
        markup = g_strdup_printf ("%s — %s", escaped_status, extra_markup);
        gtk_label_set_markup (self->status_label, markup);
    } else {
        gtk_label_set_text (self->status_label, status);
    }
}

static void
gdu_job_row_update (GduJobRow *self)
{
    GduLocalJobState state;
    const gchar *description;
    g_autofree gchar *status = NULL;
    gboolean can_cancel;
    gdouble progress;

    g_assert (GDU_IS_JOB_ROW (self));

    if (self->job == NULL)
        return;

    description = gdu_local_job_get_description (self->job);
    if (description == NULL || *description == '\0')
        description = gdu_local_job_get_operation (self->job);
    if (description == NULL || *description == '\0')
        description = _("Disk Operation");

    adw_preferences_row_set_title (ADW_PREFERENCES_ROW (self), description);

    progress = gdu_local_job_get_progress_valid (self->job) ? gdu_local_job_get_progress (self->job) : 0.0;
    gtk_progress_bar_set_fraction (self->progress_bar, CLAMP (progress, 0.0, 1.0));

    state = gdu_local_job_get_state (self->job);
    can_cancel = gdu_local_job_get_cancelable (self->job) && state != GDU_LOCAL_JOB_STATE_CANCELING
                 && state != GDU_LOCAL_JOB_STATE_FINISHED;
    gtk_widget_action_set_enabled (GTK_WIDGET (self), "job.cancel", can_cancel);

    status = format_job_status (self);
    gdu_job_row_set_status_label (self, status);
}

static void
gdu_job_row_cancel_clicked_cb (GtkWidget *widget, const gchar *action_name, GVariant *parameter)
{
    GduJobRow *self = GDU_JOB_ROW (widget);

    g_assert (GDU_IS_JOB_ROW (self));

    if (self->job == NULL)
        return;

    if (self->job_manager != NULL)
        gdu_job_manager_cancel_job (self->job_manager, self->job);
    else
        gdu_local_job_request_cancel (self->job);
}

static void
gdu_job_row_dispose (GObject *object)
{
    GduJobRow *self = GDU_JOB_ROW (object);

    g_clear_signal_handler (&self->job_notify_id, self->job);

    g_clear_object (&self->job);
    g_clear_object (&self->job_manager);

    G_OBJECT_CLASS (gdu_job_row_parent_class)->dispose (object);
}

static void
gdu_job_row_class_init (GduJobRowClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

    object_class->dispose = gdu_job_row_dispose;

    gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/DiskUtility/ui/"
                                                               "gdu-job-row.ui");

    gtk_widget_class_bind_template_child (widget_class, GduJobRow, progress_bar);
    gtk_widget_class_bind_template_child (widget_class, GduJobRow, status_label);

    gtk_widget_class_install_action (widget_class, "job.cancel", NULL, gdu_job_row_cancel_clicked_cb);
}

static void
gdu_job_row_init (GduJobRow *self)
{
    gtk_widget_init_template (GTK_WIDGET (self));
}

GduJobRow *
gdu_job_row_new (GduLocalJob *job, GduJobManager *job_manager)
{
    GduJobRow *self;

    g_return_val_if_fail (GDU_IS_LOCAL_JOB (job), NULL);
    g_return_val_if_fail (GDU_IS_JOB_MANAGER (job_manager), NULL);

    self = g_object_new (GDU_TYPE_JOB_ROW, NULL);
    self->job = g_object_ref (job);
    self->job_manager = g_object_ref (job_manager);
    self->job_notify_id = g_signal_connect_swapped (self->job, "notify", G_CALLBACK (gdu_job_row_update), self);

    gdu_job_row_update (self);

    return self;
}

GduLocalJob *
gdu_job_row_get_job (GduJobRow *self)
{
    g_return_val_if_fail (GDU_IS_JOB_ROW (self), NULL);

    return self->job;
}
