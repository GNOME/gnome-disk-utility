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

static GduPool *device_pool;
static GtkWidget *notebook;

static GtkWidget *summary_vbox;

static GtkUIManager *ui_manager;

static void
_remove_child (GtkWidget *widget, gpointer user_data)
{
        GtkContainer *container = GTK_CONTAINER (user_data);
        gtk_container_remove (container, widget);
}

static const char *
gpt_part_type_guid_to_string (const char *guid)
{
        int n;
        /* see also http://en.wikipedia.org/wiki/GUID_Partition_Table */
        static struct {
                const char *guid;
                char *name;
        } part_type[] = {
                {"024DEE41-33E7-11D3-9D69-0008C781F39F", N_("MBR Partition Scheme")},
                {"C12A7328-F81F-11D2-BA4B-00A0C93EC93B", N_("EFI System Partition")},
                /* Microsoft */
                {"E3C9E316-0B5C-4DB8-817D-F92DF00215AE", N_("Microsoft Reserved Partition")},
                {"EBD0A0A2-B9E5-4433-87C0-68B6B72699C7", N_("Basic Data Partition")},
                {"5808C8AA-7E8F-42E0-85D2-E1E90434CFB3", N_("LDM meta data Partition")},
                {"AF9B60A0-1431-4F62-BC68-3311714A69AD", N_("LDM data Partition")},
                /* Linux */
                {"EBD0A0A2-B9E5-4433-87C0-68B6B72699C7", N_("Basic Data Partition")}, /* Same GUID as MS! */
                {"A19D880F-05FC-4D3B-A006-743F0F84911E", N_("Linux RAID Partition")},
                {"0657FD6D-A4AB-43C4-84E5-0933C84B4F4F", N_("Linux Swap Partition")},
                {"E6D6D379-F507-44C2-A23C-238F2A3DF928", N_("Linux LVM Partition")},
                {"8DA63339-0007-60C0-C436-083AC8230908", N_("Linux Reserved Partition")},
                /* Mac OS X */
                {"48465300-0000-11AA-AA11-00306543ECAC", N_("Apple HFS/HFS+ Partition")},
                {"55465300-0000-11AA-AA11-00306543ECAC", N_("Apple UFS Partition")},
                {"52414944-0000-11AA-AA11-00306543ECAC", N_("Apple RAID Partition")},
                /* TODO: add more entries */
                {NULL,  NULL}
        };

        for (n = 0; part_type[n].name != NULL; n++) {
                if (g_ascii_strcasecmp (part_type[n].guid, guid) == 0)
                        return part_type[n].name;
        }

        return guid;
}

static const char *
apm_part_type_to_string (const char *type)
{
        int n;
        /* see also http://developer.apple.com/documentation/mac/Devices/Devices-126.html
         * and http://lists.apple.com/archives/Darwin-drivers/2003/May/msg00021.html */
        static struct {
                const char *type;
                char *name;
        } part_type[] = {
                {"Apple_Unix_SVR2", N_("Apple UFS Partition")},
                {"Apple_HFS", N_("Apple HFS/HFS+ Partition")},
                {"Apple_partition_map", N_("Apple Partition Map")},
                {"DOS_FAT_12", N_("FAT 12")},
                {"DOS_FAT_16", N_("FAT 16")},
                {"DOS_FAT_32", N_("FAT 32")},
                {"Windows_FAT_16", N_("FAT 16")},
                {"Windows_FAT_32", N_("FAT 32")},
                /* TODO: add more entries */
                {NULL,  NULL}
        };

        for (n = 0; part_type[n].name != NULL; n++) {
                if (g_ascii_strcasecmp (part_type[n].type, type) == 0)
                        return part_type[n].name;
        }

        return type;
}

static const char *
msdos_part_type_to_string (int msdos_type)
{
        int n;
        /* see also http://www.win.tue.nl/~aeb/partitions/partition_types-1.html */
        static struct {
                int type;
                char *name;
        } part_type[] = {
                {0x00,  N_("Empty")},
                {0x01,  N_("FAT12")},
                {0x04,  N_("FAT16 <32M")},
                {0x05,  N_("Extended")},
                {0x06,  N_("FAT16")},
                {0x07,  N_("HPFS/NTFS")},
                {0x0b,  N_("W95 FAT32")},
                {0x0c,  N_("W95 FAT32 (LBA)")},
                {0x0e,  N_("W95 FAT16 (LBA)")},
                {0x0f,  N_("W95 Ext d (LBA)")},
                {0x10,  N_("OPUS")},
                {0x11,  N_("Hidden FAT12")},
                {0x12,  N_("Compaq diagnostics")},
                {0x14,  N_("Hidden FAT16 <32M")},
                {0x16,  N_("Hidden FAT16")},
                {0x17,  N_("Hidden HPFS/NTFS")},
                {0x1b,  N_("Hidden W95 FAT32")},
                {0x1c,  N_("Hidden W95 FAT32 (LBA)")},
                {0x1e,  N_("Hidden W95 FAT16 (LBA)")},
                {0x3c,  N_("PartitionMagic")},
                {0x82,  N_("Linux swap")},
                {0x83,  N_("Linux")},
                {0x84,  N_("Hibernation")},
                {0x85,  N_("Linux Extended")},
                {0x8e,  N_("Linux LVM")},
                {0xa0,  N_("Hibernation")},
                {0xa5,  N_("FreeBSD")},
                {0xa6,  N_("OpenBSD")},
                {0xa8,  N_("Mac OS X")},
                {0xaf,  N_("Mac OS X")},
                {0xbe,  N_("Solaris boot")},
                {0xbf,  N_("Solaris")},
                {0xeb,  N_("BeOS BFS")},
                {0xec,  N_("SkyOS SkyFS")},
                {0xee,  N_("EFI GPT")},
                {0xef,  N_("EFI (FAT-12/16/32")},
                {0xfd,  N_("Linux RAID autodetect")},
                {0x00,  NULL}
        };

        for (n = 0; part_type[n].name != NULL; n++) {
                if (part_type[n].type == msdos_type)
                        return part_type[n].name;
        }

        return _("Unknown");
}

static void
info_page_show_for_device (GduDevice *device)
{
        int n;
        GList *i;
        GList *kv_pairs;
        int num_pairs;
        GtkTable *info_table;

        /* delete all old widgets */
        gtk_container_foreach (GTK_CONTAINER (summary_vbox),
                               _remove_child,
                               summary_vbox);

        kv_pairs = NULL;

        if (gdu_device_is_drive (device)) {
                kv_pairs = g_list_prepend (kv_pairs, g_strdup (_("Vendor")));
                kv_pairs = g_list_prepend (kv_pairs, g_strdup (gdu_device_drive_get_vendor (device)));
                kv_pairs = g_list_prepend (kv_pairs, g_strdup (_("Model")));
                kv_pairs = g_list_prepend (kv_pairs, g_strdup (gdu_device_drive_get_model (device)));
                kv_pairs = g_list_prepend (kv_pairs, g_strdup (_("Revision")));
                kv_pairs = g_list_prepend (kv_pairs, g_strdup (gdu_device_drive_get_revision (device)));
                kv_pairs = g_list_prepend (kv_pairs, g_strdup (_("Serial Number")));
                kv_pairs = g_list_prepend (kv_pairs, g_strdup (gdu_device_drive_get_serial (device)));

                kv_pairs = g_list_prepend (kv_pairs, g_strdup (_("Device File")));
                kv_pairs = g_list_prepend (kv_pairs, g_strdup (gdu_device_get_device_file (device)));
                kv_pairs = g_list_prepend (kv_pairs, g_strdup (_("Connection")));
                kv_pairs = g_list_prepend (kv_pairs, g_strdup (_("-"))); /* TODO */

                kv_pairs = g_list_prepend (kv_pairs, g_strdup (_("Removable Media")));
                if (gdu_device_is_removable (device)) {
                        if (gdu_device_is_media_available (device)) {
                                kv_pairs = g_list_prepend (kv_pairs, g_strdup (_("Yes")));
                        } else {
                                kv_pairs = g_list_prepend (kv_pairs, g_strdup (_("Yes (No media inserted)")));
                        }
                } else {
                        kv_pairs = g_list_prepend (kv_pairs, g_strdup (_("No")));
                }
                kv_pairs = g_list_prepend (kv_pairs, g_strdup (_("Media Type")));
                kv_pairs = g_list_prepend (kv_pairs, g_strdup (_("Disk"))); /* TODO */
                kv_pairs = g_list_prepend (kv_pairs, g_strdup (_("Capacity")));
                if (gdu_device_is_media_available (device)) {
                        kv_pairs = g_list_prepend (kv_pairs,
                                                   gdu_util_get_size_for_display (gdu_device_get_size (device), TRUE));
                } else {
                        kv_pairs = g_list_prepend (kv_pairs, g_strdup (_("-")));
                }

                kv_pairs = g_list_prepend (kv_pairs, g_strdup (_("Partitioning")));
                if (gdu_device_is_partition_table (device)) {
                        const char *scheme;
                        char *name;
                        scheme = gdu_device_partition_table_get_scheme (device);
                        if (strcmp (scheme, "apm") == 0) {
                                name = g_strdup (_("Apple Partition Map"));
                        } else if (strcmp (scheme, "mbr") == 0) {
                                name = g_strdup (_("Master Boot Record"));
                        } else if (strcmp (scheme, "gpt") == 0) {
                                name = g_strdup (_("GUID Partition Table"));
                        } else {
                                name = g_strdup_printf (_("Unknown (%s)"), scheme);
                        }
                        kv_pairs = g_list_prepend (kv_pairs, name);
                } else {
                        kv_pairs = g_list_prepend (kv_pairs, g_strdup (_("-")));
                }
        } else {
                kv_pairs = g_list_prepend (kv_pairs, g_strdup (_("Mount Point")));
                kv_pairs = g_list_prepend (kv_pairs, g_strdup (_("-"))); /* TODO */
                kv_pairs = g_list_prepend (kv_pairs, g_strdup (_("Label")));
                kv_pairs = g_list_prepend (kv_pairs, g_strdup (gdu_device_id_get_label (device)));
                kv_pairs = g_list_prepend (kv_pairs, g_strdup (_("Device File")));
                kv_pairs = g_list_prepend (kv_pairs, g_strdup (gdu_device_get_device_file (device)));
                kv_pairs = g_list_prepend (kv_pairs, g_strdup (_("UUID")));
                kv_pairs = g_list_prepend (kv_pairs, g_strdup (gdu_device_id_get_uuid (device)));
                kv_pairs = g_list_prepend (kv_pairs, g_strdup (_("Partition Number")));
                if (gdu_device_is_partition (device)) {
                        kv_pairs = g_list_prepend (kv_pairs, g_strdup_printf (
                                                           "%d",
                                                           gdu_device_partition_get_number (device)));
                } else {
                        kv_pairs = g_list_prepend (kv_pairs, g_strdup (_("-")));
                }
                kv_pairs = g_list_prepend (kv_pairs, g_strdup (_("Partition Type")));
                if (gdu_device_is_partition (device)) {
                        const char *type;
                        const char *scheme;
                        char *name;

                        type = gdu_device_partition_get_type (device);
                        scheme = gdu_device_partition_get_scheme (device);

                        if (strcmp (scheme, "gpt") == 0) {
                                name = g_strdup (gpt_part_type_guid_to_string (type));
                        } else if (strcmp (scheme, "mbr") == 0) {
                                int msdos_type;
                                msdos_type = strtol (type, NULL, 0);
                                name = g_strdup_printf (_("%s (0x%02x)"),
                                                        msdos_part_type_to_string (msdos_type),
                                                        msdos_type);
                        } else if (strcmp (scheme, "apm") == 0) {
                                name = g_strdup (apm_part_type_to_string (type));
                        } else {
                                if (strlen (type) > 0)
                                        name = g_strdup_printf (_("Unknown (%s)"), type);
                                else
                                        name = g_strdup (_("-"));
                        }
                        kv_pairs = g_list_prepend (kv_pairs, name);
                } else {
                        kv_pairs = g_list_prepend (kv_pairs, g_strdup (_("-")));
                }
                kv_pairs = g_list_prepend (kv_pairs, g_strdup (_("Usage")));
                {
                        const char *usage;
                        char *name;
                        usage = gdu_device_id_get_usage (device);
                        if (strcmp (usage, "filesystem") == 0) {
                                name = g_strdup (_("File system"));
                        } else if (strcmp (usage, "crypto") == 0) {
                                name = g_strdup (_("Encrypted Block Device"));
                        } else if (strcmp (usage, "raid") == 0) {
                                name = g_strdup (_("Assembled Block Device"));
                        } else {
                                if (strlen (usage) > 0)
                                        name = g_strdup_printf (_("Unknown (%s)"), usage);
                                else
                                        name = g_strdup (_("-"));
                        }
                        kv_pairs = g_list_prepend (kv_pairs, name);
                }
                kv_pairs = g_list_prepend (kv_pairs, g_strdup (_("Capacity")));
                kv_pairs = g_list_prepend (kv_pairs,
                                           gdu_util_get_size_for_display (gdu_device_get_size (device), TRUE));
                kv_pairs = g_list_prepend (kv_pairs, g_strdup (_("Type")));
                kv_pairs = g_list_prepend (kv_pairs,
                                           gdu_util_get_fstype_for_display (gdu_device_id_get_type (device),
                                                                            gdu_device_id_get_version (device),
                                                                            TRUE));
                kv_pairs = g_list_prepend (kv_pairs, g_strdup (_("Available")));
                kv_pairs = g_list_prepend (kv_pairs, g_strdup (_("-"))); /* TODO */
        }


        kv_pairs = g_list_reverse (kv_pairs);

        if (kv_pairs != NULL) {
                int num_rows;

                num_pairs = g_list_length (kv_pairs) / 2;
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
                g_list_free (kv_pairs);

                /* add property pairs */
                gtk_box_pack_start (GTK_BOX (summary_vbox), GTK_WIDGET (info_table), FALSE, FALSE, 0);
        }

        gtk_widget_show_all (summary_vbox);
}

static GduDevice *now_showing = NULL;

static void
do_action (const char *action)
{
#if 0
        if (now_showing != NULL) {
                char *cmdline;
                const char *object_path;
                object_path = gdu_device_get_object_path (now_showing);

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
update_action_buttons (GduDevice *device)
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
device_tree_changed (GtkTreeSelection *selection, gpointer user_data)
{
        GduDevice *device;
        GtkTreeView *device_tree_view;

        device_tree_view = gtk_tree_selection_get_tree_view (selection);
        device = gdu_tree_get_selected_device (device_tree_view);

        if (device != NULL) {
                now_showing = device;
                info_page_show_for_device (device);
                update_action_buttons (device);
        }
}

static void
device_removed (GduPool *pool, GduDevice *device, gpointer user_data)
{
        //GtkTreeView *treeview = GTK_TREE_VIEW (user_data);
        /* TODO FIX: if device we currently show is removed.. go to computer device */
}

static void
device_changed (GduPool *pool, GduDevice *device, gpointer user_data)
{
        //GtkTreeView *treeview = GTK_TREE_VIEW (user_data);
        /* TODO FIX: if device we currently show is removed.. go to computer device */
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

        device_pool = gdu_pool_new ();
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
        treeview = GTK_WIDGET (gdu_tree_new (device_pool));
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
#if 0
        gdu_tree_select_device (GTK_TREE_VIEW (treeview),
                                gdu_pool_get_device_by_udi (device_pool,
                                                            "/org/freedesktop/Hal/devices/computer"));
#endif

        g_signal_connect (device_pool, "device_removed", (GCallback) device_removed, treeview);
        g_signal_connect (device_pool, "device_changed", (GCallback) device_changed, treeview);

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
