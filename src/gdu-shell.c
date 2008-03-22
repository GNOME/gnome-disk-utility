/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*- */
/* gdu-shell.c
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

#include <config.h>
#include <glib-object.h>
#include <string.h>
#include <glib/gi18n.h>
#include <polkit-gnome/polkit-gnome.h>

#include "gdu-shell.h"
#include "gdu-util.h"
#include "gdu-pool.h"
#include "gdu-tree.h"
#include "gdu-volume.h"
#include "gdu-drive.h"

#include "gdu-page.h"
#include "gdu-page-erase.h"
#include "gdu-page-summary.h"
#include "gdu-page-partition-create.h"
#include "gdu-page-partition-modify.h"
#include "gdu-page-partition-table.h"

struct _GduShellPrivate
{
        GtkWidget *app_window;
        GduPool *pool;

        GtkWidget *treeview;

        /* the summary page is special; it's always shown */
        GduPage *page_summary;

        /* an ordered list of GduPage objects (as they will appear in the UI) */
        GList *pages;

        PolKitAction *pk_mount_action;

        PolKitGnomeAction *mount_action;
        PolKitGnomeAction *unmount_action;
        PolKitGnomeAction *eject_action;

        GduPresentable *presentable_now_showing;

        GtkActionGroup *action_group;
        GtkUIManager *ui_manager;

        GtkWidget *notebook;

        GList *table_labels;

};

static GObjectClass *parent_class = NULL;

G_DEFINE_TYPE (GduShell, gdu_shell, G_TYPE_OBJECT);

static void
gdu_shell_finalize (GduShell *shell)
{

        if (G_OBJECT_CLASS (parent_class)->finalize)
                (* G_OBJECT_CLASS (parent_class)->finalize) (G_OBJECT (shell));
}

static void
gdu_shell_class_init (GduShellClass *klass)
{
        GObjectClass *obj_class = (GObjectClass *) klass;

        parent_class = g_type_class_peek_parent (klass);

        obj_class->finalize = (GObjectFinalizeFunc) gdu_shell_finalize;
}

static void create_window (GduShell *shell);

static void
gdu_shell_init (GduShell *shell)
{
        shell->priv = g_new0 (GduShellPrivate, 1);
        create_window (shell);
}

GduShell *
gdu_shell_new (void)
{
        return GDU_SHELL (g_object_new (GDU_TYPE_SHELL, NULL));;
}

GtkWidget *
gdu_shell_get_toplevel (GduShell *shell)
{
        return shell->priv->app_window;
}

GduPresentable *
gdu_shell_get_selected_presentable (GduShell *shell)
{
        return shell->priv->presentable_now_showing;
}

void
gdu_shell_select_presentable (GduShell *shell, GduPresentable *presentable)
{
        gdu_tree_select_presentable (GTK_TREE_VIEW (shell->priv->treeview), presentable);
}

/* ---------------------------------------------------------------------------------------------------- */

/* called when a new presentable is selected
 *  - or a presentable changes
 *  - or the job state of a presentable changes
 *
 * and to update visibility of embedded widgets
 */
void
gdu_shell_update (GduShell *shell)
{
        int n;
        GList *l;
        GduDevice *device;
        gboolean job_in_progress;
        gboolean last_job_failed;
        gboolean can_mount;
        gboolean can_unmount;
        gboolean can_eject;
        GtkWidget *page_widget_currently_showing;

        job_in_progress = FALSE;
        last_job_failed = FALSE;
        can_mount = FALSE;
        can_unmount = FALSE;
        can_eject = FALSE;

        /* figure out what pages in the notebook to show + update pages */
        device = gdu_presentable_get_device (shell->priv->presentable_now_showing);
        if (device != NULL) {
                if (gdu_device_job_in_progress (device)) {
                        job_in_progress = TRUE;
                }

                if (gdu_device_job_get_last_error_message (device) != NULL) {
                        last_job_failed = TRUE;
                }

                if (GDU_IS_VOLUME (shell->priv->presentable_now_showing) &&
                    strcmp (gdu_device_id_get_usage (device), "filesystem") == 0) {
                        if (gdu_device_is_mounted (device)) {
                                can_unmount = TRUE;
                        } else {
                                can_mount = TRUE;
                        }
                }

                if (GDU_IS_DRIVE (shell->priv->presentable_now_showing) &&
                    gdu_device_is_removable (device) &&
                    gdu_device_is_media_available (device)) {
                        can_eject = TRUE;
                }

        }

        page_widget_currently_showing = gtk_notebook_get_nth_page (
                GTK_NOTEBOOK (shell->priv->notebook),
                gtk_notebook_get_current_page (GTK_NOTEBOOK (shell->priv->notebook)));

        for (l = shell->priv->pages; l != NULL; l = l->next) {
                GduPage *page = GDU_PAGE (l->data);
                gboolean show_page;
                GtkWidget *page_widget;

                page_widget = gdu_page_get_widget (page);

                /* Make the page insenstive if there's a job running on the device or the last
                 * job failed. Do this before calling update() as the page may want to render
                 * itself insensitive.
                 */
                if (job_in_progress || last_job_failed)
                        gtk_widget_set_sensitive (page_widget, FALSE);
                else
                        gtk_widget_set_sensitive (page_widget, TRUE);

                show_page = gdu_page_update (page, shell->priv->presentable_now_showing);

                if (show_page) {
                        gtk_widget_show (page_widget);
                } else {
                        gtk_widget_hide (page_widget);
                        if (page_widget == page_widget_currently_showing)
                                page_widget_currently_showing = NULL;
                }

        }

        /* the page we were showing was switching away from */
        if (page_widget_currently_showing == NULL) {
                /* go to the first visible page */
                for (n = 0; n < gtk_notebook_get_n_pages (GTK_NOTEBOOK (shell->priv->notebook)); n++) {
                        GtkWidget *page_widget;

                        page_widget = gtk_notebook_get_nth_page (GTK_NOTEBOOK (shell->priv->notebook), n);
                        if (GTK_WIDGET_VISIBLE (page_widget)) {
                                gtk_notebook_set_current_page (GTK_NOTEBOOK (shell->priv->notebook), n);
                                break;
                        }
                }
        }

        /* update summary page */
        gdu_page_update (shell->priv->page_summary, shell->priv->presentable_now_showing);

        /* update all GtkActions */
        polkit_gnome_action_set_sensitive (shell->priv->mount_action, can_mount);
        polkit_gnome_action_set_sensitive (shell->priv->unmount_action, can_unmount);
        polkit_gnome_action_set_sensitive (shell->priv->eject_action, can_eject);
}

static void
presentable_changed (GduPresentable *presentable, gpointer user_data)
{
        GduShell *shell = user_data;
        if (presentable == shell->priv->presentable_now_showing)
                gdu_shell_update (shell);
}

static void
presentable_job_changed (GduPresentable *presentable, gpointer user_data)
{
        GduShell *shell = user_data;
        if (presentable == shell->priv->presentable_now_showing)
                gdu_shell_update (shell);
}

static void
device_tree_changed (GtkTreeSelection *selection, gpointer user_data)
{
        GduShell *shell = user_data;
        GduPresentable *presentable;
        GtkTreeView *device_tree_view;

        device_tree_view = gtk_tree_selection_get_tree_view (selection);
        presentable = gdu_tree_get_selected_presentable (device_tree_view);

        if (presentable != NULL) {

                if (shell->priv->presentable_now_showing != NULL) {
                        g_signal_handlers_disconnect_by_func (shell->priv->presentable_now_showing,
                                                              (GCallback) presentable_changed,
                                                              shell);
                        g_signal_handlers_disconnect_by_func (shell->priv->presentable_now_showing,
                                                              (GCallback) presentable_job_changed,
                                                              shell);
                }

                shell->priv->presentable_now_showing = presentable;

                g_signal_connect (shell->priv->presentable_now_showing, "changed",
                                  (GCallback) presentable_changed, shell);
                g_signal_connect (shell->priv->presentable_now_showing, "job-changed",
                                  (GCallback) presentable_job_changed, shell);

                gdu_shell_update (shell);
        }
}

static void
presentable_removed (GduPool *pool, GduPresentable *presentable, gpointer user_data)
{
        GduShell *shell = user_data;
        GduPresentable *enclosing_presentable;

        if (presentable == shell->priv->presentable_now_showing) {

                /* Try going to the enclosing presentable if that one
                 * is available. Otherwise go to the first one.
                 */

                enclosing_presentable = gdu_presentable_get_enclosing_presentable (presentable);
                if (enclosing_presentable != NULL) {
                        gdu_shell_select_presentable (shell, enclosing_presentable);
                        g_object_unref (enclosing_presentable);
                } else {
                        gdu_tree_select_first_presentable (GTK_TREE_VIEW (shell->priv->treeview));
                }
        }
}


static void
mount_action_callback (GtkAction *action, gpointer user_data)
{
        GduShell *shell = user_data;
        GduDevice *device;

        device = gdu_presentable_get_device (shell->priv->presentable_now_showing);
        if (device != NULL) {
                gdu_device_op_mount (device);
                g_object_unref (device);
        }
}

static void
unmount_action_callback (GtkAction *action, gpointer user_data)
{
        GduShell *shell = user_data;
        GduDevice *device;

        device = gdu_presentable_get_device (shell->priv->presentable_now_showing);
        if (device != NULL) {
                gdu_device_op_unmount (device);
                g_object_unref (device);
        }
}

static void
eject_action_callback (GtkAction *action, gpointer user_data)
{
        g_warning ("todo: eject");
}


static void
help_contents_action_callback (GtkAction *action, gpointer user_data)
{
        /* TODO */
        //gnome_help_display ("gnome-disk-utility.xml", NULL, NULL);
        g_warning ("TODO: launch help");
}

static void
quit_action_callback (GtkAction *action, gpointer user_data)
{
        gtk_main_quit ();
}

static void
about_action_callback (GtkAction *action, gpointer user_data)
{
        GduShell *shell = user_data;

        const gchar *authors[] = {
                "David Zeuthen <davidz@redhat.com>",
                NULL
        };

        gtk_show_about_dialog (GTK_WINDOW (shell->priv->app_window),
                               "program-name", _("Disk Utility"),
                               "version", VERSION,
                               "copyright", "\xc2\xa9 2008 David Zeuthen",
                               "authors", authors,
                               "translator-credits", _("translator-credits"),
                               "logo-icon-name", "gnome-disk-utility", NULL);
}

static const gchar *ui =
        "<ui>"
        "  <menubar>"
        "    <menu action='file'>"
        "      <menuitem action='quit'/>"
        "    </menu>"
        "    <menu action='action'>"
        "      <menuitem action='mount'/>"
        "      <menuitem action='unmount'/>"
        "      <menuitem action='eject'/>"
        "    </menu>"
        "    <menu action='help'>"
        "      <menuitem action='contents'/>"
        "      <menuitem action='about'/>"
        "    </menu>"
        "  </menubar>"
        "  <toolbar>"
        "    <toolitem action='mount'/>"
        "    <toolitem action='unmount'/>"
        "    <toolitem action='eject'/>"
        "  </toolbar>"
        "</ui>";

static GtkActionEntry entries[] = {
        {"file", NULL, N_("_File"), NULL, NULL, NULL },
        {"action", NULL, N_("_Actions"), NULL, NULL, NULL },
        {"help", NULL, N_("_Help"), NULL, NULL, NULL },

        //{"mount", "gdu-mount", N_("_Mount"), NULL, N_("Mount"), G_CALLBACK (mount_action_callback)},
        //{"unmount", "gdu-unmount", N_("_Unmount"), NULL, N_("Unmount"), G_CALLBACK (unmount_action_callback)},
        //{"eject", "gdu-eject", N_("_Eject"), NULL, N_("Eject"), G_CALLBACK (eject_action_callback)},

        {"quit", GTK_STOCK_QUIT, N_("_Quit"), "<Ctrl>Q", N_("Quit"),
         G_CALLBACK (quit_action_callback)},
        {"contents", GTK_STOCK_HELP, N_("_Help"), "F1", N_("Get Help on Disk Utility"),
         G_CALLBACK (help_contents_action_callback)},
        {"about", GTK_STOCK_ABOUT, N_("_About"), NULL, NULL,
         G_CALLBACK (about_action_callback)}
};

static GtkUIManager *
create_ui_manager (GduShell *shell)
{
        GtkUIManager *ui_manager;
        GError *error;

        shell->priv->action_group = gtk_action_group_new ("GnomeDiskUtilityActions");
        gtk_action_group_set_translation_domain (shell->priv->action_group, NULL);
        gtk_action_group_add_actions (shell->priv->action_group, entries, G_N_ELEMENTS (entries), shell);

        /* -------------------------------------------------------------------------------- */

        shell->priv->mount_action = polkit_gnome_action_new_default ("mount",
                                                               shell->priv->pk_mount_action,
                                                               _("_Mount"),
                                                               _("Mount"));
        g_object_set (shell->priv->mount_action,
                      "auth-label", _("_Mount..."),
                      "yes-icon-name", "gdu-mount",
                      "no-icon-name", "gdu-mount",
                      "auth-icon-name", "gdu-mount",
                      "self-blocked-icon-name", "gdu-mount",
                      NULL);
        g_signal_connect (shell->priv->mount_action, "activate", G_CALLBACK (mount_action_callback), shell);
        gtk_action_group_add_action (shell->priv->action_group, GTK_ACTION (shell->priv->mount_action));

        /* -------------------------------------------------------------------------------- */

        shell->priv->unmount_action = polkit_gnome_action_new_default ("unmount",
                                                                 NULL, /* TODO */
                                                                 _("_Unmount"),
                                                                 _("Unmount"));
        g_object_set (shell->priv->unmount_action,
                      "auth-label", _("_Unmount..."),
                      "yes-icon-name", "gdu-unmount",
                      "no-icon-name", "gdu-unmount",
                      "auth-icon-name", "gdu-unmount",
                      "self-blocked-icon-name", "gdu-unmount",
                      NULL);
        g_signal_connect (shell->priv->unmount_action, "activate", G_CALLBACK (unmount_action_callback), shell);
        gtk_action_group_add_action (shell->priv->action_group, GTK_ACTION (shell->priv->unmount_action));

        /* -------------------------------------------------------------------------------- */

        shell->priv->eject_action = polkit_gnome_action_new_default ("eject",
                                                               NULL, /* TODO */
                                                               _("_Eject"),
                                                               _("Eject"));
        g_object_set (shell->priv->eject_action,
                      "auth-label", _("_Eject..."),
                      "yes-icon-name", "gdu-eject",
                      "no-icon-name", "gdu-eject",
                      "auth-icon-name", "gdu-eject",
                      "self-blocked-icon-name", "gdu-eject",
                      NULL);
        g_signal_connect (shell->priv->eject_action, "activate", G_CALLBACK (eject_action_callback), shell);
        gtk_action_group_add_action (shell->priv->action_group, GTK_ACTION (shell->priv->eject_action));

        /* -------------------------------------------------------------------------------- */

        ui_manager = gtk_ui_manager_new ();
        gtk_ui_manager_insert_action_group (ui_manager, shell->priv->action_group, 0);

        error = NULL;
        if (!gtk_ui_manager_add_ui_from_string
            (ui_manager, ui, -1, &error)) {
                g_message ("Building menus failed: %s", error->message);
                g_error_free (error);
                gtk_main_quit ();
        }

        return ui_manager;
}

static void
create_polkit_actions (GduShell *shell)
{
        shell->priv->pk_mount_action = polkit_action_new ();
        polkit_action_set_action_id (shell->priv->pk_mount_action, "org.freedesktop.devicekit.disks.mount");
}

static void
add_page (GduShell *shell, GType type)
{
        GduPage *page;
        char *name;
        GtkWidget *tab_label;

        page = g_object_new (type, "shell", shell, NULL);
        name = gdu_page_get_name (page);

        shell->priv->pages = g_list_append (shell->priv->pages, page);
        tab_label = gtk_label_new_with_mnemonic (name);
        gtk_notebook_append_page (GTK_NOTEBOOK (shell->priv->notebook),
                                  gdu_page_get_widget (page),
                                  tab_label);
        g_free (name);
}

static void
create_window (GduShell *shell)
{
        GtkWidget *vbox;
        GtkWidget *vbox2;
        GtkWidget *menubar;
        GtkWidget *toolbar;
        GtkAccelGroup *accel_group;
        GtkWidget *hpane;
        GtkWidget *treeview_scrolled_window;
        GtkTreeSelection *select;

        shell->priv->pool = gdu_pool_new ();

        create_polkit_actions (shell);

        shell->priv->app_window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
        gtk_window_set_resizable (GTK_WINDOW (shell->priv->app_window), TRUE);
        gtk_window_set_default_size (GTK_WINDOW (shell->priv->app_window), 900, 550);
        gtk_window_set_title (GTK_WINDOW (shell->priv->app_window), _("Disk Utility"));

        vbox = gtk_vbox_new (FALSE, 0);
        gtk_container_add (GTK_CONTAINER (shell->priv->app_window), vbox);

        shell->priv->ui_manager = create_ui_manager (shell);
        accel_group = gtk_ui_manager_get_accel_group (shell->priv->ui_manager);
        gtk_window_add_accel_group (GTK_WINDOW (shell->priv->app_window), accel_group);

        menubar = gtk_ui_manager_get_widget (shell->priv->ui_manager, "/menubar");
        gtk_box_pack_start (GTK_BOX (vbox), menubar, FALSE, FALSE, 0);
        toolbar = gtk_ui_manager_get_widget (shell->priv->ui_manager, "/toolbar");
        gtk_box_pack_start (GTK_BOX (vbox), toolbar, FALSE, FALSE, 0);

        /* tree view */
        treeview_scrolled_window = gtk_scrolled_window_new (NULL, NULL);
        gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (treeview_scrolled_window),
                                        GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
        shell->priv->treeview = GTK_WIDGET (gdu_tree_new (shell->priv->pool));
        gtk_container_add (GTK_CONTAINER (treeview_scrolled_window), shell->priv->treeview);

        /* add pages in a notebook */
        shell->priv->notebook = gtk_notebook_new ();
        add_page (shell, GDU_TYPE_PAGE_PARTITION_TABLE);
        add_page (shell, GDU_TYPE_PAGE_PARTITION_MODIFY);
        add_page (shell, GDU_TYPE_PAGE_PARTITION_CREATE);
        add_page (shell, GDU_TYPE_PAGE_ERASE);

        vbox2 = gtk_vbox_new (FALSE, 0);
        gtk_box_pack_start (GTK_BOX (vbox2), shell->priv->notebook, TRUE, TRUE, 0);

        /* setup and add horizontal pane */
        hpane = gtk_hpaned_new ();
        gtk_paned_add1 (GTK_PANED (hpane), treeview_scrolled_window);
        gtk_paned_add2 (GTK_PANED (hpane), vbox2);
        gtk_paned_set_position (GTK_PANED (hpane), 260);

        gtk_box_pack_start (GTK_BOX (vbox), hpane, TRUE, TRUE, 0);

        /* finally add the summary page */
        shell->priv->page_summary = g_object_new (GDU_TYPE_PAGE_SUMMARY, "shell", shell, NULL);
        gtk_box_pack_start (GTK_BOX (vbox), gdu_page_get_widget (shell->priv->page_summary), FALSE, FALSE, 0);

        select = gtk_tree_view_get_selection (GTK_TREE_VIEW (shell->priv->treeview));
        gtk_tree_selection_set_mode (select, GTK_SELECTION_SINGLE);
        g_signal_connect (select, "changed", (GCallback) device_tree_changed, shell);

        /* when starting up, set focus on tree view */
        gtk_widget_grab_focus (shell->priv->treeview);

        g_signal_connect (shell->priv->pool, "presentable-removed", (GCallback) presentable_removed, shell);
        g_signal_connect (shell->priv->app_window, "delete-event", gtk_main_quit, NULL);

        gtk_widget_show_all (vbox);

        gdu_tree_select_first_presentable (GTK_TREE_VIEW (shell->priv->treeview));
}

