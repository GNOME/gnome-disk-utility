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

#ifndef __GDU_PASSWORD_STRENGTH_WIDGET_H__
#define __GDU_PASSWORD_STRENGTH_WIDGET_H__

#include <gtk/gtk.h>
#include "gdutypes.h"

G_BEGIN_DECLS

#define GDU_TYPE_PASSWORD_STRENGTH_WIDGET         gdu_password_strength_widget_get_type()
#define GDU_PASSWORD_STRENGTH_WIDGET(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), GDU_TYPE_PASSWORD_STRENGTH_WIDGET, GduPasswordStrengthWidget))
#define GDU_IS_PASSWORD_STRENGTH_WIDGET(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), GDU_TYPE_PASSWORD_STRENGTH_WIDGET))

GType        gdu_password_strength_widget_get_type       (void) G_GNUC_CONST;
GtkWidget*   gdu_password_strength_widget_new            (void);
void         gdu_password_strength_widget_set_password   (GduPasswordStrengthWidget *widget,
                                                          const gchar               *password);

G_END_DECLS

#endif /* __GDU_PASSWORD_STRENGTH_WIDGET_H__ */
