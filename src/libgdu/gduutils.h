/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*-
 *
 * Copyright (C) 2008-2013 Red Hat, Inc.
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

gboolean        gdu_utils_show_confirmation (GtkWindow    *parent_window,
                                             const gchar  *message,
                                             const gchar  *secondary_message,
                                             const gchar  *affirmative_verb,
                                             const gchar  *checkbox_mnemonic,
                                             gboolean     *inout_checkbox_value,
                                             UDisksClient *client,
                                             GList        *objects);

gboolean gdu_utils_is_ntfs_available (void);

gchar *gdu_utils_format_mdraid_level (const gchar *level,
                                      gboolean     long_desc,
                                      gboolean     use_markup);

gboolean gdu_util_is_same_size (GList   *blocks,
                                guint64 *out_min_size);

gchar *gdu_utils_get_pretty_uri (GFile *file);

gboolean gdu_utils_is_in_use (UDisksClient *client,
                              UDisksObject *object);

void gdu_utils_ensure_unused (UDisksClient         *client,
                              GtkWindow            *parent_window,
                              UDisksObject         *object,
                              GAsyncReadyCallback   callback,
                              GCancellable         *cancellable,
                              gpointer              user_data);
gboolean gdu_utils_ensure_unused_finish (UDisksClient  *client,
                                         GAsyncResult  *res,
                                         GError       **error);

void gdu_utils_ensure_unused_list (UDisksClient         *client,
                                   GtkWindow            *parent_window,
                                   GList                *objects,
                                   GAsyncReadyCallback   callback,
                                   GCancellable         *cancellable,
                                   gpointer              user_data);
gboolean gdu_utils_ensure_unused_list_finish (UDisksClient  *client,
                                              GAsyncResult  *res,
                                              GError       **error);

gint64 gdu_utils_get_unused_for_block (UDisksClient *client,
                                       UDisksBlock  *block);



G_END_DECLS

#endif /* __GDU_UTILS_H__ */
