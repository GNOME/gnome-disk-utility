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

#ifndef __LIB_GDU_ENUMS_H__
#define __LIB_GDU_ENUMS_H__

#include <gtk/gtk.h>

G_BEGIN_DECLS

typedef enum
{
  GDU_FORMAT_DURATION_FLAGS_NONE                 = 0,
  GDU_FORMAT_DURATION_FLAGS_SUBSECOND_PRECISION  = (1<<0),
  GDU_FORMAT_DURATION_FLAGS_NO_SECONDS           = (1<<1)
} GduFormatDurationFlags;

G_END_DECLS

#endif /* __LIB_GDU_ENUMS_H__ */
