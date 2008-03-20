/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*- */
/* gdu-disk-widget.h
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

#ifndef GDU_DISK_WIDGET_H
#define GDU_DISK_WIDGET_H

#include <gtk/gtk.h>
#include "gdu-presentable.h"

#define GDU_TYPE_DISK_WIDGET             (gdu_disk_widget_get_type ())
#define GDU_DISK_WIDGET(obj)             (G_TYPE_CHECK_INSTANCE_CAST ((obj), GDU_TYPE_DISK_WIDGET, GduDiskWidget))
#define GDU_DISK_WIDGET_CLASS(obj)       (G_TYPE_CHECK_CLASS_CAST ((obj), GDU_DISK_WIDGET,  GduDiskWidgetClass))
#define GDU_IS_DISK_WIDGET(obj)          (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GDU_TYPE_DISK_WIDGET))
#define GDU_IS_DISK_WIDGET_CLASS(obj)    (G_TYPE_CHECK_CLASS_TYPE ((obj), GDU_TYPE_DISK_WIDGET))
#define GDU_DISK_WIDGET_GET_CLASS        (G_TYPE_INSTANCE_GET_CLASS ((obj), GDU_TYPE_DISK_WIDGET, GduDiskWidgetClass))

typedef struct _GduDiskWidgetClass       GduDiskWidgetClass;
typedef struct _GduDiskWidget            GduDiskWidget;

struct _GduDiskWidgetPrivate;
typedef struct _GduDiskWidgetPrivate     GduDiskWidgetPrivate;

struct _GduDiskWidget
{
        GtkDrawingArea parent;

        /* private */
        GduDiskWidgetPrivate *priv;
};

struct _GduDiskWidgetClass
{
        GtkDrawingAreaClass parent_class;
};

GType            gdu_disk_widget_get_type         (void);
GtkWidget       *gdu_disk_widget_new              (GduPresentable *presentable);
void             gdu_disk_widget_set_presentable  (GduDiskWidget  *disk_widget,
                                                   GduPresentable *presentable);
GduPresentable  *gdu_disk_widget_get_presentable  (GduDiskWidget  *disk_widget);

#endif /* GDU_DISK_WIDGET_H */
