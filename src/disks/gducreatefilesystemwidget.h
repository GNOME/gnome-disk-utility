/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*-
 *
 * Copyright (C) 2008-2012 Red Hat, Inc.
 *
 * Licensed under GPL version 2 or later.
 *
 * Author: David Zeuthen <zeuthen@gmail.com>
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
                                                          UDisksDrive               *drive,
                                                          const gchar * const       *addtional_fstypes);
const gchar *gdu_create_filesystem_widget_get_name       (GduCreateFilesystemWidget *widget);
const gchar *gdu_create_filesystem_widget_get_erase      (GduCreateFilesystemWidget *widget);
const gchar *gdu_create_filesystem_widget_get_fstype     (GduCreateFilesystemWidget *widget);
const gchar *gdu_create_filesystem_widget_get_passphrase (GduCreateFilesystemWidget *widget);
gboolean     gdu_create_filesystem_widget_get_has_info   (GduCreateFilesystemWidget *widget);

GtkWidget   *gdu_create_filesystem_widget_get_name_entry (GduCreateFilesystemWidget *widget);

G_END_DECLS

#endif /* __GDU_CREATE_FILESYSTEM_WIDGET_H__ */
