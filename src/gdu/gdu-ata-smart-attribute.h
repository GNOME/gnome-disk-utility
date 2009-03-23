/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*- */
/* gdu-ata-smart-attribute.h
 *
 * Copyright (C) 2009 David Zeuthen
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

#ifndef __GDU_ATA_SMART_ATTRIBUTE_H
#define __GDU_ATA_SMART_ATTRIBUTE_H

#include <gdu/gdu-types.h>

G_BEGIN_DECLS

#define GDU_TYPE_ATA_SMART_ATTRIBUTE         (gdu_ata_smart_attribute_get_type ())
#define GDU_ATA_SMART_ATTRIBUTE(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), GDU_TYPE_ATA_SMART_ATTRIBUTE, GduAtaSmartAttribute))
#define GDU_ATA_SMART_ATTRIBUTE_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST ((k), GDU_ATA_SMART_ATTRIBUTE,  GduAtaSmartAttributeClass))
#define GDU_IS_ATA_SMART_ATTRIBUTE(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), GDU_TYPE_ATA_SMART_ATTRIBUTE))
#define GDU_IS_ATA_SMART_ATTRIBUTE_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), GDU_TYPE_ATA_SMART_ATTRIBUTE))
#define GDU_ATA_SMART_ATTRIBUTE_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), GDU_TYPE_ATA_SMART_ATTRIBUTE, GduAtaSmartAttributeClass))

typedef struct _GduAtaSmartAttributeClass       GduAtaSmartAttributeClass;
typedef struct _GduAtaSmartAttributePrivate     GduAtaSmartAttributePrivate;

struct _GduAtaSmartAttribute
{
        GObject parent;

        /*< private >*/
        GduAtaSmartAttributePrivate *priv;
};

struct _GduAtaSmartAttributeClass
{
        GObjectClass parent_class;

};

typedef enum {
        GDU_ATA_SMART_ATTRIBUTE_UNIT_UNKNOWN,
        GDU_ATA_SMART_ATTRIBUTE_UNIT_NONE,
        GDU_ATA_SMART_ATTRIBUTE_UNIT_MSECONDS,
        GDU_ATA_SMART_ATTRIBUTE_UNIT_SECTORS,
        GDU_ATA_SMART_ATTRIBUTE_UNIT_MKELVIN,
} GduAtaSmartAttributeUnit;

GType                    gdu_ata_smart_attribute_get_type                  (void);
guint                    gdu_ata_smart_attribute_get_id                    (GduAtaSmartAttribute *attribute);
const gchar             *gdu_ata_smart_attribute_get_name                  (GduAtaSmartAttribute *attribute);
gchar                   *gdu_ata_smart_attribute_get_localized_name        (GduAtaSmartAttribute *attribute);
gchar                   *gdu_ata_smart_attribute_get_localized_description (GduAtaSmartAttribute *attribute);
guint                    gdu_ata_smart_attribute_get_flags                 (GduAtaSmartAttribute *attribute);
gboolean                 gdu_ata_smart_attribute_get_online                (GduAtaSmartAttribute *attribute);
gboolean                 gdu_ata_smart_attribute_get_prefailure            (GduAtaSmartAttribute *attribute);
guint                    gdu_ata_smart_attribute_get_current               (GduAtaSmartAttribute *attribute);
gboolean                 gdu_ata_smart_attribute_get_current_valid         (GduAtaSmartAttribute *attribute);
guint                    gdu_ata_smart_attribute_get_worst                 (GduAtaSmartAttribute *attribute);
gboolean                 gdu_ata_smart_attribute_get_worst_valid           (GduAtaSmartAttribute *attribute);
guint                    gdu_ata_smart_attribute_get_threshold             (GduAtaSmartAttribute *attribute);
gboolean                 gdu_ata_smart_attribute_get_threshold_valid       (GduAtaSmartAttribute *attribute);
gboolean                 gdu_ata_smart_attribute_get_good                  (GduAtaSmartAttribute *attribute);
gboolean                 gdu_ata_smart_attribute_get_good_valid            (GduAtaSmartAttribute *attribute);
guint64                  gdu_ata_smart_attribute_get_pretty_value          (GduAtaSmartAttribute *attribute);
GduAtaSmartAttributeUnit gdu_ata_smart_attribute_get_pretty_unit           (GduAtaSmartAttribute *attribute);

G_END_DECLS

#endif /* __GDU_ATA_SMART_ATTRIBUTE_H */
