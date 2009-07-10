/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*- */
/* gdu-gtk-enums.h
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

#if !defined (__GDU_GTK_INSIDE_GDU_GTK_H) && !defined (GDU_GTK_COMPILATION)
#error "Only <gdu-gtk/gdu-gtk.h> can be included directly, this file may disappear or change contents."
#endif

#ifndef GDU_GTK_ENUMS_H
#define GDU_GTK_ENUMS_H

#include <glib-object.h>

typedef enum {
        GDU_CURVE_FLAGS_NONE                = 0,
        GDU_CURVE_FLAGS_FILLED              = (1 << 0),
        GDU_CURVE_FLAGS_FADE_EDGES          = (1 << 1),
        GDU_CURVE_FLAGS_AXIS_MARKERS_LEFT   = (1 << 2),
        GDU_CURVE_FLAGS_AXIS_MARKERS_RIGHT  = (1 << 3),
        GDU_CURVE_FLAGS_NORMALIZE           = (1 << 4),
} GduCurveFlags;

typedef enum {
        GDU_CURVE_UNIT_NUMBER      = 0,
        GDU_CURVE_UNIT_TIME        = 1,
        GDU_CURVE_UNIT_TEMPERATURE = 2
} GduCurveUnit;

#endif /* GDU_GTK_ENUMS_H */
