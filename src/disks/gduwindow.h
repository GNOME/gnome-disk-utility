/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*-
 *
 * Copyright (C) 2008-2013 Red Hat, Inc.
 *
 * Licensed under GPL version 2 or later.
 *
 * Author: David Zeuthen <zeuthen@gmail.com>
 */

#ifndef __GDU_WINDOW_H__
#define __GDU_WINDOW_H__

#include <gtk/gtk.h>
#include "gdutypes.h"

G_BEGIN_DECLS

#define GDU_TYPE_WINDOW         (gdu_window_get_type ())
#define GDU_WINDOW(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), GDU_TYPE_WINDOW, GduWindow))
#define GDU_IS_WINDOW(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), GDU_TYPE_WINDOW))

GType           gdu_window_get_type        (void) G_GNUC_CONST;
GduWindow      *gdu_window_new             (GduApplication *application,
                                            UDisksClient   *client);
GduApplication *gdu_window_get_application (GduWindow      *window);
UDisksClient   *gdu_window_get_client      (GduWindow      *window);

gboolean        gdu_window_select_object     (GduWindow    *window,
                                              UDisksObject *object);


void            gdu_window_show_attach_disk_image (GduWindow *window);

void            gdu_window_ensure_unused (GduWindow            *window,
                                          UDisksObject         *object,
                                          GAsyncReadyCallback   callback,
                                          GCancellable         *cancellable,
                                          gpointer              user_data);

gboolean        gdu_window_ensure_unused_finish (GduWindow     *window,
                                                 GAsyncResult  *res,
                                                 GError       **error);

void            gdu_window_ensure_unused_list (GduWindow            *window,
                                               GList                *objects,
                                               GAsyncReadyCallback   callback,
                                               GCancellable         *cancellable,
                                               gpointer              user_data);

gboolean        gdu_window_ensure_unused_list_finish (GduWindow     *window,
                                                      GAsyncResult  *res,
                                                      GError       **error);

gboolean        gdu_window_attach_disk_image_helper (GduWindow *window,
                                                     gchar     *filename,
                                                     gboolean   readonly);

G_END_DECLS

#endif /* __GDU_WINDOW_H__ */
