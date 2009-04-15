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
#include <libsexy/sexy.h>

#include <gdu/gdu.h>
#include <gdu-gtk/gdu-gtk.h>

#include "gdu-shell.h"
#include "gdu-tree.h"

#include "gdu-section-health.h"
#include "gdu-section-partition.h"
#include "gdu-section-create-partition-table.h"
#include "gdu-section-unallocated.h"
#include "gdu-section-unrecognized.h"
#include "gdu-section-filesystem.h"
#include "gdu-section-swapspace.h"
#include "gdu-section-encrypted.h"
#include "gdu-section-linux-md-drive.h"
#include "gdu-section-no-media.h"
#include "gdu-section-job.h"

struct _GduShellPrivate
{
        GtkWidget *app_window;
        GduPool *pool;

        GtkWidget *treeview;

        GtkWidget *icon_image;
        GtkWidget *name_label;
        GtkWidget *details0_label;
        GtkWidget *details1_label;
        GtkWidget *details2_label;
        GtkWidget *details3_label;

        /* -------------------------------------------------------------------------------- */

        GtkWidget *sections_vbox;

        /* -------------------------------------------------------------------------------- */

        PolKitAction *pk_modify_action;
        PolKitAction *pk_modify_system_internal_action;
        PolKitAction *pk_fsck_action;
        PolKitAction *pk_fsck_system_internal_action;
        PolKitAction *pk_mount_action;
        PolKitAction *pk_mount_system_internal_action;
        PolKitAction *pk_unmount_others_action;
        PolKitAction *pk_eject_action;
        PolKitAction *pk_unlock_luks_action;
        PolKitAction *pk_lock_luks_others_action;
        PolKitAction *pk_linux_md_action;

        PolKitGnomeAction *fsck_action;
        PolKitGnomeAction *mount_action;
        PolKitGnomeAction *unmount_action;
        PolKitGnomeAction *eject_action;

        PolKitGnomeAction *unlock_action;
        PolKitGnomeAction *lock_action;

        PolKitGnomeAction *start_action;
        PolKitGnomeAction *stop_action;

        PolKitGnomeAction *erase_action;

        GduPresentable *presentable_now_showing;

        GtkActionGroup *action_group;
        GtkUIManager *ui_manager;
};

static GObjectClass *parent_class = NULL;

G_DEFINE_TYPE (GduShell, gdu_shell, G_TYPE_OBJECT);

static void
gdu_shell_finalize (GduShell *shell)
{
        polkit_action_unref (shell->priv->pk_modify_action);
        polkit_action_unref (shell->priv->pk_modify_system_internal_action);
        polkit_action_unref (shell->priv->pk_fsck_action);
        polkit_action_unref (shell->priv->pk_fsck_system_internal_action);
        polkit_action_unref (shell->priv->pk_mount_action);
        polkit_action_unref (shell->priv->pk_mount_system_internal_action);
        polkit_action_unref (shell->priv->pk_unmount_others_action);
        polkit_action_unref (shell->priv->pk_unlock_luks_action);
        polkit_action_unref (shell->priv->pk_lock_luks_others_action);
        polkit_action_unref (shell->priv->pk_linux_md_action);

        if (G_OBJECT_CLASS (parent_class)->finalize)
                (* G_OBJECT_CLASS (parent_class)->finalize) (G_OBJECT (shell));
}

static void
gdu_shell_class_init (GduShellClass *klass)
{
        GObjectClass *obj_class = (GObjectClass *) klass;

        parent_class = g_type_class_peek_parent (klass);

        obj_class->finalize = (GObjectFinalizeFunc) gdu_shell_finalize;

        g_type_class_add_private (klass, sizeof (GduShellPrivate));
}

static void create_window (GduShell *shell);

static void
gdu_shell_init (GduShell *shell)
{
        shell->priv = G_TYPE_INSTANCE_GET_PRIVATE (shell, GDU_TYPE_SHELL, GduShellPrivate);
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

GduPool *
gdu_shell_get_pool (GduShell *shell)
{
        return shell->priv->pool;
}


GduPresentable *
gdu_shell_get_selected_presentable (GduShell *shell)
{
        return shell->priv->presentable_now_showing;
}

void
gdu_shell_select_presentable (GduShell *shell, GduPresentable *presentable)
{
        gdu_device_tree_select_presentable (GTK_TREE_VIEW (shell->priv->treeview), presentable);
        gtk_widget_grab_focus (shell->priv->treeview);
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
details_update (GduShell *shell)
{
        GduPresentable *presentable;
        gboolean ret;
        char *s;
        char *p;
        char *detail_color;
        char *name;
        GIcon *icon;
        GdkPixbuf *pixbuf;
        GduDevice *device;
        const char *usage;
        const char *type;
        const char *device_file;
        guint64 presentable_size;
        char *strsize_long;
        GduPresentable *toplevel_presentable;
        GduDevice *toplevel_device;
        GPtrArray *details;
        guint n;

        ret = TRUE;

        details = g_ptr_array_new ();

        presentable = shell->priv->presentable_now_showing;

        device = gdu_presentable_get_device (presentable);

        toplevel_presentable = gdu_presentable_get_toplevel (presentable);
        if (toplevel_presentable != NULL)
                toplevel_device = gdu_presentable_get_device (toplevel_presentable);

        icon = gdu_presentable_get_icon (presentable);
        name = gdu_presentable_get_name (presentable);

        pixbuf = gdu_util_get_pixbuf_for_presentable_at_pixel_size (presentable, 112);
        gtk_image_set_from_pixbuf (GTK_IMAGE (shell->priv->icon_image), pixbuf);
        g_object_unref (pixbuf);

        s = g_strdup_printf ("<span font_desc='18'><b>%s</b></span>", name);
        gtk_label_set_markup (GTK_LABEL (shell->priv->name_label), s);
        g_free (s);

        usage = NULL;
        type = NULL;
        device_file = NULL;
        if (device != NULL) {
                usage = gdu_device_id_get_usage (device);
                type = gdu_device_id_get_type (device);
                device_file = gdu_device_get_device_file (device);
        }

        presentable_size = gdu_presentable_get_size (presentable);
        if (presentable_size > 0) {
                strsize_long = gdu_util_get_size_for_display (presentable_size, TRUE);
        } else {
                strsize_long = g_strdup ("Unknown Size");
        }

        if (GDU_IS_DRIVE (presentable)) {

                g_ptr_array_add (details,
                                 g_strdup (strsize_long));

                if (device == NULL) {
                        /* TODO */
                } else {
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

                                        g_ptr_array_add (details,
                                                         g_strdup_printf (_("Partitioned Media (%s)"), s));

                                        g_free (s);
                                } else if (usage != NULL && strlen (usage) > 0) {
                                        g_ptr_array_add (details,
                                                         g_strdup (_("Unpartitioned Media")));
                                } else if (!gdu_device_is_media_available (device)) {
                                        g_ptr_array_add (details,
                                                         g_strdup_printf (_("No Media Detected")));
                                } else {
                                        g_ptr_array_add (details,
                                                         g_strdup_printf (_("Unrecognized")));
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
                                        g_ptr_array_add (details, s);
                                } else if (usage != NULL && strlen (usage) > 0) {
                                        g_ptr_array_add (details,
                                                         g_strdup_printf (_("Not Partitioned")));
                                } else if (!gdu_device_is_media_available (device)) {
                                        g_ptr_array_add (details,
                                                         g_strdup_printf (_("No Media Detected")));
                                } else {
                                        g_ptr_array_add (details,
                                                         g_strdup_printf (_("Unrecognized")));
                                }
                        }
                }

                if (GDU_IS_LINUX_MD_DRIVE (presentable)) {
                        g_ptr_array_add (details,
                                         g_strdup (_("Linux Software RAID")));
                } else {
                        s = gdu_util_get_connection_for_display (
                                gdu_device_drive_get_connection_interface (device),
                                gdu_device_drive_get_connection_speed (device));
                        g_ptr_array_add (details,
                                         g_strdup_printf (_("Connected via %s"), s));
                        g_free (s);
                }

                if (device_file != NULL) {
                        if (gdu_device_is_read_only (device)) {
                        g_ptr_array_add (details,
                                         g_strdup_printf (_("%s (Read Only)"), device_file));
                        } else {
                        g_ptr_array_add (details,
                                         g_strdup (device_file));
                        }
                } else {
                        g_ptr_array_add (details,
                                         g_strdup (_("Not running")));
                }

        } else if (GDU_IS_VOLUME (presentable)) {

                g_ptr_array_add (details,
                                 g_strdup (strsize_long));

                if (strcmp (usage, "filesystem") == 0) {
                        char *fsname;
                        fsname = gdu_util_get_fstype_for_display (
                                gdu_device_id_get_type (device),
                                gdu_device_id_get_version (device),
                                TRUE);
                        g_ptr_array_add (details,
                                         g_strdup_printf (_("%s File System"), fsname));
                        g_free (fsname);
                } else if (strcmp (usage, "raid") == 0) {
                        char *fsname;
                        fsname = gdu_util_get_fstype_for_display (
                                gdu_device_id_get_type (device),
                                gdu_device_id_get_version (device),
                                TRUE);
                        g_ptr_array_add (details, fsname);
                } else if (strcmp (usage, "crypto") == 0) {
                        g_ptr_array_add (details,
                                         g_strdup (_("Encrypted LUKS Device")));
                } else if (strcmp (usage, "other") == 0) {
                        if (strcmp (type, "swap") == 0) {
                                g_ptr_array_add (details,
                                                 g_strdup (_("Swap Space")));
                        } else {
                                g_ptr_array_add (details,
                                                 g_strdup (_("Data")));
                        }
                } else {
                        g_ptr_array_add (details,
                                         g_strdup (_("Unrecognized")));
                }

                if (gdu_device_is_luks_cleartext (device)) {
                        g_ptr_array_add (details,
                                         g_strdup (_("Cleartext LUKS Device")));
                } else {
                        if (gdu_device_is_partition (device)) {
                                char *part_desc;
                                part_desc = gdu_util_get_desc_for_part_type (gdu_device_partition_get_scheme (device),
                                                                             gdu_device_partition_get_type (device));
                                g_ptr_array_add (details,
                                                 g_strdup_printf (_("Partition %d (%s)"),
                                                                  gdu_device_partition_get_number (device), part_desc));
                                g_free (part_desc);
                        } else {
                                g_ptr_array_add (details,
                                                 g_strdup (_("Not Partitioned")));
                        }
                }

                s = g_strdup (device_file);
                if (gdu_device_is_read_only (device)) {
                        p = s;
                        s = g_strdup_printf (_("%s (Read Only)"), s);
                        g_free (p);
                }

                if (gdu_device_is_mounted (device)) {
                        gchar **mount_paths;
                        GString *str;

                        mount_paths = gdu_device_get_mount_paths (device);

                        str = g_string_new (s);
                        g_free (s);
                        g_string_append (str, " mounted at ");
                        for (n = 0; mount_paths[n] != NULL; n++) {
                                if (n > 0)
                                        g_string_append (str, ", ");
                                g_string_append_printf (str, "<a href=\"file://%s\">%s</a>",
                                                        mount_paths[n],
                                                        mount_paths[n]);
                        }
                        s = g_string_free (str, FALSE);
                }
                g_ptr_array_add (details, s);


        } else if (GDU_IS_VOLUME_HOLE (presentable)) {

                g_ptr_array_add (details,
                                 g_strdup (strsize_long));

                g_ptr_array_add (details,
                                 g_strdup (_("Unallocated Space")));

                if (toplevel_device != NULL) {
                        if (gdu_device_is_read_only (toplevel_device))
                                g_ptr_array_add (details, g_strdup_printf (_("%s (Read Only)"), gdu_device_get_device_file (toplevel_device)));
                        else
                                g_ptr_array_add (details, g_strdup (gdu_device_get_device_file (toplevel_device)));
                }
        }

        /* TODO: use symbolic colors (how?) or infer from current theme */
        detail_color = g_strdup ("#808080");

        for (n = 0; n < 4; n++) {
                GtkWidget *label;
                const gchar *detail_str;

                switch (n) {
                case 0:
                        label = shell->priv->details0_label;
                        break;
                case 1:
                        label = shell->priv->details1_label;
                        break;
                case 2:
                        label = shell->priv->details2_label;
                        break;
                case 3:
                        label = shell->priv->details3_label;
                        break;
                }

                if (n < details->len)
                        detail_str = details->pdata[n];
                else
                        detail_str = "";

                s = g_strdup_printf ("<span foreground='%s'>%s</span>", detail_color, detail_str);
                sexy_url_label_set_markup (SEXY_URL_LABEL (label), s);
                g_free (s);
        }

        if (icon != NULL)
                g_object_unref (icon);
        g_free (name);
        g_free (strsize_long);

        g_ptr_array_foreach (details, (GFunc) g_free, NULL);
        g_ptr_array_free (details, TRUE);
        g_free (detail_color);

        if (device != NULL)
                g_object_unref (device);

        if (toplevel_presentable != NULL)
                g_object_unref (toplevel_presentable);

        if (toplevel_device != NULL)
                g_object_unref (toplevel_device);

        return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

static void
remove_section (GtkWidget *widget, gpointer callback_data)
{
        gtk_container_remove (GTK_CONTAINER (callback_data), widget);
}

static void
update_section (GtkWidget *section, gpointer callback_data)
{
        gdu_section_update (GDU_SECTION (section));
}

static GList *
compute_sections_to_show (GduShell *shell, gboolean showing_job)
{
        GduDevice *device;
        GList *sections_to_show;

        sections_to_show = NULL;
        device = gdu_presentable_get_device (shell->priv->presentable_now_showing);

        /* compute sections we want to show */
        if (showing_job) {

                sections_to_show = g_list_append (sections_to_show,
                                                  (gpointer) GDU_TYPE_SECTION_JOB);

        } else {
                if (GDU_IS_LINUX_MD_DRIVE (shell->priv->presentable_now_showing)) {

                        sections_to_show = g_list_append (sections_to_show,
                                                          (gpointer) GDU_TYPE_SECTION_LINUX_MD_DRIVE);


                } else if (GDU_IS_DRIVE (shell->priv->presentable_now_showing) && device != NULL) {

                        if (gdu_device_is_removable (device) && !gdu_device_is_media_available (device)) {

                                sections_to_show = g_list_append (
                                        sections_to_show, (gpointer) GDU_TYPE_SECTION_NO_MEDIA);

                        } else {

                                if (gdu_device_drive_ata_smart_get_is_available (device)) {
                                                sections_to_show = g_list_append (sections_to_show,
                                                                                  (gpointer) GDU_TYPE_SECTION_HEALTH);
                                }

                        }

                } else if (GDU_IS_VOLUME (shell->priv->presentable_now_showing) && device != NULL) {

                        if (gdu_device_is_partition (device))
                                sections_to_show = g_list_append (sections_to_show, (gpointer) GDU_TYPE_SECTION_PARTITION);

                        if (gdu_presentable_is_recognized (shell->priv->presentable_now_showing)) {
                                const char *usage;
                                const char *type;

                                usage = gdu_device_id_get_usage (device);
                                type = gdu_device_id_get_type (device);

                                if (usage != NULL && strcmp (usage, "filesystem") == 0) {
                                        sections_to_show = g_list_append (
                                                sections_to_show, (gpointer) GDU_TYPE_SECTION_FILESYSTEM);
                                } else if (usage != NULL && strcmp (usage, "crypto") == 0) {
                                        sections_to_show = g_list_append (
                                                sections_to_show, (gpointer) GDU_TYPE_SECTION_ENCRYPTED);
                                } else if (usage != NULL && strcmp (usage, "other") == 0 &&
                                           type != NULL && strcmp (type, "swap") == 0) {
                                        sections_to_show = g_list_append (
                                                sections_to_show, (gpointer) GDU_TYPE_SECTION_SWAPSPACE);
                                }
                        } else {
                                GduPresentable *toplevel_presentable;
                                GduDevice *toplevel_device;

                                sections_to_show = g_list_append (
                                        sections_to_show, (gpointer) GDU_TYPE_SECTION_UNRECOGNIZED);

                                /* Also show a "Create partition table" section for a volume if the drive isn't partitioned */
                                toplevel_presentable = gdu_presentable_get_toplevel (shell->priv->presentable_now_showing);
                                if (toplevel_presentable != NULL) {
                                        toplevel_device = gdu_presentable_get_device (toplevel_presentable);

                                        if (toplevel_device != NULL) {
                                                if (!gdu_device_is_partition_table (toplevel_device)) {
                                                        sections_to_show = g_list_append (
                                                                sections_to_show, (gpointer) GDU_TYPE_SECTION_CREATE_PARTITION_TABLE);
                                                }
                                                g_object_unref (toplevel_device);
                                        }
                                        g_object_unref (toplevel_presentable);
                                }
                        }

                } else if (GDU_IS_VOLUME_HOLE (shell->priv->presentable_now_showing)) {

                        sections_to_show = g_list_append (sections_to_show,
                                                          (gpointer) GDU_TYPE_SECTION_UNALLOCATED);
                }
        }

        if (device != NULL)
                g_object_unref (device);

        return sections_to_show;
}

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
        gboolean can_mount;
        gboolean can_unmount;
        gboolean can_eject;
        gboolean can_lock;
        gboolean can_unlock;
        gboolean can_start;
        gboolean can_stop;
        gboolean can_fsck;
        gboolean can_erase;
        static GduPresentable *last_presentable = NULL;
        static gboolean last_showing_job = FALSE;
        gboolean showing_job;
        gboolean reset_sections;
        GList *sections_to_show;
        uid_t unlocked_by_uid;

        job_in_progress = FALSE;
        can_mount = FALSE;
        can_fsck = FALSE;
        can_unmount = FALSE;
        can_eject = FALSE;
        can_unlock = FALSE;
        can_lock = FALSE;
        unlocked_by_uid = 0;
        can_start = FALSE;
        can_stop = FALSE;
        can_erase = FALSE;

        device = gdu_presentable_get_device (shell->priv->presentable_now_showing);
        if (device != NULL) {
                if (gdu_device_job_in_progress (device)) {
                        job_in_progress = TRUE;
                }

                if (GDU_IS_VOLUME (shell->priv->presentable_now_showing)) {

                        if (strcmp (gdu_device_id_get_usage (device), "filesystem") == 0) {
                                GduKnownFilesystem *kfs;

                                kfs = gdu_pool_get_known_filesystem_by_id (shell->priv->pool,
                                                                           gdu_device_id_get_type (device));

                                if (gdu_device_is_mounted (device)) {
                                        can_unmount = TRUE;
                                        if (kfs != NULL && gdu_known_filesystem_get_supports_online_fsck (kfs))
                                                can_fsck = TRUE;
                                } else {
                                        can_mount = TRUE;
                                        can_erase = TRUE;
                                        if (kfs != NULL && gdu_known_filesystem_get_supports_fsck (kfs))
                                                can_fsck = TRUE;
                                }
                        } else if (strcmp (gdu_device_id_get_usage (device), "crypto") == 0) {
                                GList *enclosed_presentables;
                                enclosed_presentables = gdu_pool_get_enclosed_presentables (
                                        shell->priv->pool,
                                        shell->priv->presentable_now_showing);
                                if (enclosed_presentables != NULL && g_list_length (enclosed_presentables) == 1) {
                                        GduPresentable *enclosed_presentable;
                                        GduDevice *enclosed_device;

                                        can_lock = TRUE;

                                        enclosed_presentable = GDU_PRESENTABLE (enclosed_presentables->data);
                                        enclosed_device = gdu_presentable_get_device (enclosed_presentable);
                                        unlocked_by_uid = gdu_device_luks_cleartext_unlocked_by_uid (enclosed_device);
                                        g_object_unref (enclosed_device);
                                } else {
                                        can_unlock = TRUE;
                                        can_erase = TRUE;
                                }
                                g_list_foreach (enclosed_presentables, (GFunc) g_object_unref, NULL);
                                g_list_free (enclosed_presentables);
                        } else {
                                can_erase = TRUE;
                        }
                }

                if (GDU_IS_DRIVE (shell->priv->presentable_now_showing)) {
                        if (gdu_device_is_removable (device) &&
                            gdu_device_is_media_available (device) &&
                            (gdu_device_drive_get_is_media_ejectable (device) ||
                             gdu_device_drive_get_requires_eject (device))) {
                                can_eject = TRUE;
                        }

                        can_erase = TRUE;
                        if (gdu_drive_is_activatable (GDU_DRIVE (shell->priv->presentable_now_showing)) &&
                            !gdu_drive_is_active (GDU_DRIVE (shell->priv->presentable_now_showing)))
                                can_erase = FALSE;
                }
        }

        if (GDU_IS_DRIVE (shell->priv->presentable_now_showing) &&
            gdu_drive_is_activatable (GDU_DRIVE (shell->priv->presentable_now_showing))) {
                GduDrive *drive = GDU_DRIVE (shell->priv->presentable_now_showing);

                can_stop = gdu_drive_can_deactivate (drive);

                can_start = gdu_drive_can_activate (drive, NULL);
        }

        showing_job = job_in_progress;

        reset_sections =
                (shell->priv->presentable_now_showing != last_presentable) ||
                (showing_job != last_showing_job);

        last_presentable = shell->priv->presentable_now_showing;
        last_showing_job = showing_job;


        sections_to_show = compute_sections_to_show (shell, showing_job);

        /* if this differs from what we currently show, prompt a reset */
        if (!reset_sections) {
                GList *children;
                GList *l;
                GList *ll;

                children = gtk_container_get_children (GTK_CONTAINER (shell->priv->sections_vbox));
                if (g_list_length (children) != g_list_length (sections_to_show)) {
                        reset_sections = TRUE;
                } else {
                        for (l = sections_to_show, ll = children; l != NULL; l = l->next, ll = ll->next) {
                                if (G_OBJECT_TYPE (ll->data) != (GType) l->data) {
                                        reset_sections = TRUE;
                                        break;
                                }
                        }
                }
                g_list_free (children);
        }

        if (reset_sections) {
                GList *l;

                /* out with the old... */
                gtk_container_foreach (GTK_CONTAINER (shell->priv->sections_vbox),
                                       remove_section,
                                       shell->priv->sections_vbox);

                /* ... and in with the new */
                for (l = sections_to_show; l != NULL; l = l->next) {
                        GType type = (GType) l->data;
                        GtkWidget *section;

                        section = g_object_new (type,
                                                "shell", shell,
                                                "presentable", shell->priv->presentable_now_showing,
                                                NULL);

                        gtk_widget_show_all (section);

                        gtk_box_pack_start (GTK_BOX (shell->priv->sections_vbox),
                                            section,
                                            TRUE, TRUE, 0);
                }

        }
        g_list_free (sections_to_show);

        /* update all sections */
        gtk_container_foreach (GTK_CONTAINER (shell->priv->sections_vbox),
                               update_section,
                               shell);

        if (can_mount) {
                if (device != NULL) {
                        g_object_set (shell->priv->mount_action,
                                      "polkit-action",
                                      gdu_device_is_system_internal (device) ?
                                      shell->priv->pk_mount_system_internal_action :
                                      shell->priv->pk_mount_action,
                                      NULL);
                }
        }

        if (can_fsck) {
                if (device != NULL) {
                        g_object_set (shell->priv->fsck_action,
                                      "polkit-action",
                                      gdu_device_is_system_internal (device) ?
                                      shell->priv->pk_fsck_system_internal_action :
                                      shell->priv->pk_fsck_action,
                                      NULL);
                }
        }

        if (can_unmount) {
                if (device != NULL) {
                        PolKitAction *action;
                        if (gdu_device_get_mounted_by_uid (device) == getuid ()) {
                                action = NULL;
                        } else {
                                action = shell->priv->pk_unmount_others_action;
                        }
                        g_object_set (shell->priv->unmount_action,
                                      "polkit-action",
                                      action,
                                      NULL);
                }
        }

        if (can_eject) {
                if (device != NULL) {
                        g_object_set (shell->priv->eject_action,
                                      "polkit-action",
                                      shell->priv->pk_eject_action,
                                      NULL);
                }
        }

        if (can_lock) {
                PolKitAction *action;
                action = NULL;
                if (unlocked_by_uid != getuid () && device != NULL) {
                        action = shell->priv->pk_lock_luks_others_action;
                }
                g_object_set (shell->priv->lock_action,
                              "polkit-action",
                              action,
                              NULL);
        }

        if (!gdu_pool_supports_luks_devices (shell->priv->pool)) {
                polkit_gnome_action_set_visible (shell->priv->lock_action, FALSE);
                polkit_gnome_action_set_visible (shell->priv->unlock_action, FALSE);
        }

        if (can_erase) {
                if (device != NULL) {
                        g_object_set (shell->priv->erase_action,
                                      "polkit-action",
                                      gdu_device_is_system_internal (device) ?
                                      shell->priv->pk_modify_system_internal_action :
                                      shell->priv->pk_modify_action,
                                      NULL);
                }
        }

        /* update all GtkActions */
        polkit_gnome_action_set_sensitive (shell->priv->mount_action, can_mount);
        polkit_gnome_action_set_sensitive (shell->priv->fsck_action, can_fsck);
        polkit_gnome_action_set_sensitive (shell->priv->unmount_action, can_unmount);
        polkit_gnome_action_set_sensitive (shell->priv->eject_action, can_eject);
        polkit_gnome_action_set_sensitive (shell->priv->lock_action, can_lock);
        polkit_gnome_action_set_sensitive (shell->priv->unlock_action, can_unlock);
        polkit_gnome_action_set_sensitive (shell->priv->start_action, can_start);
        polkit_gnome_action_set_sensitive (shell->priv->stop_action, can_stop);
        polkit_gnome_action_set_sensitive (shell->priv->erase_action, can_erase);

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

        if (device != NULL)
                g_object_unref (device);
}

static void
presentable_changed (GduPresentable *presentable, gpointer user_data)
{
        GduShell *shell = GDU_SHELL (user_data);
        gdu_shell_update (shell);
}

static void
presentable_job_changed (GduPresentable *presentable, gpointer user_data)
{
        GduShell *shell = GDU_SHELL (user_data);
        if (presentable == shell->priv->presentable_now_showing)
                gdu_shell_update (shell);
}

static void
device_tree_changed (GtkTreeSelection *selection, gpointer user_data)
{
        GduShell *shell = GDU_SHELL (user_data);
        GduPresentable *presentable;
        GtkTreeView *device_tree_view;

        device_tree_view = gtk_tree_selection_get_tree_view (selection);
        presentable = gdu_device_tree_get_selected_presentable (device_tree_view);

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
        GduShell *shell = GDU_SHELL (user_data);
        gdu_shell_update (shell);
}

static void
presentable_removed (GduPool *pool, GduPresentable *presentable, gpointer user_data)
{
        GduShell *shell = GDU_SHELL (user_data);
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
                        gdu_device_tree_select_first_presentable (GTK_TREE_VIEW (shell->priv->treeview));
                        gtk_widget_grab_focus (shell->priv->treeview);
                }
        }
        gdu_shell_update (shell);
}

typedef struct {
        GduShell *shell;
        GduPresentable *presentable;
} ShellPresentableData;

static ShellPresentableData *
shell_presentable_new (GduShell *shell, GduPresentable *presentable)
{
        ShellPresentableData *data;
        data = g_new0 (ShellPresentableData, 1);
        data->shell = g_object_ref (shell);
        data->presentable = g_object_ref (presentable);
        return data;
}

static void
shell_presentable_free (ShellPresentableData *data)
{
        g_object_unref (data->shell);
        g_object_unref (data->presentable);
        g_free (data);
}

static void
fsck_op_callback (GduDevice *device,
                  gboolean   is_clean,
                  GError    *error,
                  gpointer   user_data)
{
        ShellPresentableData *data = user_data;
        if (error != NULL) {
                gdu_shell_raise_error (data->shell,
                                       data->presentable,
                                       error,
                                       _("Error checking file system on device"));
                g_error_free (error);
        } else {
                GtkWidget *dialog;
                char *name;
                GIcon *icon;

                name = gdu_presentable_get_name (data->presentable);
                icon = gdu_presentable_get_icon (data->presentable);

                dialog = gtk_message_dialog_new (
                        GTK_WINDOW (data->shell->priv->app_window),
                        GTK_DIALOG_MODAL|GTK_DIALOG_DESTROY_WITH_PARENT,
                        is_clean ? GTK_MESSAGE_INFO : GTK_MESSAGE_WARNING,
                        GTK_BUTTONS_CLOSE,
                        _("File system check on \"%s\" completed"),
                        name);
                if (is_clean)
                        gtk_message_dialog_format_secondary_text (GTK_MESSAGE_DIALOG (dialog),
                                                                  _("File system is clean."));
                else
                        gtk_message_dialog_format_secondary_text (GTK_MESSAGE_DIALOG (dialog),
                                                                  _("File system is <b>NOT</b> clean."));

                gtk_window_set_title (GTK_WINDOW (dialog), name);
                // TODO: no support for GIcon in GtkWindow
                //gtk_window_set_icon_name (GTK_WINDOW (dialog), icon_name);

                g_signal_connect_swapped (dialog,
                                          "response",
                                          G_CALLBACK (gtk_widget_destroy),
                                          dialog);
                gtk_window_present (GTK_WINDOW (dialog));

                g_free (name);
                if (icon != NULL)
                        g_object_unref (icon);
        }
        shell_presentable_free (data);
}

static void
fsck_action_callback (GtkAction *action, gpointer user_data)
{
        GduShell *shell = GDU_SHELL (user_data);
        GduDevice *device;

        device = gdu_presentable_get_device (shell->priv->presentable_now_showing);
        if (device != NULL) {
                gdu_device_op_filesystem_check (device,
                                                fsck_op_callback,
                                                shell_presentable_new (shell, shell->priv->presentable_now_showing));
                g_object_unref (device);
        }
}

static void
mount_op_callback (GduDevice *device,
                   char      *mount_point,
                   GError    *error,
                   gpointer   user_data)
{
        ShellPresentableData *data = user_data;
        if (error != NULL) {
                gdu_shell_raise_error (data->shell,
                                       data->presentable,
                                       error,
                                       _("Error mounting device"));
                g_error_free (error);
        } else {
                g_free (mount_point);
        }
        shell_presentable_free (data);
}

static void
mount_action_callback (GtkAction *action, gpointer user_data)
{
        GduShell *shell = GDU_SHELL (user_data);
        GduDevice *device;

        device = gdu_presentable_get_device (shell->priv->presentable_now_showing);
        if (device != NULL) {
                gdu_device_op_filesystem_mount (device,
                                                NULL,
                                                mount_op_callback,
                                                shell_presentable_new (shell, shell->priv->presentable_now_showing));
                g_object_unref (device);
        }
}

static gboolean unmount_show_busy (gpointer user_data);

static void
unmount_op_callback (GduDevice *device,
                     GError    *error,
                     gpointer   user_data)
{
        ShellPresentableData *data = user_data;
        if (error != NULL) {
                if (error->domain == GDU_ERROR && error->code == GDU_ERROR_BUSY) {
                        /* show dialog in idle so the job-spinner can be hidden */
                        g_idle_add (unmount_show_busy, data);
                        g_error_free (error);
                } else {
                        gdu_shell_raise_error (data->shell,
                                               data->presentable,
                                               error,
                                               _("Error unmounting device"));
                        g_error_free (error);
                        shell_presentable_free (data);
                }
        } else {
                shell_presentable_free (data);
        }
}

static gboolean
unmount_show_busy (gpointer user_data)
{
        ShellPresentableData *data = user_data;
        if (gdu_util_dialog_show_filesystem_busy (gdu_shell_get_toplevel (data->shell), data->presentable)) {
                /* user managed to kill all applications; try again */
                GduDevice *device;
                device = gdu_presentable_get_device (data->presentable);
                if (device != NULL) {
                        gdu_device_op_filesystem_unmount (device,
                                                          unmount_op_callback,
                                                          shell_presentable_new (data->shell, data->presentable));
                        g_object_unref (device);
                }
        }
        shell_presentable_free (data);
        return FALSE;
}

static void
unmount_action_callback (GtkAction *action, gpointer user_data)
{
        GduShell *shell = GDU_SHELL (user_data);
        GduDevice *device;

        device = gdu_presentable_get_device (shell->priv->presentable_now_showing);
        if (device != NULL) {
                gdu_device_op_filesystem_unmount (device,
                                                  unmount_op_callback,
                                                  shell_presentable_new (shell, shell->priv->presentable_now_showing));
                g_object_unref (device);
        }
}


static void
eject_op_callback (GduDevice *device,
                   GError    *error,
                   gpointer   user_data)
{
        ShellPresentableData *data = user_data;
        if (error != NULL) {
                gdu_shell_raise_error (data->shell,
                                       data->presentable,
                                       error,
                                       _("Error ejecting device"));
                g_error_free (error);
        }
        shell_presentable_free (data);
}

static void
eject_action_callback (GtkAction *action, gpointer user_data)
{
        GduShell *shell = GDU_SHELL (user_data);
        GduDevice *device;

        device = gdu_presentable_get_device (shell->priv->presentable_now_showing);
        if (device != NULL) {
                gdu_device_op_drive_eject (device,
                                           eject_op_callback,
                                           shell_presentable_new (shell, shell->priv->presentable_now_showing));
                g_object_unref (device);
        }
}


static void unlock_action_do (GduShell *shell,
                              GduPresentable *presentable,
                              GduDevice *device,
                              gboolean bypass_keyring,
                              gboolean indicate_wrong_passphrase);

typedef struct {
        GduShell *shell;
        GduPresentable *presentable;
        gboolean asked_user;
} UnlockData;

static UnlockData *
unlock_data_new (GduShell *shell, GduPresentable *presentable, gboolean asked_user)
{
        UnlockData *data;
        data = g_new0 (UnlockData, 1);
        data->shell = g_object_ref (shell);
        data->presentable = g_object_ref (presentable);
        data->asked_user = asked_user;
        return data;
}

static void
unlock_data_free (UnlockData *data)
{
        g_object_unref (data->shell);
        g_object_unref (data->presentable);
        g_free (data);
}

static gboolean
unlock_retry (gpointer user_data)
{
        UnlockData *data = user_data;
        GduDevice *device;
        gboolean indicate_wrong_passphrase;

        device = gdu_presentable_get_device (data->presentable);
        if (device != NULL) {
                indicate_wrong_passphrase = FALSE;

                if (!data->asked_user) {
                        /* if we attempted to unlock the device without asking the user
                         * then the password must have come from the keyring.. hence,
                         * since we failed, the password in the keyring is bad. Remove
                         * it.
                         */
                        g_warning ("removing bad password from keyring");
                        gdu_util_delete_secret (device);
                } else {
                        /* we did ask the user on the last try and that passphrase
                         * didn't work.. make sure the new dialog tells him that
                         */
                        indicate_wrong_passphrase = TRUE;
                }

                unlock_action_do (data->shell, data->presentable, device, TRUE, indicate_wrong_passphrase);
                g_object_unref (device);
        }
        unlock_data_free (data);
        return FALSE;
}

static void
unlock_op_cb (GduDevice *device,
              char      *object_path_of_cleartext_device,
              GError    *error,
              gpointer   user_data)
{
        UnlockData *data = user_data;
        if (error != NULL && error->code == GDU_ERROR_INHIBITED) {
                gdu_shell_raise_error (data->shell,
                                       data->presentable,
                                       error,
                                       _("Error unlocking device"));
                g_error_free (error);
        } else if (error != NULL) {
                /* retry in idle so the job-spinner can be hidden */
                g_idle_add (unlock_retry, data);
                g_error_free (error);
        } else {
                unlock_data_free (data);
                g_free (object_path_of_cleartext_device);
        }
}

static void
unlock_action_do (GduShell *shell,
                  GduPresentable *presentable,
                  GduDevice *device,
                  gboolean bypass_keyring,
                  gboolean indicate_wrong_passphrase)
{
        char *secret;
        gboolean asked_user;

        secret = gdu_util_dialog_ask_for_secret (shell->priv->app_window,
                                                 presentable,
                                                 bypass_keyring,
                                                 indicate_wrong_passphrase,
                                                 &asked_user);
        if (secret != NULL) {
                gdu_device_op_luks_unlock (device,
                                                secret,
                                                unlock_op_cb,
                                                unlock_data_new (shell,
                                                                 shell->priv->presentable_now_showing,
                                                                 asked_user));

                /* scrub the password */
                memset (secret, '\0', strlen (secret));
                g_free (secret);
        }
}

static void
unlock_action_callback (GtkAction *action, gpointer user_data)
{
        GduShell *shell = GDU_SHELL (user_data);
        GduDevice *device;

        device = gdu_presentable_get_device (shell->priv->presentable_now_showing);
        if (device != NULL) {
                unlock_action_do (shell, shell->priv->presentable_now_showing, device, FALSE, FALSE);
                g_object_unref (device);
        }
}

static void
lock_op_callback (GduDevice *device,
                  GError    *error,
                  gpointer   user_data)
{
        ShellPresentableData *data = user_data;

        if (error != NULL) {
                gdu_shell_raise_error (data->shell,
                                       data->presentable,
                                       error,
                                       _("Error locking encrypted device"));
                g_error_free (error);
        }
        shell_presentable_free (data);
}

static void
lock_action_callback (GtkAction *action, gpointer user_data)
{
        GduShell *shell = GDU_SHELL (user_data);
        GduDevice *device;

        device = gdu_presentable_get_device (shell->priv->presentable_now_showing);
        if (device != NULL) {
                gdu_device_op_luks_lock (device,
                                              lock_op_callback,
                                              shell_presentable_new (shell, shell->priv->presentable_now_showing));
                g_object_unref (device);
        }
}

static void
start_cb (GduDrive *ad,
          char *assembled_array_object_path,
          GError *error,
          gpointer user_data)
{
        GduShell *shell = GDU_SHELL (user_data);

        if (error != NULL) {
                GtkWidget *dialog;

                dialog = gtk_message_dialog_new (
                        GTK_WINDOW (shell->priv->app_window),
                        GTK_DIALOG_MODAL|GTK_DIALOG_DESTROY_WITH_PARENT,
                        GTK_MESSAGE_ERROR,
                        GTK_BUTTONS_CLOSE,
                        _("There was an error starting the drive \"%s\"."),
                        gdu_presentable_get_name (GDU_PRESENTABLE (ad)));

                gtk_message_dialog_format_secondary_text (GTK_MESSAGE_DIALOG (dialog), "%s", error->message);
                gtk_dialog_run (GTK_DIALOG (dialog));
                gtk_widget_destroy (dialog);

                g_error_free (error);
                goto out;
        } else {
                g_free (assembled_array_object_path);
        }

out:
        g_object_unref (shell);
}

static void
start_action_callback (GtkAction *action, gpointer user_data)
{
        GduShell *shell = GDU_SHELL (user_data);
        GduDrive *drive;
        gboolean can_activate;
        gboolean degraded;

        if (!GDU_IS_DRIVE (shell->priv->presentable_now_showing) ||
            !gdu_drive_is_activatable (GDU_DRIVE (shell->priv->presentable_now_showing))) {
                g_warning ("presentable is not an activatable drive");
                goto out;
        }

        drive = GDU_DRIVE (shell->priv->presentable_now_showing);

        if (gdu_drive_is_active (drive)) {
                g_warning ("drive already running; refusing to activate it");
                goto out;
        }

        can_activate = gdu_drive_can_activate (drive, &degraded);
        if (!can_activate) {
                g_warning ("cannot activate drive");
                goto out;
        }

        /* ask for consent before activating in degraded mode */
        if (degraded) {
                GtkWidget *dialog;
                int response;

                dialog = gtk_message_dialog_new (
                        GTK_WINDOW (shell->priv->app_window),
                        GTK_DIALOG_MODAL|GTK_DIALOG_DESTROY_WITH_PARENT,
                        GTK_MESSAGE_WARNING,
                        GTK_BUTTONS_CANCEL,
                        _("Are you sure you want to start the drive \"%s\" in degraded mode ?"),
                        gdu_presentable_get_name (GDU_PRESENTABLE (drive)));

                gtk_message_dialog_format_secondary_text (GTK_MESSAGE_DIALOG (dialog),
                                                          _("Starting a RAID array in degraded mode means that "
                                                            "the RAID volume is no longer tolerant to drive "
                                                            "failures. Data on the volume may be irrevocably "
                                                            "lost if a drive fails."));

                gtk_dialog_add_button (GTK_DIALOG (dialog), _("_Start Array"), 0);

                response = gtk_dialog_run (GTK_DIALOG (dialog));
                gtk_widget_destroy (dialog);
                if (response != 0)
                        goto out;
        }

        gdu_drive_activate (drive,
                            start_cb,
                            g_object_ref (shell));
out:
        ;
}

static void
stop_cb (GduDrive *drive, GError *error, gpointer user_data)
{
        GduShell *shell = GDU_SHELL (user_data);

        if (error != NULL) {
                GtkWidget *dialog;

                dialog = gtk_message_dialog_new (
                        GTK_WINDOW (shell->priv->app_window),
                        GTK_DIALOG_MODAL|GTK_DIALOG_DESTROY_WITH_PARENT,
                        GTK_MESSAGE_ERROR,
                        GTK_BUTTONS_CLOSE,
                        _("There was an error stopping the drive \"%s\"."),
                        gdu_presentable_get_name (GDU_PRESENTABLE (drive)));

                gtk_message_dialog_format_secondary_text (GTK_MESSAGE_DIALOG (dialog), "%s", error->message);
                gtk_dialog_run (GTK_DIALOG (dialog));
                gtk_widget_destroy (dialog);

                g_error_free (error);
                goto out;
        }

out:
        g_object_unref (shell);
}

static void
stop_action_callback (GtkAction *action, gpointer user_data)
{
        GduShell *shell = GDU_SHELL (user_data);
        GduDrive *drive;

        if (!GDU_IS_DRIVE (shell->priv->presentable_now_showing) ||
            !gdu_drive_is_activatable (GDU_DRIVE (shell->priv->presentable_now_showing))) {
                g_warning ("presentable is not an activatable drive");
                goto out;
        }

        drive = GDU_DRIVE (shell->priv->presentable_now_showing);

        if (!gdu_drive_can_deactivate (drive)) {
                g_warning ("activatable drive isn't running; refusing to deactivate it");
                goto out;
        }

        gdu_drive_deactivate (drive,
                              stop_cb,
                              g_object_ref (shell));
out:
        ;
}

static void
op_erase_callback (GduDevice *device,
                   GError *error,
                   gpointer user_data)
{
        ShellPresentableData *data = user_data;

        if (error != NULL) {
                gdu_shell_raise_error (data->shell,
                                       data->presentable,
                                       error,
                                       _("Error erasing data"));
                g_error_free (error);
        }

        shell_presentable_free (data);
}

static void
erase_action_callback (GtkAction *action, gpointer user_data)
{
        GduShell *shell = GDU_SHELL (user_data);
        GduPresentable *presentable;
        GduDevice *device;
        GduPresentable *toplevel_presentable;
        GduDevice *toplevel_device;
        gchar *drive_name;
        gchar *primary;
        gchar *secondary;
        gboolean do_erase;

        device = NULL;
        drive_name = NULL;
        primary = NULL;
        secondary = NULL;
        toplevel_presentable = NULL;
        toplevel_device = NULL;

        presentable = shell->priv->presentable_now_showing;
        if (presentable == NULL) {
                g_warning ("%s: no presentable", __FUNCTION__);
                goto out;
        }

        device = gdu_presentable_get_device (shell->priv->presentable_now_showing);
        if (device == NULL) {
                g_warning ("%s: no device", __FUNCTION__);
                goto out;
        }

        toplevel_presentable = gdu_presentable_get_toplevel (presentable);
        if (toplevel_presentable == NULL) {
                g_warning ("%s: no toplevel presentable",  __FUNCTION__);
                goto out;
        }
        toplevel_device = gdu_presentable_get_device (toplevel_presentable);
        if (toplevel_device == NULL) {
                g_warning ("%s: no device for toplevel presentable",  __FUNCTION__);
                goto out;
        }

        drive_name = gdu_presentable_get_name (toplevel_presentable);

        primary = g_strconcat ("<b><big>", _("Are you sure you want to erase the device ?"), "</big></b>", NULL);

        if (gdu_device_is_partition (device)) {
                if (gdu_device_is_removable (toplevel_device)) {
                        secondary = g_strdup_printf (_("All data on partition %d on the media in \"%s\" will be "
                                                       "irrecovably erased. "
                                                       "Make sure important data is backed up. "
                                                       "This action cannot be undone."),
                                                     gdu_device_partition_get_number (device),
                                                     drive_name);
                } else {
                        secondary = g_strdup_printf (_("All data on partition %d of \"%s\" will be "
                                                       "irrecovably erased. "
                                                       "Make sure important data is backed up. "
                                                       "This action cannot be undone."),
                                                     gdu_device_partition_get_number (device),
                                                     drive_name);
                }
        } else {
                if (gdu_device_is_removable (toplevel_device)) {
                        secondary = g_strdup_printf (_("All data on the media in \"%s\" will be irrecovably erased. "
                                                       "Make sure important data is backed up. "
                                                       "This action cannot be undone."),
                                                     drive_name);
                } else {
                        secondary = g_strdup_printf (_("All data on the drive \"%s\" will be irrecovably erased. "
                                                       "Make sure important data is backed up. "
                                                       "This action cannot be undone."),
                                                     drive_name);
                }
        }

        do_erase = gdu_util_delete_confirmation_dialog (gdu_shell_get_toplevel (shell),
                                                        "",
                                                        primary,
                                                        secondary,
                                                        _("_Erase"));
        if (do_erase) {
                gdu_device_op_filesystem_create (device,
                                                 "empty",
                                                 "",
                                                 NULL,
                                                 FALSE,
                                                 op_erase_callback,
                                                 shell_presentable_new (shell, presentable));
        }

out:
        g_free (secondary);
        g_free (primary);
        g_free (drive_name);
        if (toplevel_presentable != NULL)
                g_object_unref (toplevel_presentable);
        if (toplevel_device != NULL)
                g_object_unref (toplevel_device);
        if (device != NULL)
                g_object_unref (device);
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
        GduShell *shell = GDU_SHELL (user_data);
        GdkPixbuf *logo;
        const char *artists[] = {
                "Mike Langlie <mlanglie@redhat.com>",
                NULL
        };
        const char *authors[] = {
                "David Zeuthen <davidz@redhat.com>",
                NULL
        };

        logo = gtk_icon_theme_load_icon (gtk_icon_theme_get_default (),
                                         "palimpsest",
                                         96,
                                         GTK_ICON_LOOKUP_GENERIC_FALLBACK,
                                         NULL);

        gtk_show_about_dialog (GTK_WINDOW (shell->priv->app_window),
                               "program-name", _("Palimpsest Disk Utility"),
                               "version", VERSION,
                               "copyright", "\xc2\xa9 2008 Red Hat, Inc.",
                               "authors", authors,
                               "artists", artists,
                               "translator-credits", _("translator-credits"),
                               "logo", logo,
                               NULL);
        if (logo != NULL)
                g_object_unref (logo);
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
        "      <menuitem action='fsck'/>"
        "      <separator/>"
        "      <menuitem action='unlock'/>"
        "      <menuitem action='lock'/>"
        "      <separator/>"
        "      <menuitem action='start'/>"
        "      <menuitem action='stop'/>"
        "      <separator/>"
        "      <menuitem action='erase'/>"
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
        "    <toolitem action='fsck'/>"
        "    <separator/>"
        "    <toolitem action='unlock'/>"
        "    <toolitem action='lock'/>"
        "    <separator/>"
        "    <toolitem action='start'/>"
        "    <toolitem action='stop'/>"
        "    <separator/>"
        "    <toolitem action='erase'/>"
        "  </toolbar>"
        "</ui>";

static GtkActionEntry entries[] = {
        {"file", NULL, N_("_File"), NULL, NULL, NULL },
        {"edit", NULL, N_("_Edit"), NULL, NULL, NULL },
        {"help", NULL, N_("_Help"), NULL, NULL, NULL },

        {"quit", GTK_STOCK_QUIT, N_("_Quit"), "<Ctrl>Q", N_("Quit"),
         G_CALLBACK (quit_action_callback)},
        {"contents", GTK_STOCK_HELP, N_("_Help"), "F1", N_("Get Help on Palimpsest Disk Utility"),
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

        shell->priv->fsck_action = polkit_gnome_action_new_default ("fsck",
                                                                    shell->priv->pk_fsck_action,
                                                                    _("_Check File System"),
                                                                    _("Check the file system"));
        g_object_set (shell->priv->fsck_action,
                      "auth-label", _("_Check File System..."),
                      "auth-short-label", _("_Check"),
                      "yes-short-label", _("_Check"),
                      "no-short-label", _("_Check"),
                      "yes-icon-name", "gdu-check-disk",
                      "no-icon-name", "gdu-check-disk",
                      "auth-icon-name", "gdu-check-disk",
                      "self-blocked-icon-name", "gdu-check-disk",
                      NULL);
        g_signal_connect (shell->priv->fsck_action, "activate", G_CALLBACK (fsck_action_callback), shell);
        gtk_action_group_add_action (shell->priv->action_group, GTK_ACTION (shell->priv->fsck_action));

        /* -------------------------------------------------------------------------------- */

        shell->priv->mount_action = polkit_gnome_action_new_default ("mount",
                                                                     shell->priv->pk_mount_action,
                                                                     _("_Mount"),
                                                                     _("Mount the device"));
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
                                                                       NULL,
                                                                       _("_Unmount"),
                                                                       _("Unmount the device"));
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
                                                                     NULL,
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
                                                                      shell->priv->pk_unlock_luks_action,
                                                                      _("_Unlock"),
                                                                      _("Unlock the encrypted device, making the data available in cleartext"));
        g_object_set (shell->priv->unlock_action,
                      "auth-label", _("_Unlock..."),
                      "yes-icon-name", "gdu-encrypted-unlock",
                      "no-icon-name", "gdu-encrypted-unlock",
                      "auth-icon-name", "gdu-encrypted-unlock",
                      "self-blocked-icon-name", "gdu-encrypted-unlock",
                      NULL);
        g_signal_connect (shell->priv->unlock_action, "activate", G_CALLBACK (unlock_action_callback), shell);
        gtk_action_group_add_action (shell->priv->action_group, GTK_ACTION (shell->priv->unlock_action));

        /* -------------------------------------------------------------------------------- */

        shell->priv->lock_action = polkit_gnome_action_new_default ("lock",
                                                                    NULL,
                                                                    _("_Lock"),
                                                                    _("Lock the encrypted device, making the cleartext data unavailable"));
        g_object_set (shell->priv->lock_action,
                      "auth-label", _("_Lock..."),
                      "yes-icon-name", "gdu-encrypted-lock",
                      "no-icon-name", "gdu-encrypted-lock",
                      "auth-icon-name", "gdu-encrypted-lock",
                      "self-blocked-icon-name", "gdu-encrypted-lock",
                      NULL);
        g_signal_connect (shell->priv->lock_action, "activate", G_CALLBACK (lock_action_callback), shell);
        gtk_action_group_add_action (shell->priv->action_group, GTK_ACTION (shell->priv->lock_action));

        /* -------------------------------------------------------------------------------- */

        shell->priv->start_action = polkit_gnome_action_new_default ("start",
                                                                     shell->priv->pk_linux_md_action,
                                                                     _("_Start"),
                                                                     _("Start the array"));
        g_object_set (shell->priv->start_action,
                      "auth-label", _("_Start..."),
                      "yes-icon-name", "gdu-raid-array-start",
                      "no-icon-name", "gdu-raid-array-start",
                      "auth-icon-name", "gdu-raid-array-start",
                      "self-blocked-icon-name", "gdu-raid-array-start",
                      NULL);
        g_signal_connect (shell->priv->start_action, "activate", G_CALLBACK (start_action_callback), shell);
        gtk_action_group_add_action (shell->priv->action_group, GTK_ACTION (shell->priv->start_action));

        /* -------------------------------------------------------------------------------- */

        shell->priv->stop_action = polkit_gnome_action_new_default ("stop",
                                                                    shell->priv->pk_linux_md_action,
                                                                    _("_Stop"),
                                                                    _("Stop the array"));
        g_object_set (shell->priv->stop_action,
                      "auth-label", _("_Stop..."),
                      "yes-icon-name", "gdu-raid-array-stop",
                      "no-icon-name", "gdu-raid-array-stop",
                      "auth-icon-name", "gdu-raid-array-stop",
                      "self-blocked-icon-name", "gdu-raid-array-stop",
                      NULL);
        g_signal_connect (shell->priv->stop_action, "activate", G_CALLBACK (stop_action_callback), shell);
        gtk_action_group_add_action (shell->priv->action_group, GTK_ACTION (shell->priv->stop_action));

        /* -------------------------------------------------------------------------------- */

        shell->priv->erase_action = polkit_gnome_action_new_default ("erase",
                                                                     shell->priv->pk_modify_action,
                                                                     _("_Erase..."),
                                                                     _("Erase the contents of the selected device"));
        g_object_set (shell->priv->erase_action,
                      "auth-short-label", _("_Erase"),
                      "no-short-label", _("_Erase"),
                      "yes-short-label", _("_Erase"),
                      "yes-icon-name", GTK_STOCK_CLEAR,
                      "no-icon-name", GTK_STOCK_CLEAR,
                      "auth-icon-name", GTK_STOCK_CLEAR,
                      "self-blocked-icon-name", GTK_STOCK_CLEAR,
                      NULL);
        g_signal_connect (shell->priv->erase_action, "activate", G_CALLBACK (erase_action_callback), shell);
        gtk_action_group_add_action (shell->priv->action_group, GTK_ACTION (shell->priv->erase_action));

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
        shell->priv->pk_modify_action = polkit_action_new ();
        polkit_action_set_action_id (shell->priv->pk_modify_action,
                                     "org.freedesktop.devicekit.disks.change");

        shell->priv->pk_modify_system_internal_action = polkit_action_new ();
        polkit_action_set_action_id (shell->priv->pk_modify_system_internal_action,
                                     "org.freedesktop.devicekit.disks.change-system-internal");

        shell->priv->pk_fsck_action = polkit_action_new ();
        polkit_action_set_action_id (shell->priv->pk_fsck_action,
                                     "org.freedesktop.devicekit.disks.filesystem-check");

        shell->priv->pk_fsck_system_internal_action = polkit_action_new ();
        polkit_action_set_action_id (shell->priv->pk_fsck_system_internal_action,
                                     "org.freedesktop.devicekit.disks.filesystem-check-system-internal");

        shell->priv->pk_mount_action = polkit_action_new ();
        polkit_action_set_action_id (shell->priv->pk_mount_action,
                                     "org.freedesktop.devicekit.disks.filesystem-mount");

        shell->priv->pk_mount_system_internal_action = polkit_action_new ();
        polkit_action_set_action_id (shell->priv->pk_mount_system_internal_action,
                                     "org.freedesktop.devicekit.disks.filesystem-mount-system-internal");

        shell->priv->pk_unmount_others_action = polkit_action_new ();
        polkit_action_set_action_id (shell->priv->pk_unmount_others_action,
                                     "org.freedesktop.devicekit.disks.filesystem-unmount-others");


        shell->priv->pk_unlock_luks_action = polkit_action_new ();
        polkit_action_set_action_id (shell->priv->pk_unlock_luks_action,
                                     "org.freedesktop.devicekit.disks.luks-unlock");

        shell->priv->pk_lock_luks_others_action = polkit_action_new ();
        polkit_action_set_action_id (shell->priv->pk_lock_luks_others_action,
                                     "org.freedesktop.devicekit.disks.luks-lock-others");

        shell->priv->pk_linux_md_action = polkit_action_new ();
        polkit_action_set_action_id (shell->priv->pk_linux_md_action,
                                     "org.freedesktop.devicekit.disks.linux-md");
}

static void
url_activated (SexyUrlLabel *url_label,
               const char   *url,
               gpointer      user_data)
{
        char *s;
        /* TODO: startup notification, determine what file manager to use etc. */
        s = g_strdup_printf ("nautilus \"%s\"", url);
        g_spawn_command_line_async (s, NULL);
        g_free (s);
}

static void
fix_focus_cb (GtkDialog *dialog, gpointer data)
{
        GtkWidget *button;

        button = gtk_window_get_default_widget (GTK_WINDOW (dialog));
        gtk_widget_grab_focus (button);
}

static void
expander_cb (GtkExpander *expander, GParamSpec *pspec, GtkWindow *dialog)
{
        gtk_window_set_resizable (dialog, gtk_expander_get_expanded (expander));
}

/**
 * gdu_shell_raise_error:
 * @shell: An object implementing the #GduShell interface
 * @presentable: The #GduPresentable for which the error was rasied
 * @error: The #GError obtained from the operation
 * @primary_markup_format: Format string for the primary markup text of the dialog
 * @...: Arguments for markup string
 *
 * Show the user (through a dialog or other means (e.g. cluebar)) that an error occured.
 **/
void
gdu_shell_raise_error (GduShell       *shell,
                       GduPresentable *presentable,
                       GError         *error,
                       const char     *primary_markup_format,
                       ...)
{
        GtkWidget *dialog;
        char *error_text;
        char *window_title;
        GIcon *window_icon;
        va_list args;
        char *error_msg;
        GtkWidget *box, *hbox, *expander, *sw, *tv;
        GList *children;
        GtkTextBuffer *buffer;

        g_return_if_fail (shell != NULL);
        g_return_if_fail (presentable != NULL);
        g_return_if_fail (error != NULL);

        window_title = gdu_presentable_get_name (presentable);
        window_icon = gdu_presentable_get_icon (presentable);

        va_start (args, primary_markup_format);
        error_text = g_strdup_vprintf (primary_markup_format, args);
        va_end (args);

        switch (error->code) {
        case GDU_ERROR_FAILED:
                error_msg = _("The operation failed.");
                break;
        case GDU_ERROR_BUSY:
                error_msg = _("The device is busy.");
                break;
        case GDU_ERROR_CANCELLED:
                error_msg = _("The operation was cancelled.");
                break;
        case GDU_ERROR_INHIBITED:
                error_msg = _("The daemon is being inhibited.");
                break;
        case GDU_ERROR_INVALID_OPTION:
                error_msg = _("An invalid option was passed.");
                break;
        case GDU_ERROR_NOT_SUPPORTED:
                error_msg = _("The operation is not supported.");
                break;
        case GDU_ERROR_ATA_SMART_WOULD_WAKEUP:
                error_msg = _("Getting ATA SMART data would wake up the device.");
                break;
        default:
                error_msg = _("Unknown error");
                break;
        }

        dialog = gtk_message_dialog_new_with_markup (
                GTK_WINDOW (shell->priv->app_window),
                GTK_DIALOG_MODAL|GTK_DIALOG_DESTROY_WITH_PARENT,
                GTK_MESSAGE_ERROR,
                GTK_BUTTONS_CLOSE,
                "<big><b>%s</b></big>",
                error_text);
        gtk_message_dialog_format_secondary_text (GTK_MESSAGE_DIALOG (dialog), "%s", error_msg);

	/* Here we cheat a little by poking in the messagedialog internals
         * to add the details expander to the inner vbox and arrange things
         * so that resizing the dialog works as expected.
         */
	box = gtk_dialog_get_content_area (GTK_DIALOG (dialog));
	children = gtk_container_get_children (GTK_CONTAINER (box));
	hbox = GTK_WIDGET (children->data);
	gtk_container_child_set (GTK_CONTAINER (box), hbox,
                                 "expand", TRUE,
                                 "fill", TRUE,
                                 NULL);
	g_list_free (children);
	children = gtk_container_get_children (GTK_CONTAINER (hbox));
	box = GTK_WIDGET (children->next->data);
	g_list_free (children);
	children = gtk_container_get_children (GTK_CONTAINER (box));
	gtk_container_child_set (GTK_CONTAINER (box), GTK_WIDGET (children->next->data),
                                 "expand", FALSE,
                                 "fill", FALSE,
                                 NULL);
	g_list_free (children);

	expander = g_object_new (GTK_TYPE_EXPANDER,
                                 "label", _("_Details:"),
                                 "use-underline", TRUE,
                                 "use-markup", TRUE,
                                 NULL);
        sw = g_object_new (GTK_TYPE_SCROLLED_WINDOW,
                           "hscrollbar-policy", GTK_POLICY_AUTOMATIC,
                           "vscrollbar-policy", GTK_POLICY_AUTOMATIC,
                           "shadow-type", GTK_SHADOW_IN,
                           NULL);
        buffer = gtk_text_buffer_new (NULL);
        gtk_text_buffer_set_text (buffer, error->message, -1);
	tv = gtk_text_view_new_with_buffer (buffer);
        gtk_text_view_set_editable (GTK_TEXT_VIEW (tv), FALSE);

        gtk_container_add (GTK_CONTAINER (sw), tv);
        gtk_container_add (GTK_CONTAINER (expander), sw);
	gtk_box_pack_end (GTK_BOX (box), expander, TRUE, TRUE, 0);
        gtk_widget_show_all (expander);

        /* Make the window resizable when the details are visible
         */
	g_signal_connect (expander, "notify::expanded", G_CALLBACK (expander_cb), dialog);

        /* We don't want the initial focus to end up on the expander,
         * so grab it to the close button on map.
         */
        gtk_dialog_set_default_response (GTK_DIALOG (dialog), GTK_RESPONSE_CLOSE);
        g_signal_connect (dialog, "map", G_CALLBACK (fix_focus_cb), NULL);


        gtk_window_set_title (GTK_WINDOW (dialog), window_title);
        // TODO: no support for GIcon in GtkWindow
        //gtk_window_set_icon_name (GTK_WINDOW (dialog), window_icon_name);

        g_signal_connect_swapped (dialog,
                                  "response",
                                  G_CALLBACK (gtk_widget_destroy),
                                  dialog);
        gtk_window_present (GTK_WINDOW (dialog));

        g_free (window_title);
        if (window_icon != NULL)
                g_object_unref (window_icon);
        g_free (error_text);
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
        gtk_window_set_title (GTK_WINDOW (shell->priv->app_window), _("Palimpsest Disk Utility"));

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
                                        GTK_POLICY_NEVER,
                                        GTK_POLICY_AUTOMATIC);
        gtk_scrolled_window_set_shadow_type (GTK_SCROLLED_WINDOW (treeview_scrolled_window),
                                             GTK_SHADOW_IN);
        shell->priv->treeview = gdu_device_tree_new (shell->priv->pool);
        gtk_container_add (GTK_CONTAINER (treeview_scrolled_window), shell->priv->treeview);

        vbox2 = gtk_vbox_new (FALSE, 0);
        gtk_container_set_border_width (GTK_CONTAINER (vbox2), 12);

        /* --- */
        GtkWidget *label;
        GtkWidget *align;
        GtkWidget *vbox3;
        GtkWidget *hbox;
        GtkWidget *image;

        hbox = gtk_hbox_new (FALSE, 12);
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

        label = sexy_url_label_new ();
        gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
        gtk_box_pack_start (GTK_BOX (vbox3), label, FALSE, TRUE, 0);
        shell->priv->details0_label = label;

        label = sexy_url_label_new ();
        gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
        gtk_box_pack_start (GTK_BOX (vbox3), label, FALSE, TRUE, 0);
        shell->priv->details1_label = label;

        label = sexy_url_label_new ();
        gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
        gtk_box_pack_start (GTK_BOX (vbox3), label, FALSE, TRUE, 0);
        shell->priv->details2_label = label;

        label = sexy_url_label_new ();
        gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
        gtk_box_pack_start (GTK_BOX (vbox3), label, FALSE, TRUE, 0);
        shell->priv->details3_label = label;

        g_signal_connect (shell->priv->details1_label, "url-activated", (GCallback) url_activated, shell);
        g_signal_connect (shell->priv->details2_label, "url-activated", (GCallback) url_activated, shell);
        g_signal_connect (shell->priv->details3_label, "url-activated", (GCallback) url_activated, shell);

        /* --- */

        shell->priv->sections_vbox = gtk_vbox_new (FALSE, 18);
        gtk_container_set_border_width (GTK_CONTAINER (shell->priv->sections_vbox), 8);
        gtk_box_pack_start (GTK_BOX (vbox2), shell->priv->sections_vbox, TRUE, TRUE, 0);


        /* setup and add horizontal pane */
        hpane = gtk_hpaned_new ();
        gtk_paned_add1 (GTK_PANED (hpane), treeview_scrolled_window);
        gtk_paned_add2 (GTK_PANED (hpane), vbox2);
        //gtk_paned_set_position (GTK_PANED (hpane), 260);

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

        gdu_device_tree_select_first_presentable (GTK_TREE_VIEW (shell->priv->treeview));
}

