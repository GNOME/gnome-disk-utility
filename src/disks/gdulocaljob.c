/*
 * Copyright (C) 2008-2013 Red Hat, Inc.
 * Copyright 2026 The GNOME Project
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Authors:
 *   David Zeuthen <zeuthen@gmail.com>
 *   Inam Ul Haq <inam123451@gmail.com>
 */

#include "config.h"

#include <unistd.h>

#include "gdulocaljob.h"

struct _GduLocalJob {
    UDisksJobSkeleton parent_instance;

    UDisksObject *object;
    gchar *description;
    gchar *extra_markup;

    GduLocalJobRunFunc run_func;
    GduLocalJobUpdateFunc update_func;
    GduLocalJobCompletedFunc completed_func;
    gpointer user_data;
    GDestroyNotify user_data_destroy;

    GduLocalJobState state;
    GCancellable *cancellable;
    GTask *task;

    GMutex update_lock;
    guint update_id;
};

G_DEFINE_FINAL_TYPE (GduLocalJob, gdu_local_job, UDISKS_TYPE_JOB_SKELETON)

G_DEFINE_ENUM_TYPE (GduLocalJobState, gdu_local_job_state, G_DEFINE_ENUM_VALUE (GDU_LOCAL_JOB_STATE_QUEUED, "queued"),
                    G_DEFINE_ENUM_VALUE (GDU_LOCAL_JOB_STATE_RUNNING, "running"),
                    G_DEFINE_ENUM_VALUE (GDU_LOCAL_JOB_STATE_CANCELING, "canceling"),
                    G_DEFINE_ENUM_VALUE (GDU_LOCAL_JOB_STATE_FINISHED, "finished"));

enum {
    PROP_0,
    PROP_OBJECT,
    PROP_OBJECT_PATH,
    PROP_DESCRIPTION,
    PROP_EXTRA_MARKUP,
    PROP_STATE,
    PROP_CANCELLABLE,
    N_PROPS
};

enum {
    CANCEL_REQUESTED_SIGNAL,
    LAST_SIGNAL
};

static GParamSpec *props[N_PROPS];
static guint signals[LAST_SIGNAL];

static void gdu_local_job_set_state (GduLocalJob *job, GduLocalJobState state);
static void gdu_local_job_clear_queued_update (GduLocalJob *job);
static void gdu_local_job_complete (GduLocalJob *job, GduLocalJobResult result, GError *error);

static void
gdu_local_job_set_object (GduLocalJob *self, UDisksObject *object)
{
    const gchar *objects[2];

    g_assert (self->object == NULL);

    self->object = g_object_ref (object);

    objects[0] = gdu_local_job_get_object_path (self);
    objects[1] = NULL;
    udisks_job_set_objects (UDISKS_JOB (self), objects);
}

static gboolean
handle_cancel_cb (UDisksJob *object, GDBusMethodInvocation *invocation, GVariant *arg_options, gpointer user_data)
{
    GduLocalJob *self = GDU_LOCAL_JOB (object);

    if (!udisks_job_get_cancelable (object) || self->state == GDU_LOCAL_JOB_STATE_FINISHED) {
        g_dbus_method_invocation_return_error_literal (invocation, UDISKS_ERROR, UDISKS_ERROR_FAILED,
                                                       "Job is not cancelable");
        return G_DBUS_METHOD_INVOCATION_HANDLED;
    }

    gdu_local_job_request_cancel (self);
    udisks_job_complete_cancel (object, invocation);

    return G_DBUS_METHOD_INVOCATION_HANDLED;
}

static void
gdu_local_job_get_property (GObject *object, guint property_id, GValue *value, GParamSpec *pspec)
{
    GduLocalJob *self = GDU_LOCAL_JOB (object);

    switch (property_id) {
    case PROP_OBJECT:
        g_value_set_object (value, self->object);
        break;

    case PROP_OBJECT_PATH:
        g_value_set_string (value, gdu_local_job_get_object_path (self));
        break;

    case PROP_DESCRIPTION:
        g_value_set_string (value, self->description);
        break;

    case PROP_EXTRA_MARKUP:
        g_value_set_string (value, self->extra_markup);
        break;

    case PROP_STATE:
        g_value_set_enum (value, self->state);
        break;

    case PROP_CANCELLABLE:
        g_value_set_object (value, self->cancellable);
        break;

    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
        break;
    }
}

static void
gdu_local_job_set_property (GObject *object, guint property_id, const GValue *value, GParamSpec *pspec)
{
    GduLocalJob *self = GDU_LOCAL_JOB (object);

    switch (property_id) {
    case PROP_OBJECT:
        gdu_local_job_set_object (self, g_value_get_object (value));
        break;

    case PROP_DESCRIPTION:
        gdu_local_job_set_description (self, g_value_get_string (value));
        break;

    case PROP_EXTRA_MARKUP:
        gdu_local_job_set_extra_markup (self, g_value_get_string (value));
        break;

    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
        break;
    }
}

static void
gdu_local_job_call_completed_func (GduLocalJob *job, GduLocalJobResult result, GError *error)
{
    GduLocalJobCompletedFunc completed_func;

    completed_func = job->completed_func;
    job->completed_func = NULL;

    if (completed_func != NULL)
        completed_func (job, result, error);
}

static void
gdu_local_job_clear_user_data (GduLocalJob *job)
{
    gpointer user_data;
    GDestroyNotify user_data_destroy;

    user_data = job->user_data;
    user_data_destroy = job->user_data_destroy;
    job->user_data = NULL;
    job->user_data_destroy = NULL;

    if (user_data_destroy != NULL && user_data != NULL)
        user_data_destroy (user_data);
}

static void
gdu_local_job_finalize (GObject *object)
{
    GduLocalJob *self = GDU_LOCAL_JOB (object);

    gdu_local_job_clear_queued_update (self);
    gdu_local_job_clear_user_data (self);

    g_clear_object (&self->task);
    g_clear_object (&self->object);
    g_clear_object (&self->cancellable);
    g_clear_pointer (&self->description, g_free);
    g_clear_pointer (&self->extra_markup, g_free);
    g_mutex_clear (&self->update_lock);

    G_OBJECT_CLASS (gdu_local_job_parent_class)->finalize (object);
}

static void
gdu_local_job_class_init (GduLocalJobClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->get_property = gdu_local_job_get_property;
    object_class->set_property = gdu_local_job_set_property;
    object_class->finalize = gdu_local_job_finalize;

    props[PROP_OBJECT] = g_param_spec_object ("object", NULL, NULL, UDISKS_TYPE_OBJECT,
                                              G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);

    props[PROP_OBJECT_PATH] =
        g_param_spec_string ("object-path", NULL, NULL, NULL, G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

    props[PROP_DESCRIPTION] = g_param_spec_string (
        "description", NULL, NULL, NULL, G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

    props[PROP_EXTRA_MARKUP] = g_param_spec_string (
        "extra-markup", NULL, NULL, NULL, G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

    props[PROP_STATE] = g_param_spec_enum ("state", NULL, NULL, GDU_TYPE_LOCAL_JOB_STATE, GDU_LOCAL_JOB_STATE_QUEUED,
                                           G_PARAM_READABLE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

    props[PROP_CANCELLABLE] =
        g_param_spec_object ("cancellable", NULL, NULL, G_TYPE_CANCELLABLE, G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties (object_class, N_PROPS, props);

    signals[CANCEL_REQUESTED_SIGNAL] = g_signal_new ("cancel-requested", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
                                                     0, NULL, NULL, g_cclosure_marshal_VOID__VOID, G_TYPE_NONE, 0);
}

static void
gdu_local_job_init (GduLocalJob *self)
{
    self->cancellable = g_cancellable_new ();
    g_mutex_init (&self->update_lock);

    udisks_job_set_started_by_uid (UDISKS_JOB (self), getuid ());

    g_signal_connect (self, "handle-cancel", G_CALLBACK (handle_cancel_cb), NULL);
}

GduLocalJob *
gdu_local_job_new (UDisksObject *object, const gchar *operation, const gchar *description, GduLocalJobRunFunc run_func,
                   GduLocalJobUpdateFunc update_func, GduLocalJobCompletedFunc completed_func, gpointer user_data,
                   GDestroyNotify user_data_destroy)
{
    GduLocalJob *job;

    g_return_val_if_fail (UDISKS_IS_OBJECT (object), NULL);
    g_return_val_if_fail (run_func != NULL, NULL);

    job = GDU_LOCAL_JOB (
        g_object_new (GDU_TYPE_LOCAL_JOB, "object", object, "operation", operation, "description", description, NULL));
    job->run_func = run_func;
    job->update_func = update_func;
    job->completed_func = completed_func;
    job->user_data = user_data;
    job->user_data_destroy = user_data_destroy;

    return job;
}

static void
gdu_local_job_task_thread_func (GTask *task, gpointer source_object, gpointer task_data, GCancellable *cancellable)
{
    GduLocalJob *job = GDU_LOCAL_JOB (source_object);
    GduLocalJobResult result;
    g_autoptr(GError) error = NULL;

    if (g_task_return_error_if_cancelled (task))
        return;

    result = job->run_func (job, cancellable, job->user_data, &error);

    if (result == GDU_LOCAL_JOB_RESULT_ERROR) {
        if (error != NULL)
            g_task_return_error (task, g_steal_pointer (&error));
        else
            g_task_return_new_error (task, G_IO_ERROR, G_IO_ERROR_FAILED, "Job failed without setting an error");
        return;
    }

    if (error != NULL) {
        g_warning ("Job returned a non-error result with an error set: %s", error->message);
        g_clear_error (&error);
    }

    if (result == GDU_LOCAL_JOB_RESULT_CANCELLED) {
        g_task_return_new_error (task, G_IO_ERROR, G_IO_ERROR_CANCELLED, "Job was cancelled");
        return;
    }

    if (result != GDU_LOCAL_JOB_RESULT_SUCCESS) {
        g_task_return_new_error (task, G_IO_ERROR, G_IO_ERROR_FAILED, "Job returned an invalid result");
        return;
    }

    g_task_return_int (task, result);
}

static void
gdu_local_job_task_completed_cb (GObject *source_object, GAsyncResult *res, gpointer user_data)
{
    GduLocalJob *job = GDU_LOCAL_JOB (source_object);
    g_autoptr(GError) error = NULL;
    gssize result;

    result = g_task_propagate_int (G_TASK (res), &error);
    if (error != NULL) {
        if (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
            gdu_local_job_complete (job, GDU_LOCAL_JOB_RESULT_CANCELLED, NULL);
        else
            gdu_local_job_complete (job, GDU_LOCAL_JOB_RESULT_ERROR, g_steal_pointer (&error));
        return;
    }

    gdu_local_job_complete (job, result, NULL);
}

void
gdu_local_job_start (GduLocalJob *job)
{
    g_autoptr(GTask) task = NULL;

    g_return_if_fail (GDU_IS_LOCAL_JOB (job));
    g_return_if_fail (job->state == GDU_LOCAL_JOB_STATE_QUEUED);
    g_return_if_fail (job->run_func != NULL);

    task = g_task_new (job, job->cancellable, gdu_local_job_task_completed_cb, NULL);
    g_task_set_source_tag (task, gdu_local_job_start);

    g_mutex_lock (&job->update_lock);
    job->task = g_object_ref (task);
    g_mutex_unlock (&job->update_lock);

    gdu_local_job_set_state (job, GDU_LOCAL_JOB_STATE_RUNNING);
    udisks_job_set_start_time (UDISKS_JOB (job), g_get_real_time ());

    g_task_run_in_thread (task, gdu_local_job_task_thread_func);
}

static gboolean
queued_update_cb (gpointer user_data)
{
    GTask *task = G_TASK (user_data);
    GduLocalJob *job = g_task_get_source_object (task);
    GduLocalJobUpdateFunc update_func;

    g_mutex_lock (&job->update_lock);
    job->update_id = 0;
    update_func = job->update_func;
    g_mutex_unlock (&job->update_lock);

    if (update_func != NULL)
        update_func (job);

    return G_SOURCE_REMOVE;
}

void
gdu_local_job_queue_update (GduLocalJob *job)
{
    g_autoptr(GTask) task = NULL;
    g_autoptr(GSource) source = NULL;

    g_return_if_fail (GDU_IS_LOCAL_JOB (job));

    g_mutex_lock (&job->update_lock);
    if (job->update_func == NULL || job->update_id != 0 || job->task == NULL) {
        g_mutex_unlock (&job->update_lock);
        return;
    }

    task = g_object_ref (job->task);
    source = g_idle_source_new ();
    g_source_set_priority (source, G_PRIORITY_DEFAULT_IDLE);
    g_task_attach_source (task, source, queued_update_cb);
    job->update_id = g_source_get_id (source);

    g_mutex_unlock (&job->update_lock);
}

static void
gdu_local_job_clear_queued_update (GduLocalJob *job)
{
    guint update_id = 0;

    g_return_if_fail (GDU_IS_LOCAL_JOB (job));

    g_mutex_lock (&job->update_lock);
    if (job->update_id != 0) {
        update_id = job->update_id;
        job->update_id = 0;
    }
    g_mutex_unlock (&job->update_lock);

    if (update_id != 0)
        g_source_remove (update_id);
}

static void
gdu_local_job_cancel_updates (GduLocalJob *job)
{
    guint update_id = 0;

    g_mutex_lock (&job->update_lock);
    job->update_func = NULL;
    if (job->update_id != 0) {
        update_id = job->update_id;
        job->update_id = 0;
    }
    g_mutex_unlock (&job->update_lock);

    if (update_id != 0)
        g_source_remove (update_id);
}

static void
gdu_local_job_clear_task (GduLocalJob *job)
{
    g_autoptr(GTask) task = NULL;

    g_mutex_lock (&job->update_lock);
    task = g_steal_pointer (&job->task);
    g_mutex_unlock (&job->update_lock);
}

gpointer
gdu_local_job_get_user_data (GduLocalJob *job)
{
    g_return_val_if_fail (GDU_IS_LOCAL_JOB (job), NULL);

    return job->user_data;
}

UDisksObject *
gdu_local_job_get_object (GduLocalJob *job)
{
    g_return_val_if_fail (GDU_IS_LOCAL_JOB (job), NULL);

    return job->object;
}

const gchar *
gdu_local_job_get_object_path (GduLocalJob *self)
{
    g_return_val_if_fail (GDU_IS_LOCAL_JOB (self), NULL);

    if (self->object == NULL)
        return NULL;

    return g_dbus_object_get_object_path (G_DBUS_OBJECT (self->object));
}

const gchar *
gdu_local_job_get_operation (GduLocalJob *job)
{
    g_return_val_if_fail (GDU_IS_LOCAL_JOB (job), NULL);

    return udisks_job_get_operation (UDISKS_JOB (job));
}

void
gdu_local_job_set_operation (GduLocalJob *job, const gchar *operation)
{
    g_return_if_fail (GDU_IS_LOCAL_JOB (job));

    if (g_strcmp0 (udisks_job_get_operation (UDISKS_JOB (job)), operation) == 0)
        return;

    udisks_job_set_operation (UDISKS_JOB (job), operation);
}

const gchar *
gdu_local_job_get_description (GduLocalJob *job)
{
    g_return_val_if_fail (GDU_IS_LOCAL_JOB (job), NULL);

    return job->description;
}

void
gdu_local_job_set_description (GduLocalJob *job, const gchar *description)
{
    g_return_if_fail (GDU_IS_LOCAL_JOB (job));

    if (g_strcmp0 (job->description, description) == 0)
        return;

    g_free (job->description);
    job->description = g_strdup (description);
    g_object_notify_by_pspec (G_OBJECT (job), props[PROP_DESCRIPTION]);
}

const gchar *
gdu_local_job_get_extra_markup (GduLocalJob *job)
{
    g_return_val_if_fail (GDU_IS_LOCAL_JOB (job), NULL);

    return job->extra_markup;
}

void
gdu_local_job_set_extra_markup (GduLocalJob *job, const gchar *markup)
{
    g_return_if_fail (GDU_IS_LOCAL_JOB (job));

    if (g_strcmp0 (job->extra_markup, markup) == 0)
        return;

    g_free (job->extra_markup);
    job->extra_markup = g_strdup (markup);
    g_object_notify_by_pspec (G_OBJECT (job), props[PROP_EXTRA_MARKUP]);
}

GduLocalJobState
gdu_local_job_get_state (GduLocalJob *job)
{
    g_return_val_if_fail (GDU_IS_LOCAL_JOB (job), GDU_LOCAL_JOB_STATE_FINISHED);

    return job->state;
}

static void
gdu_local_job_set_state (GduLocalJob *job, GduLocalJobState state)
{
    g_return_if_fail (GDU_IS_LOCAL_JOB (job));
    g_return_if_fail (state <= GDU_LOCAL_JOB_STATE_FINISHED);

    if (job->state == state)
        return;

    job->state = state;
    g_object_notify_by_pspec (G_OBJECT (job), props[PROP_STATE]);
}

GCancellable *
gdu_local_job_get_cancellable (GduLocalJob *job)
{
    g_return_val_if_fail (GDU_IS_LOCAL_JOB (job), NULL);

    return job->cancellable;
}

gboolean
gdu_local_job_get_cancelable (GduLocalJob *job)
{
    g_return_val_if_fail (GDU_IS_LOCAL_JOB (job), FALSE);

    return udisks_job_get_cancelable (UDISKS_JOB (job));
}

void
gdu_local_job_set_cancelable (GduLocalJob *job, gboolean cancelable)
{
    g_return_if_fail (GDU_IS_LOCAL_JOB (job));

    cancelable = !!cancelable;
    if (udisks_job_get_cancelable (UDISKS_JOB (job)) == cancelable)
        return;

    udisks_job_set_cancelable (UDISKS_JOB (job), cancelable);
}

gboolean
gdu_local_job_get_progress_valid (GduLocalJob *job)
{
    g_return_val_if_fail (GDU_IS_LOCAL_JOB (job), FALSE);

    return udisks_job_get_progress_valid (UDISKS_JOB (job));
}

void
gdu_local_job_set_progress_valid (GduLocalJob *job, gboolean progress_valid)
{
    g_return_if_fail (GDU_IS_LOCAL_JOB (job));

    progress_valid = !!progress_valid;
    if (udisks_job_get_progress_valid (UDISKS_JOB (job)) == progress_valid)
        return;

    udisks_job_set_progress_valid (UDISKS_JOB (job), progress_valid);
}

gdouble
gdu_local_job_get_progress (GduLocalJob *job)
{
    g_return_val_if_fail (GDU_IS_LOCAL_JOB (job), 0.0);

    return udisks_job_get_progress (UDISKS_JOB (job));
}

void
gdu_local_job_set_progress (GduLocalJob *job, gdouble progress)
{
    g_return_if_fail (GDU_IS_LOCAL_JOB (job));

    progress = CLAMP (progress, 0.0, 1.0);
    if (udisks_job_get_progress (UDISKS_JOB (job)) == progress)
        return;

    udisks_job_set_progress (UDISKS_JOB (job), progress);
}

guint64
gdu_local_job_get_bytes (GduLocalJob *job)
{
    g_return_val_if_fail (GDU_IS_LOCAL_JOB (job), 0);

    return udisks_job_get_bytes (UDISKS_JOB (job));
}

void
gdu_local_job_set_bytes (GduLocalJob *job, guint64 bytes)
{
    g_return_if_fail (GDU_IS_LOCAL_JOB (job));

    if (udisks_job_get_bytes (UDISKS_JOB (job)) == bytes)
        return;

    udisks_job_set_bytes (UDISKS_JOB (job), bytes);
}

guint64
gdu_local_job_get_rate (GduLocalJob *job)
{
    g_return_val_if_fail (GDU_IS_LOCAL_JOB (job), 0);

    return udisks_job_get_rate (UDISKS_JOB (job));
}

void
gdu_local_job_set_rate (GduLocalJob *job, guint64 rate)
{
    g_return_if_fail (GDU_IS_LOCAL_JOB (job));

    if (udisks_job_get_rate (UDISKS_JOB (job)) == rate)
        return;

    udisks_job_set_rate (UDISKS_JOB (job), rate);
}

guint64
gdu_local_job_get_expected_end_time (GduLocalJob *job)
{
    g_return_val_if_fail (GDU_IS_LOCAL_JOB (job), 0);

    return udisks_job_get_expected_end_time (UDISKS_JOB (job));
}

void
gdu_local_job_set_expected_end_time (GduLocalJob *job, guint64 expected_end_time)
{
    g_return_if_fail (GDU_IS_LOCAL_JOB (job));

    if (udisks_job_get_expected_end_time (UDISKS_JOB (job)) == expected_end_time)
        return;

    udisks_job_set_expected_end_time (UDISKS_JOB (job), expected_end_time);
}

void
gdu_local_job_request_cancel (GduLocalJob *job)
{
    GduLocalJobState previous_state;

    g_return_if_fail (GDU_IS_LOCAL_JOB (job));

    if (job->state == GDU_LOCAL_JOB_STATE_CANCELING || job->state == GDU_LOCAL_JOB_STATE_FINISHED)
        return;

    previous_state = job->state;
    gdu_local_job_set_state (job, GDU_LOCAL_JOB_STATE_CANCELING);
    g_cancellable_cancel (job->cancellable);
    g_signal_emit (job, signals[CANCEL_REQUESTED_SIGNAL], 0);

    if (previous_state == GDU_LOCAL_JOB_STATE_QUEUED)
        gdu_local_job_complete (job, GDU_LOCAL_JOB_RESULT_CANCELLED, NULL);
}

static void
gdu_local_job_complete (GduLocalJob *job, GduLocalJobResult result, GError *error)
{
    g_autoptr(GError) owned_error = error;

    g_return_if_fail (GDU_IS_LOCAL_JOB (job));
    g_return_if_fail (result <= GDU_LOCAL_JOB_RESULT_ERROR);
    g_return_if_fail ((result == GDU_LOCAL_JOB_RESULT_ERROR) == (owned_error != NULL));

    if (job->state == GDU_LOCAL_JOB_STATE_FINISHED)
        return;

    gdu_local_job_cancel_updates (job);
    gdu_local_job_clear_task (job);
    gdu_local_job_call_completed_func (job, result, owned_error);
    gdu_local_job_clear_user_data (job);
    gdu_local_job_set_state (job, GDU_LOCAL_JOB_STATE_FINISHED);
}
