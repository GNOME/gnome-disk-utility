/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*-
 *
 * Copyright (C) 2008-2012 Red Hat, Inc.
 *
 * Licensed under GPL version 2 or later.
 *
 * Author: David Zeuthen <zeuthen@gmail.com>
 */

#ifndef __GDU_UTILS_H__
#define __GDU_UTILS_H__

#include "libgdutypes.h"

G_BEGIN_DECLS

gboolean gdu_utils_has_configuration (UDisksBlock  *block,
                                      const gchar  *type,
                                      gboolean     *out_has_passphrase);

void gdu_utils_configure_file_chooser_for_disk_images (GtkFileChooser *file_chooser,
                                                       gboolean        set_file_types);

void gdu_utils_file_chooser_for_disk_images_update_settings (GtkFileChooser *file_chooser);

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

gchar *gdu_utils_format_duration_usec (guint64                usec,
                                       GduFormatDurationFlags flags);

void            gdu_utils_show_error      (GtkWindow      *parent_window,
                                           const gchar    *message,
                                           GError         *error);

gboolean        gdu_utils_show_confirmation (GtkWindow   *parent_window,
                                             const gchar *message,
                                             const gchar *secondary_message,
                                             const gchar *affirmative_verb);

gboolean gdu_utils_is_ntfs_available (void);

G_END_DECLS

#endif /* __GDU_UTILS_H__ */
