/*
 * Copyright (C) 2008-2013 Red Hat, Inc.
 *
 * Licensed under GPL version 2 or later.
 *
 * Author: Programejt <programejt@proton.me>
 */

#include "gdu-attach-disk-image-dialog.h"

#include <glib/gi18n.h>

#include "gduutils.h"

struct _GduAttachDiskImageDialog {
    AdwDialog parent_instance;

    GtkButton *attach_image_button;
    AdwActionRow *disk_image_file_row;
    AdwSwitchRow *readonly_row;

    GFile *disk_image_file;

    GduManager *manager;
};

G_DEFINE_FINAL_TYPE (GduAttachDiskImageDialog, gdu_attach_disk_image_dialog, ADW_TYPE_DIALOG)

static GtkWindow *
attach_disk_dialog_get_window (GduAttachDiskImageDialog *self)
{
    return GTK_WINDOW (gtk_widget_get_ancestor (GTK_WIDGET (self), GTK_TYPE_WINDOW));
}

static void
loop_open_cb (GObject *object, GAsyncResult *result, gpointer user_data)
{
    g_autoptr (GtkWindow) window = user_data;
    g_autoptr (GError) error = NULL;

    gdu_manager_open_loop_finish (GDU_MANAGER (object), result, &error);

    if (error)
        gdu_utils_show_error (window, _("Error attaching disk image"), error);
}

static void
file_dialog_open_cb (GObject *source_object, GAsyncResult *res, gpointer user_data)
{
    g_autoptr (GduAttachDiskImageDialog) self = user_data;
    GtkFileDialog *file_dialog = GTK_FILE_DIALOG (source_object);
    g_autoptr (GFile) file = NULL;
    g_autofree char *name = NULL;

    g_assert (GDU_IS_ATTACH_DISK_IMAGE_DIALOG (self));

    file = gtk_file_dialog_open_finish (file_dialog, res, NULL);

    if (!file)
        return;

    g_set_object (&self->disk_image_file, file);

    name = g_file_get_basename (file);
    adw_action_row_set_subtitle (self->disk_image_file_row, name ? name : "");

    gtk_widget_set_sensitive (GTK_WIDGET (self->attach_image_button), TRUE);
}

static void
on_choose_disk_image_button_clicked_cb (GduAttachDiskImageDialog *self)
{
    GtkWindow *window;
    g_autoptr (GtkFileDialog) dialog = gtk_file_dialog_new ();

    g_return_if_fail (GDU_IS_ATTACH_DISK_IMAGE_DIALOG (self));

    window = attach_disk_dialog_get_window (self);
    if (window == NULL) {
        g_info ("Could not get native window for dialog");
        return;
    }

    gtk_file_dialog_set_title (dialog, _("Select a Disk Image to Attach"));
    gtk_file_dialog_set_modal (dialog, TRUE);

    gdu_utils_configure_file_dialog_for_disk_images (dialog, TRUE, /* set file types */
                                                     FALSE);       /* allow_compressed */

    gtk_file_dialog_open (dialog, window, NULL, file_dialog_open_cb, g_object_ref (self));
}

static void
on_attach_image_button_clicked_cb (GduAttachDiskImageDialog *self)
{
    GtkWindow *window;

    g_return_if_fail (G_IS_FILE (self->disk_image_file));

    window = attach_disk_dialog_get_window (self);
    g_return_if_fail (GTK_IS_WINDOW (window));

    gdu_manager_open_loop_async (self->manager, self->disk_image_file, adw_switch_row_get_active (self->readonly_row),
                                 loop_open_cb, g_object_ref (window));

    adw_dialog_close (ADW_DIALOG (self));
}

static void
gdu_attach_disk_image_dialog_finalize (GObject *object)
{
    GduAttachDiskImageDialog *self = GDU_ATTACH_DISK_IMAGE_DIALOG (object);

    g_clear_object (&self->disk_image_file);
    g_clear_object (&self->manager);

    G_OBJECT_CLASS (gdu_attach_disk_image_dialog_parent_class)->finalize (object);
}

void
gdu_attach_disk_image_dialog_class_init (GduAttachDiskImageDialogClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

    object_class->finalize = gdu_attach_disk_image_dialog_finalize;

    gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/DiskUtility/ui/"
                                                               "gdu-attach-disk-image-dialog.ui");

    gtk_widget_class_bind_template_child (widget_class, GduAttachDiskImageDialog, attach_image_button);
    gtk_widget_class_bind_template_child (widget_class, GduAttachDiskImageDialog, disk_image_file_row);
    gtk_widget_class_bind_template_child (widget_class, GduAttachDiskImageDialog, readonly_row);

    gtk_widget_class_bind_template_callback (widget_class, on_choose_disk_image_button_clicked_cb);
    gtk_widget_class_bind_template_callback (widget_class, on_attach_image_button_clicked_cb);
}

void
gdu_attach_disk_image_dialog_init (GduAttachDiskImageDialog *self)
{
    gtk_widget_init_template (GTK_WIDGET (self));
}

void
gdu_attach_disk_image_dialog_show (GtkWindow *parent_window, GduManager *manager)
{
    GduAttachDiskImageDialog *self;

    self = g_object_new (GDU_TYPE_ATTACH_DISK_IMAGE_DIALOG, NULL);

    self->manager = g_object_ref (manager);

    adw_dialog_present (ADW_DIALOG (self), GTK_WIDGET (parent_window));
}
