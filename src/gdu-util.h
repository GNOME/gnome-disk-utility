/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*- */
/* gdu-util.h
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

#ifndef GDU_UTIL_H
#define GDU_UTIL_H

char *gdu_util_get_size_for_display (guint64 size, gboolean long_string);
char *gdu_util_get_fstype_for_display (const char *fstype, const char *fsversion, gboolean long_string);

char *gdu_util_get_desc_for_part_type (const char *part_scheme, const char *part_type);

char *gdu_get_job_description (const char *job_id);
char *gdu_get_task_description (const char *task_id);


typedef struct
{
        char *id;
        int   max_label_len;
        char *desc;
} GduCreatableFilesystem;

GList                  *gdu_util_get_creatable_filesystems (void);
GduCreatableFilesystem *gdu_util_find_creatable_filesystem_for_fstype (const char *fstype);

gboolean                gdu_util_can_create_encrypted_device (void);

/* ---------------------------------------------------------------------------------------------------- */

GtkWidget *gdu_util_fstype_combo_box_create         (const char *include_extended_partitions_for_scheme);
void       gdu_util_fstype_combo_box_rebuild        (GtkWidget  *combo_box,
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

char      *gdu_util_get_default_part_type_for_scheme_and_fstype (const char *scheme, const char *fstype, guint64 size);

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

GduPresentable *gdu_util_find_toplevel_presentable (GduPresentable *presentable);

/* ---------------------------------------------------------------------------------------------------- */

char *gdu_util_dialog_ask_for_secret (GtkWidget      *parent_window,
                                      GduPresentable *presentable,
                                      gboolean        bypass_keyring);

gboolean gdu_util_dialog_change_secret (GtkWidget       *parent_window,
                                        GduPresentable  *presentable,
                                        char           **old_secret,
                                        char           **new_secret,
                                        gboolean        *save_in_keyring,
                                        gboolean        *save_in_keyring_session,
                                        gboolean         bypass_keyring);

char *gdu_util_dialog_ask_for_new_secret (GtkWidget      *parent_window);

gboolean gdu_util_save_secret (GduPresentable *presentable,
                               const char     *secret,
                               gboolean        save_in_keyring_session);

gboolean gdu_util_delete_secret (GduPresentable *presentable);

gboolean gdu_util_have_secret (GduPresentable *presentable);


#endif /* GDU_UTIL_H */
