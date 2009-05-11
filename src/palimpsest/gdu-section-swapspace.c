/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*- */
/* gdu-section-swapspace.c
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
#include "gdu-section-swapspace.h"

struct _GduSectionSwapspacePrivate
{
        gint foo;
};

static GObjectClass *parent_class = NULL;

G_DEFINE_TYPE (GduSectionSwapspace, gdu_section_swapspace, GDU_TYPE_SECTION)

/* ---------------------------------------------------------------------------------------------------- */

static void
update (GduSectionSwapspace *section)
{
}

/* ---------------------------------------------------------------------------------------------------- */

static void
gdu_section_swapspace_finalize (GduSectionSwapspace *section)
{
        if (G_OBJECT_CLASS (parent_class)->finalize)
                (* G_OBJECT_CLASS (parent_class)->finalize) (G_OBJECT (section));
}

static void
gdu_section_swapspace_class_init (GduSectionSwapspaceClass *klass)
{
        GObjectClass *obj_class = (GObjectClass *) klass;
        GduSectionClass *section_class = (GduSectionClass *) klass;

        parent_class = g_type_class_peek_parent (klass);

        obj_class->finalize = (GObjectFinalizeFunc) gdu_section_swapspace_finalize;
        section_class->update = (gpointer) update;

        g_type_class_add_private (klass, sizeof (GduSectionSwapspacePrivate));
}

static void
gdu_section_swapspace_init (GduSectionSwapspace *section)
{
        GtkWidget *vbox2;
        GtkWidget *label;
        GtkWidget *align;
        gchar *s;

        section->priv = G_TYPE_INSTANCE_GET_PRIVATE (section, GDU_TYPE_SECTION_SWAPSPACE, GduSectionSwapspacePrivate);

        label = gtk_label_new (NULL);
        s = g_strconcat ("<b>", _("Swap Space"), "</b>", NULL);
        gtk_label_set_markup (GTK_LABEL (label), s);
        g_free (s);
        gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
        gtk_box_pack_start (GTK_BOX (section), label, FALSE, FALSE, 6);
        vbox2 = gtk_vbox_new (FALSE, 5);
        align = gtk_alignment_new (0.5, 0.5, 1.0, 1.0);
        gtk_alignment_set_padding (GTK_ALIGNMENT (align), 0, 0, 12, 0);
        gtk_container_add (GTK_CONTAINER (align), vbox2);
        gtk_box_pack_start (GTK_BOX (section), align, FALSE, TRUE, 0);

        /* explanatory text */
        label = gtk_label_new (NULL);
        gtk_label_set_markup (GTK_LABEL (label), _("The volume contains swap space."));
        gtk_label_set_width_chars (GTK_LABEL (label), 60);
        gtk_label_set_line_wrap (GTK_LABEL (label), TRUE);
        gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
        gtk_box_pack_start (GTK_BOX (vbox2), label, FALSE, TRUE, 0);
}

GtkWidget *
gdu_section_swapspace_new (GduShell       *shell,
                           GduPresentable *presentable)
{
        return GTK_WIDGET (g_object_new (GDU_TYPE_SECTION_SWAPSPACE,
                                         "shell", shell,
                                         "presentable", presentable,
                                         NULL));
}
