/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*- */
/* gdu-ata-smart-historical-data.h
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

#ifndef __GDU_ATA_SMART_HISTORICAL_DATA_H
#define __GDU_ATA_SMART_HISTORICAL_DATA_H

#include <gdu/gdu-types.h>

G_BEGIN_DECLS

#define GDU_TYPE_ATA_SMART_HISTORICAL_DATA           (gdu_ata_smart_historical_data_get_type ())
#define GDU_ATA_SMART_HISTORICAL_DATA(o)             (G_TYPE_CHECK_INSTANCE_CAST ((o), GDU_TYPE_ATA_SMART_HISTORICAL_DATA, GduAtaSmartHistoricalData))
#define GDU_ATA_SMART_HISTORICAL_DATA_CLASS(k)       (G_TYPE_CHECK_CLASS_CAST ((k), GDU_ATA_SMART_HISTORICAL_DATA,  GduAtaSmartHistoricalDataClass))
#define GDU_IS_ATA_SMART_HISTORICAL_DATA(o)          (G_TYPE_CHECK_INSTANCE_TYPE ((o), GDU_TYPE_ATA_SMART_HISTORICAL_DATA))
#define GDU_IS_ATA_SMART_HISTORICAL_DATA_CLASS(k)    (G_TYPE_CHECK_CLASS_TYPE ((k), GDU_TYPE_ATA_SMART_HISTORICAL_DATA))
#define GDU_ATA_SMART_HISTORICAL_DATA_GET_CLASS(o)   (G_TYPE_INSTANCE_GET_CLASS ((o), GDU_TYPE_ATA_SMART_HISTORICAL_DATA, GduAtaSmartHistoricalDataClass))

typedef struct _GduAtaSmartHistoricalDataClass       GduAtaSmartHistoricalDataClass;
typedef struct _GduAtaSmartHistoricalDataPrivate     GduAtaSmartHistoricalDataPrivate;

struct _GduAtaSmartHistoricalData
{
        GObject parent;

        /* private */
        GduAtaSmartHistoricalDataPrivate *priv;
};

struct _GduAtaSmartHistoricalDataClass
{
        GObjectClass parent_class;

};

GType                     gdu_ata_smart_historical_data_get_type                (void);
guint64                   gdu_ata_smart_historical_data_get_time_collected      (GduAtaSmartHistoricalData *data);
gboolean                  gdu_ata_smart_historical_data_get_is_failing          (GduAtaSmartHistoricalData *data);
gboolean                  gdu_ata_smart_historical_data_get_is_failing_valid    (GduAtaSmartHistoricalData *data);
gboolean                  gdu_ata_smart_historical_data_get_has_bad_sectors     (GduAtaSmartHistoricalData *data);
gboolean                  gdu_ata_smart_historical_data_get_has_bad_attributes  (GduAtaSmartHistoricalData *data);
gdouble                   gdu_ata_smart_historical_data_get_temperature_kelvin  (GduAtaSmartHistoricalData *data);
guint64                   gdu_ata_smart_historical_data_get_power_on_seconds    (GduAtaSmartHistoricalData *data);
GList                    *gdu_ata_smart_historical_data_get_attributes          (GduAtaSmartHistoricalData *data);
GduAtaSmartAttribute     *gdu_ata_smart_historical_data_get_attribute           (GduAtaSmartHistoricalData *data,
                                                                                 const gchar               *attr_name);

G_END_DECLS

#endif /* __GDU_ATA_SMART_HISTORICAL_DATA_H */
