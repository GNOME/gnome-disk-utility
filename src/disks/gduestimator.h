/*
 * Copyright (C) 2008-2013 Red Hat, Inc.
 *
 * Licensed under GPL version 2 or later.
 *
 * Author: David Zeuthen <zeuthen@gmail.com>
 */

#ifndef __GDU_ESTIMATOR_H__
#define __GDU_ESTIMATOR_H__

#include <gtk/gtk.h>
#include "gdutypes.h"

G_BEGIN_DECLS

#define GDU_TYPE_ESTIMATOR (gdu_estimator_get_type ())
G_DECLARE_FINAL_TYPE (GduEstimator, gdu_estimator, GDU, ESTIMATOR, GObject)

GduEstimator  *gdu_estimator_new                 (guint64         target_bytes);
void           gdu_estimator_add_sample          (GduEstimator    *estimator,
                                                  guint64          completed_bytes);
guint64        gdu_estimator_get_target_bytes    (GduEstimator    *estimator);
guint64        gdu_estimator_get_completed_bytes (GduEstimator    *estimator);

guint64        gdu_estimator_get_bytes_per_sec   (GduEstimator    *estimator);
guint64        gdu_estimator_get_usec_remaining  (GduEstimator    *estimator);

G_END_DECLS

#endif /* __GDU_ESTIMATOR_H__ */
