/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*-
 *
 * Copyright (C) 2008-2011 Red Hat, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General
 * Public License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place, Suite 330,
 * Boston, MA 02111-1307, USA.
 *
 * Author: David Zeuthen <davidz@redhat.com>
 */

#include "config.h"
#include <string.h>
#include <stdlib.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>

#include <glib/gi18n.h>
#include <gio/gunixfdlist.h>

#include "gduapplication.h"
#include "gduwindow.h"
#include "gdudevicetreemodel.h"
#include "gduutils.h"
#include "gduvolumegrid.h"
#include "gduatasmartdialog.h"
#include "gducrypttabdialog.h"
#include "gdufstabdialog.h"
#include "gdufilesystemdialog.h"
#include "gdupartitiondialog.h"
#include "gduunlockdialog.h"

/* Keep in sync with tabs in palimpsest.ui file */
typedef enum
{
  DETAILS_PAGE_NOT_SELECTED,
  DETAILS_PAGE_NOT_IMPLEMENTED,
  DETAILS_PAGE_DEVICE,
} DetailsPage;

struct _GduWindow
{
  GtkWindow parent_instance;

  GduApplication *application;
  UDisksClient *client;

  const gchar *builder_path;
  GtkBuilder *builder;
  GduDeviceTreeModel *model;

  DetailsPage current_page;
  UDisksObject *current_object;

  GtkWidget *volume_grid;

  GtkWidget *main_hpane;
  GtkWidget *details_notebook;
  GtkWidget *device_scrolledwindow;
  GtkWidget *device_treeview;
  GtkWidget *device_toolbar;
  GtkWidget *device_toolbar_attach_disk_image_button;
  GtkWidget *device_toolbar_detach_disk_image_button;
  GtkWidget *devtab_drive_vbox;
  GtkWidget *devtab_drive_name_label;
  GtkWidget *devtab_drive_devices_label;
  GtkWidget *devtab_drive_image;
  GtkWidget *devtab_table;
  GtkWidget *devtab_drive_table;
  GtkWidget *devtab_grid_hbox;
  GtkWidget *devtab_volumes_label;
  GtkWidget *devtab_grid_toolbar;
  GtkWidget *devtab_toolbar_generic_button;
  GtkWidget *devtab_toolbar_partition_create_button;
  GtkWidget *devtab_toolbar_mount_button;
  GtkWidget *devtab_toolbar_unmount_button;
  GtkWidget *devtab_toolbar_eject_button;
  GtkWidget *devtab_toolbar_unlock_button;
  GtkWidget *devtab_toolbar_lock_button;
  GtkWidget *devtab_toolbar_activate_swap_button;
  GtkWidget *devtab_toolbar_deactivate_swap_button;

  GtkWidget *generic_menu;
  GtkWidget *generic_menu_item_view_smart;
  GtkWidget *generic_menu_item_configure_fstab;
  GtkWidget *generic_menu_item_configure_crypttab;
  GtkWidget *generic_menu_item_edit_label;
  GtkWidget *generic_menu_item_edit_partition;

  GHashTable *label_connections;
};

static const struct {
  goffset offset;
  const gchar *name;
} widget_mapping[] = {
  {G_STRUCT_OFFSET (GduWindow, main_hpane), "main-hpane"},
  {G_STRUCT_OFFSET (GduWindow, device_scrolledwindow), "device-tree-scrolledwindow"},
  {G_STRUCT_OFFSET (GduWindow, device_toolbar), "device-tree-add-remove-toolbar"},
  {G_STRUCT_OFFSET (GduWindow, device_toolbar_attach_disk_image_button), "device-tree-attach-disk-image-button"},
  {G_STRUCT_OFFSET (GduWindow, device_toolbar_detach_disk_image_button), "device-tree-detach-disk-image-button"},
  {G_STRUCT_OFFSET (GduWindow, device_treeview), "device-tree-treeview"},
  {G_STRUCT_OFFSET (GduWindow, details_notebook), "palimpsest-notebook"},
  {G_STRUCT_OFFSET (GduWindow, devtab_drive_table), "devtab-drive-table"},
  {G_STRUCT_OFFSET (GduWindow, devtab_drive_vbox), "devtab-drive-vbox"},
  {G_STRUCT_OFFSET (GduWindow, devtab_drive_name_label), "devtab-drive-name-label"},
  {G_STRUCT_OFFSET (GduWindow, devtab_drive_devices_label), "devtab-drive-devices-label"},
  {G_STRUCT_OFFSET (GduWindow, devtab_drive_image), "devtab-drive-image"},
  {G_STRUCT_OFFSET (GduWindow, devtab_table), "devtab-table"},
  {G_STRUCT_OFFSET (GduWindow, devtab_grid_hbox), "devtab-grid-hbox"},
  {G_STRUCT_OFFSET (GduWindow, devtab_volumes_label), "devtab-volumes-label"},
  {G_STRUCT_OFFSET (GduWindow, devtab_grid_toolbar), "devtab-grid-toolbar"},
  {G_STRUCT_OFFSET (GduWindow, devtab_toolbar_generic_button), "devtab-action-generic"},
  {G_STRUCT_OFFSET (GduWindow, devtab_toolbar_partition_create_button), "devtab-action-partition-create"},
  {G_STRUCT_OFFSET (GduWindow, devtab_toolbar_mount_button), "devtab-action-mount"},
  {G_STRUCT_OFFSET (GduWindow, devtab_toolbar_unmount_button), "devtab-action-unmount"},
  {G_STRUCT_OFFSET (GduWindow, devtab_toolbar_eject_button), "devtab-action-eject"},
  {G_STRUCT_OFFSET (GduWindow, devtab_toolbar_unlock_button), "devtab-action-unlock"},
  {G_STRUCT_OFFSET (GduWindow, devtab_toolbar_lock_button), "devtab-action-lock"},
  {G_STRUCT_OFFSET (GduWindow, devtab_toolbar_activate_swap_button), "devtab-action-activate-swap"},
  {G_STRUCT_OFFSET (GduWindow, devtab_toolbar_deactivate_swap_button), "devtab-action-deactivate-swap"},
  {G_STRUCT_OFFSET (GduWindow, generic_menu), "generic-menu"},
  {G_STRUCT_OFFSET (GduWindow, generic_menu_item_view_smart), "generic-menu-item-view-smart"},
  {G_STRUCT_OFFSET (GduWindow, generic_menu_item_configure_fstab), "generic-menu-item-configure-fstab"},
  {G_STRUCT_OFFSET (GduWindow, generic_menu_item_configure_crypttab), "generic-menu-item-configure-crypttab"},
  {G_STRUCT_OFFSET (GduWindow, generic_menu_item_edit_label), "generic-menu-item-edit-label"},
  {G_STRUCT_OFFSET (GduWindow, generic_menu_item_edit_partition), "generic-menu-item-edit-partition"},
  {0, NULL}
};

typedef struct
{
  GtkWindowClass parent_class;
} GduWindowClass;

enum
{
  PROP_0,
  PROP_APPLICATION,
  PROP_CLIENT
};

typedef enum
{
  SHOW_FLAGS_NONE                    = 0,
  SHOW_FLAGS_DETACH_DISK_IMAGE       = (1<<0),
  SHOW_FLAGS_EJECT_BUTTON            = (1<<1),
  SHOW_FLAGS_PARTITION_CREATE_BUTTON = (1<<2),
  SHOW_FLAGS_MOUNT_BUTTON            = (1<<3),
  SHOW_FLAGS_UNMOUNT_BUTTON          = (1<<4),
  SHOW_FLAGS_ACTIVATE_SWAP_BUTTON    = (1<<5),
  SHOW_FLAGS_DEACTIVATE_SWAP_BUTTON  = (1<<6),
  SHOW_FLAGS_ENCRYPTED_UNLOCK_BUTTON = (1<<7),
  SHOW_FLAGS_ENCRYPTED_LOCK_BUTTON   = (1<<8),

  SHOW_FLAGS_POPUP_MENU_VIEW_SMART         = (1<<9),
  SHOW_FLAGS_POPUP_MENU_CONFIGURE_FSTAB    = (1<<10),
  SHOW_FLAGS_POPUP_MENU_CONFIGURE_CRYPTTAB = (1<<11),
  SHOW_FLAGS_POPUP_MENU_EDIT_LABEL         = (1<<12),
  SHOW_FLAGS_POPUP_MENU_EDIT_PARTITION     = (1<<13),
} ShowFlags;

static void setup_device_page (GduWindow *window, UDisksObject *object);
static void update_device_page (GduWindow *window, ShowFlags *show_flags);
static void teardown_device_page (GduWindow *window);

static void on_volume_grid_changed (GduVolumeGrid  *grid,
                                    gpointer        user_data);

static void on_devtab_action_generic_activated (GtkAction *action, gpointer user_data);
static void on_devtab_action_partition_create_activated (GtkAction *action, gpointer user_data);
static void on_devtab_action_mount_activated (GtkAction *action, gpointer user_data);
static void on_devtab_action_unmount_activated (GtkAction *action, gpointer user_data);
static void on_devtab_action_eject_activated (GtkAction *action, gpointer user_data);
static void on_devtab_action_unlock_activated (GtkAction *action, gpointer user_data);
static void on_devtab_action_lock_activated (GtkAction *action, gpointer user_data);
static void on_devtab_action_activate_swap_activated (GtkAction *action, gpointer user_data);
static void on_devtab_action_deactivate_swap_activated (GtkAction *action, gpointer user_data);

static void on_generic_menu_item_view_smart (GtkMenuItem *menu_item,
                                             gpointer   user_data);
static void on_generic_menu_item_configure_fstab (GtkMenuItem *menu_item,
                                                  gpointer   user_data);
static void on_generic_menu_item_configure_crypttab (GtkMenuItem *menu_item,
                                                     gpointer   user_data);
static void on_generic_menu_item_edit_label (GtkMenuItem *menu_item,
                                             gpointer   user_data);
static void on_generic_menu_item_edit_partition (GtkMenuItem *menu_item,
                                                 gpointer   user_data);

G_DEFINE_TYPE (GduWindow, gdu_window, GTK_TYPE_WINDOW);

static void
gdu_window_init (GduWindow *window)
{
  window->label_connections = g_hash_table_new_full (g_str_hash,
                                                     g_str_equal,
                                                     g_free,
                                                     NULL);
}

static void on_client_changed (UDisksClient  *client,
                               gpointer       user_data);

static void
gdu_window_finalize (GObject *object)
{
  GduWindow *window = GDU_WINDOW (object);

  gtk_window_remove_mnemonic (GTK_WINDOW (window),
                              'd',
                              window->device_treeview);

  g_signal_handlers_disconnect_by_func (window->client,
                                        G_CALLBACK (on_client_changed),
                                        window);

  if (window->current_object != NULL)
    g_object_unref (window->current_object);

  g_object_unref (window->builder);
  g_object_unref (window->model);
  g_object_unref (window->client);
  g_object_unref (window->application);

  G_OBJECT_CLASS (gdu_window_parent_class)->finalize (object);
}

static gboolean
dont_select_headings (GtkTreeSelection *selection,
                      GtkTreeModel     *model,
                      GtkTreePath      *path,
                      gboolean          selected,
                      gpointer          data)
{
  GtkTreeIter iter;
  gboolean is_heading;

  gtk_tree_model_get_iter (model,
                           &iter,
                           path);
  gtk_tree_model_get (model,
                      &iter,
                      GDU_DEVICE_TREE_MODEL_COLUMN_IS_HEADING,
                      &is_heading,
                      -1);

  return !is_heading;
}

static void
on_row_inserted (GtkTreeModel *tree_model,
                 GtkTreePath  *path,
                 GtkTreeIter  *iter,
                 gpointer      user_data)
{
  GduWindow *window = GDU_WINDOW (user_data);
  gtk_tree_view_expand_all (GTK_TREE_VIEW (window->device_treeview));
}

static void select_details_page (GduWindow       *window,
                                 UDisksObject    *object,
                                 DetailsPage      page,
                                 ShowFlags       *show_flags);

static void
update_for_show_flags (GduWindow *window,
                       ShowFlags  show_flags)
{
  gtk_widget_set_visible (GTK_WIDGET (window->device_toolbar_detach_disk_image_button),
                          show_flags & SHOW_FLAGS_DETACH_DISK_IMAGE);
  gtk_action_set_visible (GTK_ACTION (window->devtab_toolbar_eject_button),
                          show_flags & SHOW_FLAGS_EJECT_BUTTON);
  gtk_action_set_visible (GTK_ACTION (window->devtab_toolbar_partition_create_button),
                          show_flags & SHOW_FLAGS_PARTITION_CREATE_BUTTON);
  gtk_action_set_visible (GTK_ACTION (window->devtab_toolbar_unmount_button),
                          show_flags & SHOW_FLAGS_UNMOUNT_BUTTON);
  gtk_action_set_visible (GTK_ACTION (window->devtab_toolbar_mount_button),
                          show_flags & SHOW_FLAGS_MOUNT_BUTTON);
  gtk_action_set_visible (GTK_ACTION (window->devtab_toolbar_activate_swap_button),
                          show_flags & SHOW_FLAGS_ACTIVATE_SWAP_BUTTON);
  gtk_action_set_visible (GTK_ACTION (window->devtab_toolbar_deactivate_swap_button),
                          show_flags & SHOW_FLAGS_DEACTIVATE_SWAP_BUTTON);
  gtk_action_set_visible (GTK_ACTION (window->devtab_toolbar_unlock_button),
                          show_flags & SHOW_FLAGS_ENCRYPTED_UNLOCK_BUTTON);
  gtk_action_set_visible (GTK_ACTION (window->devtab_toolbar_lock_button),
                          show_flags & SHOW_FLAGS_ENCRYPTED_LOCK_BUTTON);

  gtk_widget_set_visible (GTK_WIDGET (window->generic_menu_item_view_smart),
                          show_flags & SHOW_FLAGS_POPUP_MENU_VIEW_SMART);
  gtk_widget_set_visible (GTK_WIDGET (window->generic_menu_item_configure_fstab),
                          show_flags & SHOW_FLAGS_POPUP_MENU_CONFIGURE_FSTAB);
  gtk_widget_set_visible (GTK_WIDGET (window->generic_menu_item_configure_crypttab),
                          show_flags & SHOW_FLAGS_POPUP_MENU_CONFIGURE_CRYPTTAB);
  gtk_widget_set_visible (GTK_WIDGET (window->generic_menu_item_edit_label),
                          show_flags & SHOW_FLAGS_POPUP_MENU_EDIT_LABEL);
  gtk_widget_set_visible (GTK_WIDGET (window->generic_menu_item_edit_partition),
                          show_flags & SHOW_FLAGS_POPUP_MENU_EDIT_PARTITION);
  /* TODO: don't show the button bringing up the popup menu if it has no items */
}

static void
set_selected_object (GduWindow    *window,
                     UDisksObject *object)
{
  ShowFlags show_flags;
  GtkTreeIter iter;

  if (gdu_device_tree_model_get_iter_for_object (window->model, object, &iter))
    {
      GtkTreePath *path;
      GtkTreeSelection *tree_selection;
      tree_selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (window->device_treeview));
      gtk_tree_selection_select_iter (tree_selection, &iter);
      path = gtk_tree_model_get_path (GTK_TREE_MODEL (window->model), &iter);
      gtk_tree_view_set_cursor (GTK_TREE_VIEW (window->device_treeview),
                                path,
                                NULL,
                                FALSE);
      gtk_tree_path_free (path);
    }

  show_flags = SHOW_FLAGS_NONE;
  if (object != NULL)
    {
      if (udisks_object_peek_drive (object) != NULL ||
          udisks_object_peek_block (object) != NULL)
        {
          select_details_page (window, object, DETAILS_PAGE_DEVICE, &show_flags);
        }
      else
        {
          g_warning ("no page for object %s", g_dbus_object_get_object_path (G_DBUS_OBJECT (object)));
          select_details_page (window, NULL, DETAILS_PAGE_NOT_IMPLEMENTED, &show_flags);
        }
    }
  else
    {
      select_details_page (window, NULL, DETAILS_PAGE_NOT_SELECTED, &show_flags);
    }
  update_for_show_flags (window, show_flags);
}

static gboolean
ensure_something_selected_foreach_cb (GtkTreeModel  *model,
                                      GtkTreePath   *path,
                                      GtkTreeIter   *iter,
                                      gpointer       user_data)
{
  UDisksObject **object = user_data;
  gtk_tree_model_get (model, iter,
                      GDU_DEVICE_TREE_MODEL_COLUMN_OBJECT, object,
                      -1);
  if (*object != NULL)
    return TRUE;
  return FALSE;
}

static void
ensure_something_selected (GduWindow *window)
{
  UDisksObject *object = NULL;
  gtk_tree_model_foreach (GTK_TREE_MODEL (window->model),
                          ensure_something_selected_foreach_cb,
                          &object);
  if (object != NULL)
    {
      set_selected_object (window, object);
      g_object_unref (object);
    }
}

static void
on_tree_selection_changed (GtkTreeSelection *tree_selection,
                           gpointer          user_data)
{
  GduWindow *window = GDU_WINDOW (user_data);
  GtkTreeIter iter;
  GtkTreeModel *model;

  if (gtk_tree_selection_get_selected (tree_selection, &model, &iter))
    {
      UDisksObject *object;
      gtk_tree_model_get (model,
                          &iter,
                          GDU_DEVICE_TREE_MODEL_COLUMN_OBJECT,
                          &object,
                          -1);
      set_selected_object (window, object);
      g_object_unref (object);
    }
  else
    {
      set_selected_object (window, NULL);
      ensure_something_selected (window);
    }
}

/* ---------------------------------------------------------------------------------------------------- */

static void
loop_delete_cb (UDisksLoop   *loop,
                GAsyncResult *res,
                gpointer      user_data)
{
  GduWindow *window = GDU_WINDOW (user_data);
  GError *error;

  error = NULL;
  if (!udisks_loop_call_delete_finish (loop, res, &error))
    {
      gdu_window_show_error (window,
                             _("Error deleting loop device"),
                             error);
      g_error_free (error);
    }
  g_object_unref (window);
}

static void
on_device_tree_detach_disk_image_button_clicked (GtkToolButton *button,
                                                 gpointer       user_data)
{
  GduWindow *window = GDU_WINDOW (user_data);
  UDisksLoop *loop;

  loop = udisks_object_peek_loop (window->current_object);
  if (loop != NULL)
    {
      GVariantBuilder options_builder;
      g_variant_builder_init (&options_builder, G_VARIANT_TYPE_VARDICT);
      udisks_loop_call_delete (loop,
                               g_variant_builder_end (&options_builder),
                               NULL, /* GCancellable */
                               (GAsyncReadyCallback) loop_delete_cb,
                               g_object_ref (window));
    }
  else
    {
      g_warning ("remove action not implemented for object");
    }
}

/* ---------------------------------------------------------------------------------------------------- */

typedef struct
{
  GduWindow *window;
  gchar *filename;
} LoopSetupData;

static LoopSetupData *
loop_setup_data_new (GduWindow   *window,
                     const gchar *filename)
{
  LoopSetupData *data;
  data = g_slice_new0 (LoopSetupData);
  data->window = g_object_ref (window);
  data->filename = g_strdup (filename);
  return data;
}

static void
loop_setup_data_free (LoopSetupData *data)
{
  g_object_unref (data->window);
  g_free (data->filename);
  g_slice_free (LoopSetupData, data);
}

static void
loop_setup_cb (UDisksManager  *manager,
               GAsyncResult   *res,
               gpointer        user_data)
{
  LoopSetupData *data = user_data;
  gchar *out_loop_device_object_path;
  GError *error;

  error = NULL;
  if (!udisks_manager_call_loop_setup_finish (manager, &out_loop_device_object_path, NULL, res, &error))
    {
      gdu_window_show_error (data->window,
                             _("Error attaching disk image"),
                             error);
      g_error_free (error);
    }
  else
    {
      gchar *uri;
      UDisksObject *object;

      /* This is to make it appear in the file chooser's "Recently Used" list */
      uri = g_strdup_printf ("file://%s", data->filename);
      gtk_recent_manager_add_item (gtk_recent_manager_get_default (), uri);
      g_free (uri);

      udisks_client_settle (data->window->client);

      object = udisks_client_get_object (data->window->client, out_loop_device_object_path);
      set_selected_object (data->window, object);
      g_object_unref (object);
      g_free (out_loop_device_object_path);
    }

  loop_setup_data_free (data);
}

/* ---------------------------------------------------------------------------------------------------- */

static void
on_device_tree_attach_disk_image_button_clicked (GtkToolButton *button,
                                                 gpointer       user_data)
{
  GduWindow *window = GDU_WINDOW (user_data);
  GtkWidget *dialog;
  GtkFileFilter *filter;
  gchar *filename;
  gint fd;
  GUnixFDList *fd_list;
  GVariantBuilder options_builder;

  filename = NULL;
  fd = -1;

  dialog = gtk_file_chooser_dialog_new (_("Select Disk Image to Attach"),
                                        GTK_WINDOW (window),
                                        GTK_FILE_CHOOSER_ACTION_OPEN,
                                        GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
                                        _("Attach"), GTK_RESPONSE_ACCEPT,
                                        NULL);
  gtk_file_chooser_set_current_folder (GTK_FILE_CHOOSER (dialog), g_get_home_dir ());

  /* TODO: define proper mime-types */
  filter = gtk_file_filter_new ();
  gtk_file_filter_set_name (filter, _("All Files"));
  gtk_file_filter_add_pattern (filter, "*");
  gtk_file_chooser_add_filter (GTK_FILE_CHOOSER (dialog), filter); /* adopts filter */
  filter = gtk_file_filter_new ();
  gtk_file_filter_set_name (filter, _("Disk Images (*.img, *.iso)"));
  gtk_file_filter_add_pattern (filter, "*.img");
  gtk_file_filter_add_pattern (filter, "*.iso");
  gtk_file_chooser_add_filter (GTK_FILE_CHOOSER (dialog), filter); /* adopts filter */
  gtk_file_chooser_set_filter (GTK_FILE_CHOOSER (dialog), filter);

  /* Can't support non-local files because uid gets EPERM when doing fstat(2)
   * an FD from the FUSE mount... it would be nice to support this, though
   */
  /* gtk_file_chooser_set_local_only (GTK_FILE_CHOOSER (dialog), FALSE); */

  if (gtk_dialog_run (GTK_DIALOG (dialog)) != GTK_RESPONSE_ACCEPT)
    goto out;

  filename = gtk_file_chooser_get_filename (GTK_FILE_CHOOSER (dialog));
  gtk_widget_hide (dialog);

  fd = open (filename, O_RDWR);
  if (fd == -1)
    fd = open (filename, O_RDONLY);
  if (fd == -1)
    {
      GError *error;
      error = g_error_new (G_IO_ERROR,
                           g_io_error_from_errno (errno),
                           "%s", strerror (errno));
      gdu_window_show_error (window,
                             _("Error attaching disk image"),
                             error);
      g_error_free (error);
      goto out;
    }

  g_variant_builder_init (&options_builder, G_VARIANT_TYPE_VARDICT);
  /* TODO: add options to options_builder */

  fd_list = g_unix_fd_list_new_from_array (&fd, 1); /* adopts the fd */
  udisks_manager_call_loop_setup (udisks_client_get_manager (window->client),
                                  g_variant_new_handle (0),
                                  g_variant_builder_end (&options_builder),
                                  fd_list,
                                  NULL,                       /* GCancellable */
                                  (GAsyncReadyCallback) loop_setup_cb,
                                  loop_setup_data_new (window, filename));
  g_object_unref (fd_list);

 out:
  gtk_widget_destroy (dialog);
  g_free (filename);
}

/* ---------------------------------------------------------------------------------------------------- */


static void
init_css (GduWindow *window)
{
  GtkCssProvider *provider;
  GError *error;
  const gchar *css =
"#devtab-grid-toolbar.toolbar {\n"
"    border-width: 1;\n"
"    border-radius: 3;\n"
"    border-style: solid;\n"
"    background-color: @theme_base_color;\n"
"}\n"
;

  provider = gtk_css_provider_new ();
  error = NULL;
  if (!gtk_css_provider_load_from_data (provider,
                                        css,
                                        -1,
                                        &error))
    {
      g_warning ("Can't parse custom CSS: %s\n", error->message);
      g_error_free (error);
      goto out;
    }

  gtk_style_context_add_provider_for_screen (gtk_widget_get_screen (GTK_WIDGET (window)),
                                             GTK_STYLE_PROVIDER (provider),
                                             GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
  g_object_unref (provider);

 out:
  ;
}


static gboolean
on_constructed_in_idle (gpointer user_data)
{
  GduWindow *window = GDU_WINDOW (user_data);
  /* select something sensible */
  ensure_something_selected (window);
  return FALSE; /* remove source */
}

static void
gdu_window_constructed (GObject *object)
{
  GduWindow *window = GDU_WINDOW (object);
  GtkTreeViewColumn *column;
  GtkCellRenderer *renderer;
  GtkTreeSelection *selection;
  GtkStyleContext *context;
  GList *children, *l;
  guint n;

  init_css (window);

  /* chain up */
  if (G_OBJECT_CLASS (gdu_window_parent_class)->constructed != NULL)
    G_OBJECT_CLASS (gdu_window_parent_class)->constructed (object);

  /* load UI file */
  gdu_application_new_widget (window->application, "palimpsest.ui", NULL, &window->builder);

  /* set up widgets */
  for (n = 0; widget_mapping[n].name != NULL; n++)
    {
      gpointer *p = (gpointer *) ((char *) window + widget_mapping[n].offset);
      *p = G_OBJECT (gtk_builder_get_object (window->builder, widget_mapping[n].name));
    }

  gtk_widget_reparent (window->main_hpane, GTK_WIDGET (window));
  gtk_window_set_title (GTK_WINDOW (window), _("Disk Utility"));
  gtk_window_set_default_size (GTK_WINDOW (window), 800, 600);
  gtk_container_set_border_width (GTK_CONTAINER (window), 12);

  /* hide all children in the devtab list, otherwise the dialog is going to be huge by default */
  children = gtk_container_get_children (GTK_CONTAINER (window->devtab_drive_table));
  for (l = children; l != NULL; l = l->next)
    {
      gtk_widget_hide (GTK_WIDGET (l->data));
      gtk_widget_set_no_show_all (GTK_WIDGET (l->data), TRUE);
    }
  g_list_free (children);
  children = gtk_container_get_children (GTK_CONTAINER (window->devtab_table));
  for (l = children; l != NULL; l = l->next)
    {
      gtk_widget_hide (GTK_WIDGET (l->data));
      gtk_widget_set_no_show_all (GTK_WIDGET (l->data), TRUE);
    }

  gtk_notebook_set_show_tabs (GTK_NOTEBOOK (window->details_notebook), FALSE);
  gtk_notebook_set_show_border (GTK_NOTEBOOK (window->details_notebook), FALSE);

  context = gtk_widget_get_style_context (window->device_scrolledwindow);
  gtk_style_context_set_junction_sides (context, GTK_JUNCTION_BOTTOM);
  context = gtk_widget_get_style_context (window->device_toolbar);
  gtk_style_context_add_class (context, GTK_STYLE_CLASS_INLINE_TOOLBAR);
  gtk_style_context_set_junction_sides (context, GTK_JUNCTION_TOP);

  window->model = gdu_device_tree_model_new (window->client);

  /* set up mnemonic */
  gtk_window_add_mnemonic (GTK_WINDOW (window),
                           'd',
                           window->device_treeview);

  gtk_tree_view_set_model (GTK_TREE_VIEW (window->device_treeview), GTK_TREE_MODEL (window->model));
  gtk_tree_sortable_set_sort_column_id (GTK_TREE_SORTABLE (window->model),
                                        GDU_DEVICE_TREE_MODEL_COLUMN_SORT_KEY,
                                        GTK_SORT_ASCENDING);

  selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (window->device_treeview));
  gtk_tree_selection_set_select_function (selection, dont_select_headings, NULL, NULL);
  g_signal_connect (selection,
                    "changed",
                    G_CALLBACK (on_tree_selection_changed),
                    window);

  column = gtk_tree_view_column_new ();
  gtk_tree_view_append_column (GTK_TREE_VIEW (window->device_treeview), column);

  renderer = gtk_cell_renderer_text_new ();
  gtk_tree_view_column_pack_start (column, renderer, FALSE);
  gtk_tree_view_column_set_attributes (column,
                                       renderer,
                                       "markup", GDU_DEVICE_TREE_MODEL_COLUMN_HEADING_TEXT,
                                       "visible", GDU_DEVICE_TREE_MODEL_COLUMN_IS_HEADING,
                                       NULL);

  renderer = gtk_cell_renderer_pixbuf_new ();
  g_object_set (G_OBJECT (renderer),
                "stock-size", GTK_ICON_SIZE_DND,
                NULL);
  gtk_tree_view_column_pack_start (column, renderer, FALSE);
  gtk_tree_view_column_set_attributes (column,
                                       renderer,
                                       "gicon", GDU_DEVICE_TREE_MODEL_COLUMN_ICON,
                                       NULL);
  renderer = gtk_cell_renderer_text_new ();
  g_object_set (G_OBJECT (renderer),
                "ellipsize", PANGO_ELLIPSIZE_MIDDLE,
                NULL);
  gtk_tree_view_column_pack_start (column, renderer, FALSE);
  gtk_tree_view_column_set_attributes (column,
                                       renderer,
                                       "markup", GDU_DEVICE_TREE_MODEL_COLUMN_NAME,
                                       NULL);

  /* expand on insertion - hmm, I wonder if there's an easier way to do this */
  g_signal_connect (window->model,
                    "row-inserted",
                    G_CALLBACK (on_row_inserted),
                    window);
  gtk_tree_view_expand_all (GTK_TREE_VIEW (window->device_treeview));

  g_signal_connect (window->client,
                    "changed",
                    G_CALLBACK (on_client_changed),
                    window);

  /* set up non-standard widgets that isn't in the .ui file */

  window->volume_grid = gdu_volume_grid_new (window->client);
  gtk_box_pack_start (GTK_BOX (window->devtab_grid_hbox),
                      window->volume_grid,
                      TRUE, TRUE, 0);
  gtk_label_set_mnemonic_widget (GTK_LABEL (window->devtab_volumes_label),
                                 window->volume_grid);
  g_signal_connect (window->volume_grid,
                    "changed",
                    G_CALLBACK (on_volume_grid_changed),
                    window);

  context = gtk_widget_get_style_context (window->devtab_grid_toolbar);
  gtk_widget_set_name (window->devtab_grid_toolbar, "devtab-grid-toolbar");
  gtk_style_context_set_junction_sides (context, GTK_JUNCTION_TOP);

  /* main toolbar */
  g_signal_connect (window->device_toolbar_attach_disk_image_button,
                    "clicked",
                    G_CALLBACK (on_device_tree_attach_disk_image_button_clicked),
                    window);
  g_signal_connect (window->device_toolbar_detach_disk_image_button,
                    "clicked",
                    G_CALLBACK (on_device_tree_detach_disk_image_button_clicked),
                    window);

  /* actions */
  g_signal_connect (window->devtab_toolbar_generic_button,
                    "activate",
                    G_CALLBACK (on_devtab_action_generic_activated),
                    window);
  g_signal_connect (window->devtab_toolbar_partition_create_button,
                    "activate",
                    G_CALLBACK (on_devtab_action_partition_create_activated),
                    window);
  g_signal_connect (window->devtab_toolbar_mount_button,
                    "activate",
                    G_CALLBACK (on_devtab_action_mount_activated),
                    window);
  g_signal_connect (window->devtab_toolbar_unmount_button,
                    "activate",
                    G_CALLBACK (on_devtab_action_unmount_activated),
                    window);
  g_signal_connect (window->devtab_toolbar_eject_button,
                    "activate",
                    G_CALLBACK (on_devtab_action_eject_activated),
                    window);
  g_signal_connect (window->devtab_toolbar_unlock_button,
                    "activate",
                    G_CALLBACK (on_devtab_action_unlock_activated),
                    window);
  g_signal_connect (window->devtab_toolbar_lock_button,
                    "activate",
                    G_CALLBACK (on_devtab_action_lock_activated),
                    window);
  g_signal_connect (window->devtab_toolbar_activate_swap_button,
                    "activate",
                    G_CALLBACK (on_devtab_action_activate_swap_activated),
                    window);
  g_signal_connect (window->devtab_toolbar_deactivate_swap_button,
                    "activate",
                    G_CALLBACK (on_devtab_action_deactivate_swap_activated),
                    window);

  g_signal_connect (window->generic_menu_item_view_smart,
                    "activate",
                    G_CALLBACK (on_generic_menu_item_view_smart),
                    window);
  g_signal_connect (window->generic_menu_item_configure_fstab,
                    "activate",
                    G_CALLBACK (on_generic_menu_item_configure_fstab),
                    window);
  g_signal_connect (window->generic_menu_item_configure_crypttab,
                    "activate",
                    G_CALLBACK (on_generic_menu_item_configure_crypttab),
                    window);
  g_signal_connect (window->generic_menu_item_edit_label,
                    "activate",
                    G_CALLBACK (on_generic_menu_item_edit_label),
                    window);
  g_signal_connect (window->generic_menu_item_edit_partition,
                    "activate",
                    G_CALLBACK (on_generic_menu_item_edit_partition),
                    window);

  g_idle_add (on_constructed_in_idle, g_object_ref (window));
}

static void
gdu_window_get_property (GObject    *object,
                         guint       prop_id,
                         GValue     *value,
                         GParamSpec *pspec)
{
  GduWindow *window = GDU_WINDOW (object);

  switch (prop_id)
    {
    case PROP_APPLICATION:
      g_value_set_object (value, gdu_window_get_application (window));
      break;

    case PROP_CLIENT:
      g_value_set_object (value, gdu_window_get_client (window));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
gdu_window_set_property (GObject      *object,
                         guint         prop_id,
                         const GValue *value,
                         GParamSpec   *pspec)
{
  GduWindow *window = GDU_WINDOW (object);

  switch (prop_id)
    {
    case PROP_APPLICATION:
      window->application = g_value_dup_object (value);
      break;

    case PROP_CLIENT:
      window->client = g_value_dup_object (value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
gdu_window_class_init (GduWindowClass *klass)
{
  GObjectClass *gobject_class;

  gobject_class = G_OBJECT_CLASS (klass);
  gobject_class->constructed  = gdu_window_constructed;
  gobject_class->finalize     = gdu_window_finalize;
  gobject_class->get_property = gdu_window_get_property;
  gobject_class->set_property = gdu_window_set_property;

  /**
   * GduWindow:application:
   *
   * The #GduApplication for the #GduWindow.
   */
  g_object_class_install_property (gobject_class,
                                   PROP_APPLICATION,
                                   g_param_spec_object ("application",
                                                        "Application",
                                                        "The application for the window",
                                                        GDU_TYPE_APPLICATION,
                                                        G_PARAM_READABLE |
                                                        G_PARAM_WRITABLE |
                                                        G_PARAM_CONSTRUCT_ONLY |
                                                        G_PARAM_STATIC_STRINGS));

  /**
   * GduWindow:client:
   *
   * The #UDisksClient used by the #GduWindow.
   */
  g_object_class_install_property (gobject_class,
                                   PROP_CLIENT,
                                   g_param_spec_object ("client",
                                                        "Client",
                                                        "The client used by the window",
                                                        UDISKS_TYPE_CLIENT,
                                                        G_PARAM_READABLE |
                                                        G_PARAM_WRITABLE |
                                                        G_PARAM_CONSTRUCT_ONLY |
                                                        G_PARAM_STATIC_STRINGS));
}

GduWindow *
gdu_window_new (GduApplication *application,
                UDisksClient   *client)
{
  return GDU_WINDOW (g_object_new (GDU_TYPE_WINDOW,
                                   "application", application,
                                   "client", client,
                                   NULL));
}

GduApplication *
gdu_window_get_application   (GduWindow *window)
{
  g_return_val_if_fail (GDU_IS_WINDOW (window), NULL);
  return window->application;
}

UDisksClient *
gdu_window_get_client (GduWindow *window)
{
  g_return_val_if_fail (GDU_IS_WINDOW (window), NULL);
  return window->client;
}

static void
teardown_details_page (GduWindow    *window,
                       UDisksObject *object,
                       DetailsPage   page)
{
  //g_debug ("teardown for %s, page %d",
  //       object != NULL ? g_dbus_object_get_object_path (object) : "<none>",
  //         page);
  switch (page)
    {
    case DETAILS_PAGE_NOT_SELECTED:
      break;

    case DETAILS_PAGE_NOT_IMPLEMENTED:
      break;

    case DETAILS_PAGE_DEVICE:
      teardown_device_page (window);
      break;
    }
}

typedef enum
{
  SET_MARKUP_FLAGS_NONE = 0,
  SET_MARKUP_FLAGS_HYPHEN_IF_EMPTY = (1<<0)
} SetMarkupFlags;

static void
set_markup (GduWindow      *window,
            const gchar    *key_label_id,
            const gchar    *label_id,
            const gchar    *markup,
            SetMarkupFlags  flags)
{
  GtkWidget *key_label;
  GtkWidget *label;

  if (markup == NULL || strlen (markup) == 0)
    {
      if (flags & SET_MARKUP_FLAGS_HYPHEN_IF_EMPTY)
        markup = "—";
      else
        goto out;
    }

  key_label = GTK_WIDGET (gtk_builder_get_object (window->builder, key_label_id));
  label = GTK_WIDGET (gtk_builder_get_object (window->builder, label_id));

  /* TODO: utf-8 validate */

  gtk_label_set_markup (GTK_LABEL (label), markup);
  gtk_widget_show (key_label);
  gtk_widget_show (label);

 out:
  ;
}

static void
set_size (GduWindow      *window,
          const gchar    *key_label_id,
          const gchar    *label_id,
          guint64         size,
          SetMarkupFlags  flags)
{
  gchar *s;
  s = udisks_client_get_size_for_display (window->client, size, FALSE, TRUE);
  set_markup (window, key_label_id, label_id, s, size);
  g_free (s);
}

static GList *
get_top_level_blocks_for_drive (GduWindow   *window,
                                const gchar *drive_object_path)
{
  GList *ret;
  GList *l;
  GList *object_proxies;
  GDBusObjectManager *object_manager;

  object_manager = udisks_client_get_object_manager (window->client);
  object_proxies = g_dbus_object_manager_get_objects (object_manager);

  ret = NULL;
  for (l = object_proxies; l != NULL; l = l->next)
    {
      UDisksObject *object = UDISKS_OBJECT (l->data);
      UDisksBlock *block;

      block = udisks_object_peek_block (object);
      if (block == NULL)
        continue;

      if (g_strcmp0 (udisks_block_get_drive (block), drive_object_path) != 0)
        continue;

      if (udisks_object_peek_partition (object) != NULL)
        continue;

      ret = g_list_append (ret, g_object_ref (object));
    }
  g_list_foreach (object_proxies, (GFunc) g_object_unref, NULL);
  g_list_free (object_proxies);
  return ret;
}

static gint
block_compare_on_preferred (UDisksObject *a,
                            UDisksObject *b)
{
  return g_strcmp0 (udisks_block_get_preferred_device (udisks_object_peek_block (a)),
                    udisks_block_get_preferred_device (udisks_object_peek_block (b)));
}

/* ---------------------------------------------------------------------------------------------------- */

static void
setup_details_page (GduWindow     *window,
                    UDisksObject  *object,
                    DetailsPage    page)
{
  //g_debug ("setup for %s, page %d",
  //         object != NULL ? g_dbus_object_get_object_path (object) : "<none>",
  //         page);

  switch (page)
    {
    case DETAILS_PAGE_NOT_SELECTED:
      break;

    case DETAILS_PAGE_NOT_IMPLEMENTED:
      break;

    case DETAILS_PAGE_DEVICE:
      setup_device_page (window, object);
      break;
    }
}

/* ---------------------------------------------------------------------------------------------------- */

static void
update_details_page (GduWindow      *window,
                     DetailsPage     page,
                     ShowFlags      *show_flags)
{
  ;
  //g_debug ("update for %s, page %d",
  //         object != NULL ? g_dbus_object_get_object_path (object) : "<none>",
  //         page);

  switch (page)
    {
    case DETAILS_PAGE_NOT_SELECTED:
      break;

    case DETAILS_PAGE_NOT_IMPLEMENTED:
      break;

    case DETAILS_PAGE_DEVICE:
      update_device_page (window, show_flags);
      break;
    }
}

static void
select_details_page (GduWindow      *window,
                     UDisksObject   *object,
                     DetailsPage     page,
                     ShowFlags      *show_flags)
{
  teardown_details_page (window,
                         window->current_object,
                         window->current_page);

  window->current_page = page;
  if (window->current_object != NULL)
    g_object_unref (window->current_object);
  window->current_object = object != NULL ? g_object_ref (object) : NULL;

  gtk_notebook_set_current_page (GTK_NOTEBOOK (window->details_notebook), page);

  setup_details_page (window,
                      window->current_object,
                      window->current_page);

  update_details_page (window, window->current_page, show_flags);
}

static void
update_all (GduWindow     *window)
{
  ShowFlags show_flags;

  switch (window->current_page)
    {
    case DETAILS_PAGE_NOT_SELECTED:
      /* Nothing to update */
      break;

    case DETAILS_PAGE_NOT_IMPLEMENTED:
      /* Nothing to update */
      break;

    case DETAILS_PAGE_DEVICE:
      show_flags = SHOW_FLAGS_NONE;
      update_details_page (window, window->current_page, &show_flags);
      update_for_show_flags (window, show_flags);
      break;
    }
}

static void
on_client_changed (UDisksClient   *client,
                   gpointer        user_data)
{
  GduWindow *window = GDU_WINDOW (user_data);
  //g_debug ("on_client_changed");
  update_all (window);
}

static void
on_volume_grid_changed (GduVolumeGrid  *grid,
                        gpointer        user_data)
{
  GduWindow *window = GDU_WINDOW (user_data);
  //g_debug ("on_volume_grid_changed");
  update_all (window);
}

/* ---------------------------------------------------------------------------------------------------- */

static void
setup_device_page (GduWindow     *window,
                   UDisksObject  *object)
{
  UDisksDrive *drive;
  UDisksBlock *block;

  drive = udisks_object_peek_drive (object);
  block = udisks_object_peek_block (object);

  gdu_volume_grid_set_container_visible (GDU_VOLUME_GRID (window->volume_grid), FALSE);
  if (drive != NULL)
    {
      GList *blocks;
      gchar *drive_name;
      gchar *drive_desc;
      GIcon *drive_icon;
      gchar *drive_media_desc;
      GIcon *drive_media_icon;

      /* TODO: for multipath, ensure e.g. mpathk is before sda, sdb */
      blocks = get_top_level_blocks_for_drive (window, g_dbus_object_get_object_path (G_DBUS_OBJECT (object)));
      blocks = g_list_sort (blocks, (GCompareFunc) block_compare_on_preferred);

      udisks_client_get_drive_info (window->client,
                                    drive,
                                    &drive_name,
                                    &drive_desc,
                                    &drive_icon,
                                    &drive_media_desc,
                                    &drive_media_icon);
      if (blocks != NULL)
        gdu_volume_grid_set_block_object (GDU_VOLUME_GRID (window->volume_grid), blocks->data);
      else
        gdu_volume_grid_set_block_object (GDU_VOLUME_GRID (window->volume_grid), NULL);

      g_free (drive_name);
      g_free (drive_desc);
      g_object_unref (drive_icon);
      g_free (drive_media_desc);
      if (drive_media_icon != NULL)
        g_object_unref (drive_media_icon);

      g_list_foreach (blocks, (GFunc) g_object_unref, NULL);
      g_list_free (blocks);
    }
  else if (block != NULL)
    {
      gdu_volume_grid_set_block_object (GDU_VOLUME_GRID (window->volume_grid), object);
    }
  else
    {
      g_assert_not_reached ();
    }
}

static void
update_device_page_for_drive (GduWindow      *window,
                              UDisksObject   *object,
                              UDisksDrive    *drive,
                              ShowFlags      *show_flags)
{
  gchar *s;
  GList *blocks;
  GList *l;
  GString *str;
  const gchar *drive_vendor;
  const gchar *drive_model;
  const gchar *drive_revision;
  gchar *name;
  gchar *description;
  gchar *media_description;
  GIcon *drive_icon;
  GIcon *media_icon;
  guint64 size;
  UDisksDriveAta *ata;

  //g_debug ("In update_device_page_for_drive() - selected=%s",
  //         object != NULL ? g_dbus_object_get_object_path (object) : "<nothing>");

  /* TODO: for multipath, ensure e.g. mpathk is before sda, sdb */
  blocks = get_top_level_blocks_for_drive (window, g_dbus_object_get_object_path (G_DBUS_OBJECT (object)));
  blocks = g_list_sort (blocks, (GCompareFunc) block_compare_on_preferred);

  ata = udisks_object_peek_drive_ata (object);

  udisks_client_get_drive_info (window->client,
                                drive,
                                &name,
                                &description,
                                &drive_icon,
                                &media_description,
                                &media_icon);

  drive_vendor = udisks_drive_get_vendor (drive);
  drive_model = udisks_drive_get_model (drive);
  drive_revision = udisks_drive_get_revision (drive);

  str = g_string_new (NULL);
  for (l = blocks; l != NULL; l = l->next)
    {
      UDisksObject *block_object = UDISKS_OBJECT (l->data);
      if (str->len > 0)
        g_string_append_c (str, ' ');
      g_string_append (str, udisks_block_get_preferred_device (udisks_object_peek_block (block_object)));
    }
  s = g_strdup_printf ("<big><b>%s</b></big>",
                       description);
  gtk_label_set_markup (GTK_LABEL (window->devtab_drive_name_label), s);
  gtk_widget_show (window->devtab_drive_name_label);
  g_free (s);
  s = g_strdup_printf ("<small><span foreground=\"#555555\">%s</span></small>",
                       str->str);
  gtk_label_set_markup (GTK_LABEL (window->devtab_drive_devices_label), s);
  gtk_widget_show (window->devtab_drive_devices_label);
  g_free (s);
  g_string_free (str, TRUE);
  gtk_widget_show (window->devtab_drive_vbox);

  if (media_icon != NULL)
    gtk_image_set_from_gicon (GTK_IMAGE (window->devtab_drive_image), media_icon, GTK_ICON_SIZE_DIALOG);
  else
    gtk_image_set_from_gicon (GTK_IMAGE (window->devtab_drive_image), drive_icon, GTK_ICON_SIZE_DIALOG);
  gtk_widget_show (window->devtab_drive_image);

  str = g_string_new (NULL);
  if (strlen (drive_vendor) == 0)
    g_string_append (str, drive_model);
  else if (strlen (drive_model) == 0)
    g_string_append (str, drive_vendor);
  else
    g_string_append_printf (str, "%s %s", drive_vendor, drive_model);
  if (strlen (drive_revision) > 0)
    g_string_append_printf (str, " (%s)", drive_revision);
  set_markup (window,
              "devtab-model-label",
              "devtab-model-value-label", str->str, SET_MARKUP_FLAGS_HYPHEN_IF_EMPTY);
  g_string_free (str, TRUE);
  set_markup (window,
              "devtab-serial-number-label",
              "devtab-serial-number-value-label",
              udisks_drive_get_serial (drive), SET_MARKUP_FLAGS_NONE);

  if (ata != NULL && !udisks_drive_get_media_removable (drive))
    {
      gboolean smart_is_supported;
      s = gdu_ata_smart_get_overall_assessment (ata, TRUE, &smart_is_supported);
      set_markup (window,
                  "devtab-drive-smart-label",
                  "devtab-drive-smart-value-label",
                  s, SET_MARKUP_FLAGS_NONE);
      if (smart_is_supported)
        *show_flags |= SHOW_FLAGS_POPUP_MENU_VIEW_SMART;
      g_free (s);
    }

  size = udisks_drive_get_size (drive);
  if (size > 0)
    {
      s = udisks_client_get_size_for_display (window->client, size, FALSE, TRUE);
      set_markup (window,
                  "devtab-drive-size-label",
                  "devtab-drive-size-value-label",
                  s, SET_MARKUP_FLAGS_HYPHEN_IF_EMPTY);
      g_free (s);
    }
  else
    {
      set_markup (window,
                  "devtab-drive-size-label",
                  "devtab-drive-size-value-label",
                  "",
                  SET_MARKUP_FLAGS_HYPHEN_IF_EMPTY);
    }

  if (udisks_drive_get_media_available (drive))
    {
      set_markup (window,
                  "devtab-media-label",
                  "devtab-media-value-label",
                  media_description, SET_MARKUP_FLAGS_NONE);
    }
  else
    {
      set_markup (window,
                  "devtab-media-label",
                  "devtab-media-value-label",
                  "",
                  SET_MARKUP_FLAGS_HYPHEN_IF_EMPTY);
    }


  if (udisks_drive_get_media_removable (drive))
    {
      *show_flags |= SHOW_FLAGS_EJECT_BUTTON;
    }

  g_list_foreach (blocks, (GFunc) g_object_unref, NULL);
  g_list_free (blocks);
  if (media_icon != NULL)
    g_object_unref (media_icon);
  g_object_unref (drive_icon);
  g_free (description);
  g_free (media_description);
  g_free (name);
}

static UDisksObject *
lookup_cleartext_device_for_crypto_device (UDisksClient  *client,
                                           const gchar   *object_path)
{
  GDBusObjectManager *object_manager;
  UDisksObject *ret;
  GList *objects;
  GList *l;

  ret = NULL;

  object_manager = udisks_client_get_object_manager (client);
  objects = g_dbus_object_manager_get_objects (object_manager);
  for (l = objects; l != NULL; l = l->next)
    {
      UDisksObject *object = UDISKS_OBJECT (l->data);
      UDisksBlock *block;

      block = udisks_object_peek_block (object);
      if (block == NULL)
        continue;

      if (g_strcmp0 (udisks_block_get_crypto_backing_device (block),
                     object_path) == 0)
        {
          ret = g_object_ref (object);
          goto out;
        }
    }

 out:
  g_list_foreach (objects, (GFunc) g_object_unref, NULL);
  g_list_free (objects);
  return ret;
}

static gboolean
options_has (const gchar *options, const gchar *str)
{
  gboolean ret = FALSE;
  gchar **tokens;
  guint n;

  tokens = g_strsplit (options, ",", 0);
  for (n = 0; tokens != NULL && tokens[n] != NULL; n++)
    {
      if (g_strcmp0 (tokens[n], str) == 0)
        {
          ret = TRUE;
          goto out;
        }
    }
 out:
  g_strfreev (tokens);
  return ret;
}

static gchar *
calculate_configuration_for_display (UDisksBlock *block,
                                     guint        show_flags)
{
  GString *str;
  GVariantIter iter;
  const gchar *type;
  GVariant *details;
  gboolean mentioned_fstab = FALSE;
  gboolean mentioned_crypttab = FALSE;
  gchar *ret;
  const gchar *options;

  ret = NULL;

  /* TODO: could include more details such as whether the
   * device is activated at boot time
   */

  str = g_string_new (NULL);
  g_variant_iter_init (&iter, udisks_block_get_configuration (block));
  while (g_variant_iter_next (&iter, "(&s@a{sv})", &type, &details))
    {
      if (g_strcmp0 (type, "fstab") == 0)
        {
          if (!mentioned_fstab)
            {
              mentioned_fstab = TRUE;
              if (str->len > 0)
                g_string_append (str, ", ");
              if (!g_variant_lookup (details, "opts", "^&ay", &options))
                options = "";
              if (options_has (options, "noauto"))
                {
                  if (g_strcmp0 (udisks_block_get_id_usage (block), "other") == 0 &&
                      g_strcmp0 (udisks_block_get_id_type (block), "swap") == 0)
                    {
                      /* Translators: Shown when the device is configured in /etc/fstab
                       * is a swap device but not automatically mounted at boot time.
                       * This string is shown next to the label "Configured".
                       */
                      g_string_append (str, _("Yes (not activated at system startup)"));
                    }
                  else
                    {
                      /* Translators: Shown when the device is configured in /etc/fstab
                       * but not automatically mounted at boot time.
                       * This string is shown next to the label "Configured".
                       */
                      g_string_append (str, _("Yes (not mounted at system startup)"));
                    }
                }
              else
                {
                  if (g_strcmp0 (udisks_block_get_id_usage (block), "other") == 0 &&
                      g_strcmp0 (udisks_block_get_id_type (block), "swap") == 0)
                    {
                      /* Translators: Shown when the device is configured in /etc/fstab
                       * is a swap device and automatically activated at boot time.
                       * This string is shown next to the label "Configured".
                       */
                      g_string_append (str, _("Yes (activated at system startup)"));
                    }
                  else
                    {
                      /* Translators: Shown when the device is configured in /etc/fstab
                       * and automatically mounted at boot time.
                       * This string is shown next to the label "Configured".
                       */
                      g_string_append (str, _("Yes (mounted at system startup)"));
                    }
                }
            }
        }
      else if (g_strcmp0 (type, "crypttab") == 0)
        {
          if (!mentioned_crypttab)
            {
              mentioned_crypttab = TRUE;
              if (str->len > 0)
                g_string_append (str, ", ");
              if (!g_variant_lookup (details, "options", "^&ay", &options))
                options = "";
              if (options_has (options, "noauto"))
                {
                  /* Translators: Shown when the device is configured in /etc/crypttab
                   * but not automatically unlocked at boot time.
                   * This string is shown next to the label "Configured".
                   */
                  g_string_append (str, _("Yes (not unlocked at system startup)"));
                }
              else
                {
                  /* Translators: Shown when the device is configured in /etc/crypttab
                   * but not automatically unlocked at boot time.
                   * This string is shown next to the label "Configured".
                   */
                  g_string_append (str, _("Yes (unlocked at system startup)"));
                }
            }
        }
      else
        {
          if (str->len > 0)
            g_string_append (str, ", ");
          g_string_append (str, type);
        }
      g_variant_unref (details);
    }

  if (str->len == 0)
    {
      UDisksObject *object;

      /* No known configuration... show "No" only if we actually
       * know how to configure the device or already offer to
       * configure the device...
       */
      object = UDISKS_OBJECT (g_dbus_interface_get_object (G_DBUS_INTERFACE (block)));
      if (udisks_object_peek_filesystem (object) != NULL ||
          udisks_object_peek_swapspace (object) != NULL ||
          udisks_object_peek_encrypted (object) != NULL ||
          show_flags & (SHOW_FLAGS_POPUP_MENU_CONFIGURE_FSTAB | SHOW_FLAGS_POPUP_MENU_CONFIGURE_CRYPTTAB))
        {
          /* Translators: Shown when the device is not configured */
          g_string_append (str, _("No"));
        }
      else
        {
          g_string_free (str, TRUE);
          goto out;
        }
    }

  ret = g_string_free (str, FALSE);
 out:
  return ret;
}

static void
update_device_page_for_block (GduWindow          *window,
                              UDisksObject       *object,
                              UDisksBlock        *block,
                              guint64             size,
                              ShowFlags          *show_flags)
{
  const gchar *usage;
  const gchar *type;
  const gchar *version;
  gchar *type_for_display;
  gchar *configuration_for_display;
  UDisksFilesystem *filesystem;
  UDisksPartition *partition;

  partition = udisks_object_peek_partition (object);
  filesystem = udisks_object_peek_filesystem (object);

  /* Since /etc/fstab, /etc/crypttab and so on can reference
   * any device regardless of its content ... we want to show
   * the relevant menu option (to get to the configuration dialog)
   * if the device matches the configuration....
   */
  if (gdu_utils_has_configuration (block, "fstab", NULL))
    *show_flags |= SHOW_FLAGS_POPUP_MENU_CONFIGURE_FSTAB;
  if (gdu_utils_has_configuration (block, "crypttab", NULL))
    *show_flags |= SHOW_FLAGS_POPUP_MENU_CONFIGURE_CRYPTTAB;

  /* if the device has no media and there is no existing configuration, then
   * show CONFIGURE_FSTAB since the user might want to add an entry for e.g.
   * /media/cdrom
   */
  if (udisks_block_get_size (block) == 0 &&
      !(*show_flags & (SHOW_FLAGS_POPUP_MENU_CONFIGURE_FSTAB |
                       SHOW_FLAGS_POPUP_MENU_CONFIGURE_CRYPTTAB)))
    {
      *show_flags |= SHOW_FLAGS_POPUP_MENU_CONFIGURE_FSTAB;
    }

  //g_debug ("In update_device_page_for_block() - size=%" G_GUINT64_FORMAT " selected=%s",
  //         size,
  //         object != NULL ? g_dbus_object_get_object_path (object) : "<nothing>");

  set_markup (window,
              "devtab-device-label",
              "devtab-device-value-label",
              udisks_block_get_preferred_device (block), SET_MARKUP_FLAGS_NONE);
  if (size > 0)
    {
      set_size (window,
                "devtab-size-label",
                "devtab-size-value-label",
                size, SET_MARKUP_FLAGS_HYPHEN_IF_EMPTY);
    }
  else
    {
      set_markup (window,
                  "devtab-size-label",
                  "devtab-size-value-label",
                  NULL, SET_MARKUP_FLAGS_HYPHEN_IF_EMPTY);
    }

  if (partition != NULL)
    {
      gchar *s;
      s = udisks_client_get_partition_info (window->client, partition);
      set_markup (window,
                  "devtab-partition-label",
                  "devtab-partition-value-label",
                  s, SET_MARKUP_FLAGS_NONE);
      g_free (s);
    }

  usage = udisks_block_get_id_usage (block);
  type = udisks_block_get_id_type (block);
  version = udisks_block_get_id_version (block);

  if (size > 0)
    {
      if (partition != NULL && udisks_partition_get_is_container (partition))
        {
          type_for_display = g_strdup (_("Extended Partition"));
        }
      else
        {
          type_for_display = udisks_client_get_id_for_display (window->client, usage, type, version, TRUE);
        }
    }
  else
    {
      type_for_display = NULL;
    }
  set_markup (window,
              "devtab-volume-type-label",
              "devtab-volume-type-value-label",
              type_for_display,
              SET_MARKUP_FLAGS_HYPHEN_IF_EMPTY);
  g_free (type_for_display);

  if (partition != NULL)
    {
      *show_flags |= SHOW_FLAGS_POPUP_MENU_EDIT_PARTITION;
    }
  else
    {
      UDisksObject *drive_object;
      drive_object = (UDisksObject *) g_dbus_object_manager_get_object (udisks_client_get_object_manager (window->client),
                                                                        udisks_block_get_drive (block));
      if (drive_object != NULL)
        {
          UDisksDrive *drive;
          drive = udisks_object_peek_drive (drive_object);
          if (udisks_drive_get_media_removable (drive))
            *show_flags |= SHOW_FLAGS_EJECT_BUTTON;
          g_object_unref (drive_object);
        }
    }

  if (filesystem != NULL)
    {
      const gchar *const *mount_points;
      gchar *mount_point;

      mount_points = udisks_filesystem_get_mount_points (filesystem);
      if (g_strv_length ((gchar **) mount_points) > 0)
        {
          /* TODO: right now we only display the first mount point */
          if (g_strcmp0 (mount_points[0], "/") == 0)
            {
              /* Translators: This is shown for a device mounted at the filesystem root / - we show
               * this text instead of '/', because '/' is too small to hit as a hyperlink
               */
              mount_point = g_strdup_printf ("<a href=\"file:///\">%s</a>", _("Root Filesystem (/)"));
            }
          else
            {
              mount_point = g_strdup_printf ("<a href=\"file://%s\">%s</a>",
                                             mount_points[0], mount_points[0]);
            }
        }
      else
        {
          /* Translators: Shown when the device is not mounted next to the "Mounted" label */
          mount_point = g_strdup (_("No"));
        }
      set_markup (window,
                  "devtab-volume-filesystem-mounted-label",
                  "devtab-volume-filesystem-mounted-value-label",
                  mount_point,
                  SET_MARKUP_FLAGS_NONE);
      g_free (mount_point);

      if (g_strv_length ((gchar **) mount_points) > 0)
        *show_flags |= SHOW_FLAGS_UNMOUNT_BUTTON;
      else
        *show_flags |= SHOW_FLAGS_MOUNT_BUTTON;

      *show_flags |= SHOW_FLAGS_POPUP_MENU_EDIT_LABEL;
      *show_flags |= SHOW_FLAGS_POPUP_MENU_CONFIGURE_FSTAB;
    }
  else if (g_strcmp0 (udisks_block_get_id_usage (block), "other") == 0 &&
           g_strcmp0 (udisks_block_get_id_type (block), "swap") == 0)
    {
      UDisksSwapspace *swapspace;
      const gchar *str;
      swapspace = udisks_object_peek_swapspace (object);
      if (swapspace != NULL)
        {
          if (udisks_swapspace_get_active (swapspace))
            {
              *show_flags |= SHOW_FLAGS_DEACTIVATE_SWAP_BUTTON;
              /* Translators: Shown if the swap device is in use next to the "Active" label */
              str = _("Yes");
            }
          else
            {
              *show_flags |= SHOW_FLAGS_ACTIVATE_SWAP_BUTTON;
              /* Translators: Shown if the swap device is not in use next to the "Active" label */
              str = _("No");
            }
          set_markup (window,
                      "devtab-volume-swap-active-label",
                      "devtab-volume-swap-active-value-label",
                      str,
                      SET_MARKUP_FLAGS_NONE);
        }
    }
  else if (g_strcmp0 (udisks_block_get_id_usage (block), "crypto") == 0)
    {
      UDisksObject *cleartext_device;
      const gchar *str;

      cleartext_device = lookup_cleartext_device_for_crypto_device (window->client,
                                                                    g_dbus_object_get_object_path (G_DBUS_OBJECT (object)));
      if (cleartext_device != NULL)
        {
          *show_flags |= SHOW_FLAGS_ENCRYPTED_LOCK_BUTTON;
          /* Translators: Shown if the encrypted device is unlocked next to the "Unlocked" label */
          str = _("Yes");
        }
      else
        {
          *show_flags |= SHOW_FLAGS_ENCRYPTED_UNLOCK_BUTTON;
          /* Translators: Shown if the encrypted device is not unlocked next to the "Unlocked" label */
          str = _("No");
        }
      set_markup (window,
                  "devtab-volume-encrypted-unlocked-label",
                  "devtab-volume-encrypted-unlocked-value-label",
                  str,
                  SET_MARKUP_FLAGS_NONE);

      *show_flags |= SHOW_FLAGS_POPUP_MENU_CONFIGURE_CRYPTTAB;
    }

  configuration_for_display = calculate_configuration_for_display (block, *show_flags);
  if (configuration_for_display != NULL)
    {
      set_markup (window,
                  "devtab-volume-configured-label",
                  "devtab-volume-configured-value-label",
                  configuration_for_display,
                  SET_MARKUP_FLAGS_HYPHEN_IF_EMPTY);
      g_free (configuration_for_display);
    }
}

static void
update_device_page_for_no_media (GduWindow          *window,
                                 UDisksObject       *object,
                                 UDisksBlock        *block,
                                 ShowFlags          *show_flags)
{
  //g_debug ("In update_device_page_for_no_media() - selected=%s",
  //         object != NULL ? g_dbus_object_get_object_path (object) : "<nothing>");
}

static void
update_device_page_for_free_space (GduWindow          *window,
                                   UDisksObject       *object,
                                   UDisksBlock        *block,
                                   guint64             size,
                                   ShowFlags          *show_flags)
{
  //g_debug ("In update_device_page_for_free_space() - size=%" G_GUINT64_FORMAT " selected=%s",
  //         size,
  //         object != NULL ? g_dbus_object_get_object_path (object) : "<nothing>");

  set_markup (window,
              "devtab-device-label",
              "devtab-device-value-label",
              udisks_block_get_preferred_device (block), SET_MARKUP_FLAGS_NONE);
  set_size (window,
            "devtab-size-label",
            "devtab-size-value-label",
            size, SET_MARKUP_FLAGS_HYPHEN_IF_EMPTY);
  set_markup (window,
              "devtab-volume-type-label",
              "devtab-volume-type-value-label",
              _("Unallocated Space"),
              SET_MARKUP_FLAGS_NONE);
  *show_flags |= SHOW_FLAGS_PARTITION_CREATE_BUTTON;
}

static void
update_device_page (GduWindow      *window,
                    ShowFlags      *show_flags)
{
  UDisksObject *object;
  GduVolumeGridElementType type;
  UDisksBlock *block;
  UDisksDrive *drive;
  guint64 size;
  GList *children;
  GList *l;

  /* first hide everything */
  gtk_container_foreach (GTK_CONTAINER (window->devtab_drive_table), (GtkCallback) gtk_widget_hide, NULL);
  gtk_container_foreach (GTK_CONTAINER (window->devtab_table), (GtkCallback) gtk_widget_hide, NULL);
  children = gtk_action_group_list_actions (GTK_ACTION_GROUP (gtk_builder_get_object (window->builder, "devtab-actions")));
  for (l = children; l != NULL; l = l->next)
    gtk_action_set_visible (GTK_ACTION (l->data), FALSE);
  g_list_free (children);

  /* always show the generic toolbar item */
  gtk_action_set_visible (GTK_ACTION (gtk_builder_get_object (window->builder,
                                                              "devtab-action-generic")), TRUE);


  object = window->current_object;
  drive = udisks_object_peek_drive (window->current_object);
  block = udisks_object_peek_block (window->current_object);
  type = gdu_volume_grid_get_selected_type (GDU_VOLUME_GRID (window->volume_grid));
  size = gdu_volume_grid_get_selected_size (GDU_VOLUME_GRID (window->volume_grid));

  if (udisks_object_peek_loop (object) != NULL)
    *show_flags |= SHOW_FLAGS_DETACH_DISK_IMAGE;

  if (drive != NULL)
    update_device_page_for_drive (window, object, drive, show_flags);

  if (type == GDU_VOLUME_GRID_ELEMENT_TYPE_CONTAINER)
    {
      if (block != NULL)
        update_device_page_for_block (window, object, block, size, show_flags);
    }
  else
    {
      object = gdu_volume_grid_get_selected_device (GDU_VOLUME_GRID (window->volume_grid));
      if (object == NULL)
        object = gdu_volume_grid_get_block_object (GDU_VOLUME_GRID (window->volume_grid));
      if (object != NULL)
        {
          block = udisks_object_peek_block (object);
          switch (type)
            {
            case GDU_VOLUME_GRID_ELEMENT_TYPE_CONTAINER:
              g_assert_not_reached (); /* already handled above */
              break;

            case GDU_VOLUME_GRID_ELEMENT_TYPE_DEVICE:
              update_device_page_for_block (window, object, block, size, show_flags);
              break;

            case GDU_VOLUME_GRID_ELEMENT_TYPE_NO_MEDIA:
              update_device_page_for_block (window, object, block, size, show_flags);
              update_device_page_for_no_media (window, object, block, show_flags);
              break;

            case GDU_VOLUME_GRID_ELEMENT_TYPE_FREE_SPACE:
              update_device_page_for_free_space (window, object, block, size, show_flags);
              break;
            }
        }
    }
}

static void
teardown_device_page (GduWindow *window)
{
  gdu_volume_grid_set_block_object (GDU_VOLUME_GRID (window->volume_grid), NULL);
}

/* ---------------------------------------------------------------------------------------------------- */

/* TODO: right now we show a MessageDialog but we could do things like an InfoBar etc */
void
gdu_window_show_error (GduWindow   *window,
                       const gchar *message,
                       GError      *error)
{
  GtkWidget *dialog;
  GError *fixed_up_error;

  /* Never show an error if it's because the user dismissed the
   * authentication dialog himself
   */
  if (error->domain == UDISKS_ERROR &&
      error->code == UDISKS_ERROR_NOT_AUTHORIZED_DISMISSED)
    goto no_dialog;

  fixed_up_error = g_error_copy (error);
  if (g_dbus_error_is_remote_error (fixed_up_error))
    g_dbus_error_strip_remote_error (fixed_up_error);

  /* TODO: probably provide the error-domain / error-code / D-Bus error name
   * in a GtkExpander.
   */
  dialog = gtk_message_dialog_new_with_markup (GTK_WINDOW (window),
                                               GTK_DIALOG_MODAL,
                                               GTK_MESSAGE_ERROR,
                                               GTK_BUTTONS_CLOSE,
                                               "<big><b>%s</b></big>",
                                               message);
  gtk_message_dialog_format_secondary_markup (GTK_MESSAGE_DIALOG (dialog),
                                              "%s (%s, %d)",
                                              fixed_up_error->message,
                                              g_quark_to_string (error->domain),
                                              error->code);
  g_error_free (fixed_up_error);
  gtk_dialog_run (GTK_DIALOG (dialog));
  gtk_widget_destroy (dialog);

 no_dialog:
  ;
}

/* ---------------------------------------------------------------------------------------------------- */

static void
on_generic_menu_item_edit_label (GtkMenuItem *menu_item,
                                 gpointer   user_data)
{
  GduWindow *window = GDU_WINDOW (user_data);
  UDisksObject *object;

  object = gdu_volume_grid_get_selected_device (GDU_VOLUME_GRID (window->volume_grid));
  g_assert (object != NULL);
  gdu_filesystem_dialog_show (window, object);
}

/* ---------------------------------------------------------------------------------------------------- */

static void
on_generic_menu_item_edit_partition (GtkMenuItem *menu_item,
                                     gpointer   user_data)
{
  GduWindow *window = GDU_WINDOW (user_data);
  UDisksObject *object;

  object = gdu_volume_grid_get_selected_device (GDU_VOLUME_GRID (window->volume_grid));
  g_assert (object != NULL);
  gdu_partition_dialog_show (window, object);
}

/* ---------------------------------------------------------------------------------------------------- */

static void
on_generic_menu_item_configure_fstab (GtkMenuItem *menu_item,
                                      gpointer   user_data)
{
  GduWindow *window = GDU_WINDOW (user_data);
  UDisksObject *object;
  object = gdu_volume_grid_get_selected_device (GDU_VOLUME_GRID (window->volume_grid));
  if (object == NULL)
    object = gdu_volume_grid_get_block_object (GDU_VOLUME_GRID (window->volume_grid));
  gdu_fstab_dialog_show (window, object);
}

static void
on_generic_menu_item_view_smart (GtkMenuItem *menu_item,
                                 gpointer     user_data)
{
  GduWindow *window = GDU_WINDOW (user_data);
  gdu_ata_smart_dialog_show (window, window->current_object);
}

static void
on_generic_menu_item_configure_crypttab (GtkMenuItem *menu_item,
                                         gpointer   user_data)
{
  GduWindow *window = GDU_WINDOW (user_data);
  UDisksObject *object;

  object = gdu_volume_grid_get_selected_device (GDU_VOLUME_GRID (window->volume_grid));
  if (object == NULL)
    object = gdu_volume_grid_get_block_object (GDU_VOLUME_GRID (window->volume_grid));
  gdu_crypttab_dialog_show (window, object);
}

/* ---------------------------------------------------------------------------------------------------- */

static void
mount_cb (UDisksFilesystem *filesystem,
          GAsyncResult     *res,
          gpointer          user_data)
{
  GduWindow *window = GDU_WINDOW (user_data);
  GError *error;

  error = NULL;
  if (!udisks_filesystem_call_mount_finish (filesystem,
                                            NULL, /* out_mount_path */
                                            res,
                                            &error))
    {
      gdu_window_show_error (window,
                             _("Error mounting filesystem"),
                             error);
      g_error_free (error);
    }
  g_object_unref (window);
}

static void
on_devtab_action_mount_activated (GtkAction *action,
                                  gpointer   user_data)
{
  GduWindow *window = GDU_WINDOW (user_data);
  UDisksObject *object;
  UDisksFilesystem *filesystem;

  object = gdu_volume_grid_get_selected_device (GDU_VOLUME_GRID (window->volume_grid));
  filesystem = udisks_object_peek_filesystem (object);
  udisks_filesystem_call_mount (filesystem,
                                g_variant_new ("a{sv}", NULL), /* options */
                                NULL, /* cancellable */
                                (GAsyncReadyCallback) mount_cb,
                                g_object_ref (window));
}

/* ---------------------------------------------------------------------------------------------------- */

static void
unmount_cb (UDisksFilesystem *filesystem,
            GAsyncResult     *res,
            gpointer          user_data)
{
  GduWindow *window = GDU_WINDOW (user_data);
  GError *error;

  error = NULL;
  if (!udisks_filesystem_call_unmount_finish (filesystem,
                                              res,
                                              &error))
    {
      gdu_window_show_error (window,
                             _("Error unmounting filesystem"),
                             error);
      g_error_free (error);
    }
  g_object_unref (window);
}

static void
on_devtab_action_unmount_activated (GtkAction *action,
                                    gpointer   user_data)
{
  GduWindow *window = GDU_WINDOW (user_data);
  UDisksObject *object;
  UDisksFilesystem *filesystem;

  object = gdu_volume_grid_get_selected_device (GDU_VOLUME_GRID (window->volume_grid));
  filesystem = udisks_object_peek_filesystem (object);
  udisks_filesystem_call_unmount (filesystem,
                                  g_variant_new ("a{sv}", NULL), /* options */
                                  NULL, /* cancellable */
                                  (GAsyncReadyCallback) unmount_cb,
                                  g_object_ref (window));
}

/* ---------------------------------------------------------------------------------------------------- */

static void
on_devtab_action_generic_activated (GtkAction *action,
                                    gpointer   user_data)
{
  GduWindow *window = GDU_WINDOW (user_data);
  gtk_menu_popup (GTK_MENU (window->generic_menu),
                  NULL, NULL, NULL, NULL, 1, gtk_get_current_event_time ());
}

/* ---------------------------------------------------------------------------------------------------- */

static void
on_devtab_action_partition_create_activated (GtkAction *action,
                                             gpointer   user_data)
{
  g_debug ("%s: TODO", G_STRFUNC);
}

/* ---------------------------------------------------------------------------------------------------- */

static void
eject_cb (UDisksDrive  *drive,
          GAsyncResult *res,
          gpointer      user_data)
{
  GduWindow *window = GDU_WINDOW (user_data);
  GError *error;

  error = NULL;
  if (!udisks_drive_call_eject_finish (drive,
                                       res,
                                       &error))
    {
      gdu_window_show_error (window,
                             _("Error ejecting media"),
                             error);
      g_error_free (error);
    }
  g_object_unref (window);
}

static void
on_devtab_action_eject_activated (GtkAction *action,
                                  gpointer   user_data)
{
  GduWindow *window = GDU_WINDOW (user_data);
  UDisksDrive *drive;

  drive = udisks_object_peek_drive (window->current_object);
  udisks_drive_call_eject (drive,
                           g_variant_new ("a{sv}", NULL), /* options */
                           NULL, /* cancellable */
                           (GAsyncReadyCallback) eject_cb,
                           g_object_ref (window));
}

/* ---------------------------------------------------------------------------------------------------- */

static void
on_devtab_action_unlock_activated (GtkAction *action,
                                   gpointer   user_data)
{
  GduWindow *window = GDU_WINDOW (user_data);
  UDisksObject *object;

  object = gdu_volume_grid_get_selected_device (GDU_VOLUME_GRID (window->volume_grid));
  g_assert (object != NULL);
  gdu_unlock_dialog_show (window, object);
}

/* ---------------------------------------------------------------------------------------------------- */

static void
lock_cb (UDisksEncrypted *encrypted,
         GAsyncResult    *res,
         gpointer         user_data)
{
  GduWindow *window = GDU_WINDOW (user_data);
  GError *error;

  error = NULL;
  if (!udisks_encrypted_call_lock_finish (encrypted,
                                          res,
                                          &error))
    {
      gdu_window_show_error (window,
                             _("Error locking encrypted device"),
                             error);
      g_error_free (error);
    }
  g_object_unref (window);
}

static void
on_devtab_action_lock_activated (GtkAction *action,
                                 gpointer   user_data)
{
  GduWindow *window = GDU_WINDOW (user_data);
  UDisksObject *object;
  UDisksEncrypted *encrypted;

  object = gdu_volume_grid_get_selected_device (GDU_VOLUME_GRID (window->volume_grid));
  encrypted = udisks_object_peek_encrypted (object);

  udisks_encrypted_call_lock (encrypted,
                              g_variant_new ("a{sv}", NULL), /* options */
                              NULL, /* cancellable */
                              (GAsyncReadyCallback) lock_cb,
                              g_object_ref (window));
}

/* ---------------------------------------------------------------------------------------------------- */

static void
swapspace_start_cb (UDisksSwapspace  *swapspace,
                    GAsyncResult     *res,
                    gpointer          user_data)
{
  GduWindow *window = GDU_WINDOW (user_data);
  GError *error;

  error = NULL;
  if (!udisks_swapspace_call_start_finish (swapspace,
                                           res,
                                           &error))
    {
      gdu_window_show_error (window,
                             _("Error starting swap"),
                             error);
      g_error_free (error);
    }
  g_object_unref (window);
}

static void
on_devtab_action_activate_swap_activated (GtkAction *action, gpointer user_data)
{
  GduWindow *window = GDU_WINDOW (user_data);
  UDisksObject *object;
  UDisksSwapspace *swapspace;

  object = gdu_volume_grid_get_selected_device (GDU_VOLUME_GRID (window->volume_grid));
  swapspace = udisks_object_peek_swapspace (object);
  udisks_swapspace_call_start (swapspace,
                               g_variant_new ("a{sv}", NULL), /* options */
                               NULL, /* cancellable */
                               (GAsyncReadyCallback) swapspace_start_cb,
                               g_object_ref (window));
}

static void
swapspace_stop_cb (UDisksSwapspace  *swapspace,
                   GAsyncResult     *res,
                   gpointer          user_data)
{
  GduWindow *window = GDU_WINDOW (user_data);
  GError *error;

  error = NULL;
  if (!udisks_swapspace_call_stop_finish (swapspace,
                                          res,
                                          &error))
    {
      gdu_window_show_error (window,
                             _("Error stopping swap"),
                             error);
      g_error_free (error);
    }
  g_object_unref (window);
}

static void
on_devtab_action_deactivate_swap_activated (GtkAction *action, gpointer user_data)
{
  GduWindow *window = GDU_WINDOW (user_data);
  UDisksObject *object;
  UDisksSwapspace *swapspace;

  object = gdu_volume_grid_get_selected_device (GDU_VOLUME_GRID (window->volume_grid));
  swapspace = udisks_object_peek_swapspace (object);
  udisks_swapspace_call_stop (swapspace,
                              g_variant_new ("a{sv}", NULL), /* options */
                              NULL, /* cancellable */
                              (GAsyncReadyCallback) swapspace_stop_cb,
                              g_object_ref (window));
}

/* ---------------------------------------------------------------------------------------------------- */
