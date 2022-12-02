/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*-
 *
 * Copyright (C) 2008-2013 Red Hat, Inc.
 *
 * Licensed under GPL version 2 or later.
 *
 * Author: David Zeuthen <zeuthen@gmail.com>
 */

#ifndef __GDU_PASSWORD_STRENGTH_WIDGET_H__
#define __GDU_PASSWORD_STRENGTH_WIDGET_H__

#include <gtk/gtk.h>
#include "gdutypes.h"

G_BEGIN_DECLS

#define GDU_TYPE_PASSWORD_STRENGTH_WIDGET (gdu_password_strength_widget_get_type ())
G_DECLARE_FINAL_TYPE (GduPasswordStrengthWidget, gdu_password_strength_widget, GDU, PASSWORD_STRENGTH_WIDGET, GtkBox)

GtkWidget*   gdu_password_strength_widget_new            (void);
void         gdu_password_strength_widget_set_password   (GduPasswordStrengthWidget *widget,
                                                          const gchar               *password);

G_END_DECLS

#endif /* __GDU_PASSWORD_STRENGTH_WIDGET_H__ */
