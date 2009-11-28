/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*- */
/* gdu-controller.h
 *
 * Copyright (C) 2009 David Zeuthen
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

#ifndef __GDU_CONTROLLER_H
#define __GDU_CONTROLLER_H

#include <unistd.h>
#include <sys/types.h>

#include <gdu/gdu-types.h>
#include <gdu/gdu-callbacks.h>

G_BEGIN_DECLS

#define GDU_TYPE_CONTROLLER           (gdu_controller_get_type ())
#define GDU_CONTROLLER(o)             (G_TYPE_CHECK_INSTANCE_CAST ((o), GDU_TYPE_CONTROLLER, GduController))
#define GDU_CONTROLLER_CLASS(k)       (G_TYPE_CHECK_CLASS_CAST ((k), GDU_CONTROLLER,  GduControllerClass))
#define GDU_IS_CONTROLLER(o)          (G_TYPE_CHECK_INSTANCE_TYPE ((o), GDU_TYPE_CONTROLLER))
#define GDU_IS_CONTROLLER_CLASS(k)    (G_TYPE_CHECK_CLASS_TYPE ((k), GDU_TYPE_CONTROLLER))
#define GDU_CONTROLLER_GET_CLASS(k)   (G_TYPE_INSTANCE_GET_CLASS ((k), GDU_TYPE_CONTROLLER, GduControllerClass))

typedef struct _GduControllerClass    GduControllerClass;
typedef struct _GduControllerPrivate  GduControllerPrivate;

struct _GduController
{
        GObject parent;

        /* private */
        GduControllerPrivate *priv;
};

struct _GduControllerClass
{
        GObjectClass parent_class;

        /* signals */
        void (*changed)     (GduController *controller);
        void (*removed)     (GduController *controller);
};

GType        gdu_controller_get_type              (void);
const char  *gdu_controller_get_object_path       (GduController   *controller);
GduPool     *gdu_controller_get_pool              (GduController   *controller);

const gchar *gdu_controller_get_native_path       (GduController   *controller);
const gchar *gdu_controller_get_vendor            (GduController   *controller);
const gchar *gdu_controller_get_model             (GduController   *controller);
const gchar *gdu_controller_get_driver            (GduController   *controller);

G_END_DECLS

#endif /* __GDU_CONTROLLER_H */
