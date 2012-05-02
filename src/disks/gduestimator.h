/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*-
 *
 * Copyright (C) 2008-2011 Red Hat, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General
 * Public License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place, Suite 330,
 * Boston, MA 02111-1307, USA.
 *
 * Author: David Zeuthen <davidz@redhat.com>
 */

#ifndef __GDU_ESTIMATOR_H__
#define __GDU_ESTIMATOR_H__

#include <gtk/gtk.h>
#include "gdutypes.h"

G_BEGIN_DECLS

#define GDU_TYPE_ESTIMATOR   gdu_estimator_get_type()
#define GDU_ESTIMATOR(o)     (G_TYPE_CHECK_INSTANCE_CAST ((o), GDU_TYPE_ESTIMATOR, GduEstimator))
#define GDU_IS_ESTIMATOR(o)  (G_TYPE_CHECK_INSTANCE_TYPE ((o), GDU_TYPE_ESTIMATOR))

GType          gdu_estimator_get_type            (void) G_GNUC_CONST;
GduEstimator  *gdu_estimator_new                 (guint64         target_bytes);
void           gdu_estimator_add_sample          (GduEstimator    *estimator,
                                                  guint64          completed_bytes);
guint64        gdu_estimator_get_target_bytes    (GduEstimator    *estimator);
guint64        gdu_estimator_get_completed_bytes (GduEstimator    *estimator);

guint64        gdu_estimator_get_bytes_per_sec   (GduEstimator    *estimator);
guint64        gdu_estimator_get_usec_remaining  (GduEstimator    *estimator);

G_END_DECLS

#endif /* __GDU_ESTIMATOR_H__ */
