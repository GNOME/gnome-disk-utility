/*
 * Copyright (C) 2008-2013 Red Hat, Inc.
 *
 * Licensed under GPL version 2 or later.
 *
 * Author: David Zeuthen <zeuthen@gmail.com>
 */

#pragma once

#include <gio/gio.h>

#include "gdutypes.h"

G_BEGIN_DECLS

#define GDU_TYPE_LOCAL_JOB (gdu_local_job_get_type ())
G_DECLARE_FINAL_TYPE (GduLocalJob, gdu_local_job, GDU, LOCAL_JOB, UDisksJobSkeleton)

typedef enum {
    GDU_LOCAL_JOB_STATE_QUEUED,
    GDU_LOCAL_JOB_STATE_RUNNING,
    GDU_LOCAL_JOB_STATE_CANCELING,
    GDU_LOCAL_JOB_STATE_FINISHED,
} GduLocalJobState;

typedef enum {
    GDU_LOCAL_JOB_RESULT_SUCCESS,
    GDU_LOCAL_JOB_RESULT_CANCELLED,
    GDU_LOCAL_JOB_RESULT_ERROR,
} GduLocalJobResult;

/* Called in a worker thread when the job is started by GduJobManager.
 * Use gdu_local_job_get_user_data() to access borrowed job-specific data and
 * gdu_local_job_queue_update() to refresh main-thread job properties. */
typedef GduLocalJobResult (*GduLocalJobRunFunc) (GduLocalJob *job, GCancellable *cancellable, GError **error);
typedef void (*GduLocalJobUpdateFunc) (GduLocalJob *job);
typedef void (*GduLocalJobCompletedFunc) (GduLocalJob *job, GduLocalJobResult result, GError *error);

#define GDU_TYPE_LOCAL_JOB_STATE (gdu_local_job_state_get_type ())
GType gdu_local_job_state_get_type (void) G_GNUC_CONST;

GduLocalJob *gdu_local_job_new (UDisksObject *object, const gchar *operation, const gchar *description,
                                GduLocalJobRunFunc run_func, GduLocalJobUpdateFunc update_func,
                                GduLocalJobCompletedFunc completed_func, gpointer user_data,
                                GDestroyNotify user_data_destroy);
void gdu_local_job_start (GduLocalJob *job);
void gdu_local_job_queue_update (GduLocalJob *job);
gpointer gdu_local_job_get_user_data (GduLocalJob *job);

UDisksObject *gdu_local_job_get_object (GduLocalJob *job);
const gchar *gdu_local_job_get_object_path (GduLocalJob *job);
const gchar *gdu_local_job_get_operation (GduLocalJob *job);
void gdu_local_job_set_operation (GduLocalJob *job, const gchar *operation);
const gchar *gdu_local_job_get_description (GduLocalJob *job);
void gdu_local_job_set_description (GduLocalJob *job, const gchar *description);
const gchar *gdu_local_job_get_extra_markup (GduLocalJob *job);
void gdu_local_job_set_extra_markup (GduLocalJob *job, const gchar *markup);
GduLocalJobState gdu_local_job_get_state (GduLocalJob *job);
gboolean gdu_local_job_get_cancelable (GduLocalJob *job);
void gdu_local_job_set_cancelable (GduLocalJob *job, gboolean cancelable);
gboolean gdu_local_job_get_progress_valid (GduLocalJob *job);
void gdu_local_job_set_progress_valid (GduLocalJob *job, gboolean progress_valid);
gdouble gdu_local_job_get_progress (GduLocalJob *job);
void gdu_local_job_set_progress (GduLocalJob *job, gdouble progress);
guint64 gdu_local_job_get_bytes (GduLocalJob *job);
void gdu_local_job_set_bytes (GduLocalJob *job, guint64 bytes);
guint64 gdu_local_job_get_rate (GduLocalJob *job);
void gdu_local_job_set_rate (GduLocalJob *job, guint64 rate);
guint64 gdu_local_job_get_expected_end_time (GduLocalJob *job);
void gdu_local_job_set_expected_end_time (GduLocalJob *job, guint64 expected_end_time);

void gdu_local_job_request_cancel (GduLocalJob *job);

G_END_DECLS
