/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*- */

#include <glib/gi18n.h>

#include "gdu-grid-view.h"
#include "gdu-grid-details.h"

#define NUM_ROWS 6

struct GduGridDetailsPrivate
{
        GduGridView *view;

        GtkWidget *table_key_label[NUM_ROWS];
        GtkWidget *table_value_label[NUM_ROWS];
};

enum
{
        PROP_0,
        PROP_VIEW,
};

G_DEFINE_TYPE (GduGridDetails, gdu_grid_details, GTK_TYPE_VBOX)

static void on_selection_changed (GduGridView *view, gpointer user_data);

static void
gdu_grid_details_finalize (GObject *object)
{
        GduGridDetails *details = GDU_GRID_DETAILS (object);

        g_signal_handlers_disconnect_by_func (details->priv->view, on_selection_changed, details);
        g_object_unref (details->priv->view);

        if (G_OBJECT_CLASS (gdu_grid_details_parent_class)->finalize != NULL)
                G_OBJECT_CLASS (gdu_grid_details_parent_class)->finalize (object);
}

static void
gdu_grid_details_get_property (GObject    *object,
                            guint       property_id,
                            GValue     *value,
                            GParamSpec *pspec)
{
        GduGridDetails *details = GDU_GRID_DETAILS (object);

        switch (property_id) {
        case PROP_VIEW:
                g_value_set_object (value, details->priv->view);
                break;

        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
        }
}

static void
gdu_grid_details_set_property (GObject      *object,
                            guint         property_id,
                            const GValue *value,
                            GParamSpec   *pspec)
{
        GduGridDetails *details = GDU_GRID_DETAILS (object);

        switch (property_id) {
        case PROP_VIEW:
                details->priv->view = g_value_dup_object (value);
                break;

        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
        }
}

static void
gdu_grid_details_constructed (GObject *object)
{
        GduGridDetails *details = GDU_GRID_DETAILS (object);
        GtkWidget *hbox;
        GtkWidget *table;
        GtkWidget *label;
        guint row;

        hbox = gtk_hbox_new (TRUE, 12);

        table = gtk_table_new (NUM_ROWS, 2, TRUE);
        gtk_table_set_col_spacings (GTK_TABLE (table), 12);
        gtk_box_pack_start (GTK_BOX (hbox), table, TRUE, TRUE, 0);

        for (row = 0; row < NUM_ROWS; row++) {
                label = gtk_label_new (NULL);
                gtk_misc_set_alignment (GTK_MISC (label), 1.0, 0.5);
                gtk_table_attach (GTK_TABLE (table), label, 0, 1, row, row + 1,
                                  GTK_EXPAND | GTK_FILL, GTK_EXPAND | GTK_FILL, 2, 2);
                details->priv->table_key_label[row] = label;

                label = gtk_label_new (NULL);
                gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
                gtk_table_attach (GTK_TABLE (table), label, 1, 2, row, row + 1,
                                  GTK_EXPAND | GTK_FILL, GTK_EXPAND | GTK_FILL, 2, 2);
                details->priv->table_value_label[row] = label;
        }

        label = gtk_label_new ("Operations should go here");
        gtk_box_pack_start (GTK_BOX (hbox), label, TRUE, TRUE, 0);

        gtk_container_add (GTK_CONTAINER (details), hbox);

        g_signal_connect (details->priv->view,
                          "selection-changed",
                          G_CALLBACK (on_selection_changed),
                          details);

        if (G_OBJECT_CLASS (gdu_grid_details_parent_class)->constructed != NULL)
                G_OBJECT_CLASS (gdu_grid_details_parent_class)->constructed (object);
}

static void
gdu_grid_details_class_init (GduGridDetailsClass *klass)
{
        GObjectClass *object_class = G_OBJECT_CLASS (klass);

        g_type_class_add_private (klass, sizeof (GduGridDetailsPrivate));

        object_class->get_property = gdu_grid_details_get_property;
        object_class->set_property = gdu_grid_details_set_property;
        object_class->constructed  = gdu_grid_details_constructed;
        object_class->finalize     = gdu_grid_details_finalize;

        g_object_class_install_property (object_class,
                                         PROP_VIEW,
                                         g_param_spec_object ("view",
                                                              _("View"),
                                                              _("The view to show details for"),
                                                              GDU_TYPE_GRID_VIEW,
                                                              G_PARAM_READABLE |
                                                              G_PARAM_WRITABLE |
                                                              G_PARAM_CONSTRUCT_ONLY));
}

static void
gdu_grid_details_init (GduGridDetails *details)
{
        details->priv = G_TYPE_INSTANCE_GET_PRIVATE (details, GDU_TYPE_GRID_DETAILS, GduGridDetailsPrivate);
}

GtkWidget *
gdu_grid_details_new (GduGridView *view)
{
        return GTK_WIDGET (g_object_new (GDU_TYPE_GRID_DETAILS,
                                         "view", view,
                                         NULL));
}

static void
set_kv (GduGridDetails *details,
        guint           row,
        const gchar    *key,
        const gchar    *value)
{
        gchar *s;

        g_return_if_fail (row < NUM_ROWS);

        s = g_strdup_printf ("<b>%s</b>", key);
        gtk_label_set_markup (GTK_LABEL (details->priv->table_key_label[row]), s);
        g_free (s);

        gtk_label_set_markup (GTK_LABEL (details->priv->table_value_label[row]), value);
}

static void
on_selection_changed (GduGridView *view,
                      gpointer user_data)
{
        GduGridDetails *details = GDU_GRID_DETAILS (user_data);
        GList *selection;
        guint n;

        for (n = 0; n < NUM_ROWS; n++) {
                gtk_label_set_text (GTK_LABEL (details->priv->table_key_label[n]), "");
                gtk_label_set_text (GTK_LABEL (details->priv->table_value_label[n]), "");
        }

        selection = gdu_grid_view_selection_get (view);

        g_debug ("selection has %d items", g_list_length (selection));
        if (g_list_length (selection) == 1) {
                GduPresentable *p = GDU_PRESENTABLE (selection->data);
                GduDevice *d;

                d = gdu_presentable_get_device (p);

                if (GDU_IS_VOLUME (p) && d != NULL) {
                        set_kv (details, 0, _("Device:"), gdu_device_get_device_file (d));
                }

                if (d != NULL)
                        g_object_unref (d);
        }

        g_list_foreach (selection, (GFunc) g_object_unref, NULL);
        g_list_free (selection);
}
