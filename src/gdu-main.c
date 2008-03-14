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
#include <string.h>
#include <glib/gi18n.h>

#include "gdu-pool.h"
#include "gdu-main.h"
#include "gdu-menus.h"
#include "gdu-tree.h"

static GduPool *pool;
static GtkWidget *notebook;

static GList *table_labels = NULL;

static GtkUIManager *ui_manager;

static GduPresentable *presentable_now_showing = NULL;

static void
info_page_show_for_presentable (GduPresentable *presentable)
{
        GList *i;
        GList *j;
        GList *kv_pairs;

        kv_pairs = gdu_presentable_get_info (presentable);
        for (i = kv_pairs, j = table_labels; i != NULL && j != NULL; i = i->next, j = j->next) {
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
}

static void
do_action (const char *action)
{
#if 0
        if (presentable_now_showing != NULL) {
                char *cmdline;
                const char *object_path;
                object_path = gdu_device_get_object_path (presentable_now_showing);

                cmdline = g_strdup_printf ("gnome-mount %s --hal-udi %s", action, udi);
                g_debug ("running '%s'", cmdline);
                g_spawn_command_line_async (cmdline, NULL);
                g_free (cmdline);
        }
#endif
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
update_action_buttons (GduPresentable *presentable)
{
        GtkAction *a;
        gboolean can_mount = FALSE;
        gboolean can_unmount = FALSE;
        gboolean can_eject = FALSE;

#if 0
        gboolean is_drv;
        gboolean is_vol;

        is_drv = gdu_device_test_capability (device, "storage");
        is_vol = gdu_device_test_capability (device, "volume");

        if (is_vol) {
                gboolean mounted;
                mounted = gdu_device_get_property_bool (device, "volume.is_mounted");
                if (mounted) {
                        can_unmount = TRUE;
                } else {
                        const char *fsusage;
                        fsusage = gdu_device_get_property_string (device, "volume.fsusage");
                        if (fsusage != NULL && strcmp (fsusage, "filesystem") == 0)
                                can_mount = TRUE;
                }
                /* TODO: should check storage.removable */
                can_eject = TRUE;
        }

        if (is_drv) {
                gboolean removable;
                removable = gdu_device_get_property_bool (device, "storage.removable");
                if (removable)
                        can_eject = TRUE;
        }
#endif

        a = gtk_ui_manager_get_action (ui_manager, "/toolbar/mount");
        gtk_action_set_sensitive (a, can_mount);
        a = gtk_ui_manager_get_action (ui_manager, "/toolbar/unmount");
        gtk_action_set_sensitive (a, can_unmount);
        a = gtk_ui_manager_get_action (ui_manager, "/toolbar/eject");
        gtk_action_set_sensitive (a, can_eject);
}

static void
presentable_changed (GduPresentable *presentable, gpointer user_data)
{
        info_page_show_for_presentable (presentable);
        update_action_buttons (presentable);
}

static void
device_tree_changed (GtkTreeSelection *selection, gpointer user_data)
{
        GduPresentable *presentable;
        GtkTreeView *device_tree_view;

        device_tree_view = gtk_tree_selection_get_tree_view (selection);
        presentable = gdu_tree_get_selected_presentable (device_tree_view);

        if (presentable != NULL) {

                if (presentable_now_showing != NULL) {
                        g_signal_handlers_disconnect_by_func (presentable_now_showing,
                                                              (GCallback) presentable_changed,
                                                              NULL);
                }

                presentable_now_showing = presentable;

                g_signal_connect (presentable_now_showing, "changed", (GCallback) presentable_changed, NULL);

                info_page_show_for_presentable (presentable_now_showing);
                update_action_buttons (presentable_now_showing);
        }
}

static void
presentable_removed (GduPool *pool, GduPresentable *presentable, gpointer user_data)
{
        //GtkTreeView *treeview = GTK_TREE_VIEW (user_data);
        /* TODO FIX: if presentable we currently show is removed.. go to computer presentable */
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
        GtkWidget *summary_vbox;
        int row, column;
        GtkTable *info_table;

        app = NULL;

        pool = gdu_pool_new ();
        if (pool == NULL) {
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
        treeview = GTK_WIDGET (gdu_tree_new (pool));
        gtk_container_add (GTK_CONTAINER (treeview_scrolled_window), treeview);

        /* notebook */
        notebook = gtk_notebook_new ();

        /* summary pane */
        summary_vbox = gtk_vbox_new (FALSE, 10);
        gtk_container_set_border_width (GTK_CONTAINER (summary_vbox), 8);
        tab_summary_label = gtk_label_new (_("Summary"));

        info_table = GTK_TABLE (gtk_table_new (5, 2 * 2, FALSE));
        gtk_table_set_col_spacings (GTK_TABLE (info_table), 8);
        gtk_table_set_row_spacings (GTK_TABLE (info_table), 4);
        gtk_table_set_homogeneous (GTK_TABLE (info_table), TRUE);
        table_labels = NULL;
        for (row = 0; row < 5; row++) {
                for (column = 0; column < 2; column++) {
                        GtkWidget *key_label;
                        GtkWidget *value_label;

                        key_label = gtk_label_new (NULL);
                        gtk_misc_set_alignment (GTK_MISC (key_label), 1.0, 0.5);

                        value_label = gtk_label_new (NULL);
                        gtk_misc_set_alignment (GTK_MISC (value_label), 0.0, 0.5);
                        gtk_label_set_selectable (GTK_LABEL (value_label), TRUE);
                        gtk_label_set_ellipsize (GTK_LABEL (value_label), PANGO_ELLIPSIZE_END);

                        gtk_table_attach_defaults (info_table, key_label,   column*2 + 0, column*2 + 1, row, row + 1);
                        gtk_table_attach_defaults (info_table, value_label, column*2 + 1, column*2 + 2, row, row + 1);

                        table_labels = g_list_append (table_labels, key_label);
                        table_labels = g_list_append (table_labels, value_label);
                }
        }
        gtk_box_pack_start (GTK_BOX (summary_vbox), GTK_WIDGET (info_table), FALSE, FALSE, 0);
        gtk_widget_show_all (summary_vbox);


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
#if 0
        gdu_tree_select_device (GTK_TREE_VIEW (treeview),
                                gdu_pool_get_device_by_udi (pool,
                                                            "/org/freedesktop/Hal/devices/computer"));
#endif

        g_signal_connect (pool, "presentable_removed", (GCallback) presentable_removed, treeview);

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

#define KILOBYTE_FACTOR 1024.0
#define MEGABYTE_FACTOR (1024.0 * 1024.0)
#define GIGABYTE_FACTOR (1024.0 * 1024.0 * 1024.0)

char *
gdu_util_get_size_for_display (guint64 size, gboolean long_string)
{
        char *str;
        gdouble displayed_size;

        if (size < MEGABYTE_FACTOR) {
                displayed_size = (double) size / KILOBYTE_FACTOR;
                if (long_string)
                        str = g_strdup_printf (_("%.1f KB (%'lld bytes)"), displayed_size, size);
                else
                        str = g_strdup_printf (_("%.1f KB"), displayed_size);
        } else if (size < GIGABYTE_FACTOR) {
                displayed_size = (double) size / MEGABYTE_FACTOR;
                if (long_string)
                        str = g_strdup_printf (_("%.1f MB (%'lld bytes)"), displayed_size, size);
                else
                        str = g_strdup_printf (_("%.1f MB"), displayed_size);
        } else {
                displayed_size = (double) size / GIGABYTE_FACTOR;
                if (long_string)
                        str = g_strdup_printf (_("%.1f GB (%'lld bytes)"), displayed_size, size);
                else
                        str = g_strdup_printf (_("%.1f GB"), displayed_size);
        }

        return str;
}

char *
gdu_util_get_fstype_for_display (const char *fstype, const char *fsversion, gboolean long_string)
{
        char *s;

        if (fstype == NULL) {
                fstype = "";
        }

        if (fsversion == NULL) {
                fsversion = "";
        }

        if (strcmp (fstype, "vfat") == 0) {
                /* version = FAT12 | FAT16 | FAT32 */

                if (strcmp (fsversion, "FAT12") == 0) {
                        if (long_string) {
                                s = g_strdup (_("Microsoft FAT (12-bit version)"));
                        } else {
                                s = g_strdup (_("FAT"));
                        }
                } else if (strcmp (fsversion, "FAT16") == 0) {
                        if (long_string) {
                                s = g_strdup (_("Microsoft FAT (16-bit version)"));
                        } else {
                                s = g_strdup (_("FAT"));
                        }
                } else if (strcmp (fsversion, "FAT32") == 0) {
                        if (long_string) {
                                s = g_strdup (_("Microsoft FAT (32-bit version)"));
                        } else {
                                s = g_strdup (_("FAT"));
                        }
                } else {
                        if (long_string) {
                                s = g_strdup (_("Microsoft FAT"));
                        } else {
                                s = g_strdup (_("FAT"));
                        }
                }
        } else if (strcmp (fstype, "ntfs") == 0) {
                if (long_string) {
                        s = g_strdup_printf (_("Microsoft NTFS version %s"), fsversion);
                } else {
                        s = g_strdup (_("NTFS"));
                }
        } else if (strcmp (fstype, "hfs") == 0) {
                if (long_string) {
                        s = g_strdup (_("Apple HFS"));
                } else {
                        s = g_strdup (_("HFS"));
                }
        } else if (strcmp (fstype, "hfsplus") == 0) {
                if (long_string) {
                        s = g_strdup (_("Apple HFS+"));
                } else {
                        s = g_strdup (_("HFS+"));
                }
        } else if (strcmp (fstype, "crypto_LUKS") == 0) {
                if (long_string) {
                        s = g_strdup (_("Linux Unified Key Setup"));
                } else {
                        s = g_strdup (_("LUKS"));
                }
        } else if (strcmp (fstype, "ext2") == 0) {
                if (long_string) {
                        s = g_strdup_printf (_("Linux Second Ext. FS (version %s)"), fsversion);
                } else {
                        s = g_strdup (_("ext2"));
                }
        } else if (strcmp (fstype, "ext3") == 0) {
                if (long_string) {
                        s = g_strdup_printf (_("Linux Third Ext. FS (version %s)"), fsversion);
                } else {
                        s = g_strdup (_("ext3"));
                }
        } else if (strcmp (fstype, "jbd") == 0) {
                if (long_string) {
                        s = g_strdup_printf (_("Journal for Linux ext3 (version %s)"), fsversion);
                } else {
                        s = g_strdup (_("jbd"));
                }
        } else if (strcmp (fstype, "iso9660") == 0) {
                if (long_string) {
                        s = g_strdup (_("ISO 9660"));
                } else {
                        s = g_strdup (_("iso9660"));
                }
        } else if (strcmp (fstype, "udf") == 0) {
                if (long_string) {
                        s = g_strdup (_("Universal Disk Format"));
                } else {
                        s = g_strdup (_("udf"));
                }
        } else {
                s = g_strdup (fstype);
        }

        return s;
}
