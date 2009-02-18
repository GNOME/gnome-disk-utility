/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*- */
/* gdu-smart-data.h
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


#if !defined (__GDU_INSIDE_GDU_H) && !defined (GDU_COMPILATION)
#error "Only <gdu/gdu.h> can be included directly, this file may disappear or change contents."
#endif

#ifndef __GDU_SMART_DATA_H
#define __GDU_SMART_DATA_H

#include <gdu/gdu-types.h>

G_BEGIN_DECLS

#define GDU_TYPE_SMART_DATA           (gdu_smart_data_get_type ())
#define GDU_SMART_DATA(o)             (G_TYPE_CHECK_INSTANCE_CAST ((o), GDU_TYPE_SMART_DATA, GduSmartData))
#define GDU_SMART_DATA_CLASS(k)       (G_TYPE_CHECK_CLASS_CAST ((k), GDU_SMART_DATA,  GduSmartDataClass))
#define GDU_IS_SMART_DATA(o)          (G_TYPE_CHECK_INSTANCE_TYPE ((o), GDU_TYPE_SMART_DATA))
#define GDU_IS_SMART_DATA_CLASS(k)    (G_TYPE_CHECK_CLASS_TYPE ((k), GDU_TYPE_SMART_DATA))
#define GDU_SMART_DATA_GET_CLASS(k)   (G_TYPE_INSTANCE_GET_CLASS ((k), GDU_TYPE_SMART_DATA, GduSmartDataClass))

typedef struct _GduSmartDataClass       GduSmartDataClass;
typedef struct _GduSmartDataPrivate     GduSmartDataPrivate;

struct _GduSmartData
{
        GObject parent;

        /* private */
        GduSmartDataPrivate *priv;
};

struct _GduSmartDataClass
{
        GObjectClass parent_class;

};

GType                  gdu_smart_data_get_type                  (void);
guint64                gdu_smart_data_get_time_collected        (GduSmartData *smart_data);
double                 gdu_smart_data_get_temperature           (GduSmartData *smart_data);
guint64                gdu_smart_data_get_time_powered_on       (GduSmartData *smart_data);
char                  *gdu_smart_data_get_last_self_test_result (GduSmartData *smart_data);
gboolean               gdu_smart_data_get_is_failing            (GduSmartData *smart_data);
GList                 *gdu_smart_data_get_attributes            (GduSmartData *smart_data);
gboolean               gdu_smart_data_get_attribute_warning     (GduSmartData *smart_data);
gboolean               gdu_smart_data_get_attribute_failing     (GduSmartData *smart_data);
GduSmartDataAttribute *gdu_smart_data_get_attribute             (GduSmartData *smart_data,
                                                                 int id);

G_END_DECLS

#endif /* __GDU_SMART_DATA_H */
