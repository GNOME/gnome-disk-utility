/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*- */
/* gdu-private.h
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

#ifndef GDU_PRIVATE_H
#define GDU_PRIVATE_H

#include <glib-object.h>
#include "gdu-smart-data.h"

#define SMART_DATA_STRUCT_TYPE (dbus_g_type_get_struct ("GValueArray",   \
                                                        G_TYPE_INT,      \
                                                        G_TYPE_STRING,   \
                                                        G_TYPE_INT,      \
                                                        G_TYPE_INT,      \
                                                        G_TYPE_INT,      \
                                                        G_TYPE_INT,      \
                                                        G_TYPE_STRING,   \
                                                        G_TYPE_INVALID))

#define HISTORICAL_SMART_DATA_STRUCT_TYPE (dbus_g_type_get_struct ("GValueArray",   \
                                                                   G_TYPE_UINT64, \
                                                                   G_TYPE_DOUBLE, \
                                                                   G_TYPE_UINT64, \
                                                                   G_TYPE_STRING, \
                                                                   G_TYPE_BOOLEAN, \
                                                                   dbus_g_type_get_collection ("GPtrArray", SMART_DATA_STRUCT_TYPE), \
                                                                   G_TYPE_INVALID))

GduSmartDataAttribute *_gdu_smart_data_attribute_new   (gpointer data);
GduSmartData          *_gdu_smart_data_new_from_values (guint64     time_collected,
                                                        double      temperature,
                                                        guint64     time_powered_on,
                                                        const char *last_self_test_result,
                                                        gboolean    is_failing,
                                                        GPtrArray  *attrs);
GduSmartData          * _gdu_smart_data_new            (gpointer data);

void _gdu_device_fixup_error (GError *error);


#endif /* GDU_PRIVATE_H */
