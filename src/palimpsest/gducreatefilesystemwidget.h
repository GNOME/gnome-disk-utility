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

#ifndef __GDU_CREATE_FILESYSTEM_WIDGET_H__
#define __GDU_CREATE_FILESYSTEM_WIDGET_H__

#include <gtk/gtk.h>
#include "gdutypes.h"

G_BEGIN_DECLS

#define GDU_TYPE_CREATE_FILESYSTEM_WIDGET         gdu_create_filesystem_widget_get_type()
#define GDU_CREATE_FILESYSTEM_WIDGET(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), GDU_TYPE_CREATE_FILESYSTEM_WIDGET, GduCreateFilesystemWidget))
#define GDU_IS_CREATE_FILESYSTEM_WIDGET(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), GDU_TYPE_CREATE_FILESYSTEM_WIDGET))

GType        gdu_create_filesystem_widget_get_type       (void) G_GNUC_CONST;
GtkWidget*   gdu_create_filesystem_widget_new            (GduApplication            *application,
                                                          UDisksDrive               *drive);
const gchar *gdu_create_filesystem_widget_get_name       (GduCreateFilesystemWidget *widget);
const gchar *gdu_create_filesystem_widget_get_fstype     (GduCreateFilesystemWidget *widget);
const gchar *gdu_create_filesystem_widget_get_passphrase (GduCreateFilesystemWidget *widget);
gboolean     gdu_create_filesystem_widget_get_has_info   (GduCreateFilesystemWidget *widget);

GtkWidget   *gdu_create_filesystem_widget_get_name_entry (GduCreateFilesystemWidget *widget);

G_END_DECLS

#endif /* __GDU_CREATE_FILESYSTEM_WIDGET_H__ */
