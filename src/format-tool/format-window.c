/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*- */
/*
 *  format-window.c
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

#include "config.h"

#include <glib/gi18n-lib.h>

#include <gdu/gdu.h>
#include <gdu-gtk/gdu-gtk.h>

#include "gdu-utils.h"
#include "format-window.h"
#include "format-window-operation.h"

#define DISK_MANAGEMENT_UTILITY  "palimpsest"



/* ---------------------------------------------------------------------------*/

static void set_new_presentable (FormatDialogPrivate *priv,
                                 GduPresentable      *presentable);

/* -------------------------------------------------------------------------- */


static void
populate_type_combo (FormatDialogPrivate *priv)
{
        guint i;
        gint old_active;

        g_return_if_fail (priv != NULL);

        old_active = gtk_combo_box_get_active (GTK_COMBO_BOX (priv->part_type_combo_box));

        for (i = G_N_ELEMENTS (filesystem_combo_items); i > 0; i--)
                gtk_combo_box_remove_text (GTK_COMBO_BOX (priv->part_type_combo_box), i - 1);

        for (i = 0; i < G_N_ELEMENTS (filesystem_combo_items); i++) {
                if (! filesystem_combo_items[i].encrypted || gdu_pool_supports_luks_devices (priv->pool))
                        gtk_combo_box_append_text (GTK_COMBO_BOX (priv->part_type_combo_box), _(filesystem_combo_items[i].title));
        }

        gtk_combo_box_set_active (GTK_COMBO_BOX (priv->part_type_combo_box), old_active);
        /*  fallback  */
        if (gtk_combo_box_get_active (GTK_COMBO_BOX (priv->part_type_combo_box)) == -1)
                gtk_combo_box_set_active (GTK_COMBO_BOX (priv->part_type_combo_box), 0);
}

static void
type_combo_box_changed (GtkWidget           *combo_box,
                        FormatDialogPrivate *priv)
{
        const gchar *fstype;
        GduKnownFilesystem *kfs;
        gint max_label_len;
        gint part_combo_item_index;

        fstype = NULL;

        max_label_len = 0;

        part_combo_item_index = gtk_combo_box_get_active (GTK_COMBO_BOX (combo_box));
        if (part_combo_item_index < 0 || part_combo_item_index >= (int) G_N_ELEMENTS (filesystem_combo_items))
                return;
        fstype = filesystem_combo_items[part_combo_item_index].fstype;

        kfs = gdu_pool_get_known_filesystem_by_id (priv->pool, fstype);
        if (kfs != NULL) {
                max_label_len = gdu_known_filesystem_get_max_label_len (kfs);
                g_object_unref (kfs);
        }

        gtk_entry_set_max_length (GTK_ENTRY (priv->label_entry), max_label_len);
}

void
update_ui_controls (FormatDialogPrivate *priv)
{
        GduDevice *device = NULL;
        gboolean sensitive;

        g_return_if_fail (priv != NULL);

        if (priv->presentable && GDU_IS_PRESENTABLE (priv->presentable))
                device = gdu_presentable_get_device (priv->presentable);

        /*  mount warning box  */
        if (device && gdu_device_is_mounted (device))
                gtk_widget_show (priv->mount_warning);
        else
                gtk_widget_hide (priv->mount_warning);

        /*  read only info box  */
        if (device && gdu_device_is_read_only (device))
                gtk_widget_show (priv->readonly_warning);
        else
                gtk_widget_hide (priv->readonly_warning);

        /*  no media info box  */
        if (! device || gdu_device_is_media_available (device))
                gtk_widget_hide (priv->no_media_warning);
        else
                gtk_widget_show (priv->no_media_warning);

        /*  controls sensitivity  */
        sensitive = priv->presentable != NULL && GDU_IS_PRESENTABLE (priv->presentable) && (! priv->job_running);
        if (device)
                sensitive = sensitive && ! gdu_device_is_read_only (device) && gdu_device_is_media_available (device);

        /*  volume label max length  */
        type_combo_box_changed (priv->part_type_combo_box, priv);

        gtk_widget_set_sensitive (priv->controls_box, sensitive);
        gtk_dialog_set_response_sensitive (priv->dialog, GTK_RESPONSE_OK, sensitive && gtk_combo_box_get_active (GTK_COMBO_BOX (priv->part_type_combo_box)) >= 0);
        gtk_dialog_set_response_sensitive (priv->dialog, GTK_RESPONSE_ACCEPT, ! priv->job_running);

        if (device != NULL)
                g_object_unref (device);
}


static void
update_ui (FormatDialogPrivate *priv)
{
        gchar *s;
        gchar *title;
        const gchar *name;
        GdkPixbuf *pixbuf;
        GduDevice *device;
        gchar *icon_name;
        GIcon *presentable_icon;


        g_return_if_fail (priv != NULL);
        g_return_if_fail (priv->presentable != NULL);

        device = gdu_presentable_get_device (priv->presentable);
        name = gdu_presentable_get_name (priv->presentable);

        /*  icon stuff  */
        pixbuf = gdu_util_get_pixbuf_for_presentable_at_pixel_size (priv->presentable, 48);
        gtk_image_set_from_pixbuf (GTK_IMAGE (priv->icon_image), pixbuf);
        if (pixbuf)
                g_object_unref (pixbuf);
        presentable_icon = gdu_presentable_get_icon (priv->presentable);
        if (presentable_icon) {
                icon_name = _g_icon_get_string (presentable_icon);
                gtk_window_set_icon_name (GTK_WINDOW (priv->dialog), icon_name);
                g_free (icon_name);
                g_object_unref (presentable_icon);
        }

        /*  window title  */
        s = g_strdup_printf (_("Format Volume '%s'"), name);
        gtk_window_set_title (GTK_WINDOW (priv->dialog), s);
        g_free (s);

        /*  title label  */
        title = g_strconcat ("<big><big><b>", _("Format Volume '%s'?"), "</b></big></big>", NULL);
        s = g_strdup_printf (title, name);
        gtk_label_set_markup (GTK_LABEL (priv->name_label), s);
        g_free (s);
        g_free (title);

        s = g_strdup_printf (_("You are about to format the volume \"%s\" (%s). All existing data will be irrevocably erased. Make sure important data is backed up. This action cannot be undone."),
                             name, gdu_device_get_device_file (device));
        gtk_label_set_markup (GTK_LABEL (priv->details_label), s);
        g_free (s);

        /*  partition type combo  */
        if (! priv->job_running) {
                populate_type_combo (priv);
        }
        /*  Is device mounted?  */
        update_ui_controls (priv);

        if (device != NULL)
                g_object_unref (device);
}


/* -------------------------------------------------------------------------- */

static void
nautilus_gdu_destroy (FormatDialogPrivate  *priv)
{
        g_return_if_fail (priv != NULL);

        /*  disconnect our handlers, since the presentable (resp. the pool) reference
         *  counter doesn't really need to be zero
         */
        set_new_presentable (priv, NULL);
        g_signal_handlers_disconnect_matched (priv->pool, G_SIGNAL_MATCH_DATA, 0, 0, NULL, NULL, priv);

        /*  destroy the dialog and internal struct  */
        gtk_widget_destroy (GTK_WIDGET (priv->dialog));
        g_object_unref (priv->pool);
        g_free (priv);

        gtk_main_quit ();
}

static void
presentable_removed (GduPresentable      *presentable,
                     FormatDialogPrivate *priv)
{
        g_return_if_fail (priv != NULL);
        g_warning ("Presentable removed event.");

        nautilus_gdu_destroy (priv);
}

static void
presentable_changed (GduPresentable      *presentable,
                     FormatDialogPrivate *priv)
{
        g_return_if_fail (priv != NULL);
        g_warning ("Presentable changed event.");

        /*  TODO: shall we preserve label or any other settings?  */
        update_ui (priv);
}

/*  we do ref presentable ourselves  */
void
select_new_presentable (FormatDialogPrivate *priv,
                        GduPresentable      *presentable)
{
        /*  TODO: force refresh when no standalone mode ? */
        /*  if (presentable != priv->presentable)  */
        set_new_presentable (priv, presentable);
}

static void
set_new_presentable (FormatDialogPrivate *priv,
                     GduPresentable      *presentable)
{
        g_return_if_fail (priv != NULL);

        if (priv->presentable) {
                /*  first of all, disconnect handlers from the old presentable  */
                g_signal_handlers_disconnect_matched (priv->presentable, G_SIGNAL_MATCH_DATA, 0, 0, NULL, NULL, priv);
                /*    g_debug ("before unreffing presentable, count = %d [%p]", ((GObject*)priv->presentable)->ref_count, priv->presentable);  */
                g_object_unref (priv->presentable);
                priv->presentable = NULL;
        }

        if (presentable) {
                priv->presentable = g_object_ref (presentable);
                /*    g_debug ("set_new_presentable: after reffing presentable, count = %d [%p]", ((GObject*)priv->presentable)->ref_count, priv->presentable);  */

                /*  catch Presentable events  */
                g_signal_connect (G_OBJECT (priv->presentable), "removed",
                                  G_CALLBACK (presentable_removed), priv);
                g_signal_connect (G_OBJECT (priv->presentable), "changed",
                                  G_CALLBACK (presentable_changed), priv);
        }
}

/* -------------------------------------------------------------------------- */
static void
spawn_palimpsest (FormatDialogPrivate *priv)
{
        gchar *argv[] = { DISK_MANAGEMENT_UTILITY, NULL, NULL };

        GduDevice *device;

        device = gdu_presentable_get_device (priv->presentable);
        if (device) {
                argv[1] = g_strdup_printf ("--%s=%s", GDU_IS_DRIVE (priv->presentable) ? "show-drive" : "show-volume", gdu_device_get_device_file (device));
                g_object_unref (device);
        }

        g_spawn_async (NULL, argv, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL, NULL, NULL);
        g_free (argv[1]);
}

/* -------------------------------------------------------------------------- */

static void
cancel_operation (FormatDialogPrivate *priv)
{
        GduDevice *device;

        g_return_if_fail (priv != NULL);
        g_return_if_fail (priv->job_running == TRUE);
        /*  TODO: check for valid device  */
        g_return_if_fail (priv->presentable != NULL);
        g_warning ("Cancelling...");

        priv->job_cancelled = TRUE;
        device = gdu_presentable_get_device (priv->presentable);

        g_return_if_fail (device != NULL);
        gdu_device_op_cancel_job (device, NULL, NULL);
        g_object_unref (device);
}

static gboolean
window_delete_event (GtkWidget            *widget,
                     GdkEvent             *event,
                     FormatDialogPrivate  *priv)
{
        g_return_val_if_fail (priv != NULL, FALSE);

        if (priv->job_running) {
                cancel_operation (priv);
                return TRUE;  /*  consume the event  */
        }

        return FALSE;
}

static void
format_dialog_got_response (GtkDialog            *dialog,
                            gint                  response_id,
                            FormatDialogPrivate  *priv)
{
        if (response_id == GTK_RESPONSE_OK) {
                do_format (priv);
        }
        else if (response_id == GTK_RESPONSE_ACCEPT) {
                spawn_palimpsest (priv);
                nautilus_gdu_destroy (priv);
        }
        else if (priv->job_running) {
                cancel_operation (priv);
        }
        else {
                /*  destroy the window and unref the presentable  */
                nautilus_gdu_destroy (priv);
        }
}

/* ---------------------------------------------------------------------------------------------------- */
static void
set_default_volume_label (FormatDialogPrivate *priv)
{
        gchar *s;
        GDate *date;

        date = g_date_new ();
        g_date_set_time_t (date, time (NULL));
        s = g_strdup_printf (_("Data %d%.2d%.2d"), g_date_get_year (date), g_date_get_month (date), g_date_get_day (date));
        gtk_entry_set_text (GTK_ENTRY (priv->label_entry), s);
        g_free (s);
        g_date_free (date);
}

/* -------------------------------------------------------------------------- */

void
nautilus_gdu_spawn_dialog (GduPresentable *presentable)
{
        FormatDialogPrivate *priv;
        GtkDialog *dialog;
        GtkWidget *content_area;
        GtkWidget *button;
        GtkWidget *icon;
        GtkWidget *label;
        GtkWidget *align;
        GtkWidget *vbox3;
        GtkWidget *vbox2;
        GtkWidget *hbox;
        GtkWidget *image;
        GtkWidget *table;
        GtkWidget *entry;
        GtkWidget *combo_box;
        GtkWidget *progress_bar;
        gint row;

        g_return_if_fail (presentable != NULL);

        priv = g_new0 (FormatDialogPrivate, 1);
        priv->job_running = FALSE;
        priv->pool = gdu_presentable_get_pool (presentable);

        dialog = GTK_DIALOG (gtk_dialog_new ());
        priv->dialog = dialog;

        /*  HIG stuff...  */
        gtk_dialog_set_has_separator (dialog, FALSE);
        gtk_container_set_border_width (GTK_CONTAINER (dialog), 5);
        gtk_box_set_spacing (GTK_BOX (dialog->vbox), 2);
        gtk_container_set_border_width (GTK_CONTAINER (dialog->action_area), 5);
        gtk_box_set_spacing (GTK_BOX (dialog->action_area), 6);

        gtk_window_set_resizable (GTK_WINDOW (dialog), FALSE);
        gtk_window_set_title (GTK_WINDOW (dialog), "");

        gtk_dialog_add_buttons (dialog,
                                _("_Format"), GTK_RESPONSE_OK,
                                NULL);
        priv->close_button = gtk_dialog_add_button (dialog, GTK_STOCK_CLOSE, GTK_RESPONSE_CLOSE);
        button = gtk_dialog_add_button (dialog, _("Open Disk Utility"), GTK_RESPONSE_ACCEPT);
        icon = gtk_image_new_from_icon_name ("palimpsest", GTK_ICON_SIZE_BUTTON);
        gtk_button_set_image (GTK_BUTTON (button), icon);
        gtk_button_box_set_child_secondary (GTK_BUTTON_BOX (gtk_dialog_get_action_area (dialog)), button, TRUE);
        gtk_widget_set_tooltip_text (button, _("Format volume using the Palimpsest Disk Utility"));
        gtk_dialog_set_default_response (dialog, GTK_RESPONSE_CLOSE);

        content_area = gtk_dialog_get_content_area (GTK_DIALOG (dialog));
        gtk_container_set_border_width (GTK_CONTAINER (content_area), 10);


        /*  headlines  */
        priv->all_controls_box = gtk_vbox_new (FALSE, 0);
        gtk_box_pack_start (GTK_BOX (content_area), priv->all_controls_box, FALSE, TRUE, 0);

        hbox = gtk_hbox_new (FALSE, 0);
        gtk_box_pack_start (GTK_BOX (priv->all_controls_box), hbox, TRUE, TRUE, 0);

        image = gtk_image_new ();
        gtk_box_pack_start (GTK_BOX (hbox), image, FALSE, TRUE, 5);
        priv->icon_image = image;

        label = gtk_label_new (NULL);
        gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
        gtk_box_pack_start (GTK_BOX (hbox), label, FALSE, TRUE, 5);
        priv->name_label = label;

        /*  partition  */
        vbox3 = gtk_vbox_new (FALSE, 2);
        align = gtk_alignment_new (0.5, 0.5, 1.0, 1.0);
        gtk_alignment_set_padding (GTK_ALIGNMENT (align), 10, 30, 14, 10);
        gtk_container_add (GTK_CONTAINER (align), vbox3);
        gtk_box_pack_start (GTK_BOX (priv->all_controls_box), align, FALSE, TRUE, 0);

        label = gtk_label_new (NULL);
        gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
        gtk_misc_set_padding (GTK_MISC (label), 10, 10);
        gtk_label_set_line_wrap (GTK_LABEL (label), TRUE);
        /*  gtk_label_set_width_chars (GTK_LABEL (label), 50);  */
        gtk_box_pack_start (GTK_BOX (vbox3), label, FALSE, TRUE, 0);
        priv->details_label = label;

        vbox2 = gtk_vbox_new (FALSE, 5);
        align = gtk_alignment_new (0.5, 0.5, 1.0, 1.0);
        gtk_alignment_set_padding (GTK_ALIGNMENT (align), 0, 0, 24, 0);
        gtk_container_add (GTK_CONTAINER (align), vbox2);
        gtk_box_pack_start (GTK_BOX (vbox3), align, FALSE, TRUE, 0);
        priv->controls_box = vbox2;

        row = 0;

        table = gtk_table_new (2, 2, FALSE);
        gtk_box_pack_start (GTK_BOX (vbox2), table, FALSE, FALSE, 0);

        /*  partition type  */
        label = gtk_label_new (NULL);
        gtk_misc_set_alignment (GTK_MISC (label), 1.0, 0.5);
        gtk_label_set_markup_with_mnemonic (GTK_LABEL (label), _("_Type:"));
        gtk_table_attach (GTK_TABLE (table), label, 0, 1, row, row + 1,
                          GTK_FILL, GTK_EXPAND | GTK_FILL, 2, 2);
        combo_box = gtk_combo_box_new_text ();
        gtk_table_attach (GTK_TABLE (table), combo_box, 1, 2, row, row +1,
                          GTK_FILL, GTK_EXPAND | GTK_FILL, 2, 2);
        gtk_label_set_mnemonic_widget (GTK_LABEL (label), combo_box);
        priv->part_type_combo_box = combo_box;
        row++;

        /*  volume label  */
        label = gtk_label_new (NULL);
        gtk_misc_set_alignment (GTK_MISC (label), 1.0, 0.5);
        gtk_label_set_markup_with_mnemonic (GTK_LABEL (label), _("_Name:"));
        gtk_table_attach (GTK_TABLE (table), label, 0, 1, row, row + 1,
                          GTK_FILL, GTK_EXPAND | GTK_FILL, 2, 2);
        entry = gtk_entry_new ();
        gtk_table_attach (GTK_TABLE (table), entry, 1, 2, row, row + 1,
                          GTK_FILL, GTK_EXPAND | GTK_FILL, 2, 2);
        gtk_label_set_mnemonic_widget (GTK_LABEL (label), entry);
        priv->label_entry = entry;
        row++;

        /*  mounted warning box  */
        hbox = gtk_hbox_new (FALSE, 7);
        align = gtk_alignment_new (0.5, 0.5, 1.0, 1.0);
        gtk_alignment_set_padding (GTK_ALIGNMENT (align), 15, 15, 20, 0);
        gtk_container_add (GTK_CONTAINER (align), hbox);
        gtk_box_pack_start (GTK_BOX (priv->all_controls_box), align, FALSE, TRUE, 0);
        priv->mount_warning = align;
        image = gtk_image_new_from_stock (GTK_STOCK_DIALOG_WARNING, GTK_ICON_SIZE_LARGE_TOOLBAR);
        gtk_box_pack_start (GTK_BOX (hbox), image, FALSE, TRUE, 0);
        label = gtk_label_new (_("The volume is currently mounted. Please make sure to close all open files before formatting."));
        gtk_label_set_line_wrap (GTK_LABEL (label), TRUE);
        gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
        gtk_box_pack_start (GTK_BOX (hbox), label, FALSE, TRUE, 0);
        gtk_widget_show_all (priv->mount_warning);
        gtk_widget_hide (priv->mount_warning);
        gtk_widget_set_no_show_all (priv->mount_warning, TRUE);

        /*  readonly info box  */
        hbox = gtk_hbox_new (FALSE, 7);
        align = gtk_alignment_new (0.5, 0.5, 1.0, 1.0);
        gtk_alignment_set_padding (GTK_ALIGNMENT (align), 10, 10, 20, 0);
        gtk_container_add (GTK_CONTAINER (align), hbox);
        gtk_box_pack_start (GTK_BOX (priv->all_controls_box), align, FALSE, TRUE, 0);
        priv->readonly_warning = align;
        image = gtk_image_new_from_stock (GTK_STOCK_DIALOG_INFO, GTK_ICON_SIZE_LARGE_TOOLBAR);
        gtk_box_pack_start (GTK_BOX (hbox), image, FALSE, TRUE, 0);
        label = gtk_label_new (_("Device is read only"));
        gtk_label_set_line_wrap (GTK_LABEL (label), TRUE);
        gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
        gtk_box_pack_start (GTK_BOX (hbox), label, FALSE, TRUE, 0);
        gtk_widget_show_all (priv->readonly_warning);
        gtk_widget_hide (priv->readonly_warning);
        gtk_widget_set_no_show_all (priv->readonly_warning, TRUE);

        /*  no media info box  */
        hbox = gtk_hbox_new (FALSE, 7);
        align = gtk_alignment_new (0.5, 0.5, 1.0, 1.0);
        gtk_alignment_set_padding (GTK_ALIGNMENT (align), 10, 10, 20, 0);
        gtk_container_add (GTK_CONTAINER (align), hbox);
        gtk_box_pack_start (GTK_BOX (priv->all_controls_box), align, FALSE, TRUE, 0);
        priv->no_media_warning = align;
        image = gtk_image_new_from_stock (GTK_STOCK_DIALOG_INFO, GTK_ICON_SIZE_LARGE_TOOLBAR);
        gtk_box_pack_start (GTK_BOX (hbox), image, FALSE, TRUE, 0);
        label = gtk_label_new (_("No media in drive"));
        gtk_label_set_line_wrap (GTK_LABEL (label), TRUE);
        gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
        gtk_box_pack_start (GTK_BOX (hbox), label, FALSE, TRUE, 0);
        gtk_widget_show_all (priv->no_media_warning);
        gtk_widget_hide (priv->no_media_warning);
        gtk_widget_set_no_show_all (priv->no_media_warning, TRUE);

        /*  progress bar  */
        align = gtk_alignment_new (0.5, 0.5, 1.0, 1.0);
        gtk_alignment_set_padding (GTK_ALIGNMENT (align), 75, 75, 30, 30);
        priv->progress_bar_box = align;

        progress_bar = gtk_progress_bar_new ();
        gtk_widget_set_size_request (progress_bar, 375, -1);   /*  TODO: this is an ugly hack  */
        priv->progress_bar = progress_bar;
        gtk_container_add (GTK_CONTAINER (align), progress_bar);
        gtk_box_pack_start (GTK_BOX (content_area), align, TRUE, FALSE, 0);
        gtk_widget_show_all (priv->progress_bar_box);
        gtk_widget_hide (priv->progress_bar_box);
        gtk_widget_set_no_show_all (priv->progress_bar_box, TRUE);


        g_signal_connect (priv->dialog, "delete-event",
                          G_CALLBACK (window_delete_event), priv);
        g_signal_connect (priv->part_type_combo_box, "changed",
                          G_CALLBACK (type_combo_box_changed), priv);
        /*  update sensivity and length of fs label + entry  */
        g_signal_connect (G_OBJECT (dialog), "response",
                          G_CALLBACK (format_dialog_got_response), priv);

        gtk_widget_show_all (GTK_WIDGET (dialog));
        gtk_widget_grab_focus (priv->close_button);
        set_new_presentable (priv, presentable);
        update_ui (priv);
        update_ui_progress (priv, NULL, FALSE);
        set_default_volume_label (priv);
}
