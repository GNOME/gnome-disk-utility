/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*- */
/* gdu-process.h
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

#if !defined (GNOME_DISK_UTILITY_INSIDE_GDU_H) && !defined (GDU_COMPILATION)
#error "Only <gdu/gdu.h> can be included directly, this file may disappear or change contents."
#endif

#ifndef GDU_PROCESS_H
#define GDU_PROCESS_H

#include <unistd.h>
#include <sys/types.h>
#include <glib-object.h>
#include <gio/gio.h>

#define GDU_TYPE_PROCESS             (gdu_process_get_type ())
#define GDU_PROCESS(obj)             (G_TYPE_CHECK_INSTANCE_CAST ((obj), GDU_TYPE_PROCESS, GduProcess))
#define GDU_PROCESS_CLASS(obj)       (G_TYPE_CHECK_CLASS_CAST ((obj), GDU_PROCESS,  GduProcessClass))
#define GDU_IS_PROCESS(obj)          (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GDU_TYPE_PROCESS))
#define GDU_IS_PROCESS_CLASS(obj)    (G_TYPE_CHECK_CLASS_TYPE ((obj), GDU_TYPE_PROCESS))
#define GDU_PROCESS_GET_CLASS        (G_TYPE_INSTANCE_GET_CLASS ((obj), GDU_TYPE_PROCESS, GduProcessClass))

typedef struct _GduProcessClass       GduProcessClass;
typedef struct _GduProcess            GduProcess;

struct _GduProcess
{
        GObject parent;

        /* private */
        GduProcessPrivate *priv;
};

struct _GduProcessClass
{
        GObjectClass parent_class;

};

GType       gdu_process_get_type         (void);
pid_t       gdu_process_get_id           (GduProcess *process);
uid_t       gdu_process_get_owner        (GduProcess *process);
const char *gdu_process_get_command_line (GduProcess *process);
GAppInfo   *gdu_process_get_app_info     (GduProcess *process);

#endif /* GDU_PROCESS_H */
