/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*- */

#include <glib/gi18n.h>

#include "gdu-grid-view.h"
#include "gdu-grid-details.h"

struct GduGridDetailsPrivate
{
        GduGridView *view;

        GtkWidget *notebook;
};

enum
{
        PROP_0,
        PROP_VIEW,
};

G_DEFINE_TYPE (GduGridDetails, gdu_grid_details, GTK_TYPE_VBOX)

static void
gdu_grid_details_finalize (GObject *object)
{
        GduGridDetails *details = GDU_GRID_DETAILS (object);

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
        GtkWidget *notebook;
        GtkWidget *no_media_page;
        GtkWidget *label;

        no_media_page = gtk_alignment_new (0.5, 0.5, 0, 0);
        label = gtk_label_new (_("No media detected"));
        gtk_container_add (GTK_CONTAINER (no_media_page), label);

        notebook = gtk_notebook_new ();
        gtk_notebook_append_page (GTK_NOTEBOOK (notebook),
                                  no_media_page,
                                  NULL);
        details->priv->notebook = notebook;

        gtk_container_add (GTK_CONTAINER (details), notebook);

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
