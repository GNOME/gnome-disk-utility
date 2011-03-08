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

#include "gdutreemodel.h"

struct _GduTreeModel
{
  GtkTreeStore parent_instance;

  UDisksClient *client;

  GList *current_luns;
  GtkTreeIter lun_iter;

  GList *current_blocks;
  GtkTreeIter block_iter;
};

typedef struct
{
  GtkTreeStoreClass parent_class;
} GduTreeModelClass;

enum
{
  PROP_0,
  PROP_CLIENT
};

G_DEFINE_TYPE (GduTreeModel, gdu_tree_model, GTK_TYPE_TREE_STORE);

static void coldplug (GduTreeModel *model);

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
                                                   GDBusObjectProxy  *object_proxy,
                                                   GDBusProxy        *interface_proxy,
                                                   GVariant          *changed_properties,
                                                   GStrv              invalidated_properties,
                                                   gpointer           user_data);

static void
gdu_tree_model_finalize (GObject *object)
{
  GduTreeModel *model = GDU_TREE_MODEL (object);
  GDBusProxyManager *proxy_manager;

  proxy_manager = udisks_client_get_proxy_manager (model->client);
  g_signal_handlers_disconnect_by_func (proxy_manager,
                                        G_CALLBACK (on_object_proxy_added),
                                        model);
  g_signal_handlers_disconnect_by_func (proxy_manager,
                                        G_CALLBACK (on_object_proxy_removed),
                                        model);
  g_signal_handlers_disconnect_by_func (proxy_manager,
                                        G_CALLBACK (on_interface_proxy_added),
                                        model);
  g_signal_handlers_disconnect_by_func (proxy_manager,
                                        G_CALLBACK (on_interface_proxy_removed),
                                        model);
  g_signal_handlers_disconnect_by_func (proxy_manager,
                                        G_CALLBACK (on_interface_proxy_properties_changed),
                                        model);

  g_list_foreach (model->current_luns, (GFunc) g_object_unref, NULL);
  g_list_free (model->current_luns);
  g_object_unref (model->client);

  G_OBJECT_CLASS (gdu_tree_model_parent_class)->finalize (object);
}

static void
gdu_tree_model_init (GduTreeModel *model)
{
}

static void
gdu_tree_model_get_property (GObject    *object,
                             guint       prop_id,
                             GValue     *value,
                             GParamSpec *pspec)
{
  GduTreeModel *model = GDU_TREE_MODEL (object);

  switch (prop_id)
    {
    case PROP_CLIENT:
      g_value_set_object (value, gdu_tree_model_get_client (model));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
gdu_tree_model_set_property (GObject      *object,
                             guint         prop_id,
                             const GValue *value,
                             GParamSpec   *pspec)
{
  GduTreeModel *model = GDU_TREE_MODEL (object);

  switch (prop_id)
    {
    case PROP_CLIENT:
      model->client = g_value_dup_object (value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

typedef struct
{
  GDBusObjectProxy *object;
  GtkTreeIter iter;
  gboolean found;
} FindIterData;

static gboolean
find_iter_for_object_proxy_cb (GtkTreeModel  *model,
                               GtkTreePath   *path,
                               GtkTreeIter   *iter,
                               gpointer       user_data)
{
  FindIterData *data = user_data;
  GDBusObjectProxy *iter_object;

  iter_object = NULL;

  gtk_tree_model_get (model,
                      iter,
                      GDU_TREE_MODEL_COLUMN_OBJECT_PROXY, &iter_object,
                      -1);
  if (iter_object == NULL)
    goto out;

  if (iter_object == data->object)
    {
      data->iter = *iter;
      data->found = TRUE;
      goto out;
    }

 out:
  if (iter_object != NULL)
    g_object_unref (iter_object);
  return data->found;
}

static gboolean
find_iter_for_object_proxy (GduTreeModel     *model,
                            GDBusObjectProxy *object,
                            GtkTreeIter      *out_iter)
{
  FindIterData data;

  memset (&data, 0, sizeof (data));
  data.object = object;
  data.found = FALSE;
  gtk_tree_model_foreach (GTK_TREE_MODEL (model),
                          find_iter_for_object_proxy_cb,
                          &data);
  if (data.found)
    {
      if (out_iter != NULL)
        *out_iter = data.iter;
    }

  return data.found;
}

/* ---------------------------------------------------------------------------------------------------- */

static void
diff_sorted_lists (GList         *list1,
                   GList         *list2,
                   GCompareFunc   compare,
                   GList        **added,
                   GList        **removed)
{
  gint order;

  *added = *removed = NULL;

  while (list1 != NULL &&
         list2 != NULL)
    {
      order = (*compare) (list1->data, list2->data);
      if (order < 0)
        {
          *removed = g_list_prepend (*removed, list1->data);
          list1 = list1->next;
        }
      else if (order > 0)
        {
          *added = g_list_prepend (*added, list2->data);
          list2 = list2->next;
        }
      else
        { /* same item */
          list1 = list1->next;
          list2 = list2->next;
        }
    }

  while (list1 != NULL)
    {
      *removed = g_list_prepend (*removed, list1->data);
      list1 = list1->next;
    }
  while (list2 != NULL)
    {
      *added = g_list_prepend (*added, list2->data);
      list2 = list2->next;
    }
}

/* ---------------------------------------------------------------------------------------------------- */

static gint
_g_dbus_object_proxy_compare (GDBusObjectProxy *a,
                              GDBusObjectProxy *b)
{
  return g_strcmp0 (g_dbus_object_proxy_get_object_path (a),
                    g_dbus_object_proxy_get_object_path (b));
}

/* ---------------------------------------------------------------------------------------------------- */

static void
gdu_tree_model_constructed (GObject *object)
{
  GduTreeModel *model = GDU_TREE_MODEL (object);
  GType types[GDU_TREE_MODEL_N_COLUMNS];
  GDBusProxyManager *proxy_manager;
  gchar *s;

  types[0] = G_TYPE_STRING;
  types[1] = G_TYPE_BOOLEAN;
  types[2] = G_TYPE_STRING;
  types[3] = G_TYPE_ICON;
  types[4] = G_TYPE_STRING;
  types[5] = G_TYPE_DBUS_OBJECT_PROXY;
  G_STATIC_ASSERT (6 == GDU_TREE_MODEL_N_COLUMNS);
  gtk_tree_store_set_column_types (GTK_TREE_STORE (model),
                                   GDU_TREE_MODEL_N_COLUMNS,
                                   types);

  g_assert (gtk_tree_model_get_flags (GTK_TREE_MODEL (model)) & GTK_TREE_MODEL_ITERS_PERSIST);

  s = g_strdup_printf ("<small><span foreground=\"#555555\">%s</span></small>",
                       _("Direct-Attached Storage"));
  gtk_tree_store_insert_with_values (GTK_TREE_STORE (model),
                                     &model->lun_iter,
                                     NULL, /* GtkTreeIter *parent */
                                     0,
                                     GDU_TREE_MODEL_COLUMN_IS_HEADING, TRUE,
                                     GDU_TREE_MODEL_COLUMN_HEADING_TEXT, s,
                                     GDU_TREE_MODEL_COLUMN_SORT_KEY, "00_lun",
                                     -1);
  g_free (s);
  s = g_strdup_printf ("<small><span foreground=\"#555555\">%s</span></small>",
                       _("Other Devices"));
  gtk_tree_store_insert_with_values (GTK_TREE_STORE (model),
                                     &model->block_iter,
                                     NULL, /* GtkTreeIter *parent */
                                     0,
                                     GDU_TREE_MODEL_COLUMN_IS_HEADING, TRUE,
                                     GDU_TREE_MODEL_COLUMN_HEADING_TEXT, s,
                                     GDU_TREE_MODEL_COLUMN_SORT_KEY, "01_block",
                                     -1);
  g_free (s);

  proxy_manager = udisks_client_get_proxy_manager (model->client);
  g_signal_connect (proxy_manager,
                    "object-proxy-added",
                    G_CALLBACK (on_object_proxy_added),
                    model);
  g_signal_connect (proxy_manager,
                    "object-proxy-removed",
                    G_CALLBACK (on_object_proxy_removed),
                    model);
  g_signal_connect (proxy_manager,
                    "interface-proxy-added",
                    G_CALLBACK (on_interface_proxy_added),
                    model);
  g_signal_connect (proxy_manager,
                    "interface-proxy-removed",
                    G_CALLBACK (on_interface_proxy_removed),
                    model);
  g_signal_connect (proxy_manager,
                    "interface-proxy-properties-changed",
                    G_CALLBACK (on_interface_proxy_properties_changed),
                    model);
  coldplug (model);

  if (G_OBJECT_CLASS (gdu_tree_model_parent_class)->constructed != NULL)
    G_OBJECT_CLASS (gdu_tree_model_parent_class)->constructed (object);
}

static void
gdu_tree_model_class_init (GduTreeModelClass *klass)
{
  GObjectClass *gobject_class;

  gobject_class = G_OBJECT_CLASS (klass);
  gobject_class->finalize     = gdu_tree_model_finalize;
  gobject_class->constructed  = gdu_tree_model_constructed;
  gobject_class->get_property = gdu_tree_model_get_property;
  gobject_class->set_property = gdu_tree_model_set_property;

  /**
   * GduTreeModel:client:
   *
   * The #UDisksClient used by the #GduTreeModel instance.
   */
  g_object_class_install_property (gobject_class,
                                   PROP_CLIENT,
                                   g_param_spec_object ("client",
                                                        "Client",
                                                        "The client used by the tree model",
                                                        UDISKS_TYPE_CLIENT,
                                                        G_PARAM_READABLE |
                                                        G_PARAM_WRITABLE |
                                                        G_PARAM_CONSTRUCT_ONLY |
                                                        G_PARAM_STATIC_STRINGS));
}

/**
 * gdu_tree_model_new:
 * @client: A #UDisksClient.
 *
 * Creates a new #GduTreeModel for viewing the devices belonging to
 * @client.
 *
 * Returns: A #GduTreeModel. Free with g_object_unref().
 */
GduTreeModel *
gdu_tree_model_new (UDisksClient *client)
{
  return GDU_TREE_MODEL (g_object_new (GDU_TYPE_TREE_MODEL,
                                       "client", client,
                                       NULL));
}

/**
 * gdu_tree_model_get_client:
 * @model: A #GduTreeModel.
 *
 * Gets the #UDisksClient used by @model.
 *
 * Returns: (transfer none): A #UDisksClient. Do not free, the object
 * belongs to @model.
 */
UDisksClient *
gdu_tree_model_get_client (GduTreeModel *model)
{
  g_return_val_if_fail (GDU_IS_TREE_MODEL (model), NULL);
  return model->client;
}

/* ---------------------------------------------------------------------------------------------------- */

static void
add_lun (GduTreeModel     *model,
         GDBusObjectProxy *object_proxy,
         GtkTreeIter      *parent)
{
  const gchar *lun_vendor;
  const gchar *lun_model;
  UDisksLun *lun;
  GIcon *icon;
  gchar *name;
  gchar *sort_key;
  GtkTreeIter iter;

  lun = UDISKS_PEEK_LUN (object_proxy);
  lun_vendor = udisks_lun_get_vendor (lun);
  lun_model = udisks_lun_get_model (lun);

  if (strlen (lun_vendor) == 0)
    name = g_strdup (lun_model);
  else if (strlen (lun_model) == 0)
    name = g_strdup (lun_vendor);
  else
    name = g_strconcat (lun_vendor, " ", lun_model, NULL);

  icon = g_themed_icon_new ("drive-harddisk"); /* for now */
  sort_key = g_strdup (g_dbus_object_proxy_get_object_path (object_proxy)); /* for now */
  gtk_tree_store_insert_with_values (GTK_TREE_STORE (model),
                                     &iter,
                                     parent,
                                     0,
                                     GDU_TREE_MODEL_COLUMN_ICON, icon,
                                     GDU_TREE_MODEL_COLUMN_NAME, name,
                                     GDU_TREE_MODEL_COLUMN_SORT_KEY, sort_key,
                                     GDU_TREE_MODEL_COLUMN_OBJECT_PROXY, object_proxy,
                                     -1);
  g_object_unref (icon);
  g_free (sort_key);
  g_free (name);
}

static void
remove_lun (GduTreeModel     *model,
            GDBusObjectProxy *object_proxy)
{
  GtkTreeIter iter;

  if (!find_iter_for_object_proxy (model,
                                   object_proxy,
                                   &iter))
    {
      g_warning ("Error finding iter for object proxy at %s",
                 g_dbus_object_proxy_get_object_path (object_proxy));
      goto out;
    }

  gtk_tree_store_remove (GTK_TREE_STORE (model), &iter);

 out:
  ;
}

static void
update_das (GduTreeModel *model)
{
  GDBusProxyManager *proxy_manager;
  GList *object_proxies;
  GList *luns;
  GList *added_luns;
  GList *removed_luns;
  GList *l;

  proxy_manager = udisks_client_get_proxy_manager (model->client);
  object_proxies = g_dbus_proxy_manager_get_all (proxy_manager);

  luns = NULL;
  for (l = object_proxies; l != NULL; l = l->next)
    {
      GDBusObjectProxy *object_proxy = G_DBUS_OBJECT_PROXY (l->data);
      UDisksLun *lun;

      lun = UDISKS_PEEK_LUN (object_proxy);
      if (lun == NULL)
        continue;

      luns = g_list_prepend (luns, g_object_ref (object_proxy));
    }
  luns = g_list_sort (luns, (GCompareFunc) _g_dbus_object_proxy_compare);

  diff_sorted_lists (model->current_luns,
                     luns,
                     (GCompareFunc) _g_dbus_object_proxy_compare,
                     &added_luns,
                     &removed_luns);

  for (l = removed_luns; l != NULL; l = l->next)
    {
      GDBusObjectProxy *object_proxy = G_DBUS_OBJECT_PROXY (l->data);

      g_assert (g_list_find (model->current_luns, object_proxy) != NULL);

      model->current_luns = g_list_remove (model->current_luns, object_proxy);
      remove_lun (model, object_proxy);
      g_object_unref (object_proxy);
    }
  for (l = added_luns; l != NULL; l = l->next)
    {
      GDBusObjectProxy *object_proxy = G_DBUS_OBJECT_PROXY (l->data);
      model->current_luns = g_list_prepend (model->current_luns, g_object_ref (object_proxy));
      add_lun (model, object_proxy, &model->lun_iter);
    }

  g_list_free (added_luns);
  g_list_free (removed_luns);
  g_list_foreach (luns, (GFunc) g_object_unref, NULL);
  g_list_free (luns);

  g_list_foreach (object_proxies, (GFunc) g_object_unref, NULL);
  g_list_free (object_proxies);
}

/* ---------------------------------------------------------------------------------------------------- */

static void
add_block (GduTreeModel     *model,
           GDBusObjectProxy *object_proxy,
           GtkTreeIter      *parent)
{
  UDisksBlockDevice *block;
  GIcon *icon;
  gchar *name;
  gchar *sort_key;
  GtkTreeIter iter;

  block = UDISKS_PEEK_BLOCK_DEVICE (object_proxy);

  name = g_strdup (udisks_block_device_get_preferred_device (block));

  icon = g_themed_icon_new ("drive-harddisk"); /* for now */
  sort_key = g_strdup (g_dbus_object_proxy_get_object_path (object_proxy)); /* for now */
  gtk_tree_store_insert_with_values (GTK_TREE_STORE (model),
                                     &iter,
                                     parent,
                                     0,
                                     GDU_TREE_MODEL_COLUMN_ICON, icon,
                                     GDU_TREE_MODEL_COLUMN_NAME, name,
                                     GDU_TREE_MODEL_COLUMN_SORT_KEY, sort_key,
                                     GDU_TREE_MODEL_COLUMN_OBJECT_PROXY, object_proxy,
                                     -1);
  g_object_unref (icon);
  g_free (sort_key);
  g_free (name);
}

static void
remove_block (GduTreeModel     *model,
              GDBusObjectProxy *object_proxy)
{
  GtkTreeIter iter;

  if (!find_iter_for_object_proxy (model,
                                   object_proxy,
                                   &iter))
    {
      g_warning ("Error finding iter for object proxy at %s",
                 g_dbus_object_proxy_get_object_path (object_proxy));
      goto out;
    }

  gtk_tree_store_remove (GTK_TREE_STORE (model), &iter);

 out:
  ;
}

static gboolean
should_include_block (GDBusObjectProxy *object_proxy)
{
  UDisksBlockDevice *block;
  gboolean ret;
  const gchar *device;
  const gchar *lun;
  guint64 size;

  ret = FALSE;

  block = UDISKS_PEEK_BLOCK_DEVICE (object_proxy);
  device = udisks_block_device_get_device (block);
  size = udisks_block_device_get_size (block);
  lun = udisks_block_device_get_lun (block);

  /* RAM devices are useless */
  if (g_str_has_prefix (device, "/dev/ram"))
    goto out;

  /* Otherwise we'd end up showing unused loop devices */
  if (size == 0)
    goto out;

  /* Don't include if already shown in "Direct-Attached devices" */
  if (g_strcmp0 (lun, "/") != 0)
    goto out;

  ret = TRUE;

 out:
  return ret;
}

static void
update_block (GduTreeModel *model)
{
  GDBusProxyManager *proxy_manager;
  GList *object_proxies;
  GList *blocks;
  GList *added_blocks;
  GList *removed_blocks;
  GList *l;

  proxy_manager = udisks_client_get_proxy_manager (model->client);
  object_proxies = g_dbus_proxy_manager_get_all (proxy_manager);

  blocks = NULL;
  for (l = object_proxies; l != NULL; l = l->next)
    {
      GDBusObjectProxy *object_proxy = G_DBUS_OBJECT_PROXY (l->data);
      UDisksBlockDevice *block;

      block = UDISKS_PEEK_BLOCK_DEVICE (object_proxy);
      if (block == NULL)
        continue;

      if (should_include_block (object_proxy))
        blocks = g_list_prepend (blocks, g_object_ref (object_proxy));
    }
  blocks = g_list_sort (blocks, (GCompareFunc) _g_dbus_object_proxy_compare);

  diff_sorted_lists (model->current_blocks,
                     blocks,
                     (GCompareFunc) _g_dbus_object_proxy_compare,
                     &added_blocks,
                     &removed_blocks);

  for (l = removed_blocks; l != NULL; l = l->next)
    {
      GDBusObjectProxy *object_proxy = G_DBUS_OBJECT_PROXY (l->data);

      g_assert (g_list_find (model->current_blocks, object_proxy) != NULL);

      model->current_blocks = g_list_remove (model->current_blocks, object_proxy);
      remove_block (model, object_proxy);
      g_object_unref (object_proxy);
    }
  for (l = added_blocks; l != NULL; l = l->next)
    {
      GDBusObjectProxy *object_proxy = G_DBUS_OBJECT_PROXY (l->data);
      model->current_blocks = g_list_prepend (model->current_blocks, g_object_ref (object_proxy));
      add_block (model, object_proxy, &model->block_iter);
    }

  g_list_free (added_blocks);
  g_list_free (removed_blocks);
  g_list_foreach (blocks, (GFunc) g_object_unref, NULL);
  g_list_free (blocks);

  g_list_foreach (object_proxies, (GFunc) g_object_unref, NULL);
  g_list_free (object_proxies);
}

/* ---------------------------------------------------------------------------------------------------- */

static void
update_all (GduTreeModel *model)
{
  /* TODO: if this is CPU intensive we could coalesce all updates / schedule timeouts */
  update_das (model);
  update_block (model);
}

static void
coldplug (GduTreeModel *model)
{
  update_all (model);
}

static void
on_object_proxy_added (GDBusProxyManager   *manager,
                       GDBusObjectProxy    *object_proxy,
                       gpointer             user_data)
{
  GduTreeModel *model = GDU_TREE_MODEL (user_data);
  update_all (model);
}

static void
on_object_proxy_removed (GDBusProxyManager   *manager,
                         GDBusObjectProxy    *object_proxy,
                         gpointer             user_data)
{
  GduTreeModel *model = GDU_TREE_MODEL (user_data);
  update_all (model);
}

static void
on_interface_proxy_added (GDBusProxyManager   *manager,
                          GDBusObjectProxy    *object_proxy,
                          GDBusProxy          *interface_proxy,
                          gpointer             user_data)
{
  GduTreeModel *model = GDU_TREE_MODEL (user_data);
  update_all (model);
}

static void
on_interface_proxy_removed (GDBusProxyManager   *manager,
                            GDBusObjectProxy    *object_proxy,
                            GDBusProxy          *interface_proxy,
                            gpointer             user_data)
{
  GduTreeModel *model = GDU_TREE_MODEL (user_data);
  update_all (model);
}

static void
on_interface_proxy_properties_changed (GDBusProxyManager   *manager,
                                       GDBusObjectProxy  *object_proxy,
                                       GDBusProxy        *interface_proxy,
                                       GVariant          *changed_properties,
                                       GStrv              invalidated_properties,
                                       gpointer           user_data)
{
  GduTreeModel *model = GDU_TREE_MODEL (user_data);
  update_all (model);
}

