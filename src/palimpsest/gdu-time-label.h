/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*- */
/* gdu-time-label.h
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


#include <gtk/gtk.h>

#ifndef GDU_TIME_LABEL_H
#define GDU_TIME_LABEL_H

#define GDU_TYPE_TIME_LABEL             (gdu_time_label_get_type ())
#define GDU_TIME_LABEL(obj)             (G_TYPE_CHECK_INSTANCE_CAST ((obj), GDU_TYPE_TIME_LABEL, GduTimeLabel))
#define GDU_TIME_LABEL_CLASS(obj)       (G_TYPE_CHECK_CLASS_CAST ((obj), GDU_TIME_LABEL,  GduTimeLabelClass))
#define GDU_IS_TIME_LABEL(obj)          (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GDU_TYPE_TIME_LABEL))
#define GDU_IS_TIME_LABEL_CLASS(obj)    (G_TYPE_CHECK_CLASS_TYPE ((obj), GDU_TYPE_TIME_LABEL))
#define GDU_TIME_LABEL_GET_CLASS        (G_TYPE_INSTANCE_GET_CLASS ((obj), GDU_TYPE_TIME_LABEL, GduTimeLabelClass))

typedef struct _GduTimeLabelClass       GduTimeLabelClass;
typedef struct _GduTimeLabel            GduTimeLabel;

struct _GduTimeLabelPrivate;
typedef struct _GduTimeLabelPrivate     GduTimeLabelPrivate;

struct _GduTimeLabel
{
        GtkLabel parent;

        /* private */
        GduTimeLabelPrivate *priv;
};

struct _GduTimeLabelClass
{
        GtkLabelClass parent_class;
};


GType      gdu_time_label_get_type     (void);
GtkWidget *gdu_time_label_new          (GTimeVal     *time);
void       gdu_time_label_set_time     (GduTimeLabel *time_label,
                                        GTimeVal     *time);

#endif /* GDU_TIME_LABEL_H */
