/*
 * Copyright (C) 2008-2013 Red Hat, Inc.
 *
 * Licensed under GPL version 2 or later.
 *
 * Author: David Zeuthen <zeuthen@gmail.com>
 */

#ifndef __GDU_APPLICATION_H__
#define __GDU_APPLICATION_H__

#include <adwaita.h>
#include <gtk/gtk.h>
#include "gdutypes.h"

G_BEGIN_DECLS

#define GDU_TYPE_APPLICATION (gdu_application_get_type ())

G_DECLARE_FINAL_TYPE (GduApplication, gdu_application, GDU, APPLICATION, AdwApplication)

GtkApplication  *gdu_application_new                        (void);
UDisksClient    *gdu_application_get_client                 (GduApplication  *application);
GObject         *gdu_application_new_widget                 (GduApplication  *application,
                                                             const gchar     *ui_file,
                                                             const gchar     *name,
                                                             GtkBuilder     **out_builder);
gboolean         gdu_application_should_exit                (GduApplication *application);
GduLocalJob     *gdu_application_create_local_job           (GduApplication *application,
                                                             UDisksObject   *object);
void             gdu_application_destroy_local_job          (GduApplication *application,
                                                             GduLocalJob    *job);


G_END_DECLS

#endif /* __GDU_APPLICATION_H__ */
