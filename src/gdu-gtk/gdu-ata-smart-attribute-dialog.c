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

#include "gdu-ata-smart-attribute-dialog.h"

struct GduAtaSmartAttributeDialogPrivate
{
        GduDevice *device;
        gchar *attribute_name;

        guint64 last_updated;
        gulong device_changed_signal_handler_id;

        GduAtaSmartAttribute *attr;

        GtkWidget *attribute_name_label;
};

enum
{
        PROP_0,
        PROP_DEVICE,
        PROP_ATTRIBUTE_NAME,
};

G_DEFINE_TYPE (GduAtaSmartAttributeDialog, gdu_ata_smart_attribute_dialog, GTK_TYPE_DIALOG)

static void update_dialog (GduAtaSmartAttributeDialog *dialog);
static void device_changed (GduDevice *device, gpointer user_data);

static void
gdu_ata_smart_attribute_dialog_finalize (GObject *object)
{
        GduAtaSmartAttributeDialog *dialog = GDU_ATA_SMART_ATTRIBUTE_DIALOG (object);

        g_signal_handler_disconnect (dialog->priv->device, dialog->priv->device_changed_signal_handler_id);

        g_object_unref (dialog->priv->device);
        g_free (dialog->priv->attribute_name);
        if (dialog->priv->attr != NULL)
                g_object_unref (dialog->priv->attr);

        if (G_OBJECT_CLASS (gdu_ata_smart_attribute_dialog_parent_class)->finalize != NULL)
                G_OBJECT_CLASS (gdu_ata_smart_attribute_dialog_parent_class)->finalize (object);
}

static void
gdu_ata_smart_attribute_dialog_get_property (GObject    *object,
                                   guint       property_id,
                                   GValue     *value,
                                   GParamSpec *pspec)
{
        GduAtaSmartAttributeDialog *dialog = GDU_ATA_SMART_ATTRIBUTE_DIALOG (object);

        switch (property_id) {
        case PROP_DEVICE:
                g_value_set_object (value, dialog->priv->device);
                break;

        case PROP_ATTRIBUTE_NAME:
                g_value_set_string (value, dialog->priv->attribute_name);
                break;

        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
        }
}

static void
gdu_ata_smart_attribute_dialog_set_property (GObject      *object,
                                             guint         property_id,
                                             const GValue *value,
                                             GParamSpec   *pspec)
{
        GduAtaSmartAttributeDialog *dialog = GDU_ATA_SMART_ATTRIBUTE_DIALOG (object);

        switch (property_id) {
        case PROP_DEVICE:
                dialog->priv->device = g_value_dup_object (value);
                break;

        case PROP_ATTRIBUTE_NAME:
                dialog->priv->attribute_name = g_value_dup_string (value);
                break;

        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
        }
}

static void
gdu_ata_smart_attribute_dialog_constructed (GObject *object)
{
        GduAtaSmartAttributeDialog *dialog = GDU_ATA_SMART_ATTRIBUTE_DIALOG (object);
        GtkWidget *content_area;
        GtkWidget *align;
        GtkWidget *vbox;
        GtkWidget *table;
        GtkWidget *label;
        gint row;

        gtk_dialog_set_has_separator (GTK_DIALOG (dialog), FALSE);

        content_area = gtk_dialog_get_content_area (GTK_DIALOG (dialog));

        align = gtk_alignment_new (0.5, 0.5, 1.0, 1.0);
        gtk_alignment_set_padding (GTK_ALIGNMENT (align), 12, 12, 12, 12);
        gtk_box_pack_start (GTK_BOX (content_area), align, TRUE, TRUE, 0);

        vbox = gtk_vbox_new (FALSE, 6);
        gtk_container_add (GTK_CONTAINER (align), vbox);

        table = gtk_table_new (4, 2, FALSE);
        gtk_table_set_col_spacings (GTK_TABLE (table), 12);
        gtk_table_set_row_spacings (GTK_TABLE (table), 4);
        gtk_box_pack_start (GTK_BOX (vbox), table, FALSE, FALSE, 0);

        row = 0;

        /* attribute name */
        label = gtk_label_new (NULL);
        gtk_misc_set_alignment (GTK_MISC (label), 1.0, 0.5);
        gtk_label_set_markup_with_mnemonic (GTK_LABEL (label), _("<b>Attribute:</b>"));
        gtk_table_attach (GTK_TABLE (table), label, 0, 1, row, row + 1,
                          GTK_FILL, GTK_EXPAND | GTK_FILL, 0, 0);

        label = gtk_label_new (NULL);
        gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
        dialog->priv->attribute_name_label = label;

        gtk_table_attach (GTK_TABLE (table), label, 1, 2, row, row + 1,
                          GTK_FILL, GTK_EXPAND | GTK_FILL, 0, 0);

        row++;

        update_dialog (dialog);

        dialog->priv->device_changed_signal_handler_id = g_signal_connect (dialog->priv->device,
                                                                           "changed",
                                                                           G_CALLBACK (device_changed),
                                                                           dialog);

        if (G_OBJECT_CLASS (gdu_ata_smart_attribute_dialog_parent_class)->constructed != NULL)
                G_OBJECT_CLASS (gdu_ata_smart_attribute_dialog_parent_class)->constructed (object);
}

static void
gdu_ata_smart_attribute_dialog_class_init (GduAtaSmartAttributeDialogClass *klass)
{
        GObjectClass *object_class = G_OBJECT_CLASS (klass);

        g_type_class_add_private (klass, sizeof (GduAtaSmartAttributeDialogPrivate));

        object_class->get_property = gdu_ata_smart_attribute_dialog_get_property;
        object_class->set_property = gdu_ata_smart_attribute_dialog_set_property;
        object_class->constructed  = gdu_ata_smart_attribute_dialog_constructed;
        object_class->finalize     = gdu_ata_smart_attribute_dialog_finalize;

        g_object_class_install_property (object_class,
                                         PROP_DEVICE,
                                         g_param_spec_object ("device",
                                                              _("Device"),
                                                              _("The device to show data for"),
                                                              GDU_TYPE_DEVICE,
                                                              G_PARAM_READABLE |
                                                              G_PARAM_WRITABLE |
                                                              G_PARAM_CONSTRUCT_ONLY));

        g_object_class_install_property (object_class,
                                         PROP_ATTRIBUTE_NAME,
                                         g_param_spec_string ("attribute-name",
                                                              _("Attribute Name"),
                                                              _("The attribute to show data for"),
                                                              NULL,
                                                              G_PARAM_READABLE |
                                                              G_PARAM_WRITABLE |
                                                              G_PARAM_CONSTRUCT_ONLY));
}

static void
gdu_ata_smart_attribute_dialog_init (GduAtaSmartAttributeDialog *dialog)
{
        dialog->priv = G_TYPE_INSTANCE_GET_PRIVATE (dialog, GDU_TYPE_ATA_SMART_ATTRIBUTE_DIALOG, GduAtaSmartAttributeDialogPrivate);
}

GtkWidget *
gdu_ata_smart_attribute_dialog_new (GtkWindow   *parent,
                                    GduDevice   *device,
                                    const gchar *attribute_name)
{
        return GTK_WIDGET (g_object_new (GDU_TYPE_ATA_SMART_ATTRIBUTE_DIALOG,
                                         "transient-for", parent,
                                         "device", device,
                                         "attribute-name", attribute_name,
                                         NULL));
}

/* ---------------------------------------------------------------------------------------------------- */

static void
update_dialog (GduAtaSmartAttributeDialog *dialog)
{
        if (dialog->priv->attr != NULL)
                g_object_unref (dialog->priv->attr);
        dialog->priv->attr = gdu_device_drive_ata_smart_get_attribute (dialog->priv->device,
                                                                       dialog->priv->attribute_name);

        gtk_label_set_markup (GTK_LABEL (dialog->priv->attribute_name_label),
                              gdu_ata_smart_attribute_get_localized_name (dialog->priv->attr));
}

static void
device_changed (GduDevice *device,
                gpointer   user_data)
{
        GduAtaSmartAttributeDialog *dialog = GDU_ATA_SMART_ATTRIBUTE_DIALOG (user_data);

        if (gdu_device_drive_ata_smart_get_time_collected (dialog->priv->device) != dialog->priv->last_updated) {
                update_dialog (dialog);
        }

}
