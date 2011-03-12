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

#include <glib/gi18n.h>

#include "gduapplication.h"
#include "gduwindow.h"
#include "gdudevicetreemodel.h"
#include "gduutils.h"
#include "gduvolumegrid.h"

/* Keep in sync with tabs in palimpsest.ui file */
typedef enum
{
  DETAILS_PAGE_NOT_SELECTED,
  DETAILS_PAGE_DEVICE,
} DetailsPage;

struct _GduWindow
{
  GtkWindow parent_instance;

  GduApplication *application;
  UDisksClient *client;

  GtkBuilder *builder;
  GduDeviceTreeModel *model;

  DetailsPage current_page;
  GDBusObjectProxy *current_object_proxy;

  GtkWidget *volume_grid;
  GtkWidget *write_cache_switch;

  GHashTable *label_connections;
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

static void on_volume_grid_changed (GduVolumeGrid  *grid,
                                    gpointer        user_data);

G_DEFINE_TYPE (GduWindow, gdu_window, GTK_TYPE_WINDOW);

static void
gdu_window_init (GduWindow *window)
{
  window->label_connections = g_hash_table_new_full (g_str_hash,
                                                     g_str_equal,
                                                     g_free,
                                                     NULL);
}

static void on_object_proxy_added (GDBusProxyManager   *manager,
                                   GDBusObjectProxy    *object_proxy,
                                   gpointer             user_data);

static void on_object_proxy_removed (GDBusProxyManager   *manager,
                                     GDBusObjectProxy    *object_proxy,
                                     gpointer             user_data);

static void on_interface_proxy_added (GDBusProxyManager   *manager,
                                      GDBusObjectProxy    *object_proxy,
                                      GDBusProxy          *interface_proxy,
                                      gpointer             user_data);

static void on_interface_proxy_removed (GDBusProxyManager   *manager,
                                        GDBusObjectProxy    *object_proxy,
                                        GDBusProxy          *interface_proxy,
                                        gpointer             user_data);

static void on_interface_proxy_properties_changed (GDBusProxyManager   *manager,
                                                   GDBusObjectProxy    *object_proxy,
                                                   GDBusProxy          *interface_proxy,
                                                   GVariant            *changed_properties,
                                                   const gchar *const *invalidated_properties,
                                                   gpointer            user_data);

static void
gdu_window_finalize (GObject *object)
{
  GduWindow *window = GDU_WINDOW (object);
  GDBusProxyManager *proxy_manager;

  g_hash_table_unref (window->label_connections);

  proxy_manager = udisks_client_get_proxy_manager (window->client);
  g_signal_handlers_disconnect_by_func (proxy_manager,
                                        G_CALLBACK (on_object_proxy_added),
                                        window);
  g_signal_handlers_disconnect_by_func (proxy_manager,
                                        G_CALLBACK (on_object_proxy_removed),
                                        window);
  g_signal_handlers_disconnect_by_func (proxy_manager,
                                        G_CALLBACK (on_interface_proxy_added),
                                        window);
  g_signal_handlers_disconnect_by_func (proxy_manager,
                                        G_CALLBACK (on_interface_proxy_removed),
                                        window);
  g_signal_handlers_disconnect_by_func (proxy_manager,
                                        G_CALLBACK (on_interface_proxy_properties_changed),
                                        window);

  if (window->current_object_proxy != NULL)
    g_object_unref (window->current_object_proxy);

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
  gtk_tree_view_expand_all (GTK_TREE_VIEW (gdu_window_get_widget (window, "device-tree-treeview")));
}

static void select_details_page (GduWindow         *window,
                                 GDBusObjectProxy  *object_proxy,
                                 DetailsPage        page);

static void
set_selected_object_proxy (GduWindow        *window,
                           GDBusObjectProxy *object_proxy)
{
  if (object_proxy != NULL)
    {
      if (UDISKS_PEEK_LUN (object_proxy) != NULL ||
          UDISKS_PEEK_BLOCK_DEVICE (object_proxy) != NULL)
        {
          select_details_page (window, object_proxy, DETAILS_PAGE_DEVICE);
        }
      else
        {
          g_assert_not_reached ();
        }
    }
  else
    {
      select_details_page (window, NULL, DETAILS_PAGE_NOT_SELECTED);
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
      GDBusObjectProxy *object_proxy;
      gtk_tree_model_get (model,
                          &iter,
                          GDU_DEVICE_TREE_MODEL_COLUMN_OBJECT_PROXY,
                          &object_proxy,
                          -1);
      set_selected_object_proxy (window, object_proxy);
      g_object_unref (object_proxy);
    }
  else
    {
      set_selected_object_proxy (window, NULL);
    }
}

gboolean _gdu_application_get_running_from_source_tree (GduApplication *app);

static void
gdu_window_constructed (GObject *object)
{
  GduWindow *window = GDU_WINDOW (object);
  GError *error;
  GtkNotebook *notebook;
  GtkTreeView *tree_view;
  GtkTreeViewColumn *column;
  GtkCellRenderer *renderer;
  GtkTreeSelection *selection;
  const gchar *path;
  GtkWidget *w;
  GtkWidget *label;
  GtkStyleContext *context;
  GDBusProxyManager *proxy_manager;

  /* chain up */
  if (G_OBJECT_CLASS (gdu_window_parent_class)->constructed != NULL)
    G_OBJECT_CLASS (gdu_window_parent_class)->constructed (object);

  window->builder = gtk_builder_new ();
  error = NULL;
  path = _gdu_application_get_running_from_source_tree (window->application)
    ? "../../data/ui/palimpsest.ui" :
      PACKAGE_DATA_DIR "/gnome-disk-utility/palimpsest.ui";
  if (gtk_builder_add_from_file (window->builder,
                                 path,
                                 &error) == 0)
    {
      g_error ("Error loading %s: %s", path, error->message);
      g_error_free (error);
    }

  w = gdu_window_get_widget (window, "palimpsest-hbox");
  gtk_widget_reparent (w, GTK_WIDGET (window));
  gtk_window_set_title (GTK_WINDOW (window), _("Disk Utility"));
  gtk_window_set_default_size (GTK_WINDOW (window), 800, 600);
  gtk_container_set_border_width (GTK_CONTAINER (window), 12);

  notebook = GTK_NOTEBOOK (gdu_window_get_widget (window, "palimpsest-notebook"));
  gtk_notebook_set_show_tabs (notebook, FALSE);
  gtk_notebook_set_show_border (notebook, FALSE);

  context = gtk_widget_get_style_context (gdu_window_get_widget (window, "device-tree-scrolledwindow"));
  gtk_style_context_set_junction_sides (context, GTK_JUNCTION_BOTTOM);
  context = gtk_widget_get_style_context (gdu_window_get_widget (window, "device-tree-add-remove-toolbar"));
  gtk_style_context_set_junction_sides (context, GTK_JUNCTION_TOP);

  window->model = gdu_device_tree_model_new (window->client);

  tree_view = GTK_TREE_VIEW (gdu_window_get_widget (window, "device-tree-treeview"));

  label = gtk_label_new (NULL);
  gtk_label_set_markup_with_mnemonic (GTK_LABEL (label), _("_Storage Devices"));
  gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
  gtk_label_set_mnemonic_widget (GTK_LABEL (label), GTK_WIDGET (tree_view));
  gtk_widget_show_all (label);

  gtk_tree_view_set_model (tree_view, GTK_TREE_MODEL (window->model));
  gtk_tree_sortable_set_sort_column_id (GTK_TREE_SORTABLE (window->model),
                                        GDU_DEVICE_TREE_MODEL_COLUMN_SORT_KEY,
                                        GTK_SORT_ASCENDING);

  selection = gtk_tree_view_get_selection (tree_view);
  gtk_tree_selection_set_select_function (selection, dont_select_headings, NULL, NULL);
  g_signal_connect (selection,
                    "changed",
                    G_CALLBACK (on_tree_selection_changed),
                    window);

  column = gtk_tree_view_column_new ();
  gtk_tree_view_column_set_widget (column, label);
  gtk_tree_view_append_column (tree_view, column);

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
                "ellipsize", PANGO_ELLIPSIZE_END,
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
  gtk_tree_view_expand_all (tree_view);

  proxy_manager = udisks_client_get_proxy_manager (window->client);
  g_signal_connect (proxy_manager,
                    "object-proxy-added",
                    G_CALLBACK (on_object_proxy_added),
                    window);
  g_signal_connect (proxy_manager,
                    "object-proxy-removed",
                    G_CALLBACK (on_object_proxy_removed),
                    window);
  g_signal_connect (proxy_manager,
                    "interface-proxy-added",
                    G_CALLBACK (on_interface_proxy_added),
                    window);
  g_signal_connect (proxy_manager,
                    "interface-proxy-removed",
                    G_CALLBACK (on_interface_proxy_removed),
                    window);
  g_signal_connect (proxy_manager,
                    "interface-proxy-properties-changed",
                    G_CALLBACK (on_interface_proxy_properties_changed),
                    window);

  /* set up non-standard widgets that isn't in the .ui file */

  window->volume_grid = gdu_volume_grid_new (window->client);
  gtk_box_pack_start (GTK_BOX (gdu_window_get_widget (window, "devtab-grid-hbox")),
                      window->volume_grid,
                      TRUE, TRUE, 0);
  gtk_label_set_mnemonic_widget (GTK_LABEL (gdu_window_get_widget (window, "devtab-details-label")),
                                 window->volume_grid);
  g_signal_connect (window->volume_grid,
                    "changed",
                    G_CALLBACK (on_volume_grid_changed),
                    window);

  window->write_cache_switch = gtk_switch_new ();
  gtk_box_pack_start (GTK_BOX (gdu_window_get_widget (window, "devtab-write-cache-hbox")),
                      window->write_cache_switch,
                      FALSE, TRUE, 0);
  gtk_label_set_mnemonic_widget (GTK_LABEL (gdu_window_get_widget (window, "devtab-write-cache-label")),
                                 window->write_cache_switch);
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

static void
teardown_details_page (GduWindow         *window,
                       GDBusObjectProxy *object_proxy,
                       gint              page)
{
  //g_debug ("teardown for %s, page %d",
  //       object_proxy != NULL ? g_dbus_object_proxy_get_object_path (object_proxy) : "<none>",
  //         page);
  switch (page)
    {
    case DETAILS_PAGE_NOT_SELECTED:
      break;
    case DETAILS_PAGE_DEVICE:
      gdu_volume_grid_set_block_device (GDU_VOLUME_GRID (window->volume_grid), NULL);
      break;
    }
}

typedef enum
{
  SET_MARKUP_FLAGS_NONE = 0,
  SET_MARKUP_FLAGS_HYPHEN_IF_EMPTY = (1<<0),
  SET_MARKUP_FLAGS_CHANGE_LINK = (1<<1)
} SetMarkupFlags;

static gboolean
on_activate_link (GtkLabel    *label,
                  const gchar *uri,
                  gpointer     user_data);

static void
set_markup (GduWindow      *window,
            const gchar    *key_label_id,
            const gchar    *label_id,
            const gchar    *markup,
            SetMarkupFlags  flags)
{
  GtkWidget *key_label;
  GtkWidget *label;
  gchar *s;

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

  if (flags & SET_MARKUP_FLAGS_CHANGE_LINK)
    {
      s = g_strdup_printf ("%s <small>— <a href=\"palimpsest://change/%s\">Change</a></small>", markup, label_id);
      if (g_hash_table_lookup (window->label_connections, label_id) == NULL)
        {
          g_signal_connect (label,
                            "activate-link",
                            G_CALLBACK (on_activate_link),
                            window);
          g_hash_table_insert (window->label_connections,
                               g_strdup (label_id),
                               label);
        }
    }
  else
    {
      s = g_strdup (markup);
    }
  gtk_label_set_markup (GTK_LABEL (label), s);
  g_free (s);
  gtk_widget_show (key_label);
  gtk_widget_show (label);

 out:
  ;
}

static void
set_size (GduWindow   *window,
          const gchar *key_label_id,
          const gchar *label_id,
          guint64      size)
{
  gchar *s;
  s = udisks_util_get_size_for_display (size, FALSE, TRUE);
  set_markup (window, key_label_id, label_id, s, SET_MARKUP_FLAGS_NONE);
  g_free (s);
}

static GList *
get_top_level_block_devices_for_lun (GduWindow   *window,
                                     const gchar *lun_object_path)
{
  GList *ret;
  GList *l;
  GList *object_proxies;
  GDBusProxyManager *proxy_manager;

  proxy_manager = udisks_client_get_proxy_manager (window->client);
  object_proxies = g_dbus_proxy_manager_get_all (proxy_manager);

  ret = NULL;
  for (l = object_proxies; l != NULL; l = l->next)
    {
      GDBusObjectProxy *object_proxy = G_DBUS_OBJECT_PROXY (l->data);
      UDisksBlockDevice *block;

      block = UDISKS_GET_BLOCK_DEVICE (object_proxy);
      if (block == NULL)
        continue;

      if (g_strcmp0 (udisks_block_device_get_lun (block), lun_object_path) == 0 &&
          !udisks_block_device_get_part_entry (block))
        {
          ret = g_list_append (ret, g_object_ref (object_proxy));
        }
      g_object_unref (block);
    }
  g_list_foreach (object_proxies, (GFunc) g_object_unref, NULL);
  g_list_free (object_proxies);
  return ret;
}

static gint
block_device_compare_on_preferred (GDBusObjectProxy *a,
                                   GDBusObjectProxy *b)
{
  return g_strcmp0 (udisks_block_device_get_preferred_device (UDISKS_PEEK_BLOCK_DEVICE (a)),
                    udisks_block_device_get_preferred_device (UDISKS_PEEK_BLOCK_DEVICE (b)));
}

static void
setup_device_page (GduWindow         *window,
                   GDBusObjectProxy *object_proxy)
{
  UDisksLun *lun;
  UDisksBlockDevice *block;
  GList *children;
  GList *l;

  children = gtk_container_get_children (GTK_CONTAINER (gdu_window_get_widget (window, "devtab-table")));
  for (l = children; l != NULL; l = l->next)
    {
      GtkWidget *child = GTK_WIDGET (l->data);
      gtk_widget_hide (child);
    }
  g_list_free (children);

  lun = UDISKS_PEEK_LUN (object_proxy);
  block = UDISKS_PEEK_BLOCK_DEVICE (object_proxy);

  if (lun != NULL)
    {
      GList *block_devices;
      gchar *lun_name;
      gchar *lun_desc;
      GIcon *lun_icon;
      GIcon *lun_media_icon;

      /* TODO: for multipath, ensure e.g. mpathk is before sda, sdb */
      block_devices = get_top_level_block_devices_for_lun (window, g_dbus_object_proxy_get_object_path (object_proxy));
      block_devices = g_list_sort (block_devices, (GCompareFunc) block_device_compare_on_preferred);

      udisks_util_get_lun_info (lun,
                                &lun_name,
                                &lun_desc,
                                &lun_icon,
                                &lun_media_icon);
      gdu_volume_grid_set_container_icon (GDU_VOLUME_GRID (window->volume_grid),
                                          lun_icon);

      gdu_volume_grid_set_container_visible (GDU_VOLUME_GRID (window->volume_grid), TRUE);
      if (block_devices != NULL)
        gdu_volume_grid_set_block_device (GDU_VOLUME_GRID (window->volume_grid), block_devices->data);
      else
        gdu_volume_grid_set_block_device (GDU_VOLUME_GRID (window->volume_grid), NULL);

      g_free (lun_name);
      g_free (lun_desc);
      g_object_unref (lun_icon);
      g_object_unref (lun_media_icon);

      g_list_foreach (block_devices, (GFunc) g_object_unref, NULL);
      g_list_free (block_devices);
    }
  else if (block != NULL)
    {
      gdu_volume_grid_set_container_visible (GDU_VOLUME_GRID (window->volume_grid), FALSE);
      gdu_volume_grid_set_block_device (GDU_VOLUME_GRID (window->volume_grid), object_proxy);
    }
  else
    {
      g_assert_not_reached ();
    }
}

static void
setup_details_page (GduWindow         *window,
                    GDBusObjectProxy *object_proxy,
                    gint              page)
{
  //g_debug ("setup for %s, page %d",
  //         object_proxy != NULL ? g_dbus_object_proxy_get_object_path (object_proxy) : "<none>",
  //         page);

  switch (page)
    {
    case DETAILS_PAGE_NOT_SELECTED:
      break;

    case DETAILS_PAGE_DEVICE:
      setup_device_page (window, object_proxy);
      break;
    }
}

static void
select_details_page (GduWindow         *window,
                     GDBusObjectProxy  *object_proxy,
                     DetailsPage        page)
{
  GtkNotebook *notebook;

  notebook = GTK_NOTEBOOK (gdu_window_get_widget (window, "palimpsest-notebook"));

  teardown_details_page (window,
                         window->current_object_proxy,
                         window->current_page);

  window->current_page = page;
  if (window->current_object_proxy != NULL)
    g_object_unref (window->current_object_proxy);
  window->current_object_proxy = object_proxy != NULL ? g_object_ref (object_proxy) : NULL;

  gtk_notebook_set_current_page (notebook, page);

  setup_details_page (window,
                      window->current_object_proxy,
                      window->current_page);
}

static void
update_details_page (GduWindow *window)
{
  teardown_details_page (window,
                         window->current_object_proxy,
                         window->current_page);
  setup_details_page (window,
                      window->current_object_proxy,
                      window->current_page);
}

static void
update_all (GduWindow         *window,
            GDBusObjectProxy  *object_proxy)
{
  if (window->current_object_proxy == object_proxy)
    update_details_page (window);
}

static void
on_object_proxy_added (GDBusProxyManager   *manager,
                       GDBusObjectProxy    *object_proxy,
                       gpointer             user_data)
{
  GduWindow *window = GDU_WINDOW (user_data);
  update_all (window, object_proxy);
}

static void
on_object_proxy_removed (GDBusProxyManager   *manager,
                         GDBusObjectProxy    *object_proxy,
                         gpointer             user_data)
{
  GduWindow *window = GDU_WINDOW (user_data);
  update_all (window, object_proxy);
}

static void
on_interface_proxy_added (GDBusProxyManager   *manager,
                          GDBusObjectProxy    *object_proxy,
                          GDBusProxy          *interface_proxy,
                          gpointer             user_data)
{
  GduWindow *window = GDU_WINDOW (user_data);
  update_all (window, object_proxy);
}

static void
on_interface_proxy_removed (GDBusProxyManager   *manager,
                            GDBusObjectProxy    *object_proxy,
                            GDBusProxy          *interface_proxy,
                            gpointer             user_data)
{
  GduWindow *window = GDU_WINDOW (user_data);
  update_all (window, object_proxy);
}

static void
on_interface_proxy_properties_changed (GDBusProxyManager   *manager,
                                       GDBusObjectProxy    *object_proxy,
                                       GDBusProxy          *interface_proxy,
                                       GVariant            *changed_properties,
                                       const gchar *const *invalidated_properties,
                                       gpointer            user_data)
{
  GduWindow *window = GDU_WINDOW (user_data);
  update_all (window, object_proxy);
}

static void
update_devtab_for_lun (GduWindow         *window,
                       GDBusObjectProxy  *object_proxy,
                       UDisksLun         *lun)
{
  gchar *s;
  GList *block_devices;
  GList *l;
  GString *str;
  const gchar *lun_vendor;
  const gchar *lun_model;

  //g_debug ("In update_devtab_for_lun() - selected=%s",
  //         object_proxy != NULL ? g_dbus_object_proxy_get_object_path (object_proxy) : "<nothing>");

  /* TODO: for multipath, ensure e.g. mpathk is before sda, sdb */
  block_devices = get_top_level_block_devices_for_lun (window, g_dbus_object_proxy_get_object_path (object_proxy));
  block_devices = g_list_sort (block_devices, (GCompareFunc) block_device_compare_on_preferred);

  lun_vendor = udisks_lun_get_vendor (lun);
  lun_model = udisks_lun_get_model (lun);

  str = g_string_new (NULL);
  for (l = block_devices; l != NULL; l = l->next)
    {
      GDBusObjectProxy *block_object_proxy = G_DBUS_OBJECT_PROXY (l->data);
      if (str->len > 0)
        g_string_append_c (str, ' ');
      g_string_append (str, udisks_block_device_get_preferred_device (UDISKS_PEEK_BLOCK_DEVICE (block_object_proxy)));
    }
  s = g_string_free (str, FALSE);
  set_markup (window,
              "devtab-device-label",
              "devtab-device-value-label",
              s, SET_MARKUP_FLAGS_NONE);
  g_free (s);
  g_list_foreach (block_devices, (GFunc) g_object_unref, NULL);
  g_list_free (block_devices);

  if (strlen (lun_vendor) == 0)
    s = g_strdup (lun_model);
  else if (strlen (lun_model) == 0)
    s = g_strdup (lun_vendor);
  else
    s = g_strconcat (lun_vendor, " ", lun_model, NULL);
  set_markup (window,
              "devtab-model-label",
              "devtab-model-value-label", s, SET_MARKUP_FLAGS_NONE);
  g_free (s);
  set_markup (window,
              "devtab-serial-number-label",
              "devtab-serial-number-value-label",
              udisks_lun_get_serial (lun), SET_MARKUP_FLAGS_NONE);
  set_markup (window,
              "devtab-firmware-version-label",
              "devtab-firmware-version-value-label",
              udisks_lun_get_revision (lun), SET_MARKUP_FLAGS_NONE);
  set_markup (window,
              "devtab-wwn-label",
              "devtab-wwn-value-label",
              udisks_lun_get_wwn (lun), SET_MARKUP_FLAGS_NONE);
  set_size (window,
            "devtab-size-label",
            "devtab-size-value-label",
            udisks_lun_get_size (lun));
  /* TODO: get this from udisks */
  gtk_switch_set_active (GTK_SWITCH (window->write_cache_switch), TRUE);
  gtk_widget_show (gdu_window_get_widget (window, "devtab-write-cache-label"));
  gtk_widget_show_all (gdu_window_get_widget (window, "devtab-write-cache-hbox"));
}

static void
update_devtab_for_block (GduWindow         *window,
                         GDBusObjectProxy  *object_proxy,
                         UDisksBlockDevice *block,
                         guint64            size)
{
  const gchar *backing_file;
  const gchar *usage;
  const gchar *type;
  const gchar *version;
  gint partition_type;
  gchar *type_for_display;

  //g_debug ("In update_devtab_for_block() - size=%" G_GUINT64_FORMAT " selected=%s",
  //         size,
  //         object_proxy != NULL ? g_dbus_object_proxy_get_object_path (object_proxy) : "<nothing>");

  set_markup (window,
              "devtab-device-label",
              "devtab-device-value-label",
              udisks_block_device_get_preferred_device (block), SET_MARKUP_FLAGS_NONE);
  set_size (window,
            "devtab-size-label",
            "devtab-size-value-label",
            size);
  backing_file = udisks_block_device_get_loop_backing_file (block);
  if (strlen (backing_file) > 0)
    {
      set_markup (window,
                  "devtab-backing-file-label",
                  "devtab-backing-file-value-label",
                  backing_file, SET_MARKUP_FLAGS_NONE);
    }

  usage = udisks_block_device_get_id_usage (block);
  type = udisks_block_device_get_id_type (block);
  version = udisks_block_device_get_id_version (block);
  partition_type = strtol (udisks_block_device_get_part_entry_type (block), NULL, 0);

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
  set_markup (window,
              "devtab-volume-type-label",
              "devtab-volume-type-value-label",
              type_for_display, SET_MARKUP_FLAGS_NONE);
  g_free (type_for_display);

  set_markup (window,
              "devtab-volume-label-label",
              "devtab-volume-label-value-label",
              udisks_block_device_get_id_label (block),
              SET_MARKUP_FLAGS_CHANGE_LINK);

  set_markup (window,
              "devtab-volume-uuid-label",
              "devtab-volume-uuid-value-label",
              udisks_block_device_get_id_uuid (block),
              SET_MARKUP_FLAGS_NONE);

  if (udisks_block_device_get_part_entry (block))
    {
      const gchar *partition_label;
      gchar *type_for_display;

      type_for_display = udisks_util_get_part_type_for_display (udisks_block_device_get_part_entry_scheme (block),
                                                                udisks_block_device_get_part_entry_type (block));

      partition_label = udisks_block_device_get_part_entry_label (block);
      set_markup (window,
                  "devtab-volume-partition-type-label",
                  "devtab-volume-partition-type-value-label",
                  type_for_display,
                  SET_MARKUP_FLAGS_CHANGE_LINK);
      set_markup (window,
                  "devtab-volume-partition-label-label",
                  "devtab-volume-partition-label-value-label",
                  partition_label,
                  SET_MARKUP_FLAGS_CHANGE_LINK);
      g_free (type_for_display);
    }
}

static void
update_devtab_for_no_media (GduWindow         *window,
                            GDBusObjectProxy  *object_proxy,
                            UDisksBlockDevice *block)
{
  //g_debug ("In update_devtab_for_no_media() - selected=%s",
  //         object_proxy != NULL ? g_dbus_object_proxy_get_object_path (object_proxy) : "<nothing>");
}

static void
update_devtab_for_free_space (GduWindow         *window,
                              GDBusObjectProxy  *object_proxy,
                              UDisksBlockDevice *block,
                              guint64            size)
{
  //g_debug ("In update_devtab_for_free_space() - size=%" G_GUINT64_FORMAT " selected=%s",
  //         size,
  //         object_proxy != NULL ? g_dbus_object_proxy_get_object_path (object_proxy) : "<nothing>");

  set_markup (window,
              "devtab-device-label",
              "devtab-device-value-label",
              udisks_block_device_get_preferred_device (block), SET_MARKUP_FLAGS_NONE);
  set_size (window,
            "devtab-size-label",
            "devtab-size-value-label",
            size);
  set_markup (window,
              "devtab-volume-type-label",
              "devtab-volume-type-value-label",
              _("Unallocated Space"),
              SET_MARKUP_FLAGS_NONE);
}

static void
update_devtab (GduWindow *window)
{
  GDBusObjectProxy *object_proxy;
  GList *children;
  GList *l;
  GduVolumeGridElementType type;
  UDisksBlockDevice *block;
  UDisksLun *lun;
  guint64 size;

  /* first hide everything */
  children = gtk_container_get_children (GTK_CONTAINER (gdu_window_get_widget (window, "devtab-table")));
  for (l = children; l != NULL; l = l->next)
    {
      GtkWidget *child = GTK_WIDGET (l->data);
      gtk_widget_hide (child);
    }
  g_list_free (children);

  object_proxy = window->current_object_proxy;
  lun = UDISKS_PEEK_LUN (window->current_object_proxy);
  block = UDISKS_PEEK_BLOCK_DEVICE (window->current_object_proxy);
  type = gdu_volume_grid_get_selected_type (GDU_VOLUME_GRID (window->volume_grid));
  size = gdu_volume_grid_get_selected_size (GDU_VOLUME_GRID (window->volume_grid));

  if (type == GDU_VOLUME_GRID_ELEMENT_TYPE_CONTAINER)
    {
      if (lun != NULL)
        update_devtab_for_lun (window, object_proxy, lun);
      else if (block != NULL)
        update_devtab_for_block (window, object_proxy, block, size);
    }
  else
    {
      object_proxy = gdu_volume_grid_get_selected_device (GDU_VOLUME_GRID (window->volume_grid));
      if (object_proxy == NULL)
        object_proxy = gdu_volume_grid_get_block_device (GDU_VOLUME_GRID (window->volume_grid));
      if (object_proxy != NULL)
        {
          block = UDISKS_PEEK_BLOCK_DEVICE (object_proxy);
          switch (type)
            {
            case GDU_VOLUME_GRID_ELEMENT_TYPE_CONTAINER:
              g_assert_not_reached (); /* already handled above */
              break;

            case GDU_VOLUME_GRID_ELEMENT_TYPE_DEVICE:
              update_devtab_for_block (window, object_proxy, block, size);
              break;

            case GDU_VOLUME_GRID_ELEMENT_TYPE_NO_MEDIA:
              update_devtab_for_no_media (window, object_proxy, block);
              break;

            case GDU_VOLUME_GRID_ELEMENT_TYPE_FREE_SPACE:
              update_devtab_for_free_space (window, object_proxy, block, size);
              break;
            }
        }
    }
}

static void
on_volume_grid_changed (GduVolumeGrid  *grid,
                        gpointer        user_data)
{
  GduWindow *window = GDU_WINDOW (user_data);
  update_devtab (window);
}

static void
on_change_label (GduWindow *window)
{
  g_debug ("TODO: %s", G_STRFUNC);
}

static void
on_change_partition_type (GduWindow *window)
{
  g_debug ("TODO: %s", G_STRFUNC);
}

static void
on_change_partition_label (GduWindow *window)
{
  g_debug ("TODO: %s", G_STRFUNC);
}

static gboolean
on_activate_link (GtkLabel    *label,
                  const gchar *uri,
                  gpointer     user_data)
{
  GduWindow *window = GDU_WINDOW (user_data);
  gboolean handled;

  handled = FALSE;
  if (!g_str_has_prefix (uri, "palimpsest://"))
    goto out;

  handled = TRUE;

  if (g_strcmp0 (uri, "palimpsest://change/devtab-volume-label-value-label") == 0)
    on_change_label (window);
  else if (g_strcmp0 (uri, "palimpsest://change/devtab-volume-partition-type-value-label") == 0)
    on_change_partition_type (window);
  else if (g_strcmp0 (uri, "palimpsest://change/devtab-volume-partition-label-value-label") == 0)
    on_change_partition_label (window);
  else
    g_warning ("Unhandled action: %s", uri);

 out:
  return handled;
}

