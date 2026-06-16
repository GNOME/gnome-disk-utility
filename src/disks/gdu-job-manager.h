/* gdu-job-manager.h
 *
 * Copyright 2026 The GNOME Project
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include <gio/gio.h>

#include "gdulocaljob.h"

G_BEGIN_DECLS

#define GDU_TYPE_JOB_MANAGER (gdu_job_manager_get_type ())
G_DECLARE_FINAL_TYPE (GduJobManager, gdu_job_manager, GDU, JOB_MANAGER, GObject)

GduJobManager *gdu_job_manager_new (void);

GListModel *gdu_job_manager_get_jobs (GduJobManager *self);
guint gdu_job_manager_get_n_jobs (GduJobManager *self);

/* Takes ownership of @job, regardless of whether enqueueing succeeds. */
gboolean gdu_job_manager_enqueue (GduJobManager *self, GduLocalJob *job);
void gdu_job_manager_cancel_job (GduJobManager *self, GduLocalJob *job);
void gdu_job_manager_cancel_all (GduJobManager *self);

G_END_DECLS
