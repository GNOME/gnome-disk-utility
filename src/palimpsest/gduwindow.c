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

  GtkWidget *main_hbox;
  GtkWidget *details_notebook;
  GtkWidget *device_scrolledwindow;
  GtkWidget *device_treeview;
  GtkWidget *device_toolbar;
  GtkWidget *device_toolbar_attach_disk_image_button;
  GtkWidget *device_toolbar_detach_disk_image_button;
  GtkWidget *devtab_drive_value_label;
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
  {G_STRUCT_OFFSET (GduWindow, main_hbox), "palimpsest-hbox"},
  {G_STRUCT_OFFSET (GduWindow, device_scrolledwindow), "device-tree-scrolledwindow"},
  {G_STRUCT_OFFSET (GduWindow, device_toolbar), "device-tree-add-remove-toolbar"},
  {G_STRUCT_OFFSET (GduWindow, device_toolbar_attach_disk_image_button), "device-tree-attach-disk-image-button"},
  {G_STRUCT_OFFSET (GduWindow, device_toolbar_detach_disk_image_button), "device-tree-detach-disk-image-button"},
  {G_STRUCT_OFFSET (GduWindow, device_treeview), "device-tree-treeview"},
  {G_STRUCT_OFFSET (GduWindow, details_notebook), "palimpsest-notebook"},
  {G_STRUCT_OFFSET (GduWindow, devtab_drive_table), "devtab-drive-table"},
  {G_STRUCT_OFFSET (GduWindow, devtab_drive_value_label), "devtab-drive-value-label"},
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

  SHOW_FLAGS_POPUP_MENU_CONFIGURE_FSTAB    = (1<<9),
  SHOW_FLAGS_POPUP_MENU_CONFIGURE_CRYPTTAB = (1<<10),
  SHOW_FLAGS_POPUP_MENU_EDIT_LABEL         = (1<<11),
  SHOW_FLAGS_POPUP_MENU_EDIT_PARTITION     = (1<<12),
} ShowFlags;

static void gdu_window_show_error (GduWindow   *window,
                                   const gchar *message,
                                   GError      *orig_error);

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

static void on_generic_menu_item_configure_fstab (GtkMenuItem *menu_item,
                                                  gpointer   user_data);
static void on_generic_menu_item_configure_crypttab (GtkMenuItem *menu_item,
                                                     gpointer   user_data);
static void on_generic_menu_item_edit_label (GtkMenuItem *menu_item,
                                             gpointer   user_data);
static void on_generic_menu_item_edit_partition (GtkMenuItem *menu_item,
                                                 gpointer   user_data);

static GtkWidget *
gdu_window_new_widget (GduWindow    *window,
                       const gchar  *name,
                       GtkBuilder  **out_builder);

G_DEFINE_TYPE (GduWindow, gdu_window, GTK_TYPE_WINDOW);

static void
gdu_window_init (GduWindow *window)
{
  window->label_connections = g_hash_table_new_full (g_str_hash,
                                                     g_str_equal,
                                                     g_free,
                                                     NULL);
}

static void on_object_added (GDBusObjectManager  *manager,
                             GDBusObject         *object,
                             gpointer             user_data);

static void on_object_removed (GDBusObjectManager  *manager,
                               GDBusObject         *object,
                               gpointer             user_data);

static void on_interface_added (GDBusObjectManager  *manager,
                                GDBusObject         *object,
                                GDBusInterface      *interface,
                                gpointer             user_data);

static void on_interface_removed (GDBusObjectManager  *manager,
                                  GDBusObject         *object,
                                  GDBusInterface      *interface,
                                  gpointer             user_data);

static void on_interface_proxy_properties_changed (GDBusObjectManagerClient   *manager,
                                                   GDBusObjectProxy           *object_proxy,
                                                   GDBusProxy                 *interface_proxy,
                                                   GVariant                   *changed_properties,
                                                   const gchar *const         *invalidated_properties,
                                                   gpointer                    user_data);

static void
gdu_window_finalize (GObject *object)
{
  GduWindow *window = GDU_WINDOW (object);
  GDBusObjectManager *object_manager;

  object_manager = udisks_client_get_object_manager (window->client);
  g_signal_handlers_disconnect_by_func (object_manager,
                                        G_CALLBACK (on_object_added),
                                        window);
  g_signal_handlers_disconnect_by_func (object_manager,
                                        G_CALLBACK (on_object_removed),
                                        window);
  g_signal_handlers_disconnect_by_func (object_manager,
                                        G_CALLBACK (on_interface_added),
                                        window);
  g_signal_handlers_disconnect_by_func (object_manager,
                                        G_CALLBACK (on_interface_removed),
                                        window);
  g_signal_handlers_disconnect_by_func (object_manager,
                                        G_CALLBACK (on_interface_proxy_properties_changed),
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
      GtkTreeSelection *tree_selection;
      tree_selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (window->device_treeview));
      gtk_tree_selection_select_iter (tree_selection, &iter);
    }

  show_flags = SHOW_FLAGS_NONE;
  if (object != NULL)
    {
      if (udisks_object_peek_drive (object) != NULL ||
          udisks_object_peek_block_device (object) != NULL)
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

      object = UDISKS_OBJECT (g_dbus_object_manager_get_object (udisks_client_get_object_manager (data->window->client),
                                                                out_loop_device_object_path));
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


gboolean _gdu_application_get_running_from_source_tree (GduApplication *app);

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

static void
gdu_window_constructed (GObject *object)
{
  GduWindow *window = GDU_WINDOW (object);
  GError *error;
  GtkTreeViewColumn *column;
  GtkCellRenderer *renderer;
  GtkTreeSelection *selection;
  GtkStyleContext *context;
  GDBusObjectManager *object_manager;
  GList *children, *l;
  GtkWidget *headers_label;
  guint n;

  init_css (window);

  /* chain up */
  if (G_OBJECT_CLASS (gdu_window_parent_class)->constructed != NULL)
    G_OBJECT_CLASS (gdu_window_parent_class)->constructed (object);

  window->builder = gtk_builder_new ();
  error = NULL;
  window->builder_path = _gdu_application_get_running_from_source_tree (window->application)
    ? "../../data/ui/palimpsest.ui" :
    PACKAGE_DATA_DIR "/gnome-disk-utility/palimpsest.ui";
  if (gtk_builder_add_from_file (window->builder,
                                 window->builder_path,
                                 &error) == 0)
    {
      g_error ("Error loading %s: %s", window->builder_path, error->message);
      g_error_free (error);
    }

  /* set up widgets */
  for (n = 0; widget_mapping[n].name != NULL; n++)
    {
      gpointer *p = (gpointer *) ((char *) window + widget_mapping[n].offset);
      *p = G_OBJECT (gtk_builder_get_object (window->builder, widget_mapping[n].name));
    }

  gtk_widget_reparent (window->main_hbox, GTK_WIDGET (window));
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

  headers_label = gtk_label_new (NULL);
  gtk_label_set_markup_with_mnemonic (GTK_LABEL (headers_label), _("_Devices"));
  gtk_misc_set_alignment (GTK_MISC (headers_label), 0.0, 0.5);
  gtk_label_set_mnemonic_widget (GTK_LABEL (headers_label), window->device_treeview);
  gtk_widget_show_all (headers_label);

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
  gtk_tree_view_column_set_widget (column, headers_label);
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

  object_manager = udisks_client_get_object_manager (window->client);
  g_signal_connect (object_manager,
                    "object-added",
                    G_CALLBACK (on_object_added),
                    window);
  g_signal_connect (object_manager,
                    "object-removed",
                    G_CALLBACK (on_object_removed),
                    window);
  g_signal_connect (object_manager,
                    "interface-added",
                    G_CALLBACK (on_interface_added),
                    window);
  g_signal_connect (object_manager,
                    "interface-removed",
                    G_CALLBACK (on_interface_removed),
                    window);
  g_signal_connect (object_manager,
                    "interface-proxy-properties-changed",
                    G_CALLBACK (on_interface_proxy_properties_changed),
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

GtkWidget *
gdu_window_get_widget (GduWindow    *window,
                       const gchar  *name)
{
  g_return_val_if_fail (GDU_IS_WINDOW (window), NULL);
  g_return_val_if_fail (name != NULL, NULL);
  return GTK_WIDGET (gtk_builder_get_object (window->builder, name));
}

static GtkWidget *
gdu_window_new_widget (GduWindow    *window,
                       const gchar  *name,
                       GtkBuilder  **out_builder)
{
  GtkWidget *ret;
  GtkBuilder *builder;
  GError *error;

  g_return_val_if_fail (GDU_IS_WINDOW (window), NULL);
  g_return_val_if_fail (name != NULL, NULL);

  ret = NULL;

  builder = gtk_builder_new ();

  error = NULL;
  if (gtk_builder_add_from_file (builder,
                                 window->builder_path,
                                 &error) == 0)
    {
      g_error ("Error loading %s: %s", window->builder_path, error->message);
      g_error_free (error);
      goto out;
    }

  ret = GTK_WIDGET (gtk_builder_get_object (builder, name));
  *out_builder = builder;
  builder = NULL;

 out:
  if (builder != NULL)
    g_object_unref (builder);
  return ret;
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

  key_label = gdu_window_get_widget (window, key_label_id);
  label = gdu_window_get_widget (window, label_id);

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
  s = udisks_util_get_size_for_display (size, FALSE, TRUE);
  set_markup (window, key_label_id, label_id, s, size);
  g_free (s);
}

static GList *
get_top_level_block_devices_for_drive (GduWindow   *window,
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
      UDisksBlockDevice *block;

      block = udisks_object_get_block_device (object);
      if (block == NULL)
        continue;

      if (g_strcmp0 (udisks_block_device_get_drive (block), drive_object_path) == 0 &&
          !udisks_block_device_get_part_entry (block))
        {
          ret = g_list_append (ret, g_object_ref (object));
        }
      g_object_unref (block);
    }
  g_list_foreach (object_proxies, (GFunc) g_object_unref, NULL);
  g_list_free (object_proxies);
  return ret;
}

static gint
block_device_compare_on_preferred (UDisksObject *a,
                                   UDisksObject *b)
{
  return g_strcmp0 (udisks_block_device_get_preferred_device (udisks_object_peek_block_device (a)),
                    udisks_block_device_get_preferred_device (udisks_object_peek_block_device (b)));
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
update_all (GduWindow     *window,
            UDisksObject  *object)
{
  switch (window->current_page)
    {
    case DETAILS_PAGE_NOT_SELECTED:
      /* Nothing to update */
      break;

    case DETAILS_PAGE_NOT_IMPLEMENTED:
      /* Nothing to update */
      break;

    case DETAILS_PAGE_DEVICE:
      /* this is a little too inclusive.. */
      if (gdu_volume_grid_includes_object (GDU_VOLUME_GRID (window->volume_grid), object))
        {
          ShowFlags show_flags;
          show_flags = SHOW_FLAGS_NONE;
          update_details_page (window, window->current_page, &show_flags);
          update_for_show_flags (window, show_flags);
        }
      break;
    }
}

static void
on_object_added (GDBusObjectManager  *manager,
                 GDBusObject         *object,
                 gpointer             user_data)
{
  GduWindow *window = GDU_WINDOW (user_data);
  update_all (window, UDISKS_OBJECT (object));
}

static void
on_object_removed (GDBusObjectManager  *manager,
                   GDBusObject         *object,
                   gpointer             user_data)
{
  GduWindow *window = GDU_WINDOW (user_data);
  update_all (window, UDISKS_OBJECT (object));
}

static void
on_interface_added (GDBusObjectManager  *manager,
                    GDBusObject         *object,
                    GDBusInterface      *interface,
                    gpointer             user_data)
{
  GduWindow *window = GDU_WINDOW (user_data);
  update_all (window, UDISKS_OBJECT (object));
}

static void
on_interface_removed (GDBusObjectManager  *manager,
                      GDBusObject         *object,
                      GDBusInterface      *interface,
                      gpointer             user_data)
{
  GduWindow *window = GDU_WINDOW (user_data);
  update_all (window, UDISKS_OBJECT (object));
}

static void
on_interface_proxy_properties_changed (GDBusObjectManagerClient   *manager,
                                       GDBusObjectProxy           *object_proxy,
                                       GDBusProxy                 *interface_proxy,
                                       GVariant                   *changed_properties,
                                       const gchar *const         *invalidated_properties,
                                       gpointer                    user_data)
{
  GduWindow *window = GDU_WINDOW (user_data);
  update_all (window, UDISKS_OBJECT (object_proxy));
}

/* ---------------------------------------------------------------------------------------------------- */

static void
setup_device_page (GduWindow     *window,
                   UDisksObject  *object)
{
  UDisksDrive *drive;
  UDisksBlockDevice *block;

  drive = udisks_object_peek_drive (object);
  block = udisks_object_peek_block_device (object);

  gdu_volume_grid_set_container_visible (GDU_VOLUME_GRID (window->volume_grid), FALSE);
  if (drive != NULL)
    {
      GList *block_devices;
      gchar *drive_name;
      gchar *drive_desc;
      GIcon *drive_icon;
      gchar *drive_media_desc;
      GIcon *drive_media_icon;

      /* TODO: for multipath, ensure e.g. mpathk is before sda, sdb */
      block_devices = get_top_level_block_devices_for_drive (window, g_dbus_object_get_object_path (G_DBUS_OBJECT (object)));
      block_devices = g_list_sort (block_devices, (GCompareFunc) block_device_compare_on_preferred);

      udisks_util_get_drive_info (drive,
                                  &drive_name,
                                  &drive_desc,
                                  &drive_icon,
                                  &drive_media_desc,
                                  &drive_media_icon);
      if (block_devices != NULL)
        gdu_volume_grid_set_block_device (GDU_VOLUME_GRID (window->volume_grid), block_devices->data);
      else
        gdu_volume_grid_set_block_device (GDU_VOLUME_GRID (window->volume_grid), NULL);

      g_free (drive_name);
      g_free (drive_desc);
      g_object_unref (drive_icon);
      g_free (drive_media_desc);
      if (drive_media_icon != NULL)
        g_object_unref (drive_media_icon);

      g_list_foreach (block_devices, (GFunc) g_object_unref, NULL);
      g_list_free (block_devices);
    }
  else if (block != NULL)
    {
      gdu_volume_grid_set_block_device (GDU_VOLUME_GRID (window->volume_grid), object);
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
  GList *block_devices;
  GList *l;
  GString *str;
  const gchar *drive_vendor;
  const gchar *drive_model;
  gchar *name;
  gchar *description;
  gchar *media_description;
  GIcon *drive_icon;
  GIcon *media_icon;
  guint64 size;

  //g_debug ("In update_device_page_for_drive() - selected=%s",
  //         object != NULL ? g_dbus_object_get_object_path (object) : "<nothing>");

  /* TODO: for multipath, ensure e.g. mpathk is before sda, sdb */
  block_devices = get_top_level_block_devices_for_drive (window, g_dbus_object_get_object_path (G_DBUS_OBJECT (object)));
  block_devices = g_list_sort (block_devices, (GCompareFunc) block_device_compare_on_preferred);

  udisks_util_get_drive_info (drive, &name, &description, &drive_icon, &media_description, &media_icon);

  drive_vendor = udisks_drive_get_vendor (drive);
  drive_model = udisks_drive_get_model (drive);

  str = g_string_new (NULL);
  for (l = block_devices; l != NULL; l = l->next)
    {
      UDisksObject *block_object = UDISKS_OBJECT (l->data);
      if (str->len > 0)
        g_string_append_c (str, ' ');
      g_string_append (str, udisks_block_device_get_preferred_device (udisks_object_peek_block_device (block_object)));
    }
  s = g_strdup_printf ("<big><b>%s</b></big>\n"
                       "<small><span foreground=\"#555555\">%s</span></small>",
                       description,
                       str->str);
  g_string_free (str, TRUE);
  gtk_label_set_markup (GTK_LABEL (window->devtab_drive_value_label), s);
  gtk_widget_show (window->devtab_drive_value_label);
  g_free (s);
  if (media_icon != NULL)
    gtk_image_set_from_gicon (GTK_IMAGE (window->devtab_drive_image), media_icon, GTK_ICON_SIZE_DIALOG);
  else
    gtk_image_set_from_gicon (GTK_IMAGE (window->devtab_drive_image), drive_icon, GTK_ICON_SIZE_DIALOG);
  gtk_widget_show (window->devtab_drive_image);

  if (strlen (drive_vendor) == 0)
    s = g_strdup (drive_model);
  else if (strlen (drive_model) == 0)
    s = g_strdup (drive_vendor);
  else
    s = g_strconcat (drive_vendor, " ", drive_model, NULL);
  set_markup (window,
              "devtab-model-label",
              "devtab-model-value-label", s, SET_MARKUP_FLAGS_HYPHEN_IF_EMPTY);
  g_free (s);
  set_markup (window,
              "devtab-serial-number-label",
              "devtab-serial-number-value-label",
              udisks_drive_get_serial (drive), SET_MARKUP_FLAGS_NONE);
  set_markup (window,
              "devtab-firmware-version-label",
              "devtab-firmware-version-value-label",
              udisks_drive_get_revision (drive), SET_MARKUP_FLAGS_NONE);
  set_markup (window,
              "devtab-wwn-label",
              "devtab-wwn-value-label",
              udisks_drive_get_wwn (drive), SET_MARKUP_FLAGS_NONE);

  size = udisks_drive_get_size (drive);
  if (size > 0)
    {
      s = udisks_util_get_size_for_display (size, FALSE, TRUE);
      set_markup (window,
                  "devtab-drive-size-label",
                  "devtab-drive-size-value-label",
                  s, SET_MARKUP_FLAGS_HYPHEN_IF_EMPTY);
      g_free (s);
      set_markup (window,
                  "devtab-media-label",
                  "devtab-media-value-label",
                  media_description, SET_MARKUP_FLAGS_NONE);
    }
  else
    {
      set_markup (window,
                  "devtab-drive-size-label",
                  "devtab-drive-size-value-label",
                  "",
                  SET_MARKUP_FLAGS_HYPHEN_IF_EMPTY);
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

  g_list_foreach (block_devices, (GFunc) g_object_unref, NULL);
  g_list_free (block_devices);
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
      UDisksBlockDevice *block;

      block = udisks_object_peek_block_device (object);
      if (block == NULL)
        continue;

      if (g_strcmp0 (udisks_block_device_get_crypto_backing_device (block),
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

static gchar *
calculate_configuration_for_display (UDisksBlockDevice *block,
                                     guint              show_flags)
{
  GString *str;
  GVariantIter iter;
  const gchar *config_type;
  gboolean mentioned_fstab = FALSE;
  gboolean mentioned_crypttab = FALSE;
  gchar *ret;

  ret = NULL;

  /* TODO: could include more details such as whether the
   * device is activated at boot time
   */

  str = g_string_new (NULL);
  g_variant_iter_init (&iter, udisks_block_device_get_configuration (block));
  while (g_variant_iter_next (&iter, "(&s@a{sv})", &config_type, NULL))
    {
      if (g_strcmp0 (config_type, "fstab") == 0)
        {
          if (!mentioned_fstab)
            {
              mentioned_fstab = TRUE;
              if (str->len > 0)
                g_string_append (str, ", ");
              /* Translators: Shown when the device is configured in /etc/fstab.
               * Do not translate "/etc/fstab".
               */
              g_string_append (str, _("Yes (via /etc/fstab)"));
            }
        }
      else if (g_strcmp0 (config_type, "crypttab") == 0)
        {
          if (!mentioned_crypttab)
            {
              mentioned_crypttab = TRUE;
              if (str->len > 0)
                g_string_append (str, ", ");
              /* Translators: Shown when the device is configured in /etc/crypttab.
               * Do not translate "/etc/crypttab".
               */
              g_string_append (str, _("Yes (via /etc/crypttab)"));
            }
        }
      else
        {
          if (str->len > 0)
            g_string_append (str, ", ");
          g_string_append (str, config_type);
        }
    }

  if (str->len == 0)
    {
      /* No known configuration... show "No" only if we actually
       * know how to configure the device or already offer to
       * configure the device...
       */
      if (g_strcmp0 (udisks_block_device_get_id_usage (block), "filesystem") == 0 ||
          (g_strcmp0 (udisks_block_device_get_id_usage (block), "other") == 0 &&
           g_strcmp0 (udisks_block_device_get_id_type (block), "swap") == 0) ||
          g_strcmp0 (udisks_block_device_get_id_usage (block), "crypto") == 0 ||
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

static gboolean
has_configuration (UDisksBlockDevice *block,
                   const gchar       *type,
                   gboolean          *out_has_passphrase)
{
  GVariantIter iter;
  const gchar *config_type;
  GVariant *config_details;
  gboolean ret;
  gboolean has_passphrase;

  ret = FALSE;
  has_passphrase = FALSE;

  g_variant_iter_init (&iter, udisks_block_device_get_configuration (block));
  while (g_variant_iter_next (&iter, "(&s@a{sv})", &config_type, &config_details))
    {
      if (g_strcmp0 (config_type, type) == 0)
        {
          if (g_strcmp0 (type, "crypttab") == 0)
            {
              const gchar *passphrase_path;
              if (g_variant_lookup (config_details, "passphrase-path", "^&ay", &passphrase_path) &&
                  strlen (passphrase_path) > 0 &&
                  !g_str_has_prefix (passphrase_path, "/dev"))
                has_passphrase = TRUE;
            }
          ret = TRUE;
          g_variant_unref (config_details);
          goto out;
        }
      g_variant_unref (config_details);
    }

 out:
  if (out_has_passphrase != NULL)
    *out_has_passphrase = has_passphrase;
  return ret;
}

static void
update_device_page_for_block (GduWindow          *window,
                              UDisksObject       *object,
                              UDisksBlockDevice  *block,
                              guint64             size,
                              ShowFlags          *show_flags)
{
  const gchar *usage;
  const gchar *type;
  const gchar *version;
  gint partition_type;
  gchar *type_for_display;
  gchar *configuration_for_display;

  /* Since /etc/fstab, /etc/crypttab and so on can reference
   * any device regardless of its content ... we want to show
   * the relevant menu option (to get to the configuration dialog)
   * if the device matches the configuration....
   */
  if (has_configuration (block, "fstab", NULL))
    *show_flags |= SHOW_FLAGS_POPUP_MENU_CONFIGURE_FSTAB;
  if (has_configuration (block, "crypttab", NULL))
    *show_flags |= SHOW_FLAGS_POPUP_MENU_CONFIGURE_CRYPTTAB;

  /* if the device has no media and there is no existing configuration, then
   * show CONFIGURE_FSTAB since the user might want to add an entry for e.g.
   * /media/cdrom
   */
  if (udisks_block_device_get_size (block) == 0 &&
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
              udisks_block_device_get_preferred_device (block), SET_MARKUP_FLAGS_NONE);
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

  usage = udisks_block_device_get_id_usage (block);
  type = udisks_block_device_get_id_type (block);
  version = udisks_block_device_get_id_version (block);
  partition_type = strtol (udisks_block_device_get_part_entry_type (block), NULL, 0);

  if (size > 0)
    {
      if (udisks_block_device_get_part_entry (block) &&
          g_strcmp0 (udisks_block_device_get_part_entry_scheme (block), "mbr") == 0 &&
          (partition_type == 0x05 || partition_type == 0x0f || partition_type == 0x85))
        {
          type_for_display = g_strdup (_("Extended Partition"));
        }
      else
        {
          type_for_display = udisks_util_get_id_for_display (usage, type, version, TRUE);
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

  if (udisks_block_device_get_part_entry (block))
    {
      *show_flags |= SHOW_FLAGS_POPUP_MENU_EDIT_PARTITION;
    }
  else
    {
      UDisksObject *drive_object;
      drive_object = (UDisksObject *) g_dbus_object_manager_get_object (udisks_client_get_object_manager (window->client),
                                                                        udisks_block_device_get_drive (block));
      if (drive_object != NULL)
        {
          UDisksDrive *drive;
          drive = udisks_object_peek_drive (drive_object);
          if (udisks_drive_get_media_removable (drive))
            *show_flags |= SHOW_FLAGS_EJECT_BUTTON;
          g_object_unref (drive_object);
        }
    }

  if (g_strcmp0 (udisks_block_device_get_id_usage (block), "filesystem") == 0)
    {
      UDisksFilesystem *filesystem;

      filesystem = udisks_object_peek_filesystem (object);
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
        }

      *show_flags |= SHOW_FLAGS_POPUP_MENU_EDIT_LABEL;
      *show_flags |= SHOW_FLAGS_POPUP_MENU_CONFIGURE_FSTAB;
    }
  else if (g_strcmp0 (udisks_block_device_get_id_usage (block), "other") == 0 &&
           g_strcmp0 (udisks_block_device_get_id_type (block), "swap") == 0)
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
  else if (g_strcmp0 (udisks_block_device_get_id_usage (block), "crypto") == 0)
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
                                 UDisksBlockDevice  *block,
                                 ShowFlags          *show_flags)
{
  //g_debug ("In update_device_page_for_no_media() - selected=%s",
  //         object != NULL ? g_dbus_object_get_object_path (object) : "<nothing>");
}

static void
update_device_page_for_free_space (GduWindow          *window,
                                   UDisksObject       *object,
                                   UDisksBlockDevice  *block,
                                   guint64             size,
                                   ShowFlags          *show_flags)
{
  //g_debug ("In update_device_page_for_free_space() - size=%" G_GUINT64_FORMAT " selected=%s",
  //         size,
  //         object != NULL ? g_dbus_object_get_object_path (object) : "<nothing>");

  set_markup (window,
              "devtab-device-label",
              "devtab-device-value-label",
              udisks_block_device_get_preferred_device (block), SET_MARKUP_FLAGS_NONE);
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
  UDisksBlockDevice *block;
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
  block = udisks_object_peek_block_device (window->current_object);
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
        object = gdu_volume_grid_get_block_device (GDU_VOLUME_GRID (window->volume_grid));
      if (object != NULL)
        {
          block = udisks_object_peek_block_device (object);
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
  gdu_volume_grid_set_block_device (GDU_VOLUME_GRID (window->volume_grid), NULL);
}

/* ---------------------------------------------------------------------------------------------------- */

static void
on_volume_grid_changed (GduVolumeGrid  *grid,
                        gpointer        user_data)
{
  GduWindow *window = GDU_WINDOW (user_data);
  ShowFlags show_flags;
  show_flags = SHOW_FLAGS_NONE;
  update_device_page (window, &show_flags);
  update_for_show_flags (window, show_flags);
}

/* ---------------------------------------------------------------------------------------------------- */

/* TODO: right now we show a MessageDialog but we could do things like an InfoBar etc */
static void
gdu_window_show_error (GduWindow   *window,
                       const gchar *message,
                       GError      *orig_error)
{
  GtkWidget *dialog;
  GError *error;

  /* Never show an error if it's because the user dismissed the
   * authentication dialog himself
   */
  if (orig_error->domain == UDISKS_ERROR &&
      orig_error->code == UDISKS_ERROR_NOT_AUTHORIZED_DISMISSED)
    goto no_dialog;

  error = g_error_copy (orig_error);
  if (g_dbus_error_is_remote_error (error))
    g_dbus_error_strip_remote_error (error);

  dialog = gtk_message_dialog_new_with_markup (GTK_WINDOW (window),
                                               GTK_DIALOG_MODAL,
                                               GTK_MESSAGE_ERROR,
                                               GTK_BUTTONS_CLOSE,
                                               "<big><b>%s</b></big>",
                                               message);
  gtk_message_dialog_format_secondary_markup (GTK_MESSAGE_DIALOG (dialog),
                                              "%s",
                                              error->message);
  g_error_free (error);
  gtk_dialog_run (GTK_DIALOG (dialog));
  gtk_widget_destroy (dialog);

 no_dialog:
  ;
}

/* ---------------------------------------------------------------------------------------------------- */

typedef struct
{
  GtkWidget *dialog;
  gchar *orig_label;
} ChangeFilesystemLabelData;

static void
on_change_filesystem_label_entry_changed (GtkEditable *editable,
                                          gpointer     user_data)
{
  ChangeFilesystemLabelData *data = user_data;
  gboolean sensitive;

  sensitive = FALSE;
  if (g_strcmp0 (gtk_entry_get_text (GTK_ENTRY (editable)), data->orig_label) != 0)
    {
      sensitive = TRUE;
    }

  gtk_dialog_set_response_sensitive (GTK_DIALOG (data->dialog),
                                     GTK_RESPONSE_OK,
                                     sensitive);
}

static void
change_filesystem_label_cb (UDisksFilesystem  *filesystem,
                            GAsyncResult      *res,
                            gpointer           user_data)
{
  GduWindow *window = GDU_WINDOW (user_data);
  GError *error;

  error = NULL;
  if (!udisks_filesystem_call_set_label_finish (filesystem,
                                                res,
                                                &error))
    {
      gdu_window_show_error (window,
                             _("Error setting label"),
                             error);
      g_error_free (error);
    }
  g_object_unref (window);
}

static void
on_generic_menu_item_edit_label (GtkMenuItem *menu_item,
                                 gpointer   user_data)
{
  GduWindow *window = GDU_WINDOW (user_data);
  gint response;
  GtkBuilder *builder;
  GtkWidget *dialog;
  GtkWidget *entry;
  UDisksObject *object;
  UDisksBlockDevice *block;
  UDisksFilesystem *filesystem;
  const gchar *label;
  ChangeFilesystemLabelData data;
  const gchar *label_to_set;

  object = gdu_volume_grid_get_selected_device (GDU_VOLUME_GRID (window->volume_grid));
  g_assert (object != NULL);
  block = udisks_object_peek_block_device (object);
  filesystem = udisks_object_peek_filesystem (object);
  g_assert (block != NULL);
  g_assert (filesystem != NULL);

  dialog = gdu_window_new_widget (window, "change-filesystem-label-dialog", &builder);
  entry = GTK_WIDGET (gtk_builder_get_object (builder, "change-filesystem-label-entry"));
  gtk_window_set_transient_for (GTK_WINDOW (dialog), GTK_WINDOW (window));
  gtk_dialog_set_default_response (GTK_DIALOG (dialog), GTK_RESPONSE_OK);

  label = udisks_block_device_get_id_label (block);
  g_signal_connect (entry,
                    "changed",
                    G_CALLBACK (on_change_filesystem_label_entry_changed),
                    &data);
  memset (&data, '\0', sizeof (ChangeFilesystemLabelData));
  data.dialog = dialog;
  data.orig_label = g_strdup (label);

  gtk_entry_set_text (GTK_ENTRY (entry), label);
  gtk_editable_select_region (GTK_EDITABLE (entry), 0, -1);

  gtk_widget_show_all (dialog);
  gtk_widget_grab_focus (entry);

  response = gtk_dialog_run (GTK_DIALOG (dialog));
  if (response != GTK_RESPONSE_OK)
    goto out;

  label_to_set = gtk_entry_get_text (GTK_ENTRY (entry));

  udisks_filesystem_call_set_label (filesystem,
                                    label_to_set,
                                    g_variant_new ("a{sv}", NULL), /* options */
                                    NULL, /* cancellable */
                                    (GAsyncReadyCallback) change_filesystem_label_cb,
                                    g_object_ref (window));

 out:
  g_free (data.orig_label);
  gtk_widget_hide (dialog);
  gtk_widget_destroy (dialog);
  g_object_unref (builder);
}

/* ---------------------------------------------------------------------------------------------------- */

typedef struct
{
  GtkWidget *dialog;
  gchar *orig_type;
  const gchar **part_types;
} ChangePartitionTypeData;

static void
on_change_partition_type_combo_box_changed (GtkComboBox *combo_box,
                                            gpointer     user_data)
{
  ChangePartitionTypeData *data = user_data;
  gint active;
  gboolean sensitive;

  sensitive = FALSE;
  active = gtk_combo_box_get_active (combo_box);
  if (active > 0)
    {
      if (g_strcmp0 (data->part_types[active], data->orig_type) != 0)
        {
          sensitive = TRUE;
        }
    }

  gtk_dialog_set_response_sensitive (GTK_DIALOG (data->dialog),
                                     GTK_RESPONSE_OK,
                                     sensitive);
}

static void
on_generic_menu_item_edit_partition (GtkMenuItem *menu_item,
                                     gpointer   user_data)
{
  GduWindow *window = GDU_WINDOW (user_data);
  gint response;
  GtkBuilder *builder;
  GtkWidget *dialog;
  GtkWidget *combo_box;
  UDisksObject *object;
  UDisksBlockDevice *block;
  const gchar *scheme;
  const gchar *cur_type;
  const gchar **part_types;
  guint n;
  gint active_index;
  ChangePartitionTypeData data;
  const gchar *type_to_set;

  object = gdu_volume_grid_get_selected_device (GDU_VOLUME_GRID (window->volume_grid));
  g_assert (object != NULL);
  block = udisks_object_peek_block_device (object);
  g_assert (block != NULL);

  dialog = gdu_window_new_widget (window, "change-partition-type-dialog", &builder);
  combo_box = GTK_WIDGET (gtk_builder_get_object (builder, "change-partition-type-combo-box"));
  gtk_window_set_transient_for (GTK_WINDOW (dialog), GTK_WINDOW (window));
  gtk_dialog_set_default_response (GTK_DIALOG (dialog), GTK_RESPONSE_OK);

  scheme = udisks_block_device_get_part_entry_scheme (block);
  cur_type = udisks_block_device_get_part_entry_type (block);
  part_types = udisks_util_get_part_types_for_scheme (scheme);
  active_index = -1;
  gtk_combo_box_text_remove_all (GTK_COMBO_BOX_TEXT (combo_box));
  for (n = 0; part_types != NULL && part_types[n] != NULL; n++)
    {
      const gchar *type;
      gchar *type_for_display;
      type = part_types[n];
      type_for_display = udisks_util_get_part_type_for_display (scheme, type, TRUE);
      if (g_strcmp0 (type, cur_type) == 0)
        active_index = n;
      gtk_combo_box_text_append (GTK_COMBO_BOX_TEXT (combo_box), NULL, type_for_display);
      g_free (type_for_display);
    }

  g_signal_connect (combo_box,
                    "changed",
                    G_CALLBACK (on_change_partition_type_combo_box_changed),
                    &data);
  memset (&data, '\0', sizeof (ChangePartitionTypeData));
  data.dialog = dialog;
  data.orig_type = g_strdup (cur_type);
  data.part_types = part_types;

  if (active_index > 0)
    gtk_combo_box_set_active (GTK_COMBO_BOX (combo_box), active_index);

  gtk_widget_show_all (dialog);
  gtk_widget_grab_focus (combo_box);

  response = gtk_dialog_run (GTK_DIALOG (dialog));
  if (response != GTK_RESPONSE_OK)
    goto out;

  type_to_set = part_types[gtk_combo_box_get_active (GTK_COMBO_BOX (combo_box))];

  g_debug ("TODO: set partition type to %s", type_to_set);

 out:
  g_free (part_types);
  g_free (data.orig_type);
  gtk_widget_hide (dialog);
  gtk_widget_destroy (dialog);
  g_object_unref (builder);
}

/* ---------------------------------------------------------------------------------------------------- */

typedef struct
{
  GtkWidget *dialog;
  GtkWidget *configure_checkbutton;
  GtkWidget *table;

  GtkWidget *infobar_hbox;
  GtkWidget *device_combobox;
  GtkWidget *device_explanation_label;
  GtkWidget *directory_entry;
  GtkWidget *type_entry;
  GtkWidget *options_entry;
  GtkWidget *freq_spinbutton;
  GtkWidget *passno_spinbutton;

  GVariant *orig_fstab_entry;
} FstabDialogData;

static void
fstab_dialog_update (FstabDialogData *data)
{
  gboolean ui_configured;
  gchar *ui_fsname;
  const gchar *ui_dir;
  const gchar *ui_type;
  const gchar *ui_opts;
  gint ui_freq;
  gint ui_passno;
  gboolean configured;
  const gchar *fsname;
  const gchar *dir;
  const gchar *type;
  const gchar *opts;
  gint freq;
  gint passno;
  gboolean can_apply;

  if (data->orig_fstab_entry != NULL)
    {
      configured = TRUE;
      g_variant_lookup (data->orig_fstab_entry, "fsname", "^&ay", &fsname);
      g_variant_lookup (data->orig_fstab_entry, "dir", "^&ay", &dir);
      g_variant_lookup (data->orig_fstab_entry, "type", "^&ay", &type);
      g_variant_lookup (data->orig_fstab_entry, "opts", "^&ay", &opts);
      g_variant_lookup (data->orig_fstab_entry, "freq", "i", &freq);
      g_variant_lookup (data->orig_fstab_entry, "passno", "i", &passno);
    }
  else
    {
      configured = FALSE;
      fsname = "";
      dir = "";
      type = "";
      opts = "";
      freq = 0;
      passno = 0;
    }

  ui_configured = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (data->configure_checkbutton));
  ui_fsname = gtk_combo_box_text_get_active_text (GTK_COMBO_BOX_TEXT (data->device_combobox));
  ui_dir = gtk_entry_get_text (GTK_ENTRY (data->directory_entry));
  ui_type = gtk_entry_get_text (GTK_ENTRY (data->type_entry));
  ui_opts = gtk_entry_get_text (GTK_ENTRY (data->options_entry));
  ui_freq = gtk_spin_button_get_value (GTK_SPIN_BUTTON (data->freq_spinbutton));
  ui_passno = gtk_spin_button_get_value (GTK_SPIN_BUTTON (data->passno_spinbutton));

  can_apply = FALSE;
  if (configured != ui_configured)
    {
      can_apply = TRUE;
    }
  else if (ui_configured)
    {
      if (g_strcmp0 (ui_fsname, fsname) != 0 ||
          g_strcmp0 (ui_dir, dir) != 0 ||
          g_strcmp0 (ui_type, type) != 0 ||
          g_strcmp0 (ui_opts, opts) != 0 ||
          freq != ui_freq ||
          passno != ui_passno)
        {
          can_apply = TRUE;
        }
    }

  gtk_widget_set_sensitive (data->table, ui_configured);

  gtk_dialog_set_response_sensitive (GTK_DIALOG (data->dialog),
                                     GTK_RESPONSE_APPLY,
                                     can_apply);

  g_free (ui_fsname);
}

static void
fstab_dialog_property_changed (GObject     *object,
                               GParamSpec  *pspec,
                               gpointer     user_data)
{
  FstabDialogData *data = user_data;
  fstab_dialog_update (data);
}

static void
fstab_update_device_explanation (FstabDialogData *data)
{
  const gchar *s;
  gchar *fsname;
  gchar *str;
  gchar *explanation;
  guint part_num;

  fsname = gtk_combo_box_text_get_active_text (GTK_COMBO_BOX_TEXT (data->device_combobox));

  part_num = 0;
  s = g_strrstr (fsname, "-part");
  if (s != NULL)
    sscanf (s, "-part%d", &part_num);

  if (g_str_has_prefix (fsname, "/dev/disk/by-id/"))
    {
      if (part_num > 0)
        explanation = g_strdup_printf (_("Matches partition %d of the device with the given vital product data"),
                                       part_num);
      else
        explanation = g_strdup (_("Matches the whole disk of the device with the given vital product data"));
    }
  else if (g_str_has_prefix (fsname, "/dev/disk/by-path/"))
    {
      if (part_num > 0)
        explanation = g_strdup_printf (_("Matches partition %d of any device connected at the given port or address"),
                                       part_num);
      else
        explanation = g_strdup (_("Matches the whole disk of any device connected at the given port or address"));
    }
  else if (g_str_has_prefix (fsname, "/dev/disk/by-label/") || g_str_has_prefix (fsname, "LABEL="))
    {
      explanation = g_strdup (_("Matches any device with the given label"));
    }
  else if (g_str_has_prefix (fsname, "/dev/disk/by-uuid/") || g_str_has_prefix (fsname, "UUID="))
    {
      explanation = g_strdup (_("Matches the device with the given UUID"));
    }
  else
    {
      explanation = g_strdup (_("Matches the given device"));
    }

  str = g_strdup_printf ("<small><i>%s</i></small>", explanation);
  gtk_label_set_markup (GTK_LABEL (data->device_explanation_label), str);
  g_free (str);
  g_free (explanation);
  g_free (fsname);
}

static void
fstab_on_device_combobox_changed (GtkComboBox *combobox,
                                  gpointer     user_data)
{
  FstabDialogData *data = user_data;
  gchar *fsname;
  gchar *proposed_mount_point;
  const gchar *s;

  fsname = gtk_combo_box_text_get_active_text (GTK_COMBO_BOX_TEXT (data->device_combobox));
  s = strrchr (fsname, '/');
  if (s == NULL)
    s = strrchr (fsname, '=');
  if (s == NULL)
    s = "/disk";
  proposed_mount_point = g_strdup_printf ("/media/%s", s + 1);

  gtk_entry_set_text (GTK_ENTRY (data->directory_entry), proposed_mount_point);
  g_free (proposed_mount_point);
  g_free (fsname);

  fstab_update_device_explanation (data);
}

static void
fstab_populate_device_combo_box (GtkWidget         *device_combobox,
                                 UDisksDrive       *drive,
                                 UDisksBlockDevice *block,
                                 const gchar       *fstab_device)
{
  const gchar *device;
  const gchar *const *symlinks;
  guint n;
  gint selected;
  const gchar *uuid;
  const gchar *label;
  guint num_items;
  gchar *s;
  gint by_uuid = -1;
  gint by_label = -1;
  gint by_id = -1;
  gint by_path = -1;

  gtk_combo_box_text_remove_all (GTK_COMBO_BOX_TEXT (device_combobox));

  num_items = 0;
  selected = -1;

  device = udisks_block_device_get_device (block);
  gtk_combo_box_text_append (GTK_COMBO_BOX_TEXT (device_combobox),
                             NULL,
                             device);
  if (g_strcmp0 (fstab_device, device) == 0)
    selected = num_items;
  num_items = 1;

  symlinks = udisks_block_device_get_symlinks (block);
  for (n = 0; symlinks != NULL && symlinks[n] != NULL; n++)
    {
      gtk_combo_box_text_append (GTK_COMBO_BOX_TEXT (device_combobox), NULL, symlinks[n]);

      if (g_str_has_prefix (symlinks[n], "/dev/disk/by-uuid"))
        by_uuid = num_items;
      else if (g_str_has_prefix (symlinks[n], "/dev/disk/by-label"))
        by_label = num_items;
      else if (g_str_has_prefix (symlinks[n], "/dev/disk/by-id"))
        by_id = num_items;
      else if (g_str_has_prefix (symlinks[n], "/dev/disk/by-path"))
        by_path = num_items;

      if (g_strcmp0 (fstab_device, symlinks[n]) == 0)
        selected = num_items;
      num_items++;
    }

  uuid = udisks_block_device_get_id_uuid (block);
  if (uuid != NULL && strlen (uuid) > 0)
    {
      s = g_strdup_printf ("UUID=%s", uuid);
      gtk_combo_box_text_append (GTK_COMBO_BOX_TEXT (device_combobox), NULL, s);
      if (g_strcmp0 (fstab_device, s) == 0)
        selected = num_items;
      g_free (s);
      num_items++;
    }

  label = udisks_block_device_get_id_label (block);
  if (label != NULL && strlen (label) > 0)
    {
      s = g_strdup_printf ("LABEL=%s", label);
      gtk_combo_box_text_append (GTK_COMBO_BOX_TEXT (device_combobox), NULL, s);
      if (g_strcmp0 (fstab_device, s) == 0)
        selected = num_items;
      g_free (s);
      num_items++;
    }

  /* Choose a device to default if creating a new entry */
  if (selected == -1 && fstab_device == NULL)
    {
      /* if the device is using removable media, prefer
       * by-id / by-path to by-uuid / by-label
       */
      if (drive != NULL && udisks_drive_get_media_removable (drive))
        {
          if (by_id != -1)
            selected = by_id;
          else if (by_path != -1)
            selected = by_path;
          else if (by_uuid != -1)
            selected = by_uuid;
          else if (by_label != -1)
            selected = by_label;
        }
      else
        {
          if (by_uuid != -1)
            selected = by_uuid;
          else if (by_label != -1)
            selected = by_label;
          else if (by_id != -1)
            selected = by_id;
          else if (by_path != -1)
            selected = by_path;
        }
    }
  /* Fall back to device name as a last resort */
  if (selected == -1)
    selected = 0;

  gtk_combo_box_set_active (GTK_COMBO_BOX (device_combobox), selected);
}

static gboolean
check_if_system_mount (const gchar *dir)
{
  guint n;
  static const gchar *dirs[] = {
    "/",
    "/boot",
    "/home",
    "/usr",
    "/usr/local",
    "/var",
    "/var/crash",
    "/var/local",
    "/var/log",
    "/var/log/audit",
    "/var/mail",
    "/var/run",
    "/var/tmp",
    "/opt",
    "/root",
    "/tmp",
    NULL
  };

  for (n = 0; dirs[n] != NULL; n++)
    if (g_strcmp0 (dir, dirs[n]) == 0)
      return TRUE;
  return FALSE;
}


static void
on_generic_menu_item_configure_fstab (GtkMenuItem *menu_item,
                                      gpointer   user_data)
{
  GduWindow *window = GDU_WINDOW (user_data);
  GtkBuilder *builder;
  UDisksObject *object;
  UDisksBlockDevice *block;
  UDisksObject *drive_object;
  UDisksDrive *drive;
  gint response;
  GtkWidget *dialog;
  FstabDialogData data;
  gboolean configured;
  gchar *fsname;
  const gchar *dir;
  const gchar *type;
  const gchar *opts;
  gint freq;
  gint passno;
  GVariantIter iter;
  const gchar *configuration_type;
  GVariant *configuration_dict;
  gboolean is_system_mount;

  object = gdu_volume_grid_get_selected_device (GDU_VOLUME_GRID (window->volume_grid));
  if (object == NULL)
    object = gdu_volume_grid_get_block_device (GDU_VOLUME_GRID (window->volume_grid));
  block = udisks_object_peek_block_device (object);
  g_assert (block != NULL);

  drive = NULL;
  drive_object = (UDisksObject *) g_dbus_object_manager_get_object (udisks_client_get_object_manager (window->client),
                                                                    udisks_block_device_get_drive (block));
  if (drive_object != NULL)
    {
      drive = udisks_object_peek_drive (drive_object);
      g_object_unref (drive_object);
    }

  dialog = gdu_window_new_widget (window, "device-fstab-dialog", &builder);
  gtk_window_set_transient_for (GTK_WINDOW (dialog), GTK_WINDOW (window));
  gtk_dialog_set_default_response (GTK_DIALOG (dialog), GTK_RESPONSE_OK);

  memset (&data, '\0', sizeof (FstabDialogData));
  data.dialog = dialog;
  data.infobar_hbox = GTK_WIDGET (gtk_builder_get_object (builder, "fstab-infobar-hbox"));
  data.configure_checkbutton = GTK_WIDGET (gtk_builder_get_object (builder, "fstab-configure-checkbutton"));
  data.table = GTK_WIDGET (gtk_builder_get_object (builder, "fstab-table"));
  data.device_combobox = GTK_WIDGET (gtk_builder_get_object (builder, "fstab-device-combobox"));
  data.device_explanation_label = GTK_WIDGET (gtk_builder_get_object (builder, "fstab-device-explanation-label"));
  data.directory_entry = GTK_WIDGET (gtk_builder_get_object (builder, "fstab-directory-entry"));
  data.type_entry = GTK_WIDGET (gtk_builder_get_object (builder, "fstab-type-entry"));
  data.options_entry = GTK_WIDGET (gtk_builder_get_object (builder, "fstab-options-entry"));
  data.freq_spinbutton = GTK_WIDGET (gtk_builder_get_object (builder, "fstab-freq-spinbutton"));
  data.passno_spinbutton = GTK_WIDGET (gtk_builder_get_object (builder, "fstab-passno-spinbutton"));

  /* there could be multiple fstab entries - we only consider the first one */
  g_variant_iter_init (&iter, udisks_block_device_get_configuration (block));
  while (g_variant_iter_next (&iter, "(&s@a{sv})", &configuration_type, &configuration_dict))
    {
      if (g_strcmp0 (configuration_type, "fstab") == 0)
        {
          data.orig_fstab_entry = configuration_dict;
          break;
        }
      else
        {
          g_variant_unref (configuration_dict);
        }
    }
  if (data.orig_fstab_entry != NULL)
    {
      configured = TRUE;
      g_variant_lookup (data.orig_fstab_entry, "fsname", "^ay", &fsname);
      g_variant_lookup (data.orig_fstab_entry, "dir", "^&ay", &dir);
      g_variant_lookup (data.orig_fstab_entry, "type", "^&ay", &type);
      g_variant_lookup (data.orig_fstab_entry, "opts", "^&ay", &opts);
      g_variant_lookup (data.orig_fstab_entry, "freq", "i", &freq);
      g_variant_lookup (data.orig_fstab_entry, "passno", "i", &passno);
    }
  else
    {
      configured = FALSE;
      fsname = NULL;
      dir = "";
      type = "auto";
      opts = "defaults";
      /* propose noauto if the media is removable - otherwise e.g. systemd will time out at boot */
      if (drive != NULL && udisks_drive_get_media_removable (drive))
        opts = "defaults,noauto";
      freq = 0;
      passno = 0;
    }
  is_system_mount = check_if_system_mount (dir);

  fstab_populate_device_combo_box (data.device_combobox,
                                   drive,
                                   block,
                                   fsname);
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (data.configure_checkbutton), configured);
  gtk_entry_set_text (GTK_ENTRY (data.directory_entry), dir);
  gtk_entry_set_text (GTK_ENTRY (data.type_entry), type);
  gtk_entry_set_text (GTK_ENTRY (data.options_entry), opts);
  gtk_spin_button_set_value (GTK_SPIN_BUTTON (data.freq_spinbutton), freq);
  gtk_spin_button_set_value (GTK_SPIN_BUTTON (data.passno_spinbutton), passno);
  if (!configured)
    fstab_on_device_combobox_changed (GTK_COMBO_BOX (data.device_combobox), &data);

  g_signal_connect (data.configure_checkbutton,
                    "notify::active", G_CALLBACK (fstab_dialog_property_changed), &data);
  g_signal_connect (data.device_combobox,
                    "notify::active", G_CALLBACK (fstab_dialog_property_changed), &data);
  g_signal_connect (data.directory_entry,
                    "notify::text", G_CALLBACK (fstab_dialog_property_changed), &data);
  g_signal_connect (data.type_entry,
                    "notify::text", G_CALLBACK (fstab_dialog_property_changed), &data);
  g_signal_connect (data.options_entry,
                    "notify::text", G_CALLBACK (fstab_dialog_property_changed), &data);
  g_signal_connect (data.freq_spinbutton,
                    "notify::value", G_CALLBACK (fstab_dialog_property_changed), &data);
  g_signal_connect (data.passno_spinbutton,
                    "notify::value", G_CALLBACK (fstab_dialog_property_changed), &data);
  g_signal_connect (data.device_combobox,
                    "changed", G_CALLBACK (fstab_on_device_combobox_changed), &data);

  /* Show a cluebar if the entry is considered a system mount */
  if (is_system_mount)
    {
      GtkWidget *bar;
      GtkWidget *label;
      GtkWidget *image;
      GtkWidget *hbox;

      bar = gtk_info_bar_new ();
      gtk_info_bar_set_message_type (GTK_INFO_BAR (bar), GTK_MESSAGE_WARNING);

      image = gtk_image_new_from_stock (GTK_STOCK_DIALOG_WARNING, GTK_ICON_SIZE_BUTTON);

      label = gtk_label_new (NULL);
      gtk_label_set_markup (GTK_LABEL (label),
                            _("<b>Warning:</b> "
                              "The system may not work correctly if this entry is modified or removed."));

      hbox = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 12);
      gtk_box_pack_start (GTK_BOX (hbox), image, FALSE, TRUE, 0);
      gtk_box_pack_start (GTK_BOX (hbox), label, TRUE, TRUE, 0);

      gtk_container_add (GTK_CONTAINER (gtk_info_bar_get_content_area (GTK_INFO_BAR (bar))), hbox);
      gtk_box_pack_start (GTK_BOX (data.infobar_hbox), bar, TRUE, TRUE, 0);
    }

  gtk_widget_show_all (dialog);

  fstab_update_device_explanation (&data);
  fstab_dialog_update (&data);

  response = gtk_dialog_run (GTK_DIALOG (dialog));
  if (response == GTK_RESPONSE_APPLY)
    {
      gboolean ui_configured;
      gchar *ui_fsname;
      const gchar *ui_dir;
      const gchar *ui_type;
      const gchar *ui_opts;
      gint ui_freq;
      gint ui_passno;
      GError *error;
      GVariant *old_item;
      GVariant *new_item;

      ui_configured = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (data.configure_checkbutton));
      ui_fsname = gtk_combo_box_text_get_active_text (GTK_COMBO_BOX_TEXT (data.device_combobox));
      ui_dir = gtk_entry_get_text (GTK_ENTRY (data.directory_entry));
      ui_type = gtk_entry_get_text (GTK_ENTRY (data.type_entry));
      ui_opts = gtk_entry_get_text (GTK_ENTRY (data.options_entry));
      ui_freq = gtk_spin_button_get_value (GTK_SPIN_BUTTON (data.freq_spinbutton));
      ui_passno = gtk_spin_button_get_value (GTK_SPIN_BUTTON (data.passno_spinbutton));

      gtk_widget_hide (dialog);

      old_item = NULL;
      new_item = NULL;

      if (configured)
        {
          old_item = g_variant_new ("(s@a{sv})", "fstab", data.orig_fstab_entry);
        }

      if (ui_configured)
        {
          GVariantBuilder builder;
          g_variant_builder_init (&builder, G_VARIANT_TYPE_VARDICT);
          g_variant_builder_add (&builder, "{sv}", "fsname", g_variant_new_bytestring (ui_fsname));
          g_variant_builder_add (&builder, "{sv}", "dir", g_variant_new_bytestring (ui_dir));
          g_variant_builder_add (&builder, "{sv}", "type", g_variant_new_bytestring (ui_type));
          g_variant_builder_add (&builder, "{sv}", "opts", g_variant_new_bytestring (ui_opts));
          g_variant_builder_add (&builder, "{sv}", "freq", g_variant_new_int32 (ui_freq));
          g_variant_builder_add (&builder, "{sv}", "passno", g_variant_new_int32 (ui_passno));
          new_item = g_variant_new ("(sa{sv})", "fstab", &builder);
        }

      if (old_item != NULL && new_item == NULL)
        {
          error = NULL;
          if (!udisks_block_device_call_remove_configuration_item_sync (block,
                                                                        old_item,
                                                                        g_variant_new ("a{sv}", NULL), /* options */
                                                                        NULL, /* GCancellable */
                                                                        &error))
            {
              gdu_window_show_error (window,
                                     _("Error removing old /etc/fstab entry"),
                                     error);
              g_error_free (error);
              g_free (ui_fsname);
              goto out;
            }
        }
      else if (old_item == NULL && new_item != NULL)
        {
          error = NULL;
          if (!udisks_block_device_call_add_configuration_item_sync (block,
                                                                     new_item,
                                                                     g_variant_new ("a{sv}", NULL), /* options */
                                                                     NULL, /* GCancellable */
                                                                     &error))
            {
              gdu_window_show_error (window,
                                     _("Error adding new /etc/fstab entry"),
                                     error);
              g_error_free (error);
              g_free (ui_fsname);
              goto out;
            }
        }
      else if (old_item != NULL && new_item != NULL)
        {
          error = NULL;
          if (!udisks_block_device_call_update_configuration_item_sync (block,
                                                                        old_item,
                                                                        new_item,
                                                                        g_variant_new ("a{sv}", NULL), /* options */
                                                                        NULL, /* GCancellable */
                                                                        &error))
            {
              gdu_window_show_error (window,
                                     _("Error updating /etc/fstab entry"),
                                     error);
              g_error_free (error);
              g_free (ui_fsname);
              goto out;
            }
        }
      else
        {
          g_assert_not_reached ();
        }
      g_free (ui_fsname);
    }

 out:
  if (data.orig_fstab_entry != NULL)
    g_variant_unref (data.orig_fstab_entry);
  g_free (fsname);

  gtk_widget_hide (dialog);
  gtk_widget_destroy (dialog);
  g_object_unref (builder);
}

/* ---------------------------------------------------------------------------------------------------- */

typedef struct
{
  UDisksBlockDevice *block;

  GduWindow *window;

  GtkBuilder *builder;
  GtkWidget *dialog;
  GtkWidget *configure_checkbutton;
  GtkWidget *table;

  GtkWidget *name_entry;
  GtkWidget *options_entry;
  GtkWidget *passphrase_label;
  GtkWidget *passphrase_entry;
  GtkWidget *show_passphrase_checkbutton;
  GtkWidget *passphrase_path_value_label;

  GVariant *orig_crypttab_entry;
} CrypttabDialogData;

static void
crypttab_dialog_free (CrypttabDialogData *data)
{
  g_object_unref (data->window);

  if (data->orig_crypttab_entry != NULL)
    g_variant_unref (data->orig_crypttab_entry);

  if (data->dialog != NULL)
    {
      gtk_widget_hide (data->dialog);
      gtk_widget_destroy (data->dialog);
    }
  g_object_unref (data->builder);
  g_free (data);
}

static void
crypttab_dialog_update (CrypttabDialogData *data)
{
  gboolean ui_configured;
  const gchar *ui_name;
  const gchar *ui_options;
  const gchar *ui_passphrase_contents;
  gboolean configured;
  const gchar *name;
  const gchar *passphrase_path;
  const gchar *passphrase_contents;
  const gchar *options;
  gboolean can_apply;
  gchar *s;

  if (data->orig_crypttab_entry != NULL)
    {
      configured = TRUE;
      g_variant_lookup (data->orig_crypttab_entry, "name", "^&ay", &name);
      g_variant_lookup (data->orig_crypttab_entry, "options", "^&ay", &options);
      g_variant_lookup (data->orig_crypttab_entry, "passphrase-path", "^&ay", &passphrase_path);
      if (!g_variant_lookup (data->orig_crypttab_entry, "passphrase-contents", "^&ay", &passphrase_contents))
        passphrase_contents = "";
    }
  else
    {
      configured = FALSE;
      name = "";
      options = "";
      passphrase_path = "";
      passphrase_contents = "";
    }

  ui_configured = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (data->configure_checkbutton));
  ui_name = gtk_entry_get_text (GTK_ENTRY (data->name_entry));
  ui_options = gtk_entry_get_text (GTK_ENTRY (data->options_entry));
  ui_passphrase_contents = gtk_entry_get_text (GTK_ENTRY (data->passphrase_entry));

  if (!configured)
    {
      if (strlen (ui_passphrase_contents) > 0)
        s = g_strdup_printf ("<i>%s</i>", _("Will be created"));
      else
        s = g_strdup_printf ("<i>%s</i>", _("None"));
    }
  else
    {
      if (g_str_has_prefix (passphrase_path, "/dev"))
        {
          /* if using a random source (for e.g. setting up swap), don't offer to edit the passphrase */
          gtk_widget_hide (data->passphrase_label);
          gtk_widget_hide (data->passphrase_entry);
          gtk_widget_hide (data->show_passphrase_checkbutton);
          gtk_widget_set_no_show_all (data->passphrase_label, TRUE);
          gtk_widget_set_no_show_all (data->passphrase_entry, TRUE);
          gtk_widget_set_no_show_all (data->show_passphrase_checkbutton, TRUE);
          s = g_strdup (passphrase_path);
        }
      else if (strlen (ui_passphrase_contents) > 0)
        {
          if (strlen (passphrase_path) == 0)
            s = g_strdup_printf ("<i>%s</i>", _("Will be created"));
          else
            s = g_strdup (passphrase_path);
        }
      else
        {
          if (strlen (passphrase_path) == 0)
            s = g_strdup_printf ("<i>%s</i>", _("None"));
          else
            s = g_strdup_printf ("<i>%s</i>", _("Will be deleted"));
        }
    }
  gtk_label_set_markup (GTK_LABEL (data->passphrase_path_value_label), s);
  g_free (s);

  can_apply = FALSE;
  if (configured != ui_configured)
    {
      can_apply = TRUE;
    }
  else if (ui_configured)
    {
      if (g_strcmp0 (ui_name, name) != 0 ||
          g_strcmp0 (ui_options, options) != 0 ||
          g_strcmp0 (ui_passphrase_contents, passphrase_contents) != 0)
        {
          can_apply = TRUE;
        }
    }

  gtk_dialog_set_response_sensitive (GTK_DIALOG (data->dialog),
                                     GTK_RESPONSE_APPLY,
                                     can_apply);
}

static void
crypttab_dialog_property_changed (GObject     *object,
                                  GParamSpec  *pspec,
                                  gpointer     user_data)
{
  CrypttabDialogData *data = user_data;
  crypttab_dialog_update (data);
}


static void
crypttab_dialog_present (CrypttabDialogData *data)
{
  gboolean configured;
  gchar *name;
  const gchar *options;
  const gchar *passphrase_contents;
  gint response;

  if (data->orig_crypttab_entry != NULL)
    {
      configured = TRUE;
      g_variant_lookup (data->orig_crypttab_entry, "name", "^ay", &name);
      g_variant_lookup (data->orig_crypttab_entry, "options", "^&ay", &options);
      if (!g_variant_lookup (data->orig_crypttab_entry, "passphrase-contents", "^&ay", &passphrase_contents))
        passphrase_contents = "";
    }
  else
    {
      configured = FALSE;
      name = g_strdup_printf ("luks-%s", udisks_block_device_get_id_uuid (data->block));
      options = "";
      passphrase_contents = "";
    }
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (data->configure_checkbutton), configured);
  gtk_entry_set_text (GTK_ENTRY (data->name_entry), name);
  gtk_entry_set_text (GTK_ENTRY (data->options_entry), options);
  gtk_entry_set_text (GTK_ENTRY (data->passphrase_entry), passphrase_contents);

  g_object_bind_property (data->show_passphrase_checkbutton,
                          "active",
                          data->passphrase_entry,
                          "visibility",
                          G_BINDING_SYNC_CREATE);

  g_object_bind_property (data->configure_checkbutton,
                          "active",
                          data->table,
                          "sensitive",
                          G_BINDING_SYNC_CREATE);

  g_signal_connect (data->configure_checkbutton,
                    "notify::active", G_CALLBACK (crypttab_dialog_property_changed), data);
  g_signal_connect (data->name_entry,
                    "notify::text", G_CALLBACK (crypttab_dialog_property_changed), data);
  g_signal_connect (data->options_entry,
                    "notify::text", G_CALLBACK (crypttab_dialog_property_changed), data);
  g_signal_connect (data->passphrase_entry,
                    "notify::text", G_CALLBACK (crypttab_dialog_property_changed), data);

  gtk_widget_show_all (data->dialog);

  crypttab_dialog_update (data);

  response = gtk_dialog_run (GTK_DIALOG (data->dialog));

  if (response == GTK_RESPONSE_APPLY)
    {
      gboolean ui_configured;
      const gchar *ui_name;
      const gchar *ui_options;
      const gchar *ui_passphrase_contents;
      const gchar *old_passphrase_path;
      GError *error;
      GVariant *old_item;
      GVariant *new_item;

      ui_configured = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (data->configure_checkbutton));
      ui_name = gtk_entry_get_text (GTK_ENTRY (data->name_entry));
      ui_options = gtk_entry_get_text (GTK_ENTRY (data->options_entry));
      ui_passphrase_contents = gtk_entry_get_text (GTK_ENTRY (data->passphrase_entry));

      gtk_widget_hide (data->dialog);

      old_item = NULL;
      new_item = NULL;

      old_passphrase_path = NULL;
      if (data->orig_crypttab_entry != NULL)
        {
          const gchar *s;
          if (g_variant_lookup (data->orig_crypttab_entry, "passphrase-path", "^&ay", &s))
            {
              if (strlen (s) > 0 && !g_str_has_prefix (s, "/dev"))
                old_passphrase_path = s;
            }
          error = NULL;
          old_item = g_variant_new ("(s@a{sv})", "crypttab",
                                    data->orig_crypttab_entry);
        }

      if (ui_configured)
        {
          GVariantBuilder builder;
          gchar *s;
          g_variant_builder_init (&builder, G_VARIANT_TYPE_VARDICT);
          s = g_strdup_printf ("UUID=%s", udisks_block_device_get_id_uuid (data->block));
          g_variant_builder_add (&builder, "{sv}", "device", g_variant_new_bytestring (s));
          g_free (s);
          g_variant_builder_add (&builder, "{sv}", "name", g_variant_new_bytestring (ui_name));
          g_variant_builder_add (&builder, "{sv}", "options", g_variant_new_bytestring (ui_options));
          if (strlen (ui_passphrase_contents) > 0)
            {
              /* use old/existing passphrase file, if available */
              if (old_passphrase_path != NULL)
                {
                  g_variant_builder_add (&builder, "{sv}", "passphrase-path",
                                         g_variant_new_bytestring (old_passphrase_path));
                }
              else
                {
                  /* otherwise fall back to the requested name */
                  s = g_strdup_printf ("/etc/luks-keys/%s", ui_name);
                  g_variant_builder_add (&builder, "{sv}", "passphrase-path", g_variant_new_bytestring (s));
                  g_free (s);
                }
              g_variant_builder_add (&builder, "{sv}", "passphrase-contents",
                                     g_variant_new_bytestring (ui_passphrase_contents));

            }
          else
            {
              g_variant_builder_add (&builder, "{sv}", "passphrase-path",
                                     g_variant_new_bytestring (""));
              g_variant_builder_add (&builder, "{sv}", "passphrase-contents",
                                     g_variant_new_bytestring (""));
            }

          new_item = g_variant_new ("(sa{sv})", "crypttab", &builder);
        }

      if (old_item != NULL && new_item == NULL)
        {
          error = NULL;
          if (!udisks_block_device_call_remove_configuration_item_sync (data->block,
                                                                        old_item,
                                                                        g_variant_new ("a{sv}", NULL), /* options */
                                                                        NULL, /* GCancellable */
                                                                        &error))
            {
              gdu_window_show_error (data->window,
                                     _("Error removing /etc/crypttab entry"),
                                     error);
              g_error_free (error);
              goto out;
            }
        }
      else if (old_item == NULL && new_item != NULL)
        {
          error = NULL;
          if (!udisks_block_device_call_add_configuration_item_sync (data->block,
                                                                     new_item,
                                                                     g_variant_new ("a{sv}", NULL), /* options */
                                                                     NULL, /* GCancellable */
                                                                     &error))
            {
              gdu_window_show_error (data->window,
                                     _("Error adding /etc/crypttab entry"),
                                     error);
              g_error_free (error);
              goto out;
            }
        }
      else if (old_item != NULL && new_item != NULL)
        {
          error = NULL;
          if (!udisks_block_device_call_update_configuration_item_sync (data->block,
                                                                        old_item,
                                                                        new_item,
                                                                        g_variant_new ("a{sv}", NULL), /* options */
                                                                        NULL, /* GCancellable */
                                                                        &error))
            {
              gdu_window_show_error (data->window,
                                     _("Error updating /etc/crypttab entry"),
                                     error);
              g_error_free (error);
              goto out;
            }
        }
      else
        {
          g_assert_not_reached ();
        }
    }

 out:
  crypttab_dialog_free (data);
  g_free (name);
}

static void
crypttab_dialog_on_get_secrets_cb (UDisksBlockDevice *block,
                                   GAsyncResult      *res,
                                   gpointer           user_data)
{
  CrypttabDialogData *data = user_data;
  GError *error;
  GVariant *configuration;
  GVariantIter iter;
  const gchar *configuration_type;
  GVariant *configuration_dict;

  configuration = NULL;
  error = NULL;
  if (!udisks_block_device_call_get_secret_configuration_finish (block,
                                                                 &configuration,
                                                                 res,
                                                                 &error))
    {
      gdu_window_show_error (data->window,
                             _("Error retrieving configuration data"),
                             error);
      g_error_free (error);
      crypttab_dialog_free (data);
      goto out;
    }

  /* there could be multiple crypttab entries - we only consider the first one */
  g_variant_iter_init (&iter, configuration);
  while (g_variant_iter_next (&iter, "(&s@a{sv})", &configuration_type, &configuration_dict))
    {
      if (g_strcmp0 (configuration_type, "crypttab") == 0)
        {
          if (data->orig_crypttab_entry != NULL)
            g_variant_unref (data->orig_crypttab_entry);
          data->orig_crypttab_entry = configuration_dict;
          break;
        }
      else
        {
          g_variant_unref (configuration_dict);
        }
    }
  g_variant_unref (configuration);

  crypttab_dialog_present (data);

 out:
  ;
}

static void
on_generic_menu_item_configure_crypttab (GtkMenuItem *menu_item,
                                         gpointer   user_data)
{
  GduWindow *window = GDU_WINDOW (user_data);
  UDisksObject *object;
  GtkWidget *dialog;
  CrypttabDialogData *data;
  GVariantIter iter;
  const gchar *configuration_type;
  GVariant *configuration_dict;
  gboolean configured;
  gboolean get_passphrase_contents;

  data = g_new0 (CrypttabDialogData, 1);
  data->window = g_object_ref (window);

  object = gdu_volume_grid_get_selected_device (GDU_VOLUME_GRID (window->volume_grid));
  if (object == NULL)
    object = gdu_volume_grid_get_block_device (GDU_VOLUME_GRID (window->volume_grid));
  data->block = udisks_object_peek_block_device (object);
  g_assert (data->block != NULL);

  dialog = gdu_window_new_widget (window, "crypttab-dialog", &data->builder);
  gtk_window_set_transient_for (GTK_WINDOW (dialog), GTK_WINDOW (window));
  gtk_dialog_set_default_response (GTK_DIALOG (dialog), GTK_RESPONSE_OK);

  data->dialog = dialog;
  data->configure_checkbutton = GTK_WIDGET (gtk_builder_get_object (data->builder, "crypttab-configure-checkbutton"));
  data->table = GTK_WIDGET (gtk_builder_get_object (data->builder, "crypttab-table"));
  data->name_entry = GTK_WIDGET (gtk_builder_get_object (data->builder, "crypttab-name-entry"));
  data->options_entry = GTK_WIDGET (gtk_builder_get_object (data->builder, "crypttab-options-entry"));
  data->passphrase_label = GTK_WIDGET (gtk_builder_get_object (data->builder, "crypttab-passphrase-label"));
  data->passphrase_entry = GTK_WIDGET (gtk_builder_get_object (data->builder, "crypttab-passphrase-entry"));
  data->show_passphrase_checkbutton = GTK_WIDGET (gtk_builder_get_object (data->builder, "crypttab-show-passphrase-checkbutton"));
  data->passphrase_path_value_label = GTK_WIDGET (gtk_builder_get_object (data->builder, "crypttab-passphrase-path-value-label"));

  /* First check if there's an existing configuration */
  configured = FALSE;
  get_passphrase_contents = FALSE;
  g_variant_iter_init (&iter, udisks_block_device_get_configuration (data->block));
  while (g_variant_iter_next (&iter, "(&s@a{sv})", &configuration_type, &configuration_dict))
    {
      if (g_strcmp0 (configuration_type, "crypttab") == 0)
        {
          const gchar *passphrase_path;
          configured = TRUE;
          g_variant_lookup (configuration_dict, "passphrase-path", "^&ay", &passphrase_path);
          if (g_strcmp0 (passphrase_path, "") != 0)
            {
              /* fetch contents of passphrase file, if it exists (unless special file) */
              if (!g_str_has_prefix (passphrase_path, "/dev"))
                {
                  get_passphrase_contents = TRUE;
                }
            }
          data->orig_crypttab_entry = configuration_dict;
          break;
        }
      else
        {
          g_variant_unref (configuration_dict);
        }
    }

  /* if there is an existing configuration and it has a passphrase, get the actual passphrase
   * as well (involves polkit dialog)
   */
  if (configured && get_passphrase_contents)
    {
      udisks_block_device_call_get_secret_configuration (data->block,
                                                         g_variant_new ("a{sv}", NULL), /* options */
                                                         NULL, /* cancellable */
                                                         (GAsyncReadyCallback) crypttab_dialog_on_get_secrets_cb,
                                                         data);
    }
  else
    {
      /* otherwise just set up the dialog */
      crypttab_dialog_present (data);
    }
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
unlock_cb (UDisksEncrypted *encrypted,
           GAsyncResult    *res,
           gpointer         user_data)
{
  GduWindow *window = GDU_WINDOW (user_data);
  GError *error;

  error = NULL;
  if (!udisks_encrypted_call_unlock_finish (encrypted,
                                            NULL, /* out_cleartext_device */
                                            res,
                                            &error))
    {
      gdu_window_show_error (window,
                             _("Error unlocking encrypted device"),
                             error);
      g_error_free (error);
    }
  g_object_unref (window);
}

static void
on_devtab_action_unlock_activated (GtkAction *action,
                                   gpointer   user_data)
{
  GduWindow *window = GDU_WINDOW (user_data);
  gint response;
  GtkBuilder *builder;
  GtkWidget *dialog;
  GtkWidget *entry;
  GtkWidget *show_passphrase_check_button;
  UDisksObject *object;
  UDisksBlockDevice *block;
  UDisksEncrypted *encrypted;
  const gchar *passphrase;
  gboolean has_passphrase;

  dialog = NULL;
  builder = NULL;

  /* TODO: look up passphrase from gnome-keyring? */

  object = gdu_volume_grid_get_selected_device (GDU_VOLUME_GRID (window->volume_grid));
  block = udisks_object_peek_block_device (object);
  encrypted = udisks_object_peek_encrypted (object);

  passphrase = "";
  has_passphrase = FALSE;
  if (has_configuration (block, "crypttab", &has_passphrase) && has_passphrase)
    goto do_call;

  dialog = gdu_window_new_widget (window, "unlock-device-dialog", &builder);
  entry = GTK_WIDGET (gtk_builder_get_object (builder, "unlock-device-passphrase-entry"));
  show_passphrase_check_button = GTK_WIDGET (gtk_builder_get_object (builder, "unlock-device-show-passphrase-check-button"));

  gtk_window_set_transient_for (GTK_WINDOW (dialog), GTK_WINDOW (window));
  gtk_dialog_set_default_response (GTK_DIALOG (dialog), GTK_RESPONSE_OK);

  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (show_passphrase_check_button), FALSE);
  gtk_entry_set_text (GTK_ENTRY (entry), "");

  g_object_bind_property (show_passphrase_check_button,
                          "active",
                          entry,
                          "visibility",
                          G_BINDING_SYNC_CREATE);

  gtk_widget_show_all (dialog);
  gtk_widget_grab_focus (entry);

  response = gtk_dialog_run (GTK_DIALOG (dialog));
  if (response != GTK_RESPONSE_OK)
    goto out;

  passphrase = gtk_entry_get_text (GTK_ENTRY (entry));

 do_call:
  udisks_encrypted_call_unlock (encrypted,
                                passphrase,
                                g_variant_new ("a{sv}", NULL), /* options */
                                NULL, /* cancellable */
                                (GAsyncReadyCallback) unlock_cb,
                                g_object_ref (window));

 out:
  if (dialog != NULL)
    {
      gtk_widget_hide (dialog);
      gtk_widget_destroy (dialog);
    }
  if (builder != NULL)
    g_object_unref (builder);
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
