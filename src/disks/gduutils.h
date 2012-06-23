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

#ifndef __GDU_UTILS_H__
#define __GDU_UTILS_H__

#include <gtk/gtk.h>
#include "gdutypes.h"

G_BEGIN_DECLS

gboolean gdu_utils_has_configuration (UDisksBlock  *block,
                                      const gchar  *type,
                                      gboolean     *out_has_passphrase);

void gdu_utils_configure_file_chooser_for_disk_images (GtkFileChooser *file_chooser);

gchar *gdu_utils_duration_to_string (guint    duration_sec,
                                     gboolean include_second_precision);

GtkWidget *gdu_utils_create_info_bar (GtkMessageType  message_type,
                                      const gchar    *markup,
                                      GtkWidget     **out_label);

gchar *gdu_utils_unfuse_path (const gchar *path);

void gdu_options_update_check_option (GtkWidget       *options_entry,
                                      const gchar     *option,
                                      GtkWidget       *widget,
                                      GtkWidget       *check_button,
                                      gboolean         negate,
                                      gboolean         add_to_front);

void gdu_options_update_entry_option (GtkWidget       *options_entry,
                                      const gchar     *option,
                                      GtkWidget       *widget,
                                      GtkWidget       *entry);

const gchar *gdu_utils_get_seat (void);

gchar *gdu_utils_format_duration_msec (guint64 msec);

G_END_DECLS

#endif /* __GDU_UTILS_H__ */
