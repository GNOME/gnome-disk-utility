/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*- */
/* gdu-page-summary.c
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

#include <config.h>
#include <glib-object.h>
#include <string.h>
#include <glib/gi18n.h>
#include <polkit-gnome/polkit-gnome.h>

#include "gdu-page.h"
#include "gdu-page-summary.h"
#include "gdu-util.h"

struct _GduPageSummaryPrivate
{
        GduShell *shell;

        GtkWidget *main_vbox;
        GList *table_labels;
};

static GObjectClass *parent_class = NULL;

static void gdu_page_summary_page_iface_init (GduPageIface *iface);
G_DEFINE_TYPE_WITH_CODE (GduPageSummary, gdu_page_summary, G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (GDU_TYPE_PAGE,
                                                gdu_page_summary_page_iface_init))

enum {
        PROP_0,
        PROP_SHELL,
};

static void
gdu_page_summary_finalize (GduPageSummary *page)
{
        if (page->priv->shell != NULL)
                g_object_unref (page->priv->shell);

        if (G_OBJECT_CLASS (parent_class)->finalize)
                (* G_OBJECT_CLASS (parent_class)->finalize) (G_OBJECT (page));
}

static void
gdu_page_summary_set_property (GObject      *object,
                             guint         prop_id,
                             const GValue *value,
                             GParamSpec   *pspec)
{
        GduPageSummary *page = GDU_PAGE_SUMMARY (object);

        switch (prop_id) {
        case PROP_SHELL:
                if (page->priv->shell != NULL)
                        g_object_unref (page->priv->shell);
                page->priv->shell = g_object_ref (g_value_get_object (value));
                break;

        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static void
gdu_page_summary_get_property (GObject     *object,
                             guint        prop_id,
                             GValue      *value,
                             GParamSpec  *pspec)
{
        GduPageSummary *page = GDU_PAGE_SUMMARY (object);

        switch (prop_id) {
        case PROP_SHELL:
                g_value_set_object (value, page->priv->shell);
                break;

        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
    }
}

static void
gdu_page_summary_class_init (GduPageSummaryClass *klass)
{
        GObjectClass *obj_class = (GObjectClass *) klass;

        parent_class = g_type_class_peek_parent (klass);

        obj_class->finalize = (GObjectFinalizeFunc) gdu_page_summary_finalize;
        obj_class->set_property = gdu_page_summary_set_property;
        obj_class->get_property = gdu_page_summary_get_property;

        /**
         * GduPageSummary:shell:
         *
         * The #GduShell instance hosting this page.
         */
        g_object_class_install_property (obj_class,
                                         PROP_SHELL,
                                         g_param_spec_object ("shell",
                                                              NULL,
                                                              NULL,
                                                              GDU_TYPE_SHELL,
                                                              G_PARAM_CONSTRUCT_ONLY |
                                                              G_PARAM_WRITABLE |
                                                              G_PARAM_READABLE));
}

static void
gdu_page_summary_init (GduPageSummary *page)
{
        int row;
        GtkWidget *table;

        page->priv = g_new0 (GduPageSummaryPrivate, 1);

        page->priv->main_vbox = gtk_vbox_new (FALSE, 10);
        gtk_container_set_border_width (GTK_CONTAINER (page->priv->main_vbox), 8);

        page->priv->table_labels = NULL;

        table = gtk_table_new (10, 2, FALSE);
        gtk_table_set_col_spacings (GTK_TABLE (table), 8);
        gtk_table_set_row_spacings (GTK_TABLE (table), 4);
        for (row = 0; row < 10; row++) {
                GtkWidget *key_label;
                GtkWidget *value_label;

                key_label = gtk_label_new (NULL);
                gtk_misc_set_alignment (GTK_MISC (key_label), 1.0, 0.5);

                value_label = gtk_label_new (NULL);
                gtk_misc_set_alignment (GTK_MISC (value_label), 0.0, 0.5);
                gtk_label_set_selectable (GTK_LABEL (value_label), TRUE);
                gtk_label_set_ellipsize (GTK_LABEL (value_label), PANGO_ELLIPSIZE_END);

                gtk_table_attach (GTK_TABLE (table), key_label,   0, 1, row, row + 1,
                                  GTK_FILL, GTK_EXPAND | GTK_FILL, 0, 0);
                gtk_table_attach (GTK_TABLE (table), value_label, 1, 2, row, row + 1,
                                  GTK_EXPAND | GTK_FILL, GTK_EXPAND | GTK_FILL, 0, 0);

                page->priv->table_labels = g_list_append (page->priv->table_labels, key_label);
                page->priv->table_labels = g_list_append (page->priv->table_labels, value_label);
        }
        gtk_box_pack_start (GTK_BOX (page->priv->main_vbox), table, FALSE, FALSE, 0);
}


GduPageSummary *
gdu_page_summary_new (GduShell *shell)
{
        return GDU_PAGE_SUMMARY (g_object_new (GDU_TYPE_PAGE_SUMMARY, "shell", shell, NULL));
}

static gboolean
gdu_page_summary_update (GduPage *_page, GduPresentable *presentable)
{
        GList *i;
        GList *j;
        GList *kv_pairs;
        GduPageSummary *page = GDU_PAGE_SUMMARY (_page);

        /* update key/value pairs on summary page */
        kv_pairs = gdu_presentable_get_info (presentable);
        for (i = kv_pairs, j = page->priv->table_labels; i != NULL && j != NULL; i = i->next, j = j->next) {
                char *key;
                char *key2;
                char *value;
                GtkWidget *key_label;
                GtkWidget *value_label;

                key = i->data;
                key_label = j->data;
                i = i->next;
                j = j->next;
                if (i == NULL || j == NULL) {
                        g_free (key);
                        break;
                }
                value = i->data;
                value_label = j->data;

                key2 = g_strdup_printf ("<b>%s:</b>", key);
                gtk_label_set_markup (GTK_LABEL (key_label), key2);
                gtk_label_set_markup (GTK_LABEL (value_label), value);
                g_free (key2);
        }
        g_list_foreach (kv_pairs, (GFunc) g_free, NULL);
        g_list_free (kv_pairs);

        /* clear remaining labels */
        for ( ; j != NULL; j = j->next) {
                GtkWidget *label = j->data;
                gtk_label_set_markup (GTK_LABEL (label), "");
        }

        return TRUE;
}

static GtkWidget *
gdu_page_summary_get_widget (GduPage *_page)
{
        GduPageSummary *page = GDU_PAGE_SUMMARY (_page);
        return page->priv->main_vbox;
}

static char *
gdu_page_summary_get_name (GduPage *page)
{
        return g_strdup (_("Summary"));
}

static void
gdu_page_summary_page_iface_init (GduPageIface *iface)
{
        iface->get_widget = gdu_page_summary_get_widget;
        iface->get_name = gdu_page_summary_get_name;
        iface->update = gdu_page_summary_update;
}
