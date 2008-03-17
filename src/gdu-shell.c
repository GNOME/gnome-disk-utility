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

struct _GduShellPrivate
{
        GtkWidget *app_window;
        GduPool *pool;

        PolKitAction *pk_mount_action;
        PolKitAction *pk_erase_action;

        PolKitGnomeAction *mount_action;
        PolKitGnomeAction *unmount_action;
        PolKitGnomeAction *eject_action;
        PolKitGnomeAction *erase_action;

        GduPresentable *presentable_now_showing;

        GtkUIManager *ui_manager;

        GtkWidget *cluebar;
        GtkWidget *cluebar_label;
        GtkWidget *cluebar_progress_bar;
        GtkWidget *cluebar_button;
        int cluebar_pulse_timer_id;

        GtkWidget *notebook;
        GtkWidget *summary_page;
        GtkWidget *erase_page;

        GtkWidget *page_erase_label_entry;
        GtkWidget *page_erase_type_combo_box;

        GList *table_labels;

        int secure_erase_option;
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

/* ---------------------------------------------------------------------------------------------------- */


static gboolean fstype_combo_box_select (GtkComboBox *combo_box, const char *fstype);

static void shell_update (GduShell *shell);


static char *
get_job_description (const char *job_id)
{
        char *s;
        if (strcmp (job_id, "Erase") == 0) {
                s = g_strdup (_("Erasing"));
        } else if (strcmp (job_id, "CreateFilesystem") == 0) {
                s = g_strdup (_("Creating File System"));
        } else if (strcmp (job_id, "Mount") == 0) {
                s = g_strdup (_("Mounting"));
        } else if (strcmp (job_id, "Unmount") == 0) {
                s = g_strdup (_("Unmounting"));
        } else {
                s = g_strdup_printf ("%s", job_id);
        }
        return s;
}

static char *
get_task_description (const char *task_id)
{
        char *s;
        if (strcmp (task_id, "zeroing") == 0) {
                s = g_strdup (_("Zeroing data"));
        } else if (strcmp (task_id, "sync") == 0) {
                s = g_strdup (_("Flushing data to disk"));
        } else if (strcmp (task_id, "mkfs") == 0) {
                s = g_strdup (_("Creating File System"));
        } else if (strlen (task_id) == 0) {
                s = g_strdup ("");
        } else {
                s = g_strdup_printf ("%s", task_id);
        }
        return s;
}

static gboolean
cluebar_pulse_timeout_handler (gpointer user_data)
{
        GduShell *shell = user_data;

        gtk_progress_bar_pulse (GTK_PROGRESS_BAR (shell->priv->cluebar_progress_bar));
        return TRUE;
}

static void
cluebar_style_set (GtkWidget *widget, GtkStyle *previous_style, gpointer user_data)
{
        GtkStyle *style;
        GdkColor default_fill_color = {0, 0xff00, 0xff00, 0xbf00};
        GdkColor bg;

        style = gtk_rc_get_style_by_paths (gtk_settings_get_default (), NULL, NULL, GTK_TYPE_BUTTON);
        if (style != NULL) {
                bg = style->bg[GTK_STATE_SELECTED];
        } else {
                bg = default_fill_color;
        }

        if (!gdk_color_equal (&bg, &widget->style->bg[GTK_STATE_NORMAL]))
                gtk_widget_modify_bg (widget, GTK_STATE_NORMAL, &bg);
}

static void
cluebar_button_clicked (GtkWidget *button, gpointer user_data)
{
        GduDevice *device;
        GduShell *shell = user_data;

        device = gdu_presentable_get_device (shell->priv->presentable_now_showing);
        g_assert (device != NULL);

        if (gdu_device_job_get_last_error_message (device) != NULL) {
                gdu_device_job_clear_last_error_message (device);
                shell_update (shell);
        } else {
                gdu_device_op_cancel_job (device);
        }

        g_object_unref (device);
}

static void
update_cluebar (GduShell *shell, GduDevice *device)
{
        char *s;
        char *job_description;
        char *task_description;
        double percentage;

        if (gdu_device_job_get_last_error_message (device) != NULL) {
                s = g_strdup_printf (_("<b>Job Failed:</b> %s"), gdu_device_job_get_last_error_message (device));
                gtk_label_set_markup (GTK_LABEL (shell->priv->cluebar_label), s);
                g_free (s);

                gtk_button_set_label (GTK_BUTTON (shell->priv->cluebar_button), _("Dismiss"));
                gtk_widget_set_sensitive (shell->priv->cluebar_button, TRUE);
                gtk_widget_set_no_show_all (shell->priv->cluebar_progress_bar, TRUE);
                gtk_widget_hide (shell->priv->cluebar_progress_bar);

        } else {
                job_description = get_job_description (gdu_device_job_get_id (device));
                task_description = get_task_description (gdu_device_job_get_cur_task_id (device));

                gtk_label_set_markup (GTK_LABEL (shell->priv->cluebar_label), job_description);

                s = g_strdup_printf (_("%s (task %d of %d)"),
                                     task_description,
                                     gdu_device_job_get_cur_task (device) + 1,
                                     gdu_device_job_get_num_tasks (device));
                gtk_widget_set_tooltip_text (shell->priv->cluebar_progress_bar, s);
                g_free (s);

                percentage = gdu_device_job_get_cur_task_percentage (device);
                if (percentage < 0) {
                        gtk_progress_bar_set_pulse_step (GTK_PROGRESS_BAR (shell->priv->cluebar_progress_bar), 1.0 / 50);
                        gtk_progress_bar_pulse (GTK_PROGRESS_BAR (shell->priv->cluebar_progress_bar));
                        if (shell->priv->cluebar_pulse_timer_id == 0) {
                                shell->priv->cluebar_pulse_timer_id = g_timeout_add (1000 / 50,
                                                                               cluebar_pulse_timeout_handler,
                                                                               shell);
                        }
                } else {
                        gtk_progress_bar_set_fraction (GTK_PROGRESS_BAR (shell->priv->cluebar_progress_bar),
                                                       percentage / 100.0);
                        if (shell->priv->cluebar_pulse_timer_id > 0) {
                                g_source_remove (shell->priv->cluebar_pulse_timer_id);
                                shell->priv->cluebar_pulse_timer_id = 0;
                        }
                }

                g_free (job_description);
                g_free (task_description);

                gtk_button_set_label (GTK_BUTTON (shell->priv->cluebar_button), _("Cancel"));
                gtk_widget_set_sensitive (shell->priv->cluebar_button, gdu_device_job_is_cancellable (device));

                gtk_widget_set_no_show_all (shell->priv->cluebar_progress_bar, FALSE);
                gtk_widget_show (shell->priv->cluebar_progress_bar);
        }
}

/* called when a new presentable is selected
 *  - or a presentable changes
 *  - or the job state of a presentable changes
 */
static void
shell_update (GduShell *shell)
{
        GList *i;
        GList *j;
        GList *kv_pairs;
        GduDevice *device;
        gboolean show_erase;
        gboolean show_cluebar;
        gboolean job_in_progress;
        gboolean last_job_failed;
        gboolean can_mount;
        gboolean can_unmount;
        gboolean can_eject;

        show_erase = FALSE;
        show_cluebar = FALSE;
        job_in_progress = FALSE;
        last_job_failed = FALSE;
        can_mount = FALSE;
        can_unmount = FALSE;
        can_eject = FALSE;

        /* figure out what pages in the notebook to show + update pages */
        device = gdu_presentable_get_device (shell->priv->presentable_now_showing);
        if (device != NULL) {
                show_erase = TRUE;

                if (gdu_device_job_in_progress (device)) {
                        job_in_progress = TRUE;
                }

                if (gdu_device_job_get_last_error_message (device) != NULL) {
                        last_job_failed = TRUE;
                }

                if (!job_in_progress) {
                        if (strcmp (gdu_device_id_get_usage (device), "filesystem") == 0) {
                                if (!fstype_combo_box_select (GTK_COMBO_BOX (shell->priv->page_erase_type_combo_box),
                                                              gdu_device_id_get_type (device))) {
                                        /* if fstype of device isn't in creatable, clear selection item */
                                        gtk_combo_box_set_active (GTK_COMBO_BOX (shell->priv->page_erase_type_combo_box), -1);
                                        gtk_entry_set_text (GTK_ENTRY (shell->priv->page_erase_label_entry), "");
                                } else {
                                        /* it was.. choose the same label */
                                        gtk_entry_set_text (GTK_ENTRY (shell->priv->page_erase_label_entry),
                                                            gdu_device_id_get_label (device));
                                }
                        } else if (strlen (gdu_device_id_get_usage (device)) == 0) {
                                /* couldn't identify anything; choose first in creatable fs list */
                                gtk_combo_box_set_active (GTK_COMBO_BOX (shell->priv->page_erase_type_combo_box), 0);
                                gtk_entry_set_text (GTK_ENTRY (shell->priv->page_erase_label_entry), "");
                        } else {
                                /* something else, not a file system, clear selection item */
                                gtk_combo_box_set_active (GTK_COMBO_BOX (shell->priv->page_erase_type_combo_box), -1);
                                gtk_entry_set_text (GTK_ENTRY (shell->priv->page_erase_label_entry), "");
                        }
                }

                if (strcmp (gdu_device_id_get_usage (device), "filesystem") == 0) {
                        if (gdu_device_is_mounted (device)) {
                                can_unmount = TRUE;
                        } else {
                                can_mount = TRUE;
                        }
                }

                if (gdu_device_is_removable (device) &&
                    gdu_device_is_media_available (device)) {
                        can_eject = TRUE;
                }

        }

        if (show_erase)
                gtk_widget_show (shell->priv->erase_page);
        else
                gtk_widget_hide (shell->priv->erase_page);

        /* make all pages insenstive if there's a job running on the device or the last job failed */
        if (job_in_progress || last_job_failed) {
                show_cluebar = TRUE;
                gtk_widget_set_sensitive (shell->priv->summary_page, FALSE);
                gtk_widget_set_sensitive (shell->priv->erase_page, FALSE);
        } else {
                gtk_widget_set_sensitive (shell->priv->summary_page, TRUE);
                gtk_widget_set_sensitive (shell->priv->erase_page, TRUE);
        }

        /* cluebar handling */
        if (show_cluebar) {
                update_cluebar (shell, device);
                gtk_widget_show (shell->priv->cluebar);
        } else {
                if (shell->priv->cluebar_pulse_timer_id > 0) {
                        g_source_remove (shell->priv->cluebar_pulse_timer_id);
                        shell->priv->cluebar_pulse_timer_id = 0;
                }
                gtk_widget_hide (shell->priv->cluebar);
        }

        /* update all GtkActions */
        polkit_gnome_action_set_sensitive (shell->priv->mount_action, can_mount);
        polkit_gnome_action_set_sensitive (shell->priv->unmount_action, can_unmount);
        polkit_gnome_action_set_sensitive (shell->priv->eject_action, can_eject);

        /* shell->priv->eject_action sensitivity is updated in page_erase_type_combo_box_changed() triggered
         * from above when updating the erase page
         */

        /* update key/value pairs on summary page */
        kv_pairs = gdu_presentable_get_info (shell->priv->presentable_now_showing);
        for (i = kv_pairs, j = shell->priv->table_labels; i != NULL && j != NULL; i = i->next, j = j->next) {
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
presentable_changed (GduPresentable *presentable, gpointer user_data)
{
        GduShell *shell = user_data;
        if (presentable == shell->priv->presentable_now_showing)
                shell_update (shell);
}

static void
presentable_job_changed (GduPresentable *presentable, gpointer user_data)
{
        GduShell *shell = user_data;
        if (presentable == shell->priv->presentable_now_showing)
                shell_update (shell);
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

                shell_update (shell);
        }
}

static void
presentable_removed (GduPool *pool, GduPresentable *presentable, gpointer user_data)
{
        //GduShell *shell = user_data;
        //GtkTreeView *treeview = GTK_TREE_VIEW (user_data);
        /* TODO FIX: if presentable we currently show is removed.. go to computer presentable */
}

static GtkWidget *
create_summary_page (GduShell *shell)
{
        int row;
        GtkWidget *vbox;
        GtkWidget *info_table;

        vbox = gtk_vbox_new (FALSE, 10);
        gtk_container_set_border_width (GTK_CONTAINER (vbox), 8);

        shell->priv->table_labels = NULL;

        info_table = gtk_table_new (10, 2, FALSE);
        gtk_table_set_col_spacings (GTK_TABLE (info_table), 8);
        gtk_table_set_row_spacings (GTK_TABLE (info_table), 4);
        for (row = 0; row < 10; row++) {

                GtkWidget *key_label;
                GtkWidget *value_label;

                key_label = gtk_label_new (NULL);
                gtk_misc_set_alignment (GTK_MISC (key_label), 1.0, 0.5);

                value_label = gtk_label_new (NULL);
                gtk_misc_set_alignment (GTK_MISC (value_label), 0.0, 0.5);
                gtk_label_set_selectable (GTK_LABEL (value_label), TRUE);
                gtk_label_set_ellipsize (GTK_LABEL (value_label), PANGO_ELLIPSIZE_END);

                gtk_table_attach (GTK_TABLE (info_table), key_label,   0, 1, row, row + 1,
                                  GTK_FILL, GTK_EXPAND | GTK_FILL, 0, 0);
                gtk_table_attach (GTK_TABLE (info_table), value_label, 1, 2, row, row + 1,
                                  GTK_EXPAND | GTK_FILL, GTK_EXPAND | GTK_FILL, 0, 0);

                shell->priv->table_labels = g_list_append (shell->priv->table_labels, key_label);
                shell->priv->table_labels = g_list_append (shell->priv->table_labels, value_label);
        }
        gtk_box_pack_start (GTK_BOX (vbox), info_table, FALSE, FALSE, 0);

        return vbox;
}

enum {
        SECURE_ERASE_NONE,
        SECURE_ERASE_OVERWRITE,
        SECURE_ERASE_OVERWRITE3,
        SECURE_ERASE_OVERWRITE7,
        SECURE_ERASE_OVERWRITE35,
};


static void
secure_erase_radio_toggled_none (GtkToggleButton *toggle_button, gpointer user_data)
{
        GduShell *shell = user_data;
        shell->priv->secure_erase_option = SECURE_ERASE_NONE;
}

static void
secure_erase_radio_toggled_overwrite (GtkToggleButton *toggle_button, gpointer user_data)
{
        GduShell *shell = user_data;
        shell->priv->secure_erase_option = SECURE_ERASE_OVERWRITE;
}

static void
secure_erase_radio_toggled_overwrite3 (GtkToggleButton *toggle_button, gpointer user_data)
{
        GduShell *shell = user_data;
        shell->priv->secure_erase_option = SECURE_ERASE_OVERWRITE3;
}

static void
secure_erase_radio_toggled_overwrite7 (GtkToggleButton *toggle_button, gpointer user_data)
{
        GduShell *shell = user_data;
        shell->priv->secure_erase_option = SECURE_ERASE_OVERWRITE7;
}

static void
secure_erase_radio_toggled_overwrite35 (GtkToggleButton *toggle_button, gpointer user_data)
{
        GduShell *shell = user_data;
        shell->priv->secure_erase_option = SECURE_ERASE_OVERWRITE35;
}

typedef struct
{
        char *id;
        int max_label_len;
        char *description;
} CreatableFilesystem;

/* TODO: retrieve this list from DeviceKit-disks */

static CreatableFilesystem creatable_fstypes[] = {
        {"vfat", 11},
        {"ext3", 16},
        {"empty", 0},
};

static int num_creatable_fstypes = sizeof (creatable_fstypes) / sizeof (CreatableFilesystem);

static CreatableFilesystem *
find_creatable_filesystem_for_fstype (const char *fstype)
{
        int n;
        CreatableFilesystem *ret;

        ret = NULL;
        for (n = 0; n < num_creatable_fstypes; n++) {
                if (strcmp (fstype, creatable_fstypes[n].id) == 0) {
                        ret = &(creatable_fstypes[n]);
                        break;
                }
        }

        return ret;
}

static GtkWidget *
fstype_combo_box_create (void)
{
        int n;
        GtkListStore *store;
	GtkCellRenderer *renderer;
        GtkWidget *combo_box;

        store = gtk_list_store_new (2, G_TYPE_STRING, G_TYPE_STRING);
        for (n = 0; n < num_creatable_fstypes; n++) {
                const char *fstype;
                char *fstype_name;
                GtkTreeIter iter;

                fstype = creatable_fstypes[n].id;

                if (strcmp (fstype, "empty") == 0) {
                        fstype_name = g_strdup (_("Empty (don't create a file system)"));
                } else {
                        fstype_name = gdu_util_get_fstype_for_display (fstype, NULL, TRUE);
                }

                gtk_list_store_append (store, &iter);
                gtk_list_store_set (store, &iter,
                                    0, fstype,
                                    1, fstype_name,
                                    -1);

                g_free (fstype_name);
        }

        combo_box = gtk_combo_box_new ();
	gtk_combo_box_set_model (GTK_COMBO_BOX (combo_box), GTK_TREE_MODEL (store));
        g_object_unref (store);

	renderer = gtk_cell_renderer_text_new ();
	gtk_cell_layout_pack_start (GTK_CELL_LAYOUT (combo_box), renderer, TRUE);
	gtk_cell_layout_set_attributes (GTK_CELL_LAYOUT (combo_box), renderer,
					"text", 1,
					NULL);

        gtk_combo_box_set_active (GTK_COMBO_BOX (combo_box), 0);

        return combo_box;
}

static gboolean
fstype_combo_box_select (GtkComboBox *combo_box, const char *fstype)
{
        GtkTreeModel *model;
        GtkTreeIter iter;
        gboolean ret;

        ret = FALSE;

        model = gtk_combo_box_get_model (combo_box);
        gtk_tree_model_get_iter_first (model, &iter);
        do {
                char *iter_fstype;

                gtk_tree_model_get (model, &iter, 0, &iter_fstype, -1);
                if (iter_fstype != NULL && strcmp (fstype, iter_fstype) == 0) {
                        gtk_combo_box_set_active_iter (combo_box, &iter);
                        ret = TRUE;
                }
                g_free (iter_fstype);
        } while (!ret && gtk_tree_model_iter_next (model, &iter));

        return ret;
}

static char *
fstype_combo_box_get_selected (GtkComboBox *combo_box)
{
        GtkTreeModel *model;
        GtkTreeIter iter;
        char *fstype;

        model = gtk_combo_box_get_model (combo_box);
        fstype = NULL;
        if (gtk_combo_box_get_active_iter (combo_box, &iter))
                gtk_tree_model_get (model, &iter, 0, &fstype, -1);

        return fstype;
}

static void
page_erase_type_combo_box_changed (GtkComboBox *combo_box, gpointer user_data)
{
        GduShell *shell = user_data;
        char *fstype;
        CreatableFilesystem *creatable_fs;
        gboolean label_entry_sensitive;
        gboolean can_erase;
        int max_label_len;

        label_entry_sensitive = FALSE;
        can_erase = FALSE;
        max_label_len = 0;

        fstype = fstype_combo_box_get_selected (combo_box);
        if (fstype != NULL) {
                creatable_fs = find_creatable_filesystem_for_fstype (fstype);
                if (creatable_fs != NULL) {
                        max_label_len = creatable_fs->max_label_len;
                }
                can_erase = TRUE;
        }

        if (max_label_len > 0)
                label_entry_sensitive = TRUE;

        gtk_entry_set_max_length (GTK_ENTRY (shell->priv->page_erase_label_entry), max_label_len);
        gtk_widget_set_sensitive (shell->priv->page_erase_label_entry, label_entry_sensitive);
        polkit_gnome_action_set_sensitive (shell->priv->erase_action, can_erase);

        g_free (fstype);
}

static GtkWidget *
create_erase_page (GduShell *shell)
{
        GtkWidget *label;
        GtkWidget *align;
        GtkWidget *vbox;
        GtkWidget *main_vbox;
        GtkWidget *radio1;
        GtkWidget *radio2;
        GtkWidget *radio3;
        GtkWidget *radio4;
        GtkWidget *radio5;
        GtkWidget *table;
        GtkWidget *combo_box;
        GtkWidget *entry;
        GtkWidget *button;
        GtkWidget *button_box;

        main_vbox = gtk_vbox_new (FALSE, 10);

        /* volume format + label */
        label = gtk_label_new (NULL);
        gtk_label_set_markup (GTK_LABEL (label), _("<b>Volume</b>"));
        gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
        gtk_box_pack_start (GTK_BOX (main_vbox), label, FALSE, FALSE, 0);
        vbox = gtk_vbox_new (FALSE, 5);
        align = gtk_alignment_new (0.5, 0.5, 1.0, 1.0);
        gtk_alignment_set_padding (GTK_ALIGNMENT (align), 0, 0, 24, 0);
        gtk_container_add (GTK_CONTAINER (align), vbox);
        gtk_box_pack_start (GTK_BOX (main_vbox), align, FALSE, TRUE, 0);

        /* explanatory text */
        label = gtk_label_new (NULL);
        gtk_label_set_markup (GTK_LABEL (label), _("To erase a volume or disk, select it from the tree and then select the format and label to use."));
        gtk_label_set_line_wrap (GTK_LABEL (label), TRUE);
        gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
        gtk_box_pack_start (GTK_BOX (vbox), label, FALSE, TRUE, 0);

        /* file system label + type */
        table = gtk_table_new (2, 2, FALSE);
        gtk_box_pack_start (GTK_BOX (vbox), table, FALSE, FALSE, 0);

        label = gtk_label_new (NULL);
        gtk_misc_set_alignment (GTK_MISC (label), 1.0, 0.5);
        gtk_label_set_markup_with_mnemonic (GTK_LABEL (label), _("_Label:"));
        gtk_table_attach (GTK_TABLE (table), label, 0, 1, 0, 1,
                          GTK_FILL, GTK_EXPAND | GTK_FILL, 2, 2);


        entry = gtk_entry_new (); /* todo: set max length, sensitivity according to fstype */
        //gtk_entry_set_text (GTK_ENTRY (entry), gdu_device_id_get_label (device));
        gtk_table_attach (GTK_TABLE (table), entry, 1, 2, 0, 1,
                          GTK_FILL, GTK_EXPAND | GTK_FILL, 2, 2);

        shell->priv->page_erase_label_entry = entry;

        gtk_label_set_mnemonic_widget (GTK_LABEL (label), entry);

        label = gtk_label_new (NULL);
        gtk_misc_set_alignment (GTK_MISC (label), 1.0, 0.5);
        gtk_label_set_markup_with_mnemonic (GTK_LABEL (label), _("_Type:"));
        gtk_table_attach (GTK_TABLE (table), label, 0, 1, 1, 2,
                          GTK_FILL, GTK_EXPAND | GTK_FILL, 2, 2);

        combo_box = fstype_combo_box_create ();
        gtk_table_attach (GTK_TABLE (table), combo_box, 1, 2, 1, 2,
                          GTK_FILL, GTK_EXPAND | GTK_FILL, 2, 2);

        gtk_label_set_mnemonic_widget (GTK_LABEL (label), combo_box);

        shell->priv->page_erase_type_combo_box = combo_box;

        /* update sensivity of label */
        g_signal_connect (shell->priv->page_erase_type_combo_box, "changed",
                          G_CALLBACK (page_erase_type_combo_box_changed), shell);

        /* secure erase */
        label = gtk_label_new (NULL);
        gtk_label_set_markup (GTK_LABEL (label), _("<b>Secure Erase</b>"));
        gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
        gtk_box_pack_start (GTK_BOX (main_vbox), label, FALSE, FALSE, 0);
        vbox = gtk_vbox_new (FALSE, 5);
        align = gtk_alignment_new (0.5, 0.5, 1.0, 1.0);
        gtk_alignment_set_padding (GTK_ALIGNMENT (align), 0, 0, 24, 0);
        gtk_container_add (GTK_CONTAINER (align), vbox);
        gtk_box_pack_start (GTK_BOX (main_vbox), align, FALSE, TRUE, 0);

        label = gtk_label_new (NULL);
        gtk_label_set_markup (GTK_LABEL (label), _("Select if existing data on the volume should be erased before formatting it."));
        gtk_label_set_line_wrap (GTK_LABEL (label), TRUE);
        gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
        gtk_box_pack_start (GTK_BOX (vbox), label, FALSE, TRUE, 0);

        radio1 = gtk_radio_button_new_with_mnemonic (NULL,
                                                     _("_Don't overwrite data"));
        radio2 = gtk_radio_button_new_with_mnemonic (gtk_radio_button_get_group (GTK_RADIO_BUTTON (radio1)),
                                                     _("_Overwrite data with zeroes"));
        radio3 = gtk_radio_button_new_with_mnemonic (gtk_radio_button_get_group (GTK_RADIO_BUTTON (radio1)),
                                                     _("Overwrite data with zeroes _3 times"));
        radio4 = gtk_radio_button_new_with_mnemonic (gtk_radio_button_get_group (GTK_RADIO_BUTTON (radio1)),
                                                     _("Overwrite data with zeroes _7 times"));
        radio5 = gtk_radio_button_new_with_mnemonic (gtk_radio_button_get_group (GTK_RADIO_BUTTON (radio1)),
                                                     _("Overwrite data with zeroes 3_5 times"));
        gtk_box_pack_start (GTK_BOX (vbox), radio1, FALSE, TRUE, 0);
        gtk_box_pack_start (GTK_BOX (vbox), radio2, FALSE, TRUE, 0);
        gtk_box_pack_start (GTK_BOX (vbox), radio3, FALSE, TRUE, 0);
        gtk_box_pack_start (GTK_BOX (vbox), radio4, FALSE, TRUE, 0);
        gtk_box_pack_start (GTK_BOX (vbox), radio5, FALSE, TRUE, 0);
        /* TODO: read this from gconf and visually indicate lockdown (admin may want to force sanitation policy) */
        switch (shell->priv->secure_erase_option) {
        case SECURE_ERASE_NONE:
                gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (radio1), TRUE);
                break;
        case SECURE_ERASE_OVERWRITE:
                gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (radio2), TRUE);
                break;
        case SECURE_ERASE_OVERWRITE3:
                gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (radio3), TRUE);
                break;
        case SECURE_ERASE_OVERWRITE7:
                gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (radio4), TRUE);
                break;
        case SECURE_ERASE_OVERWRITE35:
                gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (radio5), TRUE);
                break;
        default:
                g_assert_not_reached ();
                break;
        }
	g_signal_connect (radio1, "toggled", G_CALLBACK (secure_erase_radio_toggled_none), shell);
        g_signal_connect (radio2, "toggled", G_CALLBACK (secure_erase_radio_toggled_overwrite), shell);
	g_signal_connect (radio3, "toggled", G_CALLBACK (secure_erase_radio_toggled_overwrite3), shell);
	g_signal_connect (radio4, "toggled", G_CALLBACK (secure_erase_radio_toggled_overwrite7), shell);
	g_signal_connect (radio5, "toggled", G_CALLBACK (secure_erase_radio_toggled_overwrite35), shell);


        button_box = gtk_hbutton_box_new ();
        gtk_button_box_set_layout (GTK_BUTTON_BOX (button_box), GTK_BUTTONBOX_END);
        gtk_box_set_spacing (GTK_BOX (button_box), 6);

        button = polkit_gnome_action_create_button (shell->priv->erase_action);
        gtk_container_add (GTK_CONTAINER (button_box), button);
        gtk_box_pack_start (GTK_BOX (vbox), button_box, TRUE, TRUE, 0);

        return main_vbox;
}

static GtkWidget *
create_cluebar (GduShell *shell)
{
        GtkWidget *evbox;
        GtkWidget *hbox;
        GtkWidget *label;
        GtkWidget *align;
        GtkWidget *progress_bar;
        GtkWidget *button;

        evbox = gtk_event_box_new ();
        g_signal_connect (evbox, "style-set", G_CALLBACK (cluebar_style_set), shell);

        hbox = gtk_hbox_new (FALSE, 5);

        label = gtk_label_new (NULL);
        gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
        gtk_label_set_line_wrap (GTK_LABEL (label), TRUE);
        gtk_box_pack_start (GTK_BOX (hbox), label, FALSE, FALSE, 0);

        progress_bar = gtk_progress_bar_new ();
        g_signal_connect (progress_bar, "style-set", G_CALLBACK (cluebar_style_set), shell);
        align = gtk_alignment_new (0.0, 0.0, 1.0, 1.0);
        gtk_alignment_set_padding (GTK_ALIGNMENT (align), 6, 6, 0, 0);
        gtk_container_add (GTK_CONTAINER (align), progress_bar);
        gtk_box_pack_start (GTK_BOX (hbox), align, FALSE, FALSE, 0);

        button = gtk_button_new_with_label (_("Cancel"));
        gtk_box_pack_end (GTK_BOX (hbox), button, FALSE, FALSE, 0);

        gtk_container_set_border_width (GTK_CONTAINER (hbox), 2);
        gtk_container_add (GTK_CONTAINER (evbox), hbox);

        shell->priv->cluebar_label = label;
        shell->priv->cluebar_progress_bar = progress_bar;
        shell->priv->cluebar_button = button;

        g_signal_connect (shell->priv->cluebar_button, "clicked", G_CALLBACK (cluebar_button_clicked), shell);

        return evbox;
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
erase_action_callback (GtkAction *action, gpointer user_data)
{
        GduShell *shell = user_data;
        int response;
        GtkWidget *dialog;
        const char *fslabel;
        char *fstype;
        const char *fserase;
        GduDevice *device;

        device = gdu_presentable_get_device (shell->priv->presentable_now_showing);
        g_assert (device != NULL);

        fstype = fstype_combo_box_get_selected (GTK_COMBO_BOX (shell->priv->page_erase_type_combo_box));
        if (GTK_WIDGET_IS_SENSITIVE (shell->priv->page_erase_label_entry))
                fslabel = gtk_entry_get_text (GTK_ENTRY (shell->priv->page_erase_label_entry));
        else
                fslabel = "";

        switch (shell->priv->secure_erase_option) {
        case SECURE_ERASE_NONE:
                fserase = "none";
                break;
        case SECURE_ERASE_OVERWRITE:
                fserase = "full";
                break;
        case SECURE_ERASE_OVERWRITE3:
                fserase = "full3pass";
                break;
        case SECURE_ERASE_OVERWRITE7:
                fserase = "full7pass";
                break;
        case SECURE_ERASE_OVERWRITE35:
                fserase = "full35pass";
                break;
        default:
                g_assert_not_reached ();
                break;
        }

        /* TODO: mention what drive the volume is on etc. */
        dialog = gtk_message_dialog_new_with_markup (GTK_WINDOW (shell->priv->app_window),
                                                     GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                                     GTK_MESSAGE_WARNING,
                                                     GTK_BUTTONS_CANCEL,
                                                     _("<b><big>Are you sure you want to erase the volume?</big></b>"));

        gtk_message_dialog_format_secondary_markup (GTK_MESSAGE_DIALOG (dialog),
                                                    _("All data on the volume will be irrecovably erase. Make sure data important to you is backed up. This action cannot be undone."));
        /* ... until we add data recovery to g-d-u! */

        gtk_dialog_add_button (GTK_DIALOG (dialog), _("Erase"), 0);

        response = gtk_dialog_run (GTK_DIALOG (dialog));
        gtk_widget_destroy (dialog);
        if (response != 0)
                goto out;

        gdu_device_op_mkfs (device, fstype, fslabel, fserase);

out:
        g_free (fstype);
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
        GtkActionGroup *action_group;
        GtkUIManager *ui_manager;
        GError *error;

        action_group = gtk_action_group_new ("GnomeDiskUtilityActions");
        gtk_action_group_set_translation_domain (action_group, NULL);
        gtk_action_group_add_actions (action_group, entries, G_N_ELEMENTS (entries), shell);

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
        gtk_action_group_add_action (action_group, GTK_ACTION (shell->priv->mount_action));

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
        gtk_action_group_add_action (action_group, GTK_ACTION (shell->priv->unmount_action));

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
        gtk_action_group_add_action (action_group, GTK_ACTION (shell->priv->eject_action));

        /* -------------------------------------------------------------------------------- */

        shell->priv->erase_action = polkit_gnome_action_new_default ("erase",
                                                               shell->priv->pk_erase_action,
                                                               _("_Erase"),
                                                               _("Erase"));
        g_object_set (shell->priv->erase_action,
                      "auth-label", _("_Erase..."),
                      "yes-icon-name", GTK_STOCK_CLEAR,
                      "no-icon-name", GTK_STOCK_CLEAR,
                      "auth-icon-name", GTK_STOCK_CLEAR,
                      "self-blocked-icon-name", GTK_STOCK_CLEAR,
                      NULL);
        g_signal_connect (shell->priv->erase_action, "activate", G_CALLBACK (erase_action_callback), shell);
        gtk_action_group_add_action (action_group, GTK_ACTION (shell->priv->erase_action));

        /* -------------------------------------------------------------------------------- */

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

static void
create_polkit_actions (GduShell *shell)
{
        shell->priv->pk_mount_action = polkit_action_new ();
        polkit_action_set_action_id (shell->priv->pk_mount_action, "org.freedesktop.devicekit.disks.mount");

        shell->priv->pk_erase_action = polkit_action_new ();
        polkit_action_set_action_id (shell->priv->pk_erase_action, "org.freedesktop.devicekit.disks.erase");
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
        GtkWidget *treeview;
        GtkTreeSelection *select;
        GtkWidget *summary_page_tab_label;
        GtkWidget *erase_page_tab_label;

        shell->priv->pool = gdu_pool_new ();

        create_polkit_actions (shell);

        shell->priv->app_window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
        gtk_window_set_resizable (GTK_WINDOW (shell->priv->app_window), TRUE);
        gtk_window_set_default_size (GTK_WINDOW (shell->priv->app_window), 750, 550);
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
        treeview = GTK_WIDGET (gdu_tree_new (shell->priv->pool));
        gtk_container_add (GTK_CONTAINER (treeview_scrolled_window), treeview);

        /* summary pane */

        shell->priv->cluebar = create_cluebar (shell);
        shell->priv->summary_page = create_summary_page (shell);
        summary_page_tab_label = gtk_label_new (_("Summary"));
        shell->priv->erase_page = create_erase_page (shell);
        erase_page_tab_label = gtk_label_new (_("Erase"));

        shell->priv->notebook = gtk_notebook_new ();
        gtk_notebook_append_page (GTK_NOTEBOOK (shell->priv->notebook), shell->priv->summary_page, summary_page_tab_label);
        gtk_notebook_append_page (GTK_NOTEBOOK (shell->priv->notebook), shell->priv->erase_page, erase_page_tab_label);

        vbox2 = gtk_vbox_new (FALSE, 0);
        gtk_box_pack_start (GTK_BOX (vbox2), shell->priv->cluebar, FALSE, FALSE, 0);
        gtk_box_pack_start (GTK_BOX (vbox2), shell->priv->notebook, TRUE, TRUE, 0);

        /* setup and add horizontal pane */
        hpane = gtk_hpaned_new ();
        gtk_paned_add1 (GTK_PANED (hpane), treeview_scrolled_window);
        gtk_paned_add2 (GTK_PANED (hpane), vbox2);
        gtk_paned_set_position (GTK_PANED (hpane), 260);

        gtk_box_pack_start (GTK_BOX (vbox), hpane, TRUE, TRUE, 0);

        select = gtk_tree_view_get_selection (GTK_TREE_VIEW (treeview));
        gtk_tree_selection_set_mode (select, GTK_SELECTION_SINGLE);
        g_signal_connect (select, "changed", (GCallback) device_tree_changed, shell);

        /* when starting up, set focus on tree view */
        gtk_widget_grab_focus (treeview);

        g_signal_connect (shell->priv->pool, "presentable-removed", (GCallback) presentable_removed, shell);
        g_signal_connect (shell->priv->app_window, "delete-event", gtk_main_quit, NULL);

        gtk_widget_show_all (vbox);
}

