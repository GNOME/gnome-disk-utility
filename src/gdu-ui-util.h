/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*- */
/* gdu-ui-util.h
 *
 * Copyright (C) 2007 David Zeuthen
 *
 * This program is free software; you can redistribute it and/or 
 * modify it under the terms of the GNU General Public License as 
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
 * USA
 */

#include <gtk/gtk.h>
#include "gdu-presentable.h"
#include "gdu-pool.h"
#include "gdu-util.h"

#ifndef GDU_UI_UTIL_H
#define GDU_UI_UTIL_H

gboolean gdu_util_dialog_show_filesystem_busy (GtkWidget *parent_window, GduPresentable *presentable);


char *gdu_util_dialog_ask_for_new_secret (GtkWidget      *parent_window,
                                          gboolean       *save_in_keyring,
                                          gboolean       *save_in_keyring_session);

char *gdu_util_dialog_ask_for_secret (GtkWidget       *parent_window,
                                      GduPresentable  *presentable,
                                      gboolean         bypass_keyring,
                                      gboolean         indicate_wrong_passphrase,
                                      gboolean        *asked_user);

gboolean gdu_util_dialog_change_secret (GtkWidget       *parent_window,
                                        GduPresentable  *presentable,
                                        char           **old_secret,
                                        char           **new_secret,
                                        gboolean        *save_in_keyring,
                                        gboolean        *save_in_keyring_session,
                                        gboolean         bypass_keyring,
                                        gboolean         indicate_wrong_passphrase);

char *gdu_util_delete_confirmation_dialog (GtkWidget *parent_window,
                                           const char *title,
                                           const char *primary_text,
                                           const char *secondary_text,
                                           const char *affirmative_action_button_mnemonic);

/* ---------------------------------------------------------------------------------------------------- */

GtkWidget *gdu_util_fstype_combo_box_create         (GduPool *pool,
                                                     const char *include_extended_partitions_for_scheme);
void       gdu_util_fstype_combo_box_rebuild        (GtkWidget  *combo_box,
                                                     GduPool *pool,
                                                     const char *include_extended_partitions_for_scheme);
void       gdu_util_fstype_combo_box_set_desc_label (GtkWidget *combo_box, GtkWidget *desc_label);
gboolean   gdu_util_fstype_combo_box_select         (GtkWidget  *combo_box,
                                                     const char *fstype);
char      *gdu_util_fstype_combo_box_get_selected   (GtkWidget  *combo_box);

/* ---------------------------------------------------------------------------------------------------- */

GtkWidget *gdu_util_secure_erase_combo_box_create         (void);
void       gdu_util_secure_erase_combo_box_set_desc_label (GtkWidget *combo_box, GtkWidget *desc_label);
char      *gdu_util_secure_erase_combo_box_get_selected   (GtkWidget *combo_box);

/* ---------------------------------------------------------------------------------------------------- */

GtkWidget *gdu_util_part_type_combo_box_create       (const char *part_scheme);
void       gdu_util_part_type_combo_box_rebuild      (GtkWidget  *combo_box,
                                                      const char *part_scheme);
gboolean   gdu_util_part_type_combo_box_select       (GtkWidget  *combo_box,
                                                      const char *part_type);
char      *gdu_util_part_type_combo_box_get_selected (GtkWidget  *combo_box);

/* ---------------------------------------------------------------------------------------------------- */

GtkWidget *gdu_util_part_table_type_combo_box_create         (void);
void       gdu_util_part_table_type_combo_box_set_desc_label (GtkWidget *combo_box, GtkWidget *desc_label);
gboolean   gdu_util_part_table_type_combo_box_select         (GtkWidget  *combo_box,
                                                              const char *part_table_type);
char      *gdu_util_part_table_type_combo_box_get_selected   (GtkWidget  *combo_box);

/* ---------------------------------------------------------------------------------------------------- */

GdkPixbuf *gdu_util_get_pixbuf_for_presentable (GduPresentable *presentable, GtkIconSize size);


#endif /* GDU_UI_UTIL_H */
