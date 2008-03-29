/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*- */
/* gdu-page-drive.h
 *
 * Copyright (C) 2008 David Zeuthen
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

#ifndef GDU_PAGE_DRIVE_H
#define GDU_PAGE_DRIVE_H

#include <glib-object.h>
#include <gtk/gtk.h>
#include "gdu-shell.h"

#define GDU_TYPE_PAGE_DRIVE             (gdu_page_drive_get_type ())
#define GDU_PAGE_DRIVE(obj)             (G_TYPE_CHECK_INSTANCE_CAST ((obj), GDU_TYPE_PAGE_DRIVE, GduPageDrive))
#define GDU_PAGE_DRIVE_CLASS(obj)       (G_TYPE_CHECK_CLASS_CAST ((obj), GDU_PAGE_DRIVE,  GduPageDriveClass))
#define GDU_IS_PAGE_DRIVE(obj)          (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GDU_TYPE_PAGE_DRIVE))
#define GDU_IS_PAGE_DRIVE_CLASS(obj)    (G_TYPE_CHECK_CLASS_TYPE ((obj), GDU_TYPE_PAGE_DRIVE))
#define GDU_PAGE_DRIVE_GET_CLASS        (G_TYPE_INSTANCE_GET_CLASS ((obj), GDU_TYPE_PAGE_DRIVE, GduPageDriveClass))

typedef struct _GduPageDriveClass       GduPageDriveClass;
typedef struct _GduPageDrive            GduPageDrive;

struct _GduPageDrivePrivate;
typedef struct _GduPageDrivePrivate     GduPageDrivePrivate;

struct _GduPageDrive
{
        GObject parent;

        /* private */
        GduPageDrivePrivate *priv;
};

struct _GduPageDriveClass
{
        GObjectClass parent_class;
};

GType                            gdu_page_drive_get_type (void) G_GNUC_CONST;
GduPageDrive *gdu_page_drive_new (GduShell *shell);

#endif /* GDU_PAGE_DRIVE_H */
