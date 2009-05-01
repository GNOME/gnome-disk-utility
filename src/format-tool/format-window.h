/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*- */
/*
 *  format-window.h
 *
 *  Copyright (C) 2008-2009 Red Hat, Inc.
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public
 *  License as published by the Free Software Foundation; either
 *  version 2 of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Library General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public
 *  License along with this library; if not, write to the Free
 *  Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 *  Author: Tomas Bzatek <tbzatek@redhat.com>
 *
 */

#ifndef FORMAT_WINDOW_H
#define FORMAT_WINDOW_H

#include <gtk/gtk.h>
#include <gdu/gdu.h>
#include <polkit-gnome/polkit-gnome.h>


G_BEGIN_DECLS

typedef struct
{
        GtkDialog *dialog;
        GtkWidget *close_button;
        GtkWidget *icon_image;
        GtkWidget *name_label;
        GtkWidget *details_label;
        GtkWidget *mount_warning;
        GtkWidget *readonly_warning;
        GtkWidget *no_media_warning;
        GtkWidget *label_entry;
        GtkWidget *part_type_combo_box;
        GtkWidget *progress_bar;
        GtkWidget *progress_bar_box;
        GtkWidget *controls_box;
        GtkWidget *all_controls_box;

        GduPresentable *presentable;
        GduPool *pool;

        gboolean job_running;
        gboolean job_cancelled;
} FormatDialogPrivate;


typedef struct
{
        const char *fstype;
        gboolean encrypted;
        char *title;
} FormatComboBoxItem;


static FormatComboBoxItem filesystem_combo_items[] =
        {
                {"vfat", FALSE, "Compatible with all systems (FAT)"},
                {"ext2", FALSE, "Compatible with Linux systems, no journal (ext2)"},
                {"ext3", FALSE, "Compatible with Linux systems (ext3)"},
                {"ext3", TRUE, "Encrypted, compatible with Linux systems (LUKS)"}
        };


/*  pass presentable=NULL and standalone_mode=TRUE to display volume selector */
/*  we do ref presentable ourselves */
void  nautilus_gdu_spawn_dialog  (GduPresentable      *presentable);

/*  update sensitivity of main controls, action buttons etc.  */
void  update_ui_controls         (FormatDialogPrivate *priv);

/*  we do ref presentable ourselves  */
void  select_new_presentable     (FormatDialogPrivate *priv,
                                  GduPresentable      *presentable);

G_END_DECLS

#endif  /* FORMAT_WINDOW_H */

