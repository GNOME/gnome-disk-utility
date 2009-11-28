/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*- */
/* gdu-section-hba.c
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
#include <string.h>
#include <glib/gi18n.h>
#include <dbus/dbus-glib.h>
#include <stdlib.h>
#include <math.h>
#include <gio/gdesktopappinfo.h>

#include <gdu-gtk/gdu-gtk.h>
#include "gdu-section-hba.h"

struct _GduSectionHbaPrivate
{
        GduDetailsElement *vendor_element;
        GduDetailsElement *model_element;
        GduDetailsElement *driver_element;
};

G_DEFINE_TYPE (GduSectionHba, gdu_section_hba, GDU_TYPE_SECTION)

/* ---------------------------------------------------------------------------------------------------- */

static void
gdu_section_hba_finalize (GObject *object)
{
        //GduSectionHba *section = GDU_SECTION_HBA (object);

        if (G_OBJECT_CLASS (gdu_section_hba_parent_class)->finalize != NULL)
                G_OBJECT_CLASS (gdu_section_hba_parent_class)->finalize (object);
}

/* ---------------------------------------------------------------------------------------------------- */

static void
gdu_section_hba_update (GduSection *_section)
{
        GduSectionHba *section = GDU_SECTION_HBA (_section);
        GduPresentable *p;
        GduAdapter *a;
        const gchar *vendor;
        const gchar *model;
        const gchar *driver;

        a = NULL;
        p = gdu_section_get_presentable (_section);

        a = gdu_hba_get_adapter (GDU_HBA (p));
        if (a == NULL)
                goto out;

        vendor = gdu_adapter_get_vendor (a);
        model = gdu_adapter_get_model (a);
        driver = gdu_adapter_get_driver (a);
        gdu_details_element_set_text (section->priv->vendor_element, vendor);
        gdu_details_element_set_text (section->priv->model_element, model);
        gdu_details_element_set_text (section->priv->driver_element, driver);


 out:
        if (a != NULL)
                g_object_unref (a);
}

/* ---------------------------------------------------------------------------------------------------- */

static void
gdu_section_hba_constructed (GObject *object)
{
        GduSectionHba *section = GDU_SECTION_HBA (object);
        GtkWidget *align;
        GtkWidget *label;
        GtkWidget *table;
        GtkWidget *vbox;
        gchar *s;
        GduPresentable *p;
        GduDevice *d;
        GPtrArray *elements;
        GduDetailsElement *element;

        p = gdu_section_get_presentable (GDU_SECTION (section));
        d = gdu_presentable_get_device (p);

        gtk_box_set_spacing (GTK_BOX (section), 12);

        /*------------------------------------- */

        label = gtk_label_new (NULL);
        gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
        s = g_strconcat ("<b>", _("Host Adapter"), "</b>", NULL);
        gtk_label_set_markup (GTK_LABEL (label), s);
        g_free (s);
        gtk_box_pack_start (GTK_BOX (section), label, FALSE, FALSE, 0);

        align = gtk_alignment_new (0.5, 0.5, 1.0, 1.0);
        gtk_alignment_set_padding (GTK_ALIGNMENT (align), 0, 0, 12, 0);
        gtk_box_pack_start (GTK_BOX (section), align, FALSE, FALSE, 0);

        vbox = gtk_vbox_new (FALSE, 6);
        gtk_container_add (GTK_CONTAINER (align), vbox);

        elements = g_ptr_array_new_with_free_func (g_object_unref);

        element = gdu_details_element_new (_("Vendor:"), NULL, NULL);
        g_ptr_array_add (elements, element);
        section->priv->vendor_element = element;

        element = gdu_details_element_new (_("Model:"), NULL, NULL);
        g_ptr_array_add (elements, element);
        section->priv->model_element = element;

        element = gdu_details_element_new (_("Driver:"), NULL, NULL);
        g_ptr_array_add (elements, element);
        section->priv->driver_element = element;

        table = gdu_details_table_new (1, elements);
        g_ptr_array_unref (elements);
        gtk_box_pack_start (GTK_BOX (vbox), table, FALSE, FALSE, 0);

        /* -------------------------------------------------------------------------------- */

        gtk_widget_show_all (GTK_WIDGET (section));

        if (d != NULL)
                g_object_unref (d);

        if (G_OBJECT_CLASS (gdu_section_hba_parent_class)->constructed != NULL)
                G_OBJECT_CLASS (gdu_section_hba_parent_class)->constructed (object);
}

static void
gdu_section_hba_class_init (GduSectionHbaClass *klass)
{
        GObjectClass *gobject_class;
        GduSectionClass *section_class;

        gobject_class = G_OBJECT_CLASS (klass);
        section_class = GDU_SECTION_CLASS (klass);

        gobject_class->finalize    = gdu_section_hba_finalize;
        gobject_class->constructed = gdu_section_hba_constructed;
        section_class->update      = gdu_section_hba_update;

        g_type_class_add_private (klass, sizeof (GduSectionHbaPrivate));
}

static void
gdu_section_hba_init (GduSectionHba *section)
{
        section->priv = G_TYPE_INSTANCE_GET_PRIVATE (section, GDU_TYPE_SECTION_HBA, GduSectionHbaPrivate);
}

GtkWidget *
gdu_section_hba_new (GduShell       *shell,
                     GduPresentable *presentable)
{
        return GTK_WIDGET (g_object_new (GDU_TYPE_SECTION_HBA,
                                         "shell", shell,
                                         "presentable", presentable,
                                         NULL));
}
