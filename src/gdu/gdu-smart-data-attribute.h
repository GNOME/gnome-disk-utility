/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*- */
/* gdu-smart-data-attribute.h
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

#ifndef __GDU_SMART_DATA_ATTRIBUTE_H
#define __GDU_SMART_DATA_ATTRIBUTE_H

#include <gdu/gdu-types.h>

G_BEGIN_DECLS

#define GDU_TYPE_SMART_DATA_ATTRIBUTE         (gdu_smart_data_attribute_get_type ())
#define GDU_SMART_DATA_ATTRIBUTE(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), GDU_TYPE_SMART_DATA_ATTRIBUTE, GduSmartDataAttribute))
#define GDU_SMART_DATA_ATTRIBUTE_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST ((k), GDU_SMART_DATA_ATTRIBUTE,  GduSmartDataAttributeClass))
#define GDU_IS_SMART_DATA_ATTRIBUTE(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), GDU_TYPE_SMART_DATA_ATTRIBUTE))
#define GDU_IS_SMART_DATA_ATTRIBUTE_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), GDU_TYPE_SMART_DATA_ATTRIBUTE))
#define GDU_SMART_DATA_ATTRIBUTE_GET_CLASS(k) (G_TYPE_INSTANCE_GET_CLASS ((k), GDU_TYPE_SMART_DATA_ATTRIBUTE, GduSmartDataAttributeClass))

typedef struct _GduSmartDataAttributeClass       GduSmartDataAttributeClass;
typedef struct _GduSmartDataAttributePrivate     GduSmartDataAttributePrivate;

struct _GduSmartDataAttribute
{
        GObject parent;

        /* private */
        GduSmartDataAttributePrivate *priv;
};

struct _GduSmartDataAttributeClass
{
        GObjectClass parent_class;

};

GType    gdu_smart_data_attribute_get_type        (void);
int      gdu_smart_data_attribute_get_id          (GduSmartDataAttribute *smart_data_attribute);
int      gdu_smart_data_attribute_get_flags       (GduSmartDataAttribute *smart_data_attribute);
int      gdu_smart_data_attribute_get_value       (GduSmartDataAttribute *smart_data_attribute);
int      gdu_smart_data_attribute_get_worst       (GduSmartDataAttribute *smart_data_attribute);
int      gdu_smart_data_attribute_get_threshold   (GduSmartDataAttribute *smart_data_attribute);
char    *gdu_smart_data_attribute_get_raw         (GduSmartDataAttribute *smart_data_attribute);
char    *gdu_smart_data_attribute_get_name        (GduSmartDataAttribute *smart_data_attribute);
char    *gdu_smart_data_attribute_get_description (GduSmartDataAttribute *smart_data_attribute);
gboolean gdu_smart_data_attribute_is_warning      (GduSmartDataAttribute *smart_data_attribute);
gboolean gdu_smart_data_attribute_is_failing      (GduSmartDataAttribute *smart_data_attribute);

G_END_DECLS

#endif /* __GDU_SMART_DATA_ATTRIBUTE_H */
