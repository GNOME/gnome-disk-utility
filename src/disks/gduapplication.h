/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*-
 *
 * Copyright (C) 2008-2012 Red Hat, Inc.
 *
 * Licensed under GPL version 2 or later.
 *
 * Author: David Zeuthen <zeuthen@gmail.com>
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
UDisksClient *gdu_application_get_client (GduApplication  *application);
GObject      *gdu_application_new_widget (GduApplication  *application,
                                          const gchar     *ui_file,
                                          const gchar     *name,
                                          GtkBuilder     **out_builder);


G_END_DECLS

#endif /* __GDU_APPLICATION_H__ */
