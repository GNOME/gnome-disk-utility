/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*- */

#include <glib/gi18n.h>

#include "gdu-grid-view.h"
#include "gdu-grid-element.h"
#include "gdu-grid-hbox.h"

struct GduGridViewPrivate
{
        GduPool *pool;
        GList *elements;

        /* GList of GduPresentable of the currently selected elements */
        GList *selected;
};

enum
{
        PROP_0,
        PROP_POOL,
};

enum
{
        SELECTION_CHANGED_SIGNAL,
        LAST_SIGNAL,
};

static guint signals[LAST_SIGNAL] = {0};

static void on_presentable_added   (GduPool        *pool,
                                    GduPresentable *presentable,
                                    GduGridView    *view);
static void on_presentable_removed (GduPool        *pool,
                                    GduPresentable *presentable,
                                    GduGridView    *view);
static void on_presentable_changed (GduPool        *pool,
                                    GduPresentable *presentable,
                                    GduGridView    *view);

G_DEFINE_TYPE (GduGridView, gdu_grid_view, GTK_TYPE_VBOX)

static void
gdu_grid_view_finalize (GObject *object)
{
        GduGridView *view = GDU_GRID_VIEW (object);

        g_list_foreach (view->priv->selected, (GFunc) g_object_unref, NULL);
        g_list_free (view->priv->selected);

        g_list_free (view->priv->elements);

        g_signal_handlers_disconnect_by_func (view->priv->pool, on_presentable_added, view);
        g_signal_handlers_disconnect_by_func (view->priv->pool, on_presentable_removed, view);
        g_signal_handlers_disconnect_by_func (view->priv->pool, on_presentable_changed, view);
        g_object_unref (view->priv->pool);

        if (G_OBJECT_CLASS (gdu_grid_view_parent_class)->finalize != NULL)
                G_OBJECT_CLASS (gdu_grid_view_parent_class)->finalize (object);
}

static void
gdu_grid_view_get_property (GObject    *object,
                            guint       property_id,
                            GValue     *value,
                            GParamSpec *pspec)
{
        GduGridView *view = GDU_GRID_VIEW (object);

        switch (property_id) {
        case PROP_POOL:
                g_value_set_object (value, view->priv->pool);
                break;

        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
        }
}

static void
gdu_grid_view_set_property (GObject      *object,
                            guint         property_id,
                            const GValue *value,
                            GParamSpec   *pspec)
{
        GduGridView *view = GDU_GRID_VIEW (object);

        switch (property_id) {
        case PROP_POOL:
                view->priv->pool = g_value_dup_object (value);
                break;

        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
        }
}

static gint
presentable_sort_offset (GduPresentable *a, GduPresentable *b)
{
        guint64 oa, ob;

        oa = gdu_presentable_get_offset (a);
        ob = gdu_presentable_get_offset (b);

        if (oa < ob)
                return -1;
        else if (oa > ob)
                return 1;
        else
                return 0;
}

static void
recompute_grid (GduGridView *view)
{
        GList *presentables;
        GList *l;
        GList *children;

        children = gtk_container_get_children (GTK_CONTAINER (view));
        for (l = children; l != NULL; l = l->next) {
                gtk_container_remove (GTK_CONTAINER (view), GTK_WIDGET (l->data));
        }
        g_list_free (children);

        g_list_free (view->priv->elements);
        view->priv->elements = NULL;

        presentables = gdu_pool_get_presentables (view->priv->pool);
        presentables = g_list_sort (presentables, (GCompareFunc) gdu_presentable_compare);
        for (l = presentables; l != NULL; l = l->next) {
                GduPresentable *p = GDU_PRESENTABLE (l->data);
                GtkWidget *element;
                GtkWidget *hbox;
                guint64 size;
                GList *enclosed_partitions;
                GList *ll;

                if (!GDU_IS_DRIVE (p))
                        continue;

                size = gdu_presentable_get_size (p);

                hbox = gdu_grid_hbox_new ();
                gtk_box_pack_start (GTK_BOX (view),
                                    hbox,
                                    FALSE,
                                    FALSE,
                                    0);

                enclosed_partitions = gdu_pool_get_enclosed_presentables (view->priv->pool, p);
                enclosed_partitions = g_list_sort (enclosed_partitions, (GCompareFunc) presentable_sort_offset);

                element = gdu_grid_element_new (view,
                                                p,
                                                180,
                                                0.0,
                                                GDU_GRID_ELEMENT_FLAGS_EDGE_TOP |
                                                GDU_GRID_ELEMENT_FLAGS_EDGE_BOTTOM |
                                                GDU_GRID_ELEMENT_FLAGS_EDGE_LEFT |
                                                (enclosed_partitions == NULL ? GDU_GRID_ELEMENT_FLAGS_EDGE_RIGHT : 0));
                view->priv->elements = g_list_prepend (view->priv->elements, element);
                gtk_box_pack_start (GTK_BOX (hbox),
                                    element,
                                    FALSE,
                                    FALSE,
                                    0);

                for (ll = enclosed_partitions; ll != NULL; ll = ll->next) {
                        GduPresentable *ep = GDU_PRESENTABLE (ll->data);
                        GList *enclosed_logical_partitions;
                        gboolean is_last;

                        is_last = (ll->next == NULL);

                        /* handle extended partitions */
                        enclosed_logical_partitions = gdu_pool_get_enclosed_presentables (view->priv->pool, ep);
                        if (enclosed_logical_partitions != NULL) {
                                GList *lll;
                                guint64 esize;
                                GtkWidget *vbox;
                                GtkWidget *hbox2;
                                guint num_logical_partitions;

                                num_logical_partitions = g_list_length (enclosed_logical_partitions);

                                vbox = gtk_vbox_new (TRUE, 0);
                                gtk_box_pack_start (GTK_BOX (hbox),
                                                    vbox,
                                                    FALSE,
                                                    FALSE,
                                                    0);

                                /* the extended partition */
                                esize = gdu_presentable_get_size (ep);
                                element = gdu_grid_element_new (view,
                                                                ep,
                                                                60 * num_logical_partitions,
                                                                ((gdouble) esize) / size,
                                                                GDU_GRID_ELEMENT_FLAGS_EDGE_TOP |
                                                                (is_last ? GDU_GRID_ELEMENT_FLAGS_EDGE_RIGHT : 0));
                                view->priv->elements = g_list_prepend (view->priv->elements, element);
                                gtk_box_pack_start (GTK_BOX (vbox),
                                                    element,
                                                    TRUE,
                                                    TRUE,
                                                    0);

                                /* hbox for logical partitions */
                                hbox2 = gdu_grid_hbox_new ();
                                gtk_box_pack_start (GTK_BOX (vbox),
                                                    hbox2,
                                                    TRUE,
                                                    TRUE,
                                                    0);

                                /* handle logical partitions in extended partition */
                                enclosed_logical_partitions = g_list_sort (enclosed_logical_partitions,
                                                                           (GCompareFunc) presentable_sort_offset);
                                for (lll = enclosed_logical_partitions; lll != NULL; lll = lll->next) {
                                        GduPresentable *lp = GDU_PRESENTABLE (lll->data);
                                        guint64 lsize;
                                        gboolean is_last_logical;

                                        is_last_logical = (is_last && (lll->next == NULL));

                                        lsize = gdu_presentable_get_size (lp);
                                        element = gdu_grid_element_new (view,
                                                                        lp,
                                                                        60,
                                                                        ((gdouble) lsize) / esize,
                                                                        GDU_GRID_ELEMENT_FLAGS_EDGE_BOTTOM |
                                                                        (is_last_logical ? GDU_GRID_ELEMENT_FLAGS_EDGE_RIGHT : 0));
                                        view->priv->elements = g_list_prepend (view->priv->elements, element);
                                        gtk_box_pack_start (GTK_BOX (hbox2),
                                                            element,
                                                            FALSE,
                                                            FALSE,
                                                            0);
                                }
                                g_list_foreach (enclosed_logical_partitions, (GFunc) g_object_unref, NULL);
                                g_list_free (enclosed_logical_partitions);
                        } else {
                                guint64 psize;

                                /* primary partition */
                                psize = gdu_presentable_get_size (ep);
                                element = gdu_grid_element_new (view,
                                                                ep,
                                                                60,
                                                                ((gdouble) psize) / size,
                                                                GDU_GRID_ELEMENT_FLAGS_EDGE_TOP |
                                                                GDU_GRID_ELEMENT_FLAGS_EDGE_BOTTOM |
                                                                (is_last ? GDU_GRID_ELEMENT_FLAGS_EDGE_RIGHT : 0));
                                view->priv->elements = g_list_prepend (view->priv->elements, element);
                                gtk_box_pack_start (GTK_BOX (hbox),
                                                    element,
                                                    FALSE,
                                                    FALSE,
                                                    0);
                        }

                }
                g_list_foreach (enclosed_partitions, (GFunc) g_object_unref, NULL);
                g_list_free (enclosed_partitions);

                gtk_widget_show_all (GTK_WIDGET (hbox));
        }
        g_list_foreach (presentables, (GFunc) g_object_unref, NULL);
        g_list_free (presentables);
}

static void
gdu_grid_view_constructed (GObject *object)
{
        GduGridView *view = GDU_GRID_VIEW (object);

        recompute_grid (view);

        g_signal_connect (view->priv->pool, "presentable-added", G_CALLBACK (on_presentable_added), view);
        g_signal_connect (view->priv->pool, "presentable-removed", G_CALLBACK (on_presentable_removed), view);
        g_signal_connect (view->priv->pool, "presentable-changed", G_CALLBACK (on_presentable_changed), view);

        if (G_OBJECT_CLASS (gdu_grid_view_parent_class)->constructed != NULL)
                G_OBJECT_CLASS (gdu_grid_view_parent_class)->constructed (object);
}

static void
gdu_grid_view_class_init (GduGridViewClass *klass)
{
        GObjectClass *object_class = G_OBJECT_CLASS (klass);

        g_type_class_add_private (klass, sizeof (GduGridViewPrivate));

        object_class->get_property = gdu_grid_view_get_property;
        object_class->set_property = gdu_grid_view_set_property;
        object_class->constructed  = gdu_grid_view_constructed;
        object_class->finalize     = gdu_grid_view_finalize;

        /**
         * GduGridView::selection-changed:
         * @view: The view emitting the signal.
         *
         * Emitted when the selection in @view chanes.
         **/
        signals[SELECTION_CHANGED_SIGNAL] = g_signal_new ("selection-changed",
                                                          GDU_TYPE_GRID_VIEW,
                                                          G_SIGNAL_RUN_LAST,
                                                          G_STRUCT_OFFSET (GduGridViewClass, selection_changed),
                                                          NULL,
                                                          NULL,
                                                          g_cclosure_marshal_VOID__VOID,
                                                          G_TYPE_NONE,
                                                          0);
        /**
         * GduGridView:pool:
         *
         * The pool of devices to show.
         */
        g_object_class_install_property (object_class,
                                         PROP_POOL,
                                         g_param_spec_object ("pool",
                                                              _("Pool"),
                                                              _("The pool of devices to show"),
                                                              GDU_TYPE_POOL,
                                                              G_PARAM_READABLE |
                                                              G_PARAM_WRITABLE |
                                                              G_PARAM_CONSTRUCT_ONLY));
}

static void
gdu_grid_view_init (GduGridView *view)
{
        view->priv = G_TYPE_INSTANCE_GET_PRIVATE (view, GDU_TYPE_GRID_VIEW, GduGridViewPrivate);
}

GtkWidget *
gdu_grid_view_new (GduPool *pool)
{
        return GTK_WIDGET (g_object_new (GDU_TYPE_GRID_VIEW,
                                         "pool", pool,
                                         NULL));
}

/* ---------------------------------------------------------------------------------------------------- */

/* TODO: It would be a lot more efficient to just add/remove elements as appropriate.. also,
 *       if we did that we wouldn't be screwing a11y users over
 */

static void
on_presentable_added (GduPool        *pool,
                      GduPresentable *presentable,
                      GduGridView    *view)
{
        recompute_grid (view);
}


static void
on_presentable_removed (GduPool        *pool,
                        GduPresentable *presentable,
                        GduGridView    *view)
{
        gdu_grid_view_selection_remove (view, presentable);
        recompute_grid (view);
}

static void
on_presentable_changed (GduPool        *pool,
                        GduPresentable *presentable,
                        GduGridView    *view)
{
}

/* ---------------------------------------------------------------------------------------------------- */

static GduGridElement *
get_element_for_presentable (GduGridView *view,
                             GduPresentable *presentable)
{
        GduGridElement *ret;
        GList *l;

        ret = NULL;

        for (l = view->priv->elements; l != NULL && ret == NULL; l = l->next) {
                GduGridElement *e = GDU_GRID_ELEMENT (l->data);
                GduPresentable *p;
                p = gdu_grid_element_get_presentable (e);
                if (p != NULL) {
                        if (p == presentable)
                                ret = e;
                        g_object_unref (p);
                }
        }

        return ret;
}

GList *
gdu_grid_view_selection_get (GduGridView *view)
{
        GList *ret;
        ret = g_list_copy (view->priv->selected);
        g_list_foreach (ret, (GFunc) g_object_ref, NULL);
        return ret;
}

void
gdu_grid_view_selection_add (GduGridView    *view,
                             GduPresentable *presentable)
{
        GduGridElement *e;

        g_return_if_fail (presentable != NULL);

        view->priv->selected = g_list_prepend (view->priv->selected, g_object_ref (presentable));

        e = get_element_for_presentable (view, presentable);
        if (e != NULL) {
                gtk_widget_queue_draw (GTK_WIDGET (e));
        }
        g_signal_emit (view, signals[SELECTION_CHANGED_SIGNAL], 0);
}

void
gdu_grid_view_selection_remove (GduGridView    *view,
                                GduPresentable *presentable)
{
        GList *l;
        for (l = view->priv->selected; l != NULL; l = l->next) {
                if (l->data == presentable) {
                        GduGridElement *e;
                        e = get_element_for_presentable (view, presentable);
                        if (e != NULL) {
                                gtk_widget_queue_draw (GTK_WIDGET (e));
                        }
                        view->priv->selected = g_list_remove (view->priv->selected, presentable);
                        g_object_unref (presentable);
                        g_signal_emit (view, signals[SELECTION_CHANGED_SIGNAL], 0);
                        break;
                }
        }
}

gboolean
gdu_grid_view_is_selected (GduGridView    *view,
                           GduPresentable *presentable)
{
        GList *l;
        gboolean ret;

        ret = FALSE;

        for (l = view->priv->selected; l != NULL; l = l->next) {
                if (l->data == presentable) {
                        ret = TRUE;
                        break;
                }
        }

        return ret;
}

void
gdu_grid_view_selection_clear (GduGridView *view)
{
        GList *l;
        gboolean changed;

        for (l = view->priv->selected; l != NULL; l = l->next) {
                GduGridElement *e;
                e = get_element_for_presentable (view, l->data);
                if (e != NULL) {
                        gtk_widget_queue_draw (GTK_WIDGET (e));
                }
        }

        changed = (view->priv->selected != NULL);

        g_list_foreach (view->priv->selected, (GFunc) g_object_unref, NULL);
        g_list_free (view->priv->selected);
        view->priv->selected = NULL;

        if (changed)
                g_signal_emit (view, signals[SELECTION_CHANGED_SIGNAL], 0);
}

/* ---------------------------------------------------------------------------------------------------- */
