/*
 * Copyright (C) 2008-2013 Red Hat, Inc.
 *
 * Licensed under GPL version 2 or later.
 *
 * Author: David Zeuthen <zeuthen@gmail.com>
 */

#pragma once

#include <gtk/gtk.h>
#include "gdutypes.h"

G_BEGIN_DECLS

#define GDU_TYPE_LOCAL_JOB  (gdu_local_job_get_type())
#define GDU_LOCAL_JOB(o)    (G_TYPE_CHECK_INSTANCE_CAST ((o), GDU_TYPE_LOCAL_JOB, GduLocalJob))
#define GDU_IS_LOCAL_JOB(o) (G_TYPE_CHECK_INSTANCE_TYPE ((o), GDU_TYPE_LOCAL_JOB))

GType         gdu_local_job_get_type            (void) G_GNUC_CONST;
GduLocalJob  *gdu_local_job_new                 (UDisksObject *object);
UDisksObject *gdu_local_job_get_object          (GduLocalJob  *job);
void          gdu_local_job_set_description     (GduLocalJob  *job,
                                                 const gchar  *description);
const gchar  *gdu_local_job_get_description     (GduLocalJob  *job);
void          gdu_local_job_set_extra_markup    (GduLocalJob  *job,
                                                 const gchar  *markup);
const gchar  *gdu_local_job_get_extra_markup    (GduLocalJob  *job);
void          gdu_local_job_canceled            (GduLocalJob  *job);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (GduLocalJob, g_object_unref)
G_END_DECLS
