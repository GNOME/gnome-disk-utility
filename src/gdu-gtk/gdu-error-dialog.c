/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*- */
/* gdu-ata-smart-dialog.c
 *
 * Copyright (C) 2009 David Zeuthen
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 */

#include <config.h>
#include <glib/gi18n.h>
#include <atasmart.h>
#include <glib/gstdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>

#include "gdu-error-dialog.h"

/* ---------------------------------------------------------------------------------------------------- */
/* Gah, someone box GError in libgobject already */

#define GDU_TYPE_ERROR (gdu_error_get_type ())

static GError *
gdu_error_copy (const GError *error)
{
        return g_error_copy (error);
}

static void
gdu_error_free (GError *error)
{
        g_error_free (error);
}

static GType
gdu_error_get_type (void)
{
        static volatile gsize type_volatile = 0;

        if (g_once_init_enter (&type_volatile)) {
                GType type = g_boxed_type_register_static (
                                                           g_intern_static_string ("GduGError"),
                                                           (GBoxedCopyFunc) gdu_error_copy,
                                                           (GBoxedFreeFunc) gdu_error_free);
                g_once_init_leave (&type_volatile, type);
        }

        return type_volatile;
}

/* ---------------------------------------------------------------------------------------------------- */

struct GduErrorDialogPrivate
{
        GduPresentable *presentable;
        gchar          *message;
        GError         *error;
};

enum
{
        PROP_0,
        PROP_PRESENTABLE,
        PROP_DRIVE_DEVICE,
        PROP_VOLUME_DEVICE,
        PROP_MESSAGE,
        PROP_ERROR
};


G_DEFINE_TYPE (GduErrorDialog, gdu_error_dialog, GTK_TYPE_DIALOG)

static void
gdu_error_dialog_finalize (GObject *object)
{
        GduErrorDialog *dialog = GDU_ERROR_DIALOG (object);

        if (dialog->priv->presentable != NULL) {
                g_object_unref (dialog->priv->presentable);
        }
        g_free (dialog->priv->message);
        g_error_free (dialog->priv->error);

        if (G_OBJECT_CLASS (gdu_error_dialog_parent_class)->finalize != NULL)
                G_OBJECT_CLASS (gdu_error_dialog_parent_class)->finalize (object);
}

static void
gdu_error_dialog_get_property (GObject    *object,
                               guint       property_id,
                               GValue     *value,
                               GParamSpec *pspec)
{
        GduErrorDialog *dialog = GDU_ERROR_DIALOG (object);

        switch (property_id) {
        case PROP_PRESENTABLE:
                g_value_set_object (value, dialog->priv->presentable);
                break;

        case PROP_MESSAGE:
                g_value_set_string (value, dialog->priv->message);
                break;

        case PROP_ERROR:
                g_value_set_boxed (value, dialog->priv->error);
                break;

        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
                break;
        }
}

static void
gdu_error_dialog_set_property (GObject      *object,
                               guint         property_id,
                               const GValue *value,
                               GParamSpec   *pspec)
{
        GduErrorDialog *dialog = GDU_ERROR_DIALOG (object);
        GduDevice *device;
        GduPool *pool;

        switch (property_id) {
        case PROP_PRESENTABLE:
                if (g_value_get_object (value) != NULL) {
                        g_warn_if_fail (dialog->priv->presentable == NULL);
                        dialog->priv->presentable = g_value_dup_object (value);
                }
                break;

        case PROP_DRIVE_DEVICE:
                device = GDU_DEVICE (g_value_get_object (value));
                if (device != NULL) {
                        pool = gdu_device_get_pool (device);
                        g_warn_if_fail (dialog->priv->presentable == NULL);
                        dialog->priv->presentable = gdu_pool_get_drive_by_device (pool, device);
                        g_object_unref (pool);
                }
                break;

        case PROP_VOLUME_DEVICE:
                device = GDU_DEVICE (g_value_get_object (value));
                if (device != NULL) {
                        pool = gdu_device_get_pool (device);
                        g_warn_if_fail (dialog->priv->presentable == NULL);
                        dialog->priv->presentable = gdu_pool_get_volume_by_device (pool, device);
                        g_object_unref (pool);
                }
                break;

        case PROP_MESSAGE:
                dialog->priv->message = g_value_dup_string (value);
                break;

        case PROP_ERROR:
                dialog->priv->error = g_value_dup_boxed (value);
                break;

        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
        }
}

/* ---------------------------------------------------------------------------------------------------- */

static void
gdu_error_dialog_constructed (GObject *object)
{
        GduErrorDialog *dialog = GDU_ERROR_DIALOG (object);
        GtkWidget *content_area;
        GtkWidget *hbox;
        GtkWidget *vbox;
        GtkWidget *image;
        GtkWidget *label;
        gchar *s;
        GIcon *icon;
        GEmblem *emblem;
        GIcon *error_icon;
        GIcon *emblemed_icon;
        gchar *name;
        gchar *vpd_name;
        const gchar *error_msg;

        icon = NULL;
        name = NULL;
        vpd_name = NULL;

        gtk_window_set_title (GTK_WINDOW (dialog), _(""));
        gtk_dialog_set_has_separator (GTK_DIALOG (dialog), FALSE);
        gtk_container_set_border_width (GTK_CONTAINER (dialog), 12);

        gtk_dialog_add_button (GTK_DIALOG (dialog),
                               GTK_STOCK_CLOSE,
                               GTK_RESPONSE_CLOSE);

        content_area = gtk_dialog_get_content_area (GTK_DIALOG (dialog));

        icon = gdu_presentable_get_icon (dialog->priv->presentable);
        error_icon = g_themed_icon_new (GTK_STOCK_DIALOG_ERROR);
        emblem = g_emblem_new (icon);
        emblemed_icon = g_emblemed_icon_new (error_icon,
                                             emblem);

        hbox = gtk_hbox_new (FALSE, 12);
        gtk_box_pack_start (GTK_BOX (content_area), hbox, TRUE, TRUE, 0);

        image = gtk_image_new_from_gicon (emblemed_icon,
                                          GTK_ICON_SIZE_DIALOG);
        gtk_misc_set_alignment (GTK_MISC (image), 0.5, 0.0);
        gtk_box_pack_start (GTK_BOX (hbox), image, FALSE, FALSE, 0);

        vbox = gtk_vbox_new (FALSE, 12);
        gtk_box_pack_start (GTK_BOX (hbox), vbox, FALSE, FALSE, 0);

        s = g_strdup_printf ("<big><big><b>%s</b></big></big>",
                             dialog->priv->message);
        label = gtk_label_new (NULL);
        gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
        gtk_label_set_markup (GTK_LABEL (label), s);
        g_free (s);
        gtk_box_pack_start (GTK_BOX (vbox), label, FALSE, FALSE, 0);

        error_msg = NULL;
        if (dialog->priv->error->domain == GDU_ERROR) {
                switch (dialog->priv->error->code) {
                case GDU_ERROR_FAILED:
                        error_msg = _("The operation failed");
                        break;
                case GDU_ERROR_BUSY:
                        error_msg = _("The device is busy");
                        break;
                case GDU_ERROR_CANCELLED:
                        error_msg = _("The operation was canceled");
                        break;
                case GDU_ERROR_INHIBITED:
                        error_msg = _("The daemon is being inhibited");
                        break;
                case GDU_ERROR_INVALID_OPTION:
                        error_msg = _("An invalid option was passed");
                        break;
                case GDU_ERROR_NOT_SUPPORTED:
                        error_msg = _("The operation is not supported");
                        break;
                case GDU_ERROR_ATA_SMART_WOULD_WAKEUP:
                        error_msg = _("Getting ATA SMART data would wake up the device");
                        break;
                case GDU_ERROR_PERMISSION_DENIED:
                        error_msg = _("Permission denied");
                        break;
                }
        }
        if (error_msg == NULL)
                error_msg = _("Unknown error");

        name = gdu_presentable_get_name (dialog->priv->presentable);
        vpd_name = gdu_presentable_get_vpd_name (dialog->priv->presentable);
        s = g_strdup_printf (_("An error occured while performing an operation "
                               "on \"%s\" (%s): %s"),
                             name,
                             vpd_name,
                             error_msg);

        label = gtk_label_new (s);
        g_free (s);
        gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
	gtk_label_set_line_wrap (GTK_LABEL (label), TRUE);
        gtk_box_pack_start (GTK_BOX (vbox), label, FALSE, FALSE, 0);

        g_object_unref (icon);
        g_object_unref (emblem);
        g_object_unref (error_icon);
        g_object_unref (emblemed_icon);

        g_free (name);
        g_free (vpd_name);

        if (G_OBJECT_CLASS (gdu_error_dialog_parent_class)->constructed != NULL)
                G_OBJECT_CLASS (gdu_error_dialog_parent_class)->constructed (object);
}

static void
gdu_error_dialog_class_init (GduErrorDialogClass *klass)
{
        GObjectClass *object_class = G_OBJECT_CLASS (klass);

        g_type_class_add_private (klass, sizeof (GduErrorDialogPrivate));

        object_class->get_property = gdu_error_dialog_get_property;
        object_class->set_property = gdu_error_dialog_set_property;
        object_class->constructed  = gdu_error_dialog_constructed;
        object_class->finalize     = gdu_error_dialog_finalize;

        g_object_class_install_property (object_class,
                                         PROP_PRESENTABLE,
                                         g_param_spec_object ("presentable",
                                                              NULL,
                                                              NULL,
                                                              GDU_TYPE_PRESENTABLE,
                                                              G_PARAM_READABLE |
                                                              G_PARAM_WRITABLE |
                                                              G_PARAM_CONSTRUCT_ONLY));

        g_object_class_install_property (object_class,
                                         PROP_DRIVE_DEVICE,
                                         g_param_spec_object ("drive-device",
                                                              NULL,
                                                              NULL,
                                                              GDU_TYPE_DEVICE,
                                                              G_PARAM_WRITABLE |
                                                              G_PARAM_CONSTRUCT_ONLY));

        g_object_class_install_property (object_class,
                                         PROP_VOLUME_DEVICE,
                                         g_param_spec_object ("volume-device",
                                                              NULL,
                                                              NULL,
                                                              GDU_TYPE_DEVICE,
                                                              G_PARAM_WRITABLE |
                                                              G_PARAM_CONSTRUCT_ONLY));

        g_object_class_install_property (object_class,
                                         PROP_MESSAGE,
                                         g_param_spec_string ("message",
                                                              NULL,
                                                              NULL,
                                                              NULL,
                                                              G_PARAM_READABLE |
                                                              G_PARAM_WRITABLE |
                                                              G_PARAM_CONSTRUCT_ONLY));

        g_object_class_install_property (object_class,
                                         PROP_ERROR,
                                         g_param_spec_boxed ("error",
                                                             NULL,
                                                             NULL,
                                                             GDU_TYPE_ERROR,
                                                             G_PARAM_READABLE |
                                                             G_PARAM_WRITABLE |
                                                             G_PARAM_CONSTRUCT_ONLY));
}

static void
gdu_error_dialog_init (GduErrorDialog *dialog)
{
        dialog->priv = G_TYPE_INSTANCE_GET_PRIVATE (dialog, GDU_TYPE_ERROR_DIALOG, GduErrorDialogPrivate);
}

GtkWidget *
gdu_error_dialog_new (GtkWindow      *parent,
                      GduPresentable *presentable,
                      const gchar    *message,
                      const GError   *error)
{
        g_return_val_if_fail (GDU_IS_PRESENTABLE (presentable), NULL);
        return GTK_WIDGET (g_object_new (GDU_TYPE_ERROR_DIALOG,
                                         "transient-for", parent,
                                         "presentable", presentable,
                                         "message", message,
                                         "error", error,
                                         NULL));
}

GtkWidget *
gdu_error_dialog_for_drive (GtkWindow      *parent,
                            GduDevice      *device,
                            const gchar    *message,
                            const GError   *error)
{
        g_return_val_if_fail (GDU_IS_DEVICE (device), NULL);
        return GTK_WIDGET (g_object_new (GDU_TYPE_ERROR_DIALOG,
                                         "transient-for", parent,
                                         "drive-device", device,
                                         "message", message,
                                         "error", error,
                                         NULL));
}

GtkWidget *
gdu_error_dialog_for_volume (GtkWindow      *parent,
                             GduDevice      *device,
                             const gchar    *message,
                             const GError   *error)
{
        g_return_val_if_fail (GDU_IS_DEVICE (device), NULL);
        return GTK_WIDGET (g_object_new (GDU_TYPE_ERROR_DIALOG,
                                         "transient-for", parent,
                                         "volume-device", device,
                                         "message", message,
                                         "error", error,
                                         NULL));
}

/* ---------------------------------------------------------------------------------------------------- */
