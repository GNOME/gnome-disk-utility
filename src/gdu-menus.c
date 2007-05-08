/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*- */
/* gdu-menus.c
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
#include <glib/gi18n.h>
#include <libgnomeui/gnome-help.h>

#include "gdu-main.h"
#include "gdu-menus.h"

static void help_contents_action_callback (GtkAction *action, gpointer data);
static void quit_action_callback (GtkAction *action, gpointer data);
static void about_action_callback (GtkAction *action, gpointer data);

static const gchar *ui =
        "<ui>"
        "  <menubar>"
        "    <menu action='file'>"
        "      <menuitem action='quit'/>"
        "    </menu>"
        "    <menu action='help'>"
        "      <menuitem action='contents'/>"
        "      <menuitem action='about'/>"
        "    </menu>"
        "  </menubar>"
        "  <toolbar>"
        "    <toolitem action='contents'/>"
        "    <toolitem action='quit'/>"
        "  </toolbar>"
        "</ui>";

static GtkActionEntry entries[] = {
        {"file", NULL, N_("_File"), NULL, NULL, NULL },
        {"view", NULL, N_("_View"), NULL, NULL, NULL },
        {"help", NULL, N_("_Help"), NULL, NULL, NULL },

        {"quit", GTK_STOCK_QUIT, N_("_Quit"), "<Ctrl>Q", N_("Quit"), 
         G_CALLBACK (quit_action_callback)},
        {"contents", GTK_STOCK_HELP, N_("_Help"), "F1", N_("Get Help on Disk Utility"), 
         G_CALLBACK (help_contents_action_callback)},
        {"about", GTK_STOCK_ABOUT, N_("_About"), NULL, NULL, 
         G_CALLBACK (about_action_callback)}
};


static void
help_contents_action_callback (GtkAction *action, gpointer data)
{
        gnome_help_display ("gnome-disk-utility.xml", NULL, NULL);
}

static void
quit_action_callback (GtkAction *action, gpointer data)
{
        gtk_main_quit ();
}

static void
about_action_callback (GtkAction *action, gpointer data)
{
        GtkWindow *app = GTK_WINDOW (data);
        const gchar *authors[] = {
                "David Zeuthen <davidz@redhat.com>",
                NULL
        };

        gtk_show_about_dialog (app,
                               "name", _("Disk Utility"),
                               "version", VERSION,
                               "copyright", "\xc2\xa9 2007 David Zeuthen",
                               "authors", authors,
                               "translator-credits",
                               _("translator-credits"), "logo-icon-name",
                               "gnome-device-manager", NULL);
}

GtkUIManager *
gdu_create_ui_manager (const gchar *group, gpointer user_data)
{
        GtkActionGroup *action_group;
        GtkUIManager *ui_manager;
        GError *error;

        action_group = gtk_action_group_new (group);
        gtk_action_group_set_translation_domain (action_group, NULL);
        gtk_action_group_add_actions (action_group, entries,
                                      G_N_ELEMENTS (entries), user_data);
        ui_manager = gtk_ui_manager_new ();
        gtk_ui_manager_insert_action_group (ui_manager, action_group, 0);

        error = NULL;
        if (!gtk_ui_manager_add_ui_from_string
            (ui_manager, ui, -1, &error)) {
                g_message ("Building menus failed: %s", error->message);
                g_error_free (error);
                gtk_main_quit ();
        }

        return ui_manager;
}
