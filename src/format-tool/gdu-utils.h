/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*- */
/* gdu-utils.h
 *
 *  Copyright (C) 2008-2009 Red Hat, Inc.
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public
 *  License as published by the Free Software Foundation; either
 *  version 2 of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Library General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public
 *  License along with this library; if not, write to the Free
 *  Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 *  Author: Tomas Bzatek <tbzatek@redhat.com>
 */

#ifndef __GDU_UTILS_H
#define __GDU_UTILS_H

#include <glib-object.h>
#include <gio/gio.h>
#include <gdu/gdu.h>

G_BEGIN_DECLS

/*  caller must unref the returned object  */
GduPresentable *  find_presentable_from_mount_path (const gchar *mount_path);

/*  caller must unref the returned object  */
GduPresentable *  find_presentable_from_device_path (const gchar *device_path);

gchar *           _g_icon_get_string (GIcon *icon);

gboolean          is_active_luks (GduPool        *pool,
                                  GduPresentable *presentable);

const gchar *     trim_dk_error (const gchar *message);

G_END_DECLS

#endif  /* __GDU_UTILS_H */

