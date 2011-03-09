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
#include <glib/gi18n.h>

#include "gduapplication.h"
#include "gduwindow.h"
#include "gdudevicetreemodel.h"
#include "gduutils.h"

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

G_DEFINE_TYPE (GduWindow, gdu_window, GTK_TYPE_WINDOW);

static void
gdu_window_init (GduWindow *window)
{
}

static void
gdu_window_finalize (GObject *object)
{
  GduWindow *window = GDU_WINDOW (object);

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
  GtkStyleContext *context;

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
      break;
    }
}

static void
set_string (GduWindow   *window,
            const gchar *key_label_id,
            const gchar *label_id,
            const gchar *text)
{
  GtkWidget *key_label;
  GtkWidget *label;

  if (text == NULL || strlen (text) == 0)
    text = "—";

  /* TODO: utf-8 validate */

  key_label = gdu_window_get_widget (window, key_label_id);
  label = gdu_window_get_widget (window, label_id);

  gtk_label_set_text (GTK_LABEL (label), text);
  gtk_widget_show (key_label);
  gtk_widget_show (label);
}

static void
set_size (GduWindow   *window,
          const gchar *key_label_id,
          const gchar *label_id,
          guint64      size)
{
  gchar *s;
  s = udisks_util_get_size_for_display (size, FALSE, TRUE);
  set_string (window, key_label_id, label_id, s);
  g_free (s);
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
      const gchar *lun_vendor;
      const gchar *lun_model;
      gchar *s;

      lun_vendor = udisks_lun_get_vendor (lun);
      lun_model = udisks_lun_get_model (lun);
      if (strlen (lun_vendor) == 0)
        s = g_strdup (lun_model);
      else if (strlen (lun_model) == 0)
        s = g_strdup (lun_vendor);
      else
        s = g_strconcat (lun_vendor, " ", lun_model, NULL);
      set_string (window,
                  "devtab-model-label",
                  "devtab-model-value-label", s);
      g_free (s);
      set_string (window,
                  "devtab-serial-number-label",
                  "devtab-serial-number-value-label",
                  udisks_lun_get_serial (lun));
      set_string (window,
                  "devtab-firmware-version-label",
                  "devtab-firmware-version-value-label",
                  udisks_lun_get_revision (lun));
      set_string (window,
                  "devtab-wwn-label",
                  "devtab-wwn-value-label",
                  udisks_lun_get_wwn (lun));
      set_size (window,
                "devtab-size-label",
                "devtab-size-value-label",
                udisks_lun_get_size (lun));
      /* TODO: get this from udisks */
      gtk_switch_set_active (GTK_SWITCH (gdu_window_get_widget (window, "devtab-write-cache-switch")), TRUE);
      gtk_widget_show (gdu_window_get_widget (window, "devtab-write-cache-label"));
      gtk_widget_show (gdu_window_get_widget (window, "devtab-write-cache-switch"));
      gtk_widget_show (gdu_window_get_widget (window, "devtab-write-cache-hbox"));
    }
  else if (block != NULL)
    {
      const gchar *backing_file;

      set_string (window,
                  "devtab-device-label",
                  "devtab-device-value-label",
                  udisks_block_device_get_preferred_device (block));
      set_size (window,
                "devtab-size-label",
                "devtab-size-value-label",
                udisks_block_device_get_size (block));
      backing_file = udisks_block_device_get_loop_backing_file (block);
      if (strlen (backing_file) > 0)
        set_string (window,
                    "devtab-backing-file-label",
                    "devtab-backing-file-value-label",
                    backing_file);
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
