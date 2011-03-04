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

#ifndef __GDU_APPLICATION_H__
#define __GDU_APPLICATION_H__

#include <gtk/gtk.h>
#include "gdutypes.h"

G_BEGIN_DECLS

#define GDU_TYPE_APPLICATION         (gdu_application_get_type ())
#define GDU_APPLICATION(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), GDU_TYPE_APPLICATION, GduApplication))
#define GDU_IS_APPLICATION(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), GDU_TYPE_APPLICATION))

GType         gdu_application_get_type   (void) G_GNUC_CONST;
GApplication *gdu_application_new        (void);
GtkWidget    *gdu_application_get_widget (GduApplication *app,
                                            const gchar    *name);


G_END_DECLS

#endif /* __GDU_APPLICATION_H__ */
