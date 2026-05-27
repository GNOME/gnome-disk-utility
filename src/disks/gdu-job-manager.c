/* gdu-job-manager.c
 *
 * Copyright 2026 The GNOME Project
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Authors:
 *   Inam Ul Haq <inam123451@gmail.com>
 */

#include "config.h"

#include "gdu-job-manager.h"

struct _GduJobManager {
    GObject parent_instance;

    UDisksClient *client;

    /* Observable list model of current jobs, used by GtkListBox for binding. */
    GListStore *jobs;

    /* Maps object paths to per-object job queues. Only the queue head may run;
     * followers wait for it to finish. */
    GHashTable *jobs_by_object_path;
};

G_DEFINE_FINAL_TYPE (GduJobManager, gdu_job_manager, G_TYPE_OBJECT)

enum {
    PROP_0,
    PROP_CLIENT,
    PROP_JOBS,
    PROP_N_JOBS,
    PROP_HAS_JOBS,
    N_PROPS
};

enum {
    JOB_ADDED_SIGNAL,
    JOB_REMOVED_SIGNAL,
    EMPTY_SIGNAL,
    LAST_SIGNAL
};

static GParamSpec *props[N_PROPS];
static guint signals[LAST_SIGNAL];

static void job_state_changed_cb (GduLocalJob *job, GParamSpec *pspec, gpointer user_data);

static void
job_queue_free (GQueue *queue)
{
    g_queue_free_full (queue, g_object_unref);
}

static gboolean
gdu_job_manager_has_job (GduJobManager *self, GduLocalJob *job)
{
    const gchar *object_path;
    GQueue *queue;

    object_path = gdu_local_job_get_object_path (job);
    if (object_path == NULL)
        return FALSE;

    queue = g_hash_table_lookup (self->jobs_by_object_path, object_path);

    return queue != NULL && g_queue_find (queue, job) != NULL;
}

static void
start_next_job_for_object_path (GduJobManager *self, const gchar *object_path)
{
    GduLocalJob *job;
    GQueue *queue;

    if (object_path == NULL)
        return;

    queue = g_hash_table_lookup (self->jobs_by_object_path, object_path);
    if (queue == NULL)
        return;

    job = g_queue_peek_head (queue);
    if (job == NULL || gdu_local_job_get_state (job) != GDU_LOCAL_JOB_STATE_QUEUED)
        return;

    g_object_ref (job);
    gdu_local_job_start (job);
    g_object_unref (job);
}

static void
notify_job_count (GduJobManager *self, guint previous_count)
{
    guint count;

    count = gdu_job_manager_get_n_jobs (self);
    g_object_notify_by_pspec (G_OBJECT (self), props[PROP_N_JOBS]);
    g_object_notify_by_pspec (G_OBJECT (self), props[PROP_HAS_JOBS]);

    if (previous_count > 0 && count == 0)
        g_signal_emit (self, signals[EMPTY_SIGNAL], 0);
}

GduLocalJob *
gdu_job_manager_enqueue (GduJobManager *self, GduLocalJob *job)
{
    g_autofree gchar *object_path = NULL;
    guint previous_count;
    GQueue *queue;

    g_return_val_if_fail (GDU_IS_JOB_MANAGER (self), NULL);
    g_return_val_if_fail (GDU_IS_LOCAL_JOB (job), NULL);
    g_return_val_if_fail (gdu_local_job_get_state (job) == GDU_LOCAL_JOB_STATE_QUEUED, NULL);

    object_path = g_strdup (gdu_local_job_get_object_path (job));
    if (object_path == NULL)
        return NULL;

    if (gdu_job_manager_has_job (self, job))
        return NULL;

    previous_count = gdu_job_manager_get_n_jobs (self);
    queue = g_hash_table_lookup (self->jobs_by_object_path, object_path);
    if (queue == NULL) {
        queue = g_queue_new ();
        g_hash_table_insert (self->jobs_by_object_path, g_strdup (object_path), queue);
    }

    g_queue_push_tail (queue, g_object_ref (job));
    g_list_store_append (self->jobs, job);
    g_signal_connect_object (job, "notify::state", G_CALLBACK (job_state_changed_cb), self, 0);

    g_signal_emit (self, signals[JOB_ADDED_SIGNAL], 0, job);
    notify_job_count (self, previous_count);
    start_next_job_for_object_path (self, object_path);

    return job;
}

static gboolean
gdu_job_manager_dequeue (GduJobManager *self, GduLocalJob *job)
{
    g_autofree gchar *object_path = NULL;
    GQueue *queue;

    object_path = g_strdup (gdu_local_job_get_object_path (job));
    if (object_path == NULL)
        return FALSE;

    queue = g_hash_table_lookup (self->jobs_by_object_path, object_path);
    if (queue == NULL || !g_queue_remove (queue, job))
        return FALSE;

    if (g_queue_is_empty (queue))
        g_hash_table_remove (self->jobs_by_object_path, object_path);

    g_object_unref (job);

    return TRUE;
}

static void
gdu_job_manager_remove_job (GduJobManager *self, GduLocalJob *job)
{
    g_autofree gchar *object_path = NULL;
    guint previous_count;
    guint position;

    if (!gdu_job_manager_has_job (self, job))
        return;

    object_path = g_strdup (gdu_local_job_get_object_path (job));
    previous_count = gdu_job_manager_get_n_jobs (self);
    g_object_ref (job);

    if (g_list_store_find (self->jobs, job, &position))
        g_list_store_remove (self->jobs, position);

    gdu_job_manager_dequeue (self, job);
    g_signal_emit (self, signals[JOB_REMOVED_SIGNAL], 0, job);
    notify_job_count (self, previous_count);
    start_next_job_for_object_path (self, object_path);

    g_object_unref (job);
}

static void
job_state_changed_cb (GduLocalJob *job, GParamSpec *pspec, gpointer user_data)
{
    GduJobManager *self = GDU_JOB_MANAGER (user_data);

    if (gdu_local_job_get_state (job) == GDU_LOCAL_JOB_STATE_FINISHED)
        gdu_job_manager_remove_job (self, job);
}

static void
gdu_job_manager_get_property (GObject *object, guint property_id, GValue *value, GParamSpec *pspec)
{
    GduJobManager *self = GDU_JOB_MANAGER (object);

    switch (property_id) {
    case PROP_CLIENT:
        g_value_set_object (value, self->client);
        break;

    case PROP_JOBS:
        g_value_set_object (value, self->jobs);
        break;

    case PROP_N_JOBS:
        g_value_set_uint (value, gdu_job_manager_get_n_jobs (self));
        break;

    case PROP_HAS_JOBS:
        g_value_set_boolean (value, gdu_job_manager_has_jobs (self));
        break;

    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
        break;
    }
}

static void
gdu_job_manager_set_property (GObject *object, guint property_id, const GValue *value, GParamSpec *pspec)
{
    GduJobManager *self = GDU_JOB_MANAGER (object);

    switch (property_id) {
    case PROP_CLIENT:
        g_assert (self->client == NULL);
        self->client = g_value_dup_object (value);
        break;

    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
        break;
    }
}

static void
gdu_job_manager_finalize (GObject *object)
{
    GduJobManager *self = GDU_JOB_MANAGER (object);

    g_clear_pointer (&self->jobs_by_object_path, g_hash_table_destroy);
    g_clear_object (&self->client);
    g_clear_object (&self->jobs);

    G_OBJECT_CLASS (gdu_job_manager_parent_class)->finalize (object);
}

static void
gdu_job_manager_class_init (GduJobManagerClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->get_property = gdu_job_manager_get_property;
    object_class->set_property = gdu_job_manager_set_property;
    object_class->finalize = gdu_job_manager_finalize;

    props[PROP_CLIENT] = g_param_spec_object ("client", NULL, NULL, UDISKS_TYPE_CLIENT,
                                              G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);

    props[PROP_JOBS] =
        g_param_spec_object ("jobs", NULL, NULL, G_TYPE_LIST_MODEL, G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

    props[PROP_N_JOBS] =
        g_param_spec_uint ("n-jobs", NULL, NULL, 0, G_MAXUINT, 0, G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

    props[PROP_HAS_JOBS] =
        g_param_spec_boolean ("has-jobs", NULL, NULL, FALSE, G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties (object_class, N_PROPS, props);

    signals[JOB_ADDED_SIGNAL] = g_signal_new ("job-added", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST, 0, NULL, NULL,
                                              g_cclosure_marshal_VOID__OBJECT, G_TYPE_NONE, 1, GDU_TYPE_LOCAL_JOB);

    signals[JOB_REMOVED_SIGNAL] =
        g_signal_new ("job-removed", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST, 0, NULL, NULL,
                      g_cclosure_marshal_VOID__OBJECT, G_TYPE_NONE, 1, GDU_TYPE_LOCAL_JOB);

    signals[EMPTY_SIGNAL] = g_signal_new ("empty", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST, 0, NULL, NULL,
                                          g_cclosure_marshal_VOID__VOID, G_TYPE_NONE, 0);
}

static void
gdu_job_manager_init (GduJobManager *self)
{
    self->jobs = g_list_store_new (GDU_TYPE_LOCAL_JOB);
    self->jobs_by_object_path =
        g_hash_table_new_full (g_str_hash, g_str_equal, g_free, (GDestroyNotify) job_queue_free);
}

GduJobManager *
gdu_job_manager_new (UDisksClient *client)
{
    g_return_val_if_fail (UDISKS_IS_CLIENT (client), NULL);

    return GDU_JOB_MANAGER (g_object_new (GDU_TYPE_JOB_MANAGER, "client", client, NULL));
}

GListModel *
gdu_job_manager_get_jobs (GduJobManager *self)
{
    g_return_val_if_fail (GDU_IS_JOB_MANAGER (self), NULL);

    return G_LIST_MODEL (self->jobs);
}

guint
gdu_job_manager_get_n_jobs (GduJobManager *self)
{
    g_return_val_if_fail (GDU_IS_JOB_MANAGER (self), 0);

    return g_list_model_get_n_items (G_LIST_MODEL (self->jobs));
}

gboolean
gdu_job_manager_has_jobs (GduJobManager *self)
{
    g_return_val_if_fail (GDU_IS_JOB_MANAGER (self), FALSE);

    return gdu_job_manager_get_n_jobs (self) > 0;
}

void
gdu_job_manager_cancel_job (GduJobManager *self, GduLocalJob *job)
{
    g_return_if_fail (GDU_IS_JOB_MANAGER (self));
    g_return_if_fail (GDU_IS_LOCAL_JOB (job));

    if (!gdu_job_manager_has_job (self, job))
        return;

    if (gdu_local_job_get_state (job) == GDU_LOCAL_JOB_STATE_FINISHED)
        return;

    gdu_local_job_request_cancel (job);
}

void
gdu_job_manager_cancel_all (GduJobManager *self)
{
    g_autoptr(GPtrArray) jobs = NULL;
    guint n_jobs;

    g_return_if_fail (GDU_IS_JOB_MANAGER (self));

    jobs = g_ptr_array_new_with_free_func (g_object_unref);
    n_jobs = g_list_model_get_n_items (G_LIST_MODEL (self->jobs));

    for (guint i = 0; i < n_jobs; i++)
        g_ptr_array_add (jobs, g_list_model_get_item (G_LIST_MODEL (self->jobs), i));

    for (guint i = 0; i < jobs->len; i++)
        gdu_job_manager_cancel_job (self, g_ptr_array_index (jobs, i));
}
