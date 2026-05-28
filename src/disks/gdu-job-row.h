/* gdu-job-row.h
 *
 * Copyright 2026 The GNOME Project
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include <adwaita.h>

#include "gdu-job-manager.h"

G_BEGIN_DECLS

#define GDU_TYPE_JOB_ROW (gdu_job_row_get_type ())
G_DECLARE_FINAL_TYPE (GduJobRow, gdu_job_row, GDU, JOB_ROW, AdwPreferencesRow)

GduJobRow *gdu_job_row_new (GduLocalJob *job, GduJobManager *job_manager);
GduLocalJob *gdu_job_row_get_job (GduJobRow *self);

G_END_DECLS
