/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*- */

#include <gtk/gtk.h>
#include <gdu/gdu.h>

#include "gdu-grid-view.h"

int
main (int argc, char *argv[])
{
        GduPool *pool;
        GtkWidget *window;
        GtkWidget *vbox;
        GtkWidget *scrolled_window;
        GtkWidget *grid_view;

        gtk_init (&argc, &argv);

        pool = gdu_pool_new ();

        window = gtk_window_new (GTK_WINDOW_TOPLEVEL);

        vbox = gtk_vbox_new (FALSE, 0);
        gtk_container_add (GTK_CONTAINER (window), vbox);

        grid_view = gdu_grid_view_new (pool);

        scrolled_window = gtk_scrolled_window_new (NULL, NULL);
        gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scrolled_window),
                                        GTK_POLICY_NEVER,
                                        GTK_POLICY_AUTOMATIC);
        gtk_scrolled_window_add_with_viewport (GTK_SCROLLED_WINDOW (scrolled_window), grid_view);
        gtk_box_pack_start (GTK_BOX (vbox),
                            scrolled_window,
                            TRUE,
                            TRUE,
                            0);

        /* add a dummy button box for now.. just to test focus */
        GtkWidget *button_box;
        button_box = gtk_hbutton_box_new ();
        gtk_container_add (GTK_CONTAINER (button_box), gtk_button_new_from_stock (GTK_STOCK_OK));
        gtk_container_add (GTK_CONTAINER (button_box), gtk_button_new_from_stock (GTK_STOCK_APPLY));
        gtk_container_add (GTK_CONTAINER (button_box), gtk_button_new_from_stock (GTK_STOCK_CANCEL));
        gtk_box_pack_start (GTK_BOX (vbox),
                            button_box,
                            FALSE,
                            FALSE,
                            0);

        gtk_window_set_default_size (GTK_WINDOW (window), 600, 400);
        gtk_widget_show_all (window);
        gtk_main ();

        g_object_unref (pool);

        return 0;
}
