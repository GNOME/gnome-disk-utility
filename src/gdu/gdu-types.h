/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*- */
/* gdu-types.h
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

#ifndef __GDU_TYPES_H
#define __GDU_TYPES_H

#include <glib-object.h>
#include <gio/gio.h>

G_BEGIN_DECLS

/* forward type definitions */

typedef struct _GduPool                   GduPool;
typedef struct _GduDevice                 GduDevice;
typedef struct _GduPresentable            GduPresentable; /* Dummy typedef */
typedef struct _GduDrive                  GduDrive;
typedef struct _GduLinuxMdDrive           GduLinuxMdDrive;
typedef struct _GduVolume                 GduVolume;
typedef struct _GduVolumeHole             GduVolumeHole;

typedef struct _GduKnownFilesystem        GduKnownFilesystem;
typedef struct _GduProcess                GduProcess;
typedef struct _GduAtaSmartHistoricalData GduAtaSmartHistoricalData;
typedef struct _GduAtaSmartAttribute      GduAtaSmartAttribute;

G_END_DECLS

#endif /* __GDU_TYPES_H */
