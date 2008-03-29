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
#include "gdu-drive.h"
#include "gdu-volume.h"
#include "gdu-volume-hole.h"

#include "gdu-page.h"
#include "gdu-page-drive.h"
#include "gdu-page-volume.h"
#include "gdu-page-volume-unallocated.h"
#include "gdu-page-volume-unrecognized.h"
#include "gdu-page-job.h"

struct _GduShellPrivate
{
        GtkWidget *app_window;
        GduPool *pool;

        GtkWidget *treeview;

        /* an ordered list of GduPage objects (as they will appear in the UI) */
        GList *pages;

        PolKitAction *pk_mount_action;

        PolKitGnomeAction *mount_action;
        PolKitGnomeAction *unmount_action;
        PolKitGnomeAction *eject_action;

        PolKitGnomeAction *unlock_action;
        PolKitGnomeAction *lock_action;

        GduPresentable *presentable_now_showing;

        GtkActionGroup *action_group;
        GtkUIManager *ui_manager;

        GtkWidget *notebook;

        GtkWidget *icon_image;
        GtkWidget *name_label;
        GtkWidget *details1_label;
        GtkWidget *details2_label;
        GtkWidget *details3_label;
};

static GObjectClass *parent_class = NULL;

G_DEFINE_TYPE (GduShell, gdu_shell, G_TYPE_OBJECT);

static void show_page (GduShell *shell, GType page_type);

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
        gtk_widget_grab_focus (shell->priv->treeview);
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
details_update (GduShell *shell)
{
        GduPresentable *presentable;
        gboolean ret;
        char *details1;
        char *details2;
        char *details3;
        char *s;
        char *s2;
        char *s3;
        char *name;
        char *icon_name;
        GdkPixbuf *pixbuf;
        GduDevice *device;
        const char *usage;
        const char *device_file;
        char *strsize;
        GduPresentable *toplevel_presentable;
        GduDevice *toplevel_device;

        ret = TRUE;

        presentable = shell->priv->presentable_now_showing;

        device = gdu_presentable_get_device (presentable);

        toplevel_presentable = gdu_util_find_toplevel_presentable (presentable);
        if (toplevel_presentable != NULL)
                toplevel_device = gdu_presentable_get_device (toplevel_presentable);

        icon_name = gdu_presentable_get_icon_name (presentable);
        name = gdu_presentable_get_name (presentable);

        pixbuf = NULL;
        if (icon_name != NULL) {

                pixbuf = gtk_icon_theme_load_icon (gtk_icon_theme_get_default (),
                                                   icon_name,
                                                   96,
                                                   GTK_ICON_LOOKUP_GENERIC_FALLBACK,
                                                   NULL);

                /* if it's unallocated or unrecognized space, make the icon greyscale */
                if (!gdu_presentable_is_allocated (presentable) ||
                    !gdu_presentable_is_recognized (presentable)) {
                        GdkPixbuf *pixbuf2;
                        pixbuf2 = pixbuf;
                        pixbuf = gdk_pixbuf_copy (pixbuf);
                        g_object_unref (pixbuf2);
                        gdk_pixbuf_saturate_and_pixelate (pixbuf,
                                                          pixbuf,
                                                          0.0,
                                                          FALSE);
                }

        }
        gtk_image_set_from_pixbuf (GTK_IMAGE (shell->priv->icon_image), pixbuf);
        g_object_unref (pixbuf);

        s = g_strdup_printf (_("<span font_desc='18'><b>%s</b></span>"), name);
        gtk_label_set_markup (GTK_LABEL (shell->priv->name_label), s);
        g_free (s);

        usage = NULL;
        device_file = NULL;
        if (device != NULL) {
                usage = gdu_device_id_get_usage (device);
                device_file = gdu_device_get_device_file (device);
        }

        strsize = gdu_util_get_size_for_display (gdu_presentable_get_size (presentable), FALSE);

        details1 = NULL;
        details2 = NULL;
        details3 = NULL;

        if (GDU_IS_DRIVE (presentable)) {
                details3 = g_strdup (device_file);

                s = gdu_util_get_connection_for_display (
                        gdu_device_drive_get_connection_interface (device),
                        gdu_device_drive_get_connection_speed (device));
                details1 = g_strdup_printf ("Connected via %s", s);
                g_free (s);

                if (gdu_device_is_removable (device)) {
                        if (gdu_device_is_partition_table (device)) {
                                const char *scheme;
                                scheme = gdu_device_partition_table_get_scheme (device);
                                if (strcmp (scheme, "apm") == 0) {
                                        s = g_strdup (_("Apple Partition Map"));
                                } else if (strcmp (scheme, "mbr") == 0) {
                                        s = g_strdup (_("Master Boot Record"));
                                } else if (strcmp (scheme, "gpt") == 0) {
                                        s = g_strdup (_("GUID Partition Table"));
                                } else {
                                        s = g_strdup_printf (_("Unknown Scheme: %s"), scheme);
                                }

                                details2 = g_strdup_printf (_("%s Partitioned Media (%s)"), strsize, s);

                                g_free (s);
                        } else if (usage != NULL && strlen (usage) > 0) {
                                details2 = g_strdup_printf (_("%s Unpartitioned Media"), strsize);
                        } else if (!gdu_device_is_media_available (device)) {
                                details2 = g_strdup_printf (_("No Media Detected"));
                        } else {
                                details2 = g_strdup_printf (_("Unrecognized"));
                        }
                } else {
                        if (gdu_device_is_partition_table (device)) {
                                const char *scheme;
                                scheme = gdu_device_partition_table_get_scheme (device);
                                if (strcmp (scheme, "apm") == 0) {
                                        s = g_strdup (_("Apple Partition Map"));
                                } else if (strcmp (scheme, "mbr") == 0) {
                                        s = g_strdup (_("Master Boot Record"));
                                } else if (strcmp (scheme, "gpt") == 0) {
                                        s = g_strdup (_("GUID Partition Table"));
                                } else {
                                        s = g_strdup_printf (_("Unknown Scheme: %s"), scheme);
                                }
                                details2 = g_strdup_printf (_("Partitioned (%s)"), s);
                                g_free (s);
                        } else if (usage != NULL && strlen (usage) > 0) {
                                details2 = g_strdup_printf (_("Not Partitioned"));
                        } else if (!gdu_device_is_media_available (device)) {
                                details2 = g_strdup_printf (_("No Media Detected"));
                        } else {
                                details2 = g_strdup_printf (_("Unrecognized"));
                        }
                }

                if (gdu_device_is_read_only (device)) {
                        s = details3;
                        details3 = g_strconcat (details3, _(" (Read Only)"), NULL);
                        g_free (s);
                }

        } else if (GDU_IS_VOLUME (presentable)) {
                details3 = g_strdup (device_file);

                if (strcmp (usage, "filesystem") == 0) {
                        char *fsname;
                        fsname = gdu_util_get_fstype_for_display (
                                gdu_device_id_get_type (device),
                                gdu_device_id_get_version (device),
                                TRUE);
                        details1 = g_strdup_printf (_("%s %s File System"), strsize, fsname);
                        g_free (fsname);
                } else if (strcmp (usage, "raid") == 0) {
                        char *fsname;
                        fsname = gdu_util_get_fstype_for_display (
                                gdu_device_id_get_type (device),
                                gdu_device_id_get_version (device),
                                TRUE);
                        details1 = g_strdup_printf (_("%s %s"), strsize, fsname);
                        g_free (fsname);
                } else if (strcmp (usage, "crypto") == 0) {
                        details1 = g_strdup_printf (_("%s Encrypted Device"), strsize);
                } else {
                        details1 = g_strdup_printf (_("%s Unrecognized"), strsize);
                }

                if (gdu_device_is_crypto_cleartext (device)) {
                        details2 = g_strdup (_("Unlocked Encrypted Volume"));
                } else {
                        if (gdu_device_is_partition (device)) {
                                char *part_desc;
                                part_desc = gdu_util_get_desc_for_part_type (gdu_device_partition_get_scheme (device),
                                                                             gdu_device_partition_get_type (device));
                                details2 = g_strdup_printf (_("Partition %d (%s)"),
                                                    gdu_device_partition_get_number (device), part_desc);
                                g_free (part_desc);
                        } else {
                                details2 = g_strdup (_("Not Partitioned"));
                        }
                }

                if (gdu_device_is_read_only (device)) {
                        s = details3;
                        details3 = g_strconcat (details3, _(" (Read Only)"), NULL);
                        g_free (s);
                }

        } else if (GDU_IS_VOLUME_HOLE (presentable)) {

                details1 = g_strdup_printf (_("%s Unallocated"), strsize);

                if (toplevel_device != NULL) {
                        details2 = g_strdup (gdu_device_get_device_file (toplevel_device));

                        if (gdu_device_is_read_only (toplevel_device)) {
                                s = details2;
                                details2 = g_strconcat (details2, _(" (Read Only)"), NULL);
                                g_free (s);
                        }
                }
        }

        g_free (icon_name);
        g_free (name);
        g_free (strsize);

        s = NULL;
        s2 = NULL;
        s3 = NULL;
        if (details1 != NULL)
                s = g_strdup_printf ("<span foreground='darkgrey'>%s</span>", details1);
        if (details2 != NULL)
                s2 = g_strdup_printf ("<span foreground='darkgrey'>%s</span>", details2);
        if (details3 != NULL)
                s3 = g_strdup_printf ("<span foreground='darkgrey'>%s</span>", details3);
        gtk_label_set_markup (GTK_LABEL (shell->priv->details1_label), s);
        gtk_label_set_markup (GTK_LABEL (shell->priv->details2_label), s2);
        gtk_label_set_markup (GTK_LABEL (shell->priv->details3_label), s3);
        g_free (s);
        g_free (s2);
        g_free (s3);
        g_free (details1);
        g_free (details2);
        g_free (details3);

        if (device != NULL)
                g_object_unref (device);

        if (toplevel_presentable != NULL)
                g_object_unref (toplevel_presentable);

        if (toplevel_device != NULL)
                g_object_unref (toplevel_device);

        return ret;
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
        GduDevice *device;
        gboolean job_in_progress;
        gboolean last_job_failed;
        gboolean can_mount;
        gboolean can_unmount;
        gboolean can_eject;
        gboolean can_lock;
        gboolean can_unlock;

        job_in_progress = FALSE;
        last_job_failed = FALSE;
        can_mount = FALSE;
        can_unmount = FALSE;
        can_eject = FALSE;
        can_unlock = FALSE;
        can_lock = FALSE;

        /* figure out what pages in the notebook to show + update pages */
        device = gdu_presentable_get_device (shell->priv->presentable_now_showing);
        if (device != NULL) {
                if (gdu_device_job_in_progress (device)) {
                        job_in_progress = TRUE;
                }

                if (gdu_device_job_get_last_error_message (device) != NULL) {
                        last_job_failed = TRUE;
                }

                if (GDU_IS_VOLUME (shell->priv->presentable_now_showing)) {

                        if (strcmp (gdu_device_id_get_usage (device), "filesystem") == 0) {
                                if (gdu_device_is_mounted (device)) {
                                        can_unmount = TRUE;
                                } else {
                                        can_mount = TRUE;
                                }
                        } else if (strcmp (gdu_device_id_get_usage (device), "crypto") == 0) {
                                GList *enclosed_presentables;
                                enclosed_presentables = gdu_pool_get_enclosed_presentables (
                                        shell->priv->pool,
                                        shell->priv->presentable_now_showing);
                                if (enclosed_presentables != NULL) {
                                        can_lock = TRUE;
                                } else {
                                        can_unlock = TRUE;
                                }
                                g_list_foreach (enclosed_presentables, (GFunc) g_object_unref, NULL);
                                g_list_free (enclosed_presentables);


                        }
                }

                if (GDU_IS_DRIVE (shell->priv->presentable_now_showing) &&
                    gdu_device_is_removable (device) &&
                    gdu_device_is_media_available (device)) {
                        can_eject = TRUE;
                }

        }

        if (job_in_progress || last_job_failed) {
                show_page (shell, GDU_TYPE_PAGE_JOB);
        } else {
                if (GDU_IS_DRIVE (shell->priv->presentable_now_showing)) {
                        show_page (shell, GDU_TYPE_PAGE_DRIVE);
                } else if (!gdu_presentable_is_allocated (shell->priv->presentable_now_showing)) {
                        show_page (shell, GDU_TYPE_PAGE_VOLUME_UNALLOCATED);
                } else if (!gdu_presentable_is_recognized (shell->priv->presentable_now_showing)) {
                        show_page (shell, GDU_TYPE_PAGE_VOLUME_UNRECOGNIZED);
                } else {
                        show_page (shell, GDU_TYPE_PAGE_VOLUME);
                }
        }

        /* update all GtkActions */
        polkit_gnome_action_set_sensitive (shell->priv->mount_action, can_mount);
        polkit_gnome_action_set_sensitive (shell->priv->unmount_action, can_unmount);
        polkit_gnome_action_set_sensitive (shell->priv->eject_action, can_eject);
        polkit_gnome_action_set_sensitive (shell->priv->lock_action, can_lock);
        polkit_gnome_action_set_sensitive (shell->priv->unlock_action, can_unlock);

#if 0
        /* TODO */
        if (can_lock || can_unlock) {
                g_warning ("a");
                polkit_gnome_action_set_visible (shell->priv->mount_action, FALSE);
                polkit_gnome_action_set_visible (shell->priv->unmount_action, FALSE);
                polkit_gnome_action_set_visible (shell->priv->eject_action, FALSE);
                polkit_gnome_action_set_visible (shell->priv->lock_action, TRUE);
                polkit_gnome_action_set_visible (shell->priv->unlock_action, TRUE);
        } else {
                g_warning ("b");
                polkit_gnome_action_set_visible (shell->priv->mount_action, TRUE);
                polkit_gnome_action_set_visible (shell->priv->unmount_action, TRUE);
                polkit_gnome_action_set_visible (shell->priv->eject_action, TRUE);
                polkit_gnome_action_set_visible (shell->priv->lock_action, FALSE);
                polkit_gnome_action_set_visible (shell->priv->unlock_action, FALSE);
        }
#endif


        details_update (shell);
}

static void
presentable_changed (GduPresentable *presentable, gpointer user_data)
{
        GduShell *shell = user_data;
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
presentable_added (GduPool *pool, GduPresentable *presentable, gpointer user_data)
{
        GduShell *shell = user_data;
        gdu_shell_update (shell);
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
                        gtk_widget_grab_focus (shell->priv->treeview);
                }
        }
        gdu_shell_update (shell);
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

static void unlock_action_do (GduShell *shell, GduDevice *device, gboolean bypass_keyring);

static void
unlock_op_cb (GduDevice *device,
              const char *object_path_of_cleartext_device,
              GError     *error,
              gpointer    user_data)
{
        GduShell *shell = GDU_SHELL (user_data);

        if (object_path_of_cleartext_device == NULL) {
                unlock_action_do (shell, device, TRUE);
        }
}

static void
unlock_action_do (GduShell *shell, GduDevice *device, gboolean bypass_keyring)
{
        char *secret;

        secret = gdu_util_dialog_ask_for_secret (shell->priv->app_window,
                                                 device,
                                                 bypass_keyring);
        if (secret != NULL) {

                gdu_device_op_unlock_encrypted (device, secret, unlock_op_cb, shell);

                /* scrub the password */
                memset (secret, '\0', strlen (secret));
                g_free (secret);
        }
}

static void
unlock_action_callback (GtkAction *action, gpointer user_data)
{
        GduShell *shell = user_data;
        GduDevice *device;

        device = gdu_presentable_get_device (shell->priv->presentable_now_showing);
        if (device != NULL) {
                unlock_action_do (shell, device, FALSE);
                g_object_unref (device);
        }
}

static void
lock_action_callback (GtkAction *action, gpointer user_data)
{
        GduShell *shell = user_data;
        GduDevice *device;

        device = gdu_presentable_get_device (shell->priv->presentable_now_showing);
        if (device != NULL) {
                gdu_device_op_lock_encrypted (device);
                g_object_unref (device);
        }
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
        "    <menu action='edit'>"
        "      <menuitem action='mount'/>"
        "      <menuitem action='unmount'/>"
        "      <menuitem action='eject'/>"
        "      <separator/>"
        "      <menuitem action='unlock'/>"
        "      <menuitem action='lock'/>"
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
        "    <separator/>"
        "    <toolitem action='unlock'/>"
        "    <toolitem action='lock'/>"
        "  </toolbar>"
        "</ui>";

static GtkActionEntry entries[] = {
        {"file", NULL, N_("_File"), NULL, NULL, NULL },
        {"edit", NULL, N_("_Edit"), NULL, NULL, NULL },
        {"help", NULL, N_("_Help"), NULL, NULL, NULL },

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
                                                                     _("Make the file system on the device available"));
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
                                                                       _("Make the file system on the device unavailable"));
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
                                                                     _("Eject media from the device"));
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

        shell->priv->unlock_action = polkit_gnome_action_new_default ("unlock",
                                                                      /* TODO: for now use the mount pk action */
                                                                      shell->priv->pk_mount_action,
                                                                      _("_Unlock"),
                                                                      _("Unlock the encrypted device, making the data available in cleartext"));
        /* TODO: the lock-secure and lock-insecure icons are from Epiphany.
         *       Probably need to ship our own copy.
         */

        g_object_set (shell->priv->unlock_action,
                      "auth-label", _("_Unlock..."),
                      "yes-icon-name", "stock_lock-open",
                      "no-icon-name", "stock_lock-open",
                      "auth-icon-name", "stock_lock-open",
                      "self-blocked-icon-name", "stock_lock-open",
                      NULL);
        g_signal_connect (shell->priv->unlock_action, "activate", G_CALLBACK (unlock_action_callback), shell);
        gtk_action_group_add_action (shell->priv->action_group, GTK_ACTION (shell->priv->unlock_action));

        /* -------------------------------------------------------------------------------- */

        shell->priv->lock_action = polkit_gnome_action_new_default ("lock",
                                                                    NULL, /* TODO */
                                                                    _("_Lock"),
                                                                    _("Lock the encrypted device, making the cleartext data unavailable"));
        g_object_set (shell->priv->lock_action,
                      "auth-label", _("_Lock..."),
                      "yes-icon-name", "stock_lock",
                      "no-icon-name", "stock_lock",
                      "auth-icon-name", "stock_lock",
                      "self-blocked-icon-name", "stock_lock",
                      NULL);
        g_signal_connect (shell->priv->lock_action, "activate", G_CALLBACK (lock_action_callback), shell);
        gtk_action_group_add_action (shell->priv->action_group, GTK_ACTION (shell->priv->lock_action));

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

        page = g_object_new (type, "shell", shell, NULL);

        shell->priv->pages = g_list_append (shell->priv->pages, page);
        gtk_notebook_append_page (GTK_NOTEBOOK (shell->priv->notebook),
                                  gdu_page_get_widget (page),
                                  NULL);
}

static void
add_pages (GduShell *shell)
{
        add_page (shell, GDU_TYPE_PAGE_DRIVE);
        add_page (shell, GDU_TYPE_PAGE_VOLUME);
        add_page (shell, GDU_TYPE_PAGE_VOLUME_UNALLOCATED);
        add_page (shell, GDU_TYPE_PAGE_VOLUME_UNRECOGNIZED);
        add_page (shell, GDU_TYPE_PAGE_JOB);
}

static void
show_page (GduShell *shell, GType page_type)
{
        int page_num;
        GduPage *page = NULL;
        gboolean reset_page;
        static GduPresentable *last_presentable = NULL;

        if (page_type == GDU_TYPE_PAGE_DRIVE) {
                page_num = 0;
        } else if (page_type == GDU_TYPE_PAGE_VOLUME) {
                page_num = 1;
        } else if (page_type == GDU_TYPE_PAGE_VOLUME_UNALLOCATED) {
                page_num = 2;
        } else if (page_type == GDU_TYPE_PAGE_VOLUME_UNRECOGNIZED) {
                page_num = 3;
        } else if (page_type == GDU_TYPE_PAGE_JOB) {
                page_num = 4;
        } else {
                g_warning ("Unknown page with type %d", page_type);
                goto out;
        }

        reset_page = (gtk_notebook_get_current_page (GTK_NOTEBOOK (shell->priv->notebook)) != page_num);

        reset_page |= (shell->priv->presentable_now_showing != last_presentable);
        last_presentable = shell->priv->presentable_now_showing;

        gtk_notebook_set_current_page (GTK_NOTEBOOK (shell->priv->notebook), page_num);
        page = g_list_nth_data (shell->priv->pages, page_num);
        gdu_page_update (page, shell->priv->presentable_now_showing, reset_page);

        /* if we're switching pages, focus the treeview to avoid accidental activation */
        if (reset_page)
                gtk_widget_grab_focus (shell->priv->treeview);

out:
        ;
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
        gtk_window_set_default_size (GTK_WINDOW (shell->priv->app_window), 800, 600);
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
        gtk_scrolled_window_set_shadow_type (GTK_SCROLLED_WINDOW (treeview_scrolled_window),
                                             GTK_SHADOW_IN);
        shell->priv->treeview = GTK_WIDGET (gdu_tree_new (shell->priv->pool));
        gtk_container_add (GTK_CONTAINER (treeview_scrolled_window), shell->priv->treeview);

        /* add pages in a notebook */
        shell->priv->notebook = gtk_notebook_new ();
        gtk_notebook_set_show_tabs (GTK_NOTEBOOK (shell->priv->notebook), FALSE);
        gtk_notebook_set_show_border (GTK_NOTEBOOK (shell->priv->notebook), FALSE);
        add_pages (shell);

        vbox2 = gtk_vbox_new (FALSE, 0);

        /* --- */
        GtkWidget *label;
        GtkWidget *align;
        GtkWidget *vbox3;
        GtkWidget *hbox;
        GtkWidget *image;

        hbox = gtk_hbox_new (FALSE, 10);
        gtk_box_pack_start (GTK_BOX (vbox2), hbox, FALSE, TRUE, 0);

        image = gtk_image_new ();
        gtk_box_pack_start (GTK_BOX (hbox), image, FALSE, TRUE, 0);
        shell->priv->icon_image = image;

        vbox3 = gtk_vbox_new (FALSE, 0);
        align = gtk_alignment_new (0.0, 0.5, 0.0, 0.0);
        gtk_container_add (GTK_CONTAINER (align), vbox3);
        gtk_box_pack_start (GTK_BOX (hbox), align, FALSE, TRUE, 0);

        label = gtk_label_new (NULL);
        gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
        gtk_box_pack_start (GTK_BOX (vbox3), label, FALSE, TRUE, 0);
        shell->priv->name_label = label;

        label = gtk_label_new (NULL);
        gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
        gtk_box_pack_start (GTK_BOX (vbox3), label, FALSE, TRUE, 0);
        shell->priv->details1_label = label;

        label = gtk_label_new (NULL);
        gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
        gtk_box_pack_start (GTK_BOX (vbox3), label, FALSE, TRUE, 0);
        shell->priv->details2_label = label;

        label = gtk_label_new (NULL);
        gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
        gtk_box_pack_start (GTK_BOX (vbox3), label, FALSE, TRUE, 0);
        shell->priv->details3_label = label;

        /* --- */

        gtk_box_pack_start (GTK_BOX (vbox2), shell->priv->notebook, TRUE, TRUE, 0);

        /* setup and add horizontal pane */
        hpane = gtk_hpaned_new ();
        gtk_paned_add1 (GTK_PANED (hpane), treeview_scrolled_window);
        gtk_paned_add2 (GTK_PANED (hpane), vbox2);
        gtk_paned_set_position (GTK_PANED (hpane), 260);

        gtk_box_pack_start (GTK_BOX (vbox), hpane, TRUE, TRUE, 0);

        select = gtk_tree_view_get_selection (GTK_TREE_VIEW (shell->priv->treeview));
        gtk_tree_selection_set_mode (select, GTK_SELECTION_SINGLE);
        g_signal_connect (select, "changed", (GCallback) device_tree_changed, shell);

        /* when starting up, set focus on tree view */
        gtk_widget_grab_focus (shell->priv->treeview);

        g_signal_connect (shell->priv->pool, "presentable-added", (GCallback) presentable_added, shell);
        g_signal_connect (shell->priv->pool, "presentable-removed", (GCallback) presentable_removed, shell);
        g_signal_connect (shell->priv->app_window, "delete-event", gtk_main_quit, NULL);

        gtk_widget_show_all (vbox);

        gdu_tree_select_first_presentable (GTK_TREE_VIEW (shell->priv->treeview));
}

