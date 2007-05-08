/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*- */
/* gdu-main.c
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
#include <gnome.h>
#include <glib/gi18n.h>

#include <gnome-device-manager/gdm-pool.h>
#include <gnome-device-manager/gdm-info-provider.h>

#include "gdu-main.h"
#include "gdu-menus.h"
#include "gdu-device-tree.h"

static GdmPool *device_pool;
static GtkWidget *notebook;

static GtkWidget *summary_vbox;

static GtkUIManager *ui_manager;

static void
_remove_child (GtkWidget *widget, gpointer user_data)
{
        GtkContainer *container = GTK_CONTAINER (user_data);
        gtk_container_remove (container, widget);
}

static void
info_page_show_for_device (GdmDevice *device)
{
        GSList *i;
        int n;
        char *name;
        char *icon_name;
        GSList *kv_pairs;
        GSList *errors;
        GSList *warnings;
        GSList *notices;
        int num_pairs;
        static GtkTable *info_table = NULL;
        GtkWidget *hbox;
        GtkWidget *image;
        GtkWidget *label;
        GtkWidget *frame;
        GtkWidget *evbox;
        GtkWidget *notif_vbox;
        struct {
                const char *icon_name;
                GSList **source;
        } notifications[] =
          {
                  {GTK_STOCK_DIALOG_ERROR, &errors},
                  {GTK_STOCK_DIALOG_WARNING, &warnings},
                  {GTK_STOCK_DIALOG_INFO, &notices},
                  {NULL, NULL}                  
          };

        /* delete all old widgets */
        gtk_container_foreach (GTK_CONTAINER (summary_vbox),
                               _remove_child,
                               summary_vbox);

        name = gdm_info_provider_get_long_name (device);
        icon_name = gdm_info_provider_get_icon_name (device);
        kv_pairs = gdm_info_provider_get_summary (device);
        errors = gdm_info_provider_get_errors (device);
        warnings = gdm_info_provider_get_warnings (device);
        notices = gdm_info_provider_get_notices (device);

        /* draw notifications */
        notif_vbox = gtk_vbox_new (FALSE, -1);
        for (n = 0; notifications[n].source != NULL; n++) {
                const char *icon;
                GSList *source;
                icon = notifications[n].icon_name;
                source = *(notifications[n].source);                
                if (source != NULL) {
                        for (i = source; i != NULL; i = g_slist_next (i)) {
                                GdkColor border_color = {0, 0xb800, 0xad00, 0x9d00};
                                GdkColor fill_color = {0, 0xff00, 0xff00, 0xbf00};
                                GdmInfoProviderTip *tip = i->data;
                                
                                frame = gtk_frame_new (NULL);
                                evbox = gtk_event_box_new ();
                                gtk_widget_modify_bg (frame, GTK_STATE_NORMAL, &border_color);
                                gtk_widget_modify_bg (evbox, GTK_STATE_NORMAL, &fill_color);
                                gtk_frame_set_shadow_type (GTK_FRAME (frame), GTK_SHADOW_IN);
                                
                                hbox = gtk_hbox_new (FALSE, 5);
                                
                                image = gtk_image_new_from_stock (notifications[n].icon_name, GTK_ICON_SIZE_MENU);
                                
                                label = gtk_label_new (NULL);
                                gtk_label_set_markup (GTK_LABEL (label), tip->text);
                                gtk_label_set_ellipsize (GTK_LABEL (label), PANGO_ELLIPSIZE_END);
                                gtk_label_set_width_chars (GTK_LABEL (label), 50);
                                gtk_label_set_selectable (GTK_LABEL (label), TRUE);
                                gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
                                
                                gtk_box_pack_start (GTK_BOX (hbox), image, FALSE, FALSE, 0);
                                gtk_box_pack_start (GTK_BOX (hbox), label, FALSE, TRUE, 0);

                                if (tip->button_text != NULL) {
                                        GtkWidget *button;
                                        button = gtk_button_new_with_label (tip->button_text);
                                        gtk_box_pack_end (GTK_BOX (hbox), button, FALSE, FALSE, 0);
                                }
                                
                                gtk_container_set_border_width (GTK_CONTAINER (hbox), 2);
                                gtk_container_add (GTK_CONTAINER (evbox), hbox);
                                gtk_container_add (GTK_CONTAINER (frame), evbox);
                                gtk_box_pack_start (GTK_BOX (notif_vbox), frame, FALSE, FALSE, 0);

                                gdm_info_provider_tip_unref (tip);
                        }

                        g_slist_free (source);
                }
        }
        gtk_box_pack_start (GTK_BOX (summary_vbox), notif_vbox, FALSE, FALSE, 0);

        if (kv_pairs != NULL) {
                int num_rows;

                num_pairs = g_slist_length (kv_pairs) / 2;
                num_rows = (num_pairs + 1 ) /2;

                info_table = GTK_TABLE (gtk_table_new (num_rows, 4, FALSE));
                gtk_table_set_col_spacings (GTK_TABLE (info_table), 8);
                gtk_table_set_row_spacings (GTK_TABLE (info_table), 4);
                gtk_table_set_homogeneous (GTK_TABLE (info_table), TRUE);

                for (i = kv_pairs, n = 0; i != NULL; i = i->next, n++) {
                        char *key;
                        char *key2;
                        char *value;
                        GtkWidget *key_label;
                        GtkWidget *value_label;
                        int column;
                        int row;

                        row = n / 2;
                        column = (n & 1) * 2;
                        
                        key = i->data;
                        i = i->next;
                        if (i == NULL) {
                                g_free (key);
                                break;
                        }
                        value = i->data;

                        key2 = g_strdup_printf ("<b>%s:</b>", key);
                        
                        key_label = gtk_label_new (NULL);
                        gtk_label_set_markup (GTK_LABEL (key_label), key2);
                        gtk_misc_set_alignment (GTK_MISC (key_label), 1.0, 0.5);
                        
                        value_label = gtk_label_new (NULL);
                        gtk_label_set_markup (GTK_LABEL (value_label), value);
                        gtk_misc_set_alignment (GTK_MISC (value_label), 0.0, 0.5);
                        gtk_label_set_selectable (GTK_LABEL (value_label), TRUE);
                        gtk_label_set_ellipsize (GTK_LABEL (value_label), PANGO_ELLIPSIZE_END);
                        
                        gtk_table_attach_defaults (info_table, key_label,   column + 0, column + 1, row, row + 1);
                        gtk_table_attach_defaults (info_table, value_label, column + 1, column + 2, row, row + 1);

                        g_free (key);
                        g_free (key2);
                        g_free (value);
                }
                g_slist_free (kv_pairs);

                /* add property pairs */
                gtk_box_pack_start (GTK_BOX (summary_vbox), GTK_WIDGET (info_table), FALSE, FALSE, 0);
        }

        gtk_widget_show_all (summary_vbox);
}

static GdmDevice *now_showing = NULL;

static void
do_action (const char *action)
{
        if (now_showing != NULL) {
                char *cmdline;
                const char *udi;
                udi = gdm_device_get_udi (now_showing);

                cmdline = g_strdup_printf ("gnome-mount %s --hal-udi %s", action, udi);
                g_debug ("running '%s'", cmdline);
                g_spawn_command_line_async (cmdline, NULL);
                g_free (cmdline);
        }
}

void
mount_action_callback (GtkAction *action, gpointer data)
{
        g_debug ("mount action");
        do_action ("");
}

void
unmount_action_callback (GtkAction *action, gpointer data)
{
        g_debug ("unmount action");
        do_action ("--unmount");
}

void
eject_action_callback (GtkAction *action, gpointer data)
{
        g_debug ("eject action");
        do_action ("--eject");
}

static void
update_action_buttons (GdmDevice *device)
{
        GtkAction *a;
        gboolean can_mount = FALSE;
        gboolean can_unmount = FALSE;
        gboolean can_eject = FALSE;
        gboolean is_drv;
        gboolean is_vol;

        is_drv = gdm_device_test_capability (device, "storage");
        is_vol = gdm_device_test_capability (device, "volume");

        if (is_vol) {
                gboolean mounted;
                mounted = gdm_device_get_property_bool (device, "volume.is_mounted");
                if (mounted) {
                        can_unmount = TRUE;
                } else {
                        const char *fsusage;
                        fsusage = gdm_device_get_property_string (device, "volume.fsusage");
                        if (fsusage != NULL && g_ascii_strcasecmp (fsusage, "filesystem") == 0)
                                can_mount = TRUE;
                }
                /* TODO: should check storage.removable */
                can_eject = TRUE;                
        }

        if (is_drv) {
                gboolean removable;
                removable = gdm_device_get_property_bool (device, "storage.removable");
                if (removable)
                        can_eject = TRUE;
        }

        a = gtk_ui_manager_get_action (ui_manager, "/toolbar/mount");
        gtk_action_set_sensitive (a, can_mount);
        a = gtk_ui_manager_get_action (ui_manager, "/toolbar/unmount");
        gtk_action_set_sensitive (a, can_unmount);
        a = gtk_ui_manager_get_action (ui_manager, "/toolbar/eject");
        gtk_action_set_sensitive (a, can_eject);
}

static void
device_tree_changed (GtkTreeSelection *selection, gpointer user_data)
{
        GdmDevice *device;
        GtkTreeView *device_tree_view;

        device_tree_view = gtk_tree_selection_get_tree_view (selection);
        device = gdu_device_tree_get_selected_device (device_tree_view);

        if (device != NULL) {
                now_showing = device;
                info_page_show_for_device (device);
                update_action_buttons (device);
        }
}

static void
device_removed (GdmPool *pool, GdmDevice *device, gpointer user_data)
{
        GtkTreeView *treeview = GTK_TREE_VIEW (user_data);

        /* TODO FIX: if device we currently show is removed.. go to computer device */
        if (now_showing == device) {
                gdu_device_tree_select_device (GTK_TREE_VIEW (treeview), 
                                               gdm_pool_get_device_by_udi (device_pool, 
                                                                           "/org/freedesktop/Hal/devices/computer"));
        }
}

static void
device_property_changed (GdmPool *pool, GdmDevice *device, const char *key, gpointer user_data)
{
        //GtkTreeView *treeview = GTK_TREE_VIEW (user_data);
        if (device == now_showing) {
                info_page_show_for_device (device);
                update_action_buttons (device);
        }
}

static GtkWidget *
create_window (const gchar * geometry)
{
        GtkWidget *app;
        GtkWidget *vbox;
        GtkWidget *menubar;
        GtkWidget *toolbar;
        GtkAccelGroup *accel_group;
        GtkWidget *hpane;
        GtkWidget *treeview_scrolled_window;
        GtkWidget *treeview;
        GtkWidget *tab_summary_label;
        GtkTreeSelection *select;

        app = NULL;

        gdm_info_provider_register_builtin ();

        device_pool = gdm_pool_new ();
        if (device_pool == NULL) {
                goto out;
        }

        app = gtk_window_new (GTK_WINDOW_TOPLEVEL);
        gtk_window_set_resizable (GTK_WINDOW (app), TRUE);
        gtk_window_set_default_size (GTK_WINDOW (app), 750, 550);
        gtk_window_set_title (GTK_WINDOW (app), _("Disk Utility"));

        vbox = gtk_vbox_new (FALSE, 0);
        gtk_container_add (GTK_CONTAINER (app), vbox);

        ui_manager = gdu_create_ui_manager ("GnomeDiskUtilityActions", app);
        accel_group = gtk_ui_manager_get_accel_group (ui_manager);
        gtk_window_add_accel_group (GTK_WINDOW (app), accel_group);

        menubar = gtk_ui_manager_get_widget (ui_manager, "/menubar");
        gtk_box_pack_start (GTK_BOX (vbox), menubar, FALSE, FALSE, 0);
        toolbar = gtk_ui_manager_get_widget (ui_manager, "/toolbar");
        gtk_box_pack_start (GTK_BOX (vbox), toolbar, FALSE, FALSE, 0);

        /* tree view */
        treeview_scrolled_window = gtk_scrolled_window_new (NULL, NULL);
        gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (treeview_scrolled_window),
                                        GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
        treeview = GTK_WIDGET (gdu_device_tree_new (device_pool));
        gtk_container_add (GTK_CONTAINER (treeview_scrolled_window), treeview);

        /* notebook */
        notebook = gtk_notebook_new ();

        /* summary pane */
        summary_vbox = gtk_vbox_new (FALSE, 10);
        gtk_container_set_border_width (GTK_CONTAINER (summary_vbox), 8);
        tab_summary_label = gtk_label_new (_("Summary"));

        /* setup and add horizontal pane */
        hpane = gtk_hpaned_new ();
        gtk_paned_add1 (GTK_PANED (hpane), treeview_scrolled_window);
        gtk_paned_add2 (GTK_PANED (hpane), notebook);
        gtk_paned_set_position (GTK_PANED (hpane), 260);

        gtk_box_pack_start (GTK_BOX (vbox), hpane, TRUE, TRUE, 0);
        gtk_box_pack_start (GTK_BOX (vbox), summary_vbox, FALSE, TRUE, 0);

        /* geometry */
        if (geometry != NULL) {
                if (!gtk_window_parse_geometry
                    (GTK_WINDOW (app), geometry)) {
                        g_error (_("Could not parse geometry string `%s'"),
                                 geometry);
                }
        }

        select = gtk_tree_view_get_selection (GTK_TREE_VIEW (treeview));
        gtk_tree_selection_set_mode (select, GTK_SELECTION_SINGLE);
        g_signal_connect (select, "changed", (GCallback) device_tree_changed, NULL);

        /* when starting up, set focus to computer entry */
        gtk_widget_grab_focus (treeview);
        gdu_device_tree_select_device (GTK_TREE_VIEW (treeview), 
                                       gdm_pool_get_device_by_udi (device_pool, 
                                                                   "/org/freedesktop/Hal/devices/computer"));
        
        g_signal_connect (device_pool, "device_removed", (GCallback) device_removed, treeview);
        g_signal_connect (device_pool, "device_property_changed", (GCallback) device_property_changed, treeview);

        g_signal_connect (app, "delete-event", gtk_main_quit, NULL);

        gtk_widget_show_all (vbox);
out:
        return app;
}

static void session_die (GnomeClient * client, gpointer client_data);
static gint save_session (GnomeClient * client, gint phase,
                          GnomeSaveStyle save_style,
                          gint is_shutdown,
                          GnomeInteractStyle interact_style, gint is_fast,
                          gpointer client_data);

static char *geometry = NULL;
static char **args = NULL;

static GOptionEntry option_entries[] = {
        {
                "geometry",
                0,
                0,
                G_OPTION_ARG_STRING,
                &geometry,
                N_("Specify the geometry of the main window"),
                N_("GEOMETRY")
        },
        {
                G_OPTION_REMAINING,
                0,
                0,
                G_OPTION_ARG_STRING_ARRAY,
                &args,
                NULL,
                NULL}
};

int
main (int argc, char **argv)
{
        GtkWidget *window;
        GOptionContext *context;
        GnomeProgram *program;
        GnomeClient *client;
        
        bindtextdomain (GETTEXT_PACKAGE, GNOMELOCALEDIR);
        bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
        textdomain (GETTEXT_PACKAGE);
        
        context = g_option_context_new (_("- GNOME Device Manager"));
        g_option_context_add_main_entries (context, option_entries,
                                           GETTEXT_PACKAGE);
        
        program = gnome_program_init (PACKAGE, VERSION, LIBGNOMEUI_MODULE,
                                      argc, argv,
                                      GNOME_PARAM_GOPTION_CONTEXT, context,
                                      GNOME_PARAM_APP_DATADIR, DATADIR,
                                      NULL);

        gtk_window_set_default_icon_name ("gnome-disk-utility");

        client = gnome_master_client ();
        g_signal_connect (client, "save_yourself",
                          G_CALLBACK (save_session), argv[0]);
        g_signal_connect (client, "die", G_CALLBACK (session_die), NULL);

        window = create_window (geometry);

        gtk_widget_show_all (window);
        gtk_main ();
        g_object_unref (program);
        return 0;
}

static gint
save_session (GnomeClient * client,
              gint phase,
              GnomeSaveStyle save_style,
              gint is_shutdown,
              GnomeInteractStyle interact_style,
              gint is_fast, gpointer client_data)
{
        gchar **argv;
        guint argc;

        argv = g_new0 (gchar *, 4);
        argc = 0;

        argv[argc++] = client_data;

        argv[argc] = NULL;

        gnome_client_set_clone_command (client, argc, argv);
        gnome_client_set_restart_command (client, argc, argv);

        return TRUE;
}

static void
session_die (GnomeClient * client, gpointer client_data)
{
        gtk_main_quit ();
}
