/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*- */
/* gdu-section-no-media.c
 *
 * Copyright (C) 2007 David Zeuthen
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
#include <string.h>
#include <glib/gi18n.h>
#include <dbus/dbus-glib.h>
#include <stdlib.h>
#include <math.h>

#include <gdu/gdu.h>
#include "gdu-section-no-media.h"

struct _GduSectionNoMediaPrivate
{
        guint foo;
};

static GObjectClass *parent_class = NULL;

G_DEFINE_TYPE (GduSectionNoMedia, gdu_section_no_media, GDU_TYPE_SECTION)

/* ---------------------------------------------------------------------------------------------------- */

static void
update (GduSectionNoMedia *section)
{
}

/* ---------------------------------------------------------------------------------------------------- */

static void
gdu_section_no_media_finalize (GduSectionNoMedia *section)
{
        if (G_OBJECT_CLASS (parent_class)->finalize)
                (* G_OBJECT_CLASS (parent_class)->finalize) (G_OBJECT (section));
}

static void
gdu_section_no_media_class_init (GduSectionNoMediaClass *klass)
{
        GObjectClass *obj_class = (GObjectClass *) klass;
        GduSectionClass *section_class = (GduSectionClass *) klass;

        parent_class = g_type_class_peek_parent (klass);

        obj_class->finalize = (GObjectFinalizeFunc) gdu_section_no_media_finalize;
        section_class->update = (gpointer) update;

        g_type_class_add_private (klass, sizeof (GduSectionNoMediaPrivate));
}

static void
op_poll_media_cb (GduDevice *device,
                  GError    *error,
                  gpointer   user_data)
{
        if (error != NULL)
                g_error_free (error);
}

static void
detect_media_button_clicked (GtkWidget *button, gpointer user_data)
{
        GduSectionNoMedia *section = GDU_SECTION_NO_MEDIA (user_data);
        GduDevice *device;

        device = gdu_presentable_get_device (gdu_section_get_presentable (GDU_SECTION (section)));
        if (device == NULL)
                goto out;

        gdu_device_op_drive_poll_media (device, op_poll_media_cb, NULL);

        update (section);

out:
        if (device != NULL)
                g_object_unref (device);

}

static void
gdu_section_no_media_init (GduSectionNoMedia *section)
{
        GtkWidget *vbox2;
        GtkWidget *label;
        GtkWidget *align;
        GtkWidget *button;
        GtkWidget *button_box;

        section->priv = G_TYPE_INSTANCE_GET_PRIVATE (section, GDU_TYPE_SECTION_NO_MEDIA, GduSectionNoMediaPrivate);

        label = gtk_label_new (NULL);
        gtk_label_set_markup (GTK_LABEL (label), _("<b>No Media Detected</b>"));
        gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
        gtk_box_pack_start (GTK_BOX (section), label, FALSE, FALSE, 6);
        vbox2 = gtk_vbox_new (FALSE, 5);
        align = gtk_alignment_new (0.5, 0.5, 1.0, 1.0);
        gtk_alignment_set_padding (GTK_ALIGNMENT (align), 0, 0, 12, 0);
        gtk_container_add (GTK_CONTAINER (align), vbox2);
        gtk_box_pack_start (GTK_BOX (section), align, FALSE, TRUE, 0);

        /* explanatory text */
        label = gtk_label_new (NULL);
        gtk_label_set_markup (GTK_LABEL (label), _("To format or edit media, insert it into the drive and wait "
                                                   "a few seconds."));
        gtk_label_set_line_wrap (GTK_LABEL (label), TRUE);
        gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
        gtk_box_pack_start (GTK_BOX (vbox2), label, FALSE, TRUE, 0);

        /* media detect button */
        button_box = gtk_hbutton_box_new ();
        gtk_button_box_set_layout (GTK_BUTTON_BOX (button_box), GTK_BUTTONBOX_START);
        gtk_box_set_spacing (GTK_BOX (button_box), 6);
        gtk_box_pack_start (GTK_BOX (vbox2), button_box, TRUE, TRUE, 0);
        button = gtk_button_new_with_mnemonic (_("_Detect Media"));
        gtk_button_set_image (GTK_BUTTON (button),
                              gtk_image_new_from_stock (GTK_STOCK_REFRESH, GTK_ICON_SIZE_BUTTON));
        gtk_container_add (GTK_CONTAINER (button_box), button);
        g_signal_connect (button, "clicked",
                          G_CALLBACK (detect_media_button_clicked), section);

}

GtkWidget *
gdu_section_no_media_new (GduShell       *shell,
                          GduPresentable *presentable)
{
        return GTK_WIDGET (g_object_new (GDU_TYPE_SECTION_NO_MEDIA,
                                         "shell", shell,
                                         "presentable", presentable,
                                         NULL));
}
