/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*-
 *
 * Copyright (C) 2008-2013 Red Hat, Inc.
 *
 * Licensed under GPL version 2 or later.
 *
 * Author: David Zeuthen <zeuthen@gmail.com>
 */

#define SPINNER_TIMEOUT_MSEC 80

#include "config.h"
#include <glib/gi18n.h>

#include "gdudevicetreemodel.h"
#include "gduapplication.h"
#include "gduatasmartdialog.h"
#include "gduenumtypes.h"

struct _GduDeviceTreeModel
{
  GtkTreeStore parent_instance;

  GduApplication *application;
  UDisksClient *client;

  GduDeviceTreeModelFlags flags;

  GList *current_drives;
  GtkTreeIter drive_iter;
  gboolean drive_iter_valid;

  GList *current_blocks;
  GtkTreeIter block_iter;
  gboolean block_iter_valid;

  GList *current_mdraids;
  GtkTreeIter mdraid_iter;
  gboolean mdraid_iter_valid;

  guint spinner_timeout;

  /* "Polling Every Few Seconds" ... e.g. power state */
  guint pefs_timeout_id;

  GHashTable *sort_mz;
};

typedef struct
{
  GtkTreeStoreClass parent_class;
} GduDeviceTreeModelClass;

enum
{
  PROP_0,
  PROP_APPLICATION,
  PROP_FLAGS
};

G_DEFINE_TYPE (GduDeviceTreeModel, gdu_device_tree_model, GTK_TYPE_TREE_STORE);

static void coldplug (GduDeviceTreeModel *model);

static void on_client_changed (UDisksClient  *client,
                               gpointer       user_data);

static gboolean update_mdraid (GduDeviceTreeModel *model,
                               UDisksObject       *object,
                               gboolean            from_timer);

static gboolean update_drive (GduDeviceTreeModel *model,
                              UDisksObject       *object,
                              gboolean            from_timer);

static gboolean update_block (GduDeviceTreeModel  *model,
                              UDisksObject        *object,
                              gboolean             from_timer);


static void
gdu_device_tree_model_finalize (GObject *object)
{
  GduDeviceTreeModel *model = GDU_DEVICE_TREE_MODEL (object);

  if (model->pefs_timeout_id != 0)
    g_source_remove (model->pefs_timeout_id);

  if (model->spinner_timeout != 0)
    g_source_remove (model->spinner_timeout);

  g_signal_handlers_disconnect_by_func (model->client,
                                        G_CALLBACK (on_client_changed),
                                        model);

  g_list_foreach (model->current_drives, (GFunc) g_object_unref, NULL);
  g_list_free (model->current_drives);

  g_list_foreach (model->current_mdraids, (GFunc) g_object_unref, NULL);
  g_list_free (model->current_mdraids);

  g_object_unref (model->application);

  G_OBJECT_CLASS (gdu_device_tree_model_parent_class)->finalize (object);
}

static void
gdu_device_tree_model_init (GduDeviceTreeModel *model)
{
}

static void
gdu_device_tree_model_get_property (GObject    *object,
                                    guint       prop_id,
                                    GValue     *value,
                                    GParamSpec *pspec)
{
  GduDeviceTreeModel *model = GDU_DEVICE_TREE_MODEL (object);

  switch (prop_id)
    {
    case PROP_APPLICATION:
      g_value_set_object (value, gdu_device_tree_model_get_application (model));
      break;

    case PROP_FLAGS:
      g_value_set_flags (value, gdu_device_tree_model_get_flags (model));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
gdu_device_tree_model_set_property (GObject      *object,
                                    guint         prop_id,
                                    const GValue *value,
                                    GParamSpec   *pspec)
{
  GduDeviceTreeModel *model = GDU_DEVICE_TREE_MODEL (object);

  switch (prop_id)
    {
    case PROP_APPLICATION:
      model->application = g_value_dup_object (value);
      model->client = gdu_application_get_client (model->application);
      break;

    case PROP_FLAGS:
      model->flags = g_value_get_flags (value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

/* ---------------------------------------------------------------------------------------------------- */

typedef struct
{
  UDisksObject *object;
  const gchar *object_path;
  GtkTreeIter iter;
  gboolean found;
} FindIterData;

static gboolean
find_iter_for_object_cb (GtkTreeModel  *model,
                         GtkTreePath   *path,
                         GtkTreeIter   *iter,
                         gpointer       user_data)
{
  FindIterData *data = user_data;
  UDisksObject *iter_object;

  iter_object = NULL;

  gtk_tree_model_get (model,
                      iter,
                      GDU_DEVICE_TREE_MODEL_COLUMN_OBJECT, &iter_object,
                      -1);
  if (iter_object == NULL)
    goto out;

  if (iter_object == data->object)
    {
      data->iter = *iter;
      data->found = TRUE;
      goto out;
    }

  if (g_strcmp0 (g_dbus_object_get_object_path (G_DBUS_OBJECT (iter_object)), data->object_path) == 0)
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
find_iter_for_object (GduDeviceTreeModel *model,
                      UDisksObject       *object,
                      GtkTreeIter        *out_iter)
{
  FindIterData data;

  memset (&data, 0, sizeof (data));
  data.object = object;
  data.found = FALSE;
  gtk_tree_model_foreach (GTK_TREE_MODEL (model),
                          find_iter_for_object_cb,
                          &data);
  if (data.found)
    {
      if (out_iter != NULL)
        *out_iter = data.iter;
    }

  return data.found;
}

gboolean
gdu_device_tree_model_get_iter_for_object (GduDeviceTreeModel *model,
                                           UDisksObject       *object,
                                           GtkTreeIter        *iter)
{
  return find_iter_for_object (model, object, iter);
}


#if 0
static gboolean
find_iter_for_object_path (GduDeviceTreeModel *model,
                           const gchar        *object_path,
                           GtkTreeIter        *out_iter)
{
  FindIterData data;

  memset (&data, 0, sizeof (data));
  data.object_path = object_path;
  data.found = FALSE;
  gtk_tree_model_foreach (GTK_TREE_MODEL (model),
                          find_iter_for_object_cb,
                          &data);
  if (data.found)
    {
      if (out_iter != NULL)
        *out_iter = data.iter;
    }

  return data.found;
}
#endif

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
_g_dbus_object_compare (GDBusObject *a,
                        GDBusObject *b)
{
  return g_strcmp0 (g_dbus_object_get_object_path (a),
                    g_dbus_object_get_object_path (b));
}

/* ---------------------------------------------------------------------------------------------------- */

static void
pm_get_state_cb (GObject       *source_object,
                 GAsyncResult  *res,
                 gpointer       user_data)
{
  GduDeviceTreeModel *model = GDU_DEVICE_TREE_MODEL (user_data);
  GDBusObject *object;
  GduPowerStateFlags flags;
  guchar state = 0x80;
  GError *error = NULL;

  flags = GDU_POWER_STATE_FLAGS_NONE;

  if (!udisks_drive_ata_call_pm_get_state_finish (UDISKS_DRIVE_ATA (source_object),
                                                  &state,
                                                  res,
                                                  &error))
    {
      if (g_error_matches (error, UDISKS_ERROR, UDISKS_ERROR_DEVICE_BUSY))
        {
          /* can happen if a secure erase in progress.. */
          g_clear_error (&error);
          goto out;
        }
      else if (g_error_matches (error, UDISKS_ERROR, UDISKS_ERROR_NOT_AUTHORIZED) ||
               g_error_matches (error, UDISKS_ERROR, UDISKS_ERROR_NOT_AUTHORIZED_CAN_OBTAIN) ||
               g_error_matches (error, UDISKS_ERROR, UDISKS_ERROR_NOT_AUTHORIZED_DISMISSED))
        {
          /* can happen e.g. if the session is fast-user-switched away from - for example a VT-switch */
          g_clear_error (&error);
          goto out;
        }
      else if (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
        {
          /* can happen if method call is being canceled */
          g_clear_error (&error);
          goto out;
        }
      else
        {
          /* Otherwise report and stop trying */
          g_printerr ("Error calling Drive.Ata.PmGetState: %s (%s, %d)\n",
                      error->message, g_quark_to_string (error->domain), error->code);
          flags |= GDU_POWER_STATE_FLAGS_FAILED; /* so we won't try again */
          g_clear_error (&error);
          goto out;
        }
    }

  if (!(state == 0x80 || state == 0xff))
    {
      flags |= GDU_POWER_STATE_FLAGS_STANDBY;
    }

 out:
  object = g_dbus_interface_get_object (G_DBUS_INTERFACE (source_object));
  if (object != NULL)
    {
      GtkTreeIter iter;
      if (gdu_device_tree_model_get_iter_for_object (model, UDISKS_OBJECT (object), &iter))
        {
          gtk_tree_store_set (GTK_TREE_STORE (model),
                              &iter,
                              GDU_DEVICE_TREE_MODEL_COLUMN_POWER_STATE_FLAGS, flags,
                              -1);
        }
    }

  g_object_unref (model);
}


static gboolean
pefs_timeout_foreach_cb (GtkTreeModel  *_model,
                         GtkTreePath   *path,
                         GtkTreeIter   *iter,
                         gpointer       user_data)
{
  GduDeviceTreeModel *model = GDU_DEVICE_TREE_MODEL (_model);
  UDisksObject *object = NULL;
  UDisksDriveAta *ata = NULL;
  GduPowerStateFlags cur_flags = GDU_POWER_STATE_FLAGS_NONE;

  gtk_tree_model_get (GTK_TREE_MODEL (model),
                      iter,
                      GDU_DEVICE_TREE_MODEL_COLUMN_OBJECT, &object,
                      GDU_DEVICE_TREE_MODEL_COLUMN_POWER_STATE_FLAGS, &cur_flags,
                      -1);

  if (object == NULL)
    goto out;

  /* Don't check power state if
   *
   *  - a check is already pending; or
   *  - a check failed in the past
   */
  if (cur_flags & GDU_POWER_STATE_FLAGS_CHECKING || cur_flags & GDU_POWER_STATE_FLAGS_FAILED)
    goto out;

  ata = udisks_object_peek_drive_ata (object);
  if (ata != NULL && udisks_drive_ata_get_pm_supported (ata) && udisks_drive_ata_get_pm_enabled (ata))
    {
      GVariantBuilder options_builder;
      g_variant_builder_init (&options_builder, G_VARIANT_TYPE_VARDICT);
      g_variant_builder_add (&options_builder,
                             "{sv}", "auth.no_user_interaction", g_variant_new_boolean (TRUE));
      udisks_drive_ata_call_pm_get_state (ata,
                                          g_variant_builder_end (&options_builder),
                                          NULL, /* GCancellable */
                                          pm_get_state_cb,
                                          g_object_ref (model));

      cur_flags |= GDU_POWER_STATE_FLAGS_CHECKING;
      gtk_tree_store_set (GTK_TREE_STORE (model),
                          iter,
                          GDU_DEVICE_TREE_MODEL_COLUMN_POWER_STATE_FLAGS, cur_flags,
                          -1);
    }

  /* TODO: add support for other PM interfaces */

 out:
  g_clear_object (&object);
  return FALSE; /* continue iterating */
}

static gboolean
on_pefs_timeout (gpointer user_data)
{
  GduDeviceTreeModel *model = GDU_DEVICE_TREE_MODEL (user_data);
  gtk_tree_model_foreach (GTK_TREE_MODEL (model),
                          pefs_timeout_foreach_cb,
                          NULL);
  return TRUE; /* keep timeout around */
}

/* ---------------------------------------------------------------------------------------------------- */

static void
gdu_device_tree_model_constructed (GObject *object)
{
  GduDeviceTreeModel *model = GDU_DEVICE_TREE_MODEL (object);
  GType types[GDU_DEVICE_TREE_MODEL_N_COLUMNS];

  G_STATIC_ASSERT (13 == GDU_DEVICE_TREE_MODEL_N_COLUMNS);

  types[0] = G_TYPE_STRING;
  types[1] = G_TYPE_BOOLEAN;
  types[2] = G_TYPE_STRING;
  types[3] = G_TYPE_ICON;
  types[4] = G_TYPE_STRING;
  types[5] = G_TYPE_DBUS_OBJECT;
  types[6] = UDISKS_TYPE_BLOCK;
  types[7] = G_TYPE_BOOLEAN;
  types[8] = G_TYPE_UINT;
  types[9] = G_TYPE_BOOLEAN;
  types[10] = GDU_TYPE_POWER_STATE_FLAGS;
  types[11] = G_TYPE_UINT64;
  types[12] = G_TYPE_BOOLEAN;
  gtk_tree_store_set_column_types (GTK_TREE_STORE (model),
                                   GDU_DEVICE_TREE_MODEL_N_COLUMNS,
                                   types);

  g_assert (gtk_tree_model_get_flags (GTK_TREE_MODEL (model)) & GTK_TREE_MODEL_ITERS_PERSIST);

  g_signal_connect (model->client,
                    "changed",
                    G_CALLBACK (on_client_changed),
                    model);
  coldplug (model);

  if (model->flags & GDU_DEVICE_TREE_MODEL_FLAGS_UPDATE_POWER_STATE)
    {
      model->pefs_timeout_id = g_timeout_add_seconds (5, on_pefs_timeout, model);
      on_pefs_timeout (model);
    }

  if (model->flags & GDU_DEVICE_TREE_MODEL_FLAGS_INCLUDE_NONE_ITEM)
    {
      gtk_tree_store_insert_with_values (GTK_TREE_STORE (model),
                                         &model->drive_iter,
                                         NULL, /* GtkTreeIter *parent */
                                         0,
                                         GDU_DEVICE_TREE_MODEL_COLUMN_NAME, _("(None)"),
                                         GDU_DEVICE_TREE_MODEL_COLUMN_SORT_KEY, "00_0select_device",
                                         -1);
    }

  if (G_OBJECT_CLASS (gdu_device_tree_model_parent_class)->constructed != NULL)
    G_OBJECT_CLASS (gdu_device_tree_model_parent_class)->constructed (object);
}

static void
gdu_device_tree_model_class_init (GduDeviceTreeModelClass *klass)
{
  GObjectClass *gobject_class;

  gobject_class = G_OBJECT_CLASS (klass);
  gobject_class->finalize     = gdu_device_tree_model_finalize;
  gobject_class->constructed  = gdu_device_tree_model_constructed;
  gobject_class->get_property = gdu_device_tree_model_get_property;
  gobject_class->set_property = gdu_device_tree_model_set_property;

  /**
   * GduDeviceTreeModel:application:
   *
   * The #GduApplication used by the #GduDeviceTreeModel instance.
   */
  g_object_class_install_property (gobject_class,
                                   PROP_APPLICATION,
                                   g_param_spec_object ("application", NULL, NULL,
                                                        GDU_TYPE_APPLICATION,
                                                        G_PARAM_READABLE |
                                                        G_PARAM_WRITABLE |
                                                        G_PARAM_CONSTRUCT_ONLY |
                                                        G_PARAM_STATIC_STRINGS));

  /**
   * GduDeviceTreeModel:flags:
   *
   * The #GduApplication used by the #GduDeviceTreeModel instance.
   */
  g_object_class_install_property (gobject_class,
                                   PROP_FLAGS,
                                   g_param_spec_flags ("flags", NULL, NULL,
                                                       GDU_TYPE_DEVICE_TREE_MODEL_FLAGS,
                                                       GDU_DEVICE_TREE_MODEL_FLAGS_NONE,
                                                       G_PARAM_READABLE |
                                                       G_PARAM_WRITABLE |
                                                       G_PARAM_CONSTRUCT_ONLY |
                                                       G_PARAM_STATIC_STRINGS));
}

/**
 * gdu_device_tree_model_new:
 * @application: A #GduApplication.
 * @flags: Flags from #GduDeviceTreeModelFlags.
 *
 * Creates a new #GduDeviceTreeModel for viewing the devices belonging to
 * @application.
 *
 * Returns: A #GduDeviceTreeModel. Free with g_object_unref().
 */
GduDeviceTreeModel *
gdu_device_tree_model_new (GduApplication          *application,
                           GduDeviceTreeModelFlags  flags)
{
  return GDU_DEVICE_TREE_MODEL (g_object_new (GDU_TYPE_DEVICE_TREE_MODEL,
                                              "application", application,
                                              "flags", flags,
                                              NULL));
}

/**
 * gdu_device_tree_model_get_application:
 * @model: A #GduDeviceTreeModel.
 *
 * Gets the #GduApplication used by @model.
 *
 * Returns: (transfer none): A #GduApplication. Do not free, the
 * object belongs to @model.
 */
GduApplication *
gdu_device_tree_model_get_application (GduDeviceTreeModel *model)
{
  g_return_val_if_fail (GDU_IS_DEVICE_TREE_MODEL (model), NULL);
  return model->application;
}

/**
 * gdu_device_tree_model_get_flags:
 * @model: A #GduDeviceTreeModel.
 *
 * Gets the #GduDeviceTreeModelFlags used by @model.
 *
 * Returns: The flags that @model was constructed with.
 */
GduDeviceTreeModelFlags
gdu_device_tree_model_get_flags (GduDeviceTreeModel *model)
{
  g_return_val_if_fail (GDU_IS_DEVICE_TREE_MODEL (model), GDU_DEVICE_TREE_MODEL_FLAGS_NONE);
  return model->flags;
}

/* ---------------------------------------------------------------------------------------------------- */

static GtkTreeIter *
get_drive_header_iter (GduDeviceTreeModel *model)
{
  gchar *s;

  if (model->flags & GDU_DEVICE_TREE_MODEL_FLAGS_FLAT)
    return NULL;

  if (model->drive_iter_valid)
    goto out;

  s = g_strdup_printf ("<small>%s</small>",
                       _("Disk Drives"));
  gtk_tree_store_insert_with_values (GTK_TREE_STORE (model),
                                     &model->drive_iter,
                                     NULL, /* GtkTreeIter *parent */
                                     0,
                                     GDU_DEVICE_TREE_MODEL_COLUMN_IS_HEADING, TRUE,
                                     GDU_DEVICE_TREE_MODEL_COLUMN_HEADING_TEXT, s,
                                     GDU_DEVICE_TREE_MODEL_COLUMN_SORT_KEY, "00_drives_0",
                                     -1);
  g_free (s);

  model->drive_iter_valid = TRUE;

 out:
  return &model->drive_iter;
}

static void
nuke_drive_header (GduDeviceTreeModel *model)
{
  if (model->drive_iter_valid)
    {
      gtk_tree_store_remove (GTK_TREE_STORE (model), &model->drive_iter);
      model->drive_iter_valid = FALSE;
    }
}

static void
add_drive (GduDeviceTreeModel *model,
           UDisksObject       *object,
           GtkTreeIter        *parent)
{
  GtkTreeIter iter;
  gtk_tree_store_insert_with_values (GTK_TREE_STORE (model),
                                     &iter,
                                     parent,
                                     0,
                                     GDU_DEVICE_TREE_MODEL_COLUMN_OBJECT, object,
                                     -1);
}

static void
remove_drive (GduDeviceTreeModel *model,
              UDisksObject       *object)
{
  GtkTreeIter iter;

  if (!find_iter_for_object (model,
                             object,
                             &iter))
    {
      g_warning ("Error finding iter for object at %s",
                 g_dbus_object_get_object_path (G_DBUS_OBJECT (object)));
      goto out;
    }

  gtk_tree_store_remove (GTK_TREE_STORE (model), &iter);

 out:
  ;
}

/* ---------------------------------------------------------------------------------------------------- */

static GtkTreeIter *
get_mdraid_header_iter (GduDeviceTreeModel *model)
{
  gchar *s;

  if (model->flags & GDU_DEVICE_TREE_MODEL_FLAGS_FLAT)
    return NULL;

  if (model->mdraid_iter_valid)
    goto out;

  s = g_strdup_printf ("<small>%s</small>",
                       _("RAID Arrays"));
  gtk_tree_store_insert_with_values (GTK_TREE_STORE (model),
                                     &model->mdraid_iter,
                                     NULL, /* GtkTreeIter *parent */
                                     0,
                                     GDU_DEVICE_TREE_MODEL_COLUMN_IS_HEADING, TRUE,
                                     GDU_DEVICE_TREE_MODEL_COLUMN_HEADING_TEXT, s,
                                     GDU_DEVICE_TREE_MODEL_COLUMN_SORT_KEY, "01_mdraid_0",
                                     -1);
  g_free (s);

  model->mdraid_iter_valid = TRUE;

 out:
  return &model->mdraid_iter;
}

static void
nuke_mdraid_header (GduDeviceTreeModel *model)
{
  if (model->mdraid_iter_valid)
    {
      gtk_tree_store_remove (GTK_TREE_STORE (model), &model->mdraid_iter);
      model->mdraid_iter_valid = FALSE;
    }
}

static void
add_mdraid (GduDeviceTreeModel *model,
            UDisksObject       *object,
            GtkTreeIter        *parent)
{
  GtkTreeIter iter;
  gtk_tree_store_insert_with_values (GTK_TREE_STORE (model),
                                     &iter,
                                     parent,
                                     0,
                                     GDU_DEVICE_TREE_MODEL_COLUMN_OBJECT, object,
                                     -1);
}

static void
remove_mdraid (GduDeviceTreeModel *model,
               UDisksObject       *object)
{
  GtkTreeIter iter;

  if (!find_iter_for_object (model,
                             object,
                             &iter))
    {
      g_warning ("Error finding iter for object at %s",
                 g_dbus_object_get_object_path (G_DBUS_OBJECT (object)));
      goto out;
    }

  gtk_tree_store_remove (GTK_TREE_STORE (model), &iter);

 out:
  ;
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
object_has_jobs (GduDeviceTreeModel *model,
                 UDisksObject       *object)
{
  GList *jobs;
  gboolean ret;

  jobs = udisks_client_get_jobs_for_object (model->client, object);
  jobs = g_list_concat (jobs, gdu_application_get_local_jobs_for_object (model->application, object));
  ret = (jobs != NULL);
  g_list_foreach (jobs, (GFunc) g_object_unref, NULL);
  g_list_free (jobs);

  return ret;
}

static gboolean
iface_has_jobs (GduDeviceTreeModel *model,
                GDBusInterface     *iface)
{
  GDBusObject *object;
  object = g_dbus_interface_get_object (G_DBUS_INTERFACE (iface));
  if (object != NULL)
    return object_has_jobs (model, UDISKS_OBJECT (object));
  else
    return FALSE;
}

static gboolean
block_has_jobs (GduDeviceTreeModel *model,
                UDisksBlock        *block)
{
  gboolean ret = FALSE;
  GDBusObject *block_object;
  UDisksPartitionTable *part_table = NULL;
  UDisksEncrypted *encrypted = NULL;
  GList *partitions = NULL, *l;
  UDisksBlock *cleartext_block = NULL;

  if (iface_has_jobs (model, G_DBUS_INTERFACE (block)))
    {
      ret = TRUE;
      goto out;
    }

  block_object = g_dbus_interface_get_object (G_DBUS_INTERFACE (block));
  if (block_object == NULL)
    goto out;

  part_table = udisks_object_get_partition_table (UDISKS_OBJECT (block_object));
  if (part_table != NULL)
    {
      partitions = udisks_client_get_partitions (model->client, part_table);
      for (l = partitions; l != NULL; l = l->next)
        {
          UDisksPartition *partition = UDISKS_PARTITION (l->data);
          GDBusObject *partition_object;
          UDisksBlock *partition_block;

          partition_object = g_dbus_interface_get_object (G_DBUS_INTERFACE (partition));
          if (partition_object != NULL)
            {
              partition_block = udisks_object_get_block (UDISKS_OBJECT (partition_object));
              if (block_has_jobs (model, partition_block))
                {
                  ret = TRUE;
                  goto out;
                }
            }
        }
    }

  encrypted = udisks_object_get_encrypted (UDISKS_OBJECT (block_object));
  if (encrypted != NULL)
    {
      cleartext_block = udisks_client_get_cleartext_block (model->client, block);
      if (cleartext_block != NULL)
        {
          if (block_has_jobs (model, cleartext_block))
            {
              ret = TRUE;
              goto out;
            }
        }
    }

 out:
  g_clear_object (&part_table);
  g_clear_object (&encrypted);
  g_clear_object (&cleartext_block);
  g_list_foreach (partitions, (GFunc) g_object_unref, NULL);
  g_list_free (partitions);
  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
drive_has_jobs (GduDeviceTreeModel *model,
                UDisksDrive        *drive)
{
  gboolean ret = FALSE;
  UDisksBlock *block = NULL;

  if (iface_has_jobs (model, G_DBUS_INTERFACE (drive)))
    {
      ret = TRUE;
      goto out;
    }

  block = udisks_client_get_block_for_drive (model->client, drive, FALSE); /* get_physical */
  if (block_has_jobs (model, block))
    {
      ret = TRUE;
      goto out;
    }

  out:
  g_clear_object (&block);
  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
mdraid_has_jobs (GduDeviceTreeModel *model,
                 UDisksMDRaid       *mdraid)
{
  gboolean ret = FALSE;
  UDisksBlock *block = NULL;

  if (iface_has_jobs (model, G_DBUS_INTERFACE (mdraid)))
    {
      ret = TRUE;
      goto out;
    }

  block = udisks_client_get_block_for_mdraid (model->client, mdraid);
  if (block != NULL && block_has_jobs (model, block))
    {
      ret = TRUE;
      goto out;
    }

  out:
  g_clear_object (&block);
  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
on_spinner_timeout (gpointer user_data)
{
  GduDeviceTreeModel *model = GDU_DEVICE_TREE_MODEL (user_data);
  GList *l;
  gboolean keep_animating = FALSE;

  for (l = model->current_mdraids; l != NULL; l = l->next)
    {
      UDisksObject *object = UDISKS_OBJECT (l->data);
      if (update_mdraid (model, object, TRUE))
        keep_animating = TRUE;
    }

  for (l = model->current_drives; l != NULL; l = l->next)
    {
      UDisksObject *object = UDISKS_OBJECT (l->data);
      if (update_drive (model, object, TRUE))
        keep_animating = TRUE;
    }

  for (l = model->current_blocks; l != NULL; l = l->next)
    {
      UDisksObject *object = UDISKS_OBJECT (l->data);
      if (update_block (model, object, TRUE))
        keep_animating = TRUE;
    }


  if (keep_animating)
    {
      return TRUE; /* keep source */
    }
  else
    {
      model->spinner_timeout = 0;
      return FALSE; /* nuke source */
    }
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
update_drive (GduDeviceTreeModel *model,
              UDisksObject       *object,
              gboolean            from_timer)
{
  UDisksDrive *drive = NULL;
  UDisksDriveAta *ata = NULL;
  UDisksObjectInfo *info = NULL;
  UDisksBlock *block = NULL;
  gchar *s = NULL;
  gchar *included_device_name = NULL;
  gboolean warning = FALSE;
  gboolean jobs_running = FALSE;
  GtkTreeIter iter;
  guint pulse;
  guint64 size = 0;

  if (!find_iter_for_object (model,
                             object,
                             &iter))
    {
      g_warning ("Error finding iter for object at %s",
                 g_dbus_object_get_object_path (G_DBUS_OBJECT (object)));
      goto out;
    }

  drive = udisks_object_peek_drive (object);
  ata = udisks_object_peek_drive_ata (object);

  block = udisks_client_get_block_for_drive (model->client, drive, FALSE); /* get_physical */

  if (ata != NULL)
    {
      s = gdu_ata_smart_get_one_liner_assessment (ata, NULL /* out_smart_supported */, &warning);
      g_free (s);
    }

  if (model->flags & GDU_DEVICE_TREE_MODEL_FLAGS_INCLUDE_DEVICE_NAME)
    included_device_name = g_strdup_printf (" (%s)", udisks_block_get_preferred_device (block));

  info = udisks_client_get_object_info (model->client, object);
  if (warning)
    {
      /* TODO: once https://bugzilla.gnome.org/show_bug.cgi?id=657194 is resolved, use that instead
       * of hard-coding the color
       */
      s = g_strdup_printf ("<span foreground=\"#ff0000\">%s</span>"
                           "%s"
                           "<small><span foreground=\"#ff0000\">%s%s</span></small>",
                           udisks_object_info_get_description (info),
                           model->flags & GDU_DEVICE_TREE_MODEL_FLAGS_ONE_LINE_NAME ? " — " : "\n",
                           udisks_object_info_get_name (info),
                           included_device_name != NULL ? included_device_name : "");
    }
  else
    {
      s = g_strdup_printf ("%s"
                           "%s"
                           "<small>%s%s</small>",
                           udisks_object_info_get_description (info),
                           model->flags & GDU_DEVICE_TREE_MODEL_FLAGS_ONE_LINE_NAME ? " — " : "\n",
                           udisks_object_info_get_name (info),
                           included_device_name != NULL ? included_device_name : "");
    }

  jobs_running = drive_has_jobs (model, drive);

  size = udisks_drive_get_size (drive);

  gtk_tree_model_get (GTK_TREE_MODEL (model),
                      &iter,
                      GDU_DEVICE_TREE_MODEL_COLUMN_PULSE, &pulse,
                      -1);
  if (from_timer)
    pulse += 1;

  gtk_tree_store_set (GTK_TREE_STORE (model),
                      &iter,
                      GDU_DEVICE_TREE_MODEL_COLUMN_ICON, udisks_object_info_get_icon (info),
                      GDU_DEVICE_TREE_MODEL_COLUMN_NAME, s,
                      GDU_DEVICE_TREE_MODEL_COLUMN_SORT_KEY, udisks_object_info_get_sort_key (info),
                      GDU_DEVICE_TREE_MODEL_COLUMN_WARNING, warning,
                      GDU_DEVICE_TREE_MODEL_COLUMN_JOBS_RUNNING, jobs_running,
                      GDU_DEVICE_TREE_MODEL_COLUMN_PULSE, pulse,
                      GDU_DEVICE_TREE_MODEL_COLUMN_SIZE, size,
                      GDU_DEVICE_TREE_MODEL_COLUMN_BLOCK, block,
                      -1);

  /* update spinner, if jobs are running */
  if (jobs_running && (model->flags & GDU_DEVICE_TREE_MODEL_FLAGS_UPDATE_PULSE))
    {
      if (model->spinner_timeout == 0)
        {
          model->spinner_timeout = g_timeout_add (SPINNER_TIMEOUT_MSEC, on_spinner_timeout, model);
        }
    }

 out:
  g_clear_object (&block);
  g_clear_object (&info);
  g_free (s);
  g_free (included_device_name);
  return jobs_running;
}

static void
update_drives (GduDeviceTreeModel *model)
{
  GDBusObjectManager *object_manager;
  GList *objects;
  GList *drives;
  GList *added_drives;
  GList *removed_drives;
  GList *l;

  object_manager = udisks_client_get_object_manager (model->client);
  objects = g_dbus_object_manager_get_objects (object_manager);

  drives = NULL;
  for (l = objects; l != NULL; l = l->next)
    {
      UDisksObject *object = UDISKS_OBJECT (l->data);
      UDisksDrive *drive;

      drive = udisks_object_peek_drive (object);
      if (drive == NULL)
        continue;

      drives = g_list_prepend (drives, g_object_ref (object));
    }

  drives = g_list_sort (drives, (GCompareFunc) _g_dbus_object_compare);
  model->current_drives = g_list_sort (model->current_drives, (GCompareFunc) _g_dbus_object_compare);
  diff_sorted_lists (model->current_drives,
                     drives,
                     (GCompareFunc) _g_dbus_object_compare,
                     &added_drives,
                     &removed_drives);

  for (l = removed_drives; l != NULL; l = l->next)
    {
      UDisksObject *object = UDISKS_OBJECT (l->data);
      g_assert (g_list_find (model->current_drives, object) != NULL);
      model->current_drives = g_list_remove (model->current_drives, object);
      remove_drive (model, object);
      g_object_unref (object);
    }
  for (l = added_drives; l != NULL; l = l->next)
    {
      UDisksObject *object = UDISKS_OBJECT (l->data);
      model->current_drives = g_list_prepend (model->current_drives, g_object_ref (object));
      add_drive (model, object, get_drive_header_iter (model));
    }

  for (l = model->current_drives; l != NULL; l = l->next)
    {
      UDisksObject *object = UDISKS_OBJECT (l->data);
      update_drive (model, object, FALSE);
    }

  if (g_list_length (model->current_drives) == 0)
    nuke_drive_header (model);

  g_list_free (added_drives);
  g_list_free (removed_drives);
  g_list_foreach (drives, (GFunc) g_object_unref, NULL);
  g_list_free (drives);

  g_list_foreach (objects, (GFunc) g_object_unref, NULL);
  g_list_free (objects);
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
update_mdraid (GduDeviceTreeModel *model,
               UDisksObject       *object,
               gboolean            from_timer)
{
  UDisksObjectInfo *info = NULL;
  UDisksMDRaid *mdraid = NULL;
  UDisksBlock *block = NULL;
  const gchar *name;
  gchar *desc = NULL;
  gchar *desc2 = NULL;
  gchar *s = NULL;
  gboolean warning = FALSE;
  gboolean jobs_running = FALSE;
  GtkTreeIter iter;
  guint pulse;
  guint64 size = 0;

  if (!find_iter_for_object (model,
                             object,
                             &iter))
    {
      g_warning ("Error finding iter for object at %s",
                 g_dbus_object_get_object_path (G_DBUS_OBJECT (object)));
      goto out;
    }

  mdraid = udisks_object_peek_mdraid (object);
  block = udisks_client_get_block_for_mdraid (model->client, mdraid);
  info = udisks_client_get_object_info (model->client, object);

  name = udisks_mdraid_get_name (mdraid);
  /* skip homehost, if any */
  s = strstr (name, ":");
  if (s != NULL)
    {
      name = s + 1;
      s = NULL;
    }

  size = udisks_mdraid_get_size (mdraid);
  if (size > 0)
    {
      s = udisks_client_get_size_for_display (model->client, size, FALSE, FALSE);
      /* Translators: Used in the device tree for a RAID Array, the first %s is the size */
      desc = g_strdup_printf (C_("md-raid-tree-primary", "%s RAID Array"), s);
      g_free (s);
    }
  else
    {
      /* Translators: Used in the device tree for a RAID Array where the size is not known  */
      desc = g_strdup (C_("md-raid-tree-primary", "RAID Array"));
    }


  if (name != NULL && strlen (name) > 0)
    {
      s = gdu_utils_format_mdraid_level (udisks_mdraid_get_level (mdraid), FALSE, FALSE);
      /* Translators: Used as a secondary line in device tree for RAID Array.
       *              The first %s is the name of the array (e.g. "My RAID Array").
       *              The second %s is the RAID level (e.g. "RAID-5").
       */
      desc2 = g_strdup_printf (C_("md-raid-tree-secondary", "%s (%s)"), name, s);
      g_free (s);
    }
  else
    {
      desc2 = gdu_utils_format_mdraid_level (udisks_mdraid_get_level (mdraid), FALSE, FALSE);
    }

  if (udisks_mdraid_get_degraded (mdraid) > 0)
    warning = TRUE;

  if (warning)
    {
      /* TODO: once https://bugzilla.gnome.org/show_bug.cgi?id=657194 is resolved, use that instead
       * of hard-coding the color
       */
      s = g_strdup_printf ("<span foreground=\"#ff0000\">%s</span>"
                           "%s"
                           "<small><span foreground=\"#ff0000\">%s</span></small>",
                           desc,
                           model->flags & GDU_DEVICE_TREE_MODEL_FLAGS_ONE_LINE_NAME ? " — " : "\n",
                           desc2);
    }
  else
    {
      s = g_strdup_printf ("%s"
                           "%s"
                           "<small>%s</small>",
                           desc,
                           model->flags & GDU_DEVICE_TREE_MODEL_FLAGS_ONE_LINE_NAME ? " — " : "\n",
                           desc2);
    }

  if (block != NULL)
    size = udisks_block_get_size (block);

  jobs_running = mdraid_has_jobs (model, mdraid);

  /* also show the spinner if a sync op is in progress */
  if (udisks_mdraid_get_sync_completed (mdraid) > 0.0)
    jobs_running = TRUE;

  gtk_tree_model_get (GTK_TREE_MODEL (model),
                      &iter,
                      GDU_DEVICE_TREE_MODEL_COLUMN_PULSE, &pulse,
                      -1);
  if (from_timer)
    pulse += 1;

  gtk_tree_store_set (GTK_TREE_STORE (model),
                      &iter,
                      GDU_DEVICE_TREE_MODEL_COLUMN_ICON, udisks_object_info_get_icon (info),
                      GDU_DEVICE_TREE_MODEL_COLUMN_NAME, s,
                      GDU_DEVICE_TREE_MODEL_COLUMN_SORT_KEY, udisks_object_info_get_sort_key (info),
                      GDU_DEVICE_TREE_MODEL_COLUMN_WARNING, warning,
                      GDU_DEVICE_TREE_MODEL_COLUMN_JOBS_RUNNING, jobs_running,
                      GDU_DEVICE_TREE_MODEL_COLUMN_PULSE, pulse,
                      GDU_DEVICE_TREE_MODEL_COLUMN_SIZE, size,
                      GDU_DEVICE_TREE_MODEL_COLUMN_BLOCK, block,
                      -1);

  /* update spinner, if jobs are running */
  if (jobs_running && (model->flags & GDU_DEVICE_TREE_MODEL_FLAGS_UPDATE_PULSE))
    {
      if (model->spinner_timeout == 0)
        {
          model->spinner_timeout = g_timeout_add (SPINNER_TIMEOUT_MSEC, on_spinner_timeout, model);
        }
    }

 out:
  g_clear_object (&info);
  g_free (s);
  g_free (desc);
  g_free (desc2);
  g_clear_object (&block);
  return jobs_running;
}

static void
update_mdraids (GduDeviceTreeModel *model)
{
  GDBusObjectManager *object_manager;
  GList *objects;
  GList *mdraids;
  GList *added_mdraids;
  GList *removed_mdraids;
  GList *l;

  object_manager = udisks_client_get_object_manager (model->client);
  objects = g_dbus_object_manager_get_objects (object_manager);

  mdraids = NULL;
  for (l = objects; l != NULL; l = l->next)
    {
      UDisksObject *object = UDISKS_OBJECT (l->data);
      UDisksMDRaid *mdraid;

      mdraid = udisks_object_peek_mdraid (object);
      if (mdraid == NULL)
        continue;

      mdraids = g_list_prepend (mdraids, g_object_ref (object));
    }

  mdraids = g_list_sort (mdraids, (GCompareFunc) _g_dbus_object_compare);
  model->current_mdraids = g_list_sort (model->current_mdraids, (GCompareFunc) _g_dbus_object_compare);
  diff_sorted_lists (model->current_mdraids,
                     mdraids,
                     (GCompareFunc) _g_dbus_object_compare,
                     &added_mdraids,
                     &removed_mdraids);

  for (l = removed_mdraids; l != NULL; l = l->next)
    {
      UDisksObject *object = UDISKS_OBJECT (l->data);
      g_assert (g_list_find (model->current_mdraids, object) != NULL);
      model->current_mdraids = g_list_remove (model->current_mdraids, object);
      remove_mdraid (model, object);
      g_object_unref (object);
    }
  for (l = added_mdraids; l != NULL; l = l->next)
    {
      UDisksObject *object = UDISKS_OBJECT (l->data);
      model->current_mdraids = g_list_prepend (model->current_mdraids, g_object_ref (object));
      add_mdraid (model, object, get_mdraid_header_iter (model));
    }

  for (l = model->current_mdraids; l != NULL; l = l->next)
    {
      UDisksObject *object = UDISKS_OBJECT (l->data);
      update_mdraid (model, object, FALSE);
    }

  if (g_list_length (model->current_mdraids) == 0)
    nuke_mdraid_header (model);

  g_list_free (added_mdraids);
  g_list_free (removed_mdraids);
  g_list_foreach (mdraids, (GFunc) g_object_unref, NULL);
  g_list_free (mdraids);

  g_list_foreach (objects, (GFunc) g_object_unref, NULL);
  g_list_free (objects);
}

/* ---------------------------------------------------------------------------------------------------- */

static GtkTreeIter *
get_block_header_iter (GduDeviceTreeModel *model)
{
  gchar *s;

  if (model->flags & GDU_DEVICE_TREE_MODEL_FLAGS_FLAT)
    return NULL;

  if (model->block_iter_valid)
    goto out;

  s = g_strdup_printf ("<small>%s</small>",
                       _("Other Devices"));
  gtk_tree_store_insert_with_values (GTK_TREE_STORE (model),
                                     &model->block_iter,
                                     NULL, /* GtkTreeIter *parent */
                                     0,
                                     GDU_DEVICE_TREE_MODEL_COLUMN_IS_HEADING, TRUE,
                                     GDU_DEVICE_TREE_MODEL_COLUMN_HEADING_TEXT, s,
                                     GDU_DEVICE_TREE_MODEL_COLUMN_SORT_KEY, "02_block_0",
                                     -1);
  g_free (s);

  model->block_iter_valid = TRUE;

 out:
  return &model->block_iter;
}

static void
nuke_block_header (GduDeviceTreeModel *model)
{
  if (model->block_iter_valid)
    {
      gtk_tree_store_remove (GTK_TREE_STORE (model), &model->block_iter);
      model->block_iter_valid = FALSE;
    }
}

static void
add_block (GduDeviceTreeModel  *model,
           UDisksObject        *object,
           GtkTreeIter         *parent)
{
  GtkTreeIter iter;
  gtk_tree_store_insert_with_values (GTK_TREE_STORE (model),
                                     &iter,
                                     parent,
                                     0,
                                     GDU_DEVICE_TREE_MODEL_COLUMN_OBJECT, object,
                                     -1);
}

static void
remove_block (GduDeviceTreeModel  *model,
              UDisksObject        *object)
{
  GtkTreeIter iter;

  if (!find_iter_for_object (model,
                             object,
                             &iter))
    {
      g_warning ("Error finding iter for object at %s",
                 g_dbus_object_get_object_path (G_DBUS_OBJECT (object)));
      goto out;
    }

  gtk_tree_store_remove (GTK_TREE_STORE (model), &iter);

 out:
  ;
}

static gboolean
update_block (GduDeviceTreeModel  *model,
              UDisksObject        *object,
              gboolean             from_timer)
{
  GtkTreeIter iter;
  UDisksBlock *block;
  UDisksLoop *loop;
  UDisksObjectInfo *info = NULL;
  gchar *s = NULL;
  const gchar *preferred_device;
  const gchar *loop_backing_file;
  guint64 size;
  gchar *size_str = NULL;
  gboolean jobs_running = FALSE;
  guint pulse;

  if (!find_iter_for_object (model,
                             object,
                             &iter))
    {
      g_warning ("Error finding iter for object at %s",
                 g_dbus_object_get_object_path (G_DBUS_OBJECT (object)));
      goto out;
    }

  block = udisks_object_peek_block (object);
  loop = udisks_object_peek_loop (object);

  size = udisks_block_get_size (block);
  size_str = udisks_client_get_size_for_display (model->client, size, FALSE, FALSE);

  info = udisks_client_get_object_info (model->client, object);

  preferred_device = udisks_block_get_preferred_device (block);
  loop_backing_file = loop != NULL ? udisks_loop_get_backing_file (loop) : NULL;
  if (loop_backing_file != NULL)
    {
      gchar *backing_file_unfused;
      backing_file_unfused = gdu_utils_unfuse_path (loop_backing_file);
      s = g_strdup_printf ("%s"
                           "%s"
                           "<small>%s</small>",
                           udisks_object_info_get_description (info),
                           model->flags & GDU_DEVICE_TREE_MODEL_FLAGS_ONE_LINE_NAME ? " — " : "\n",
                           backing_file_unfused);
      g_free (backing_file_unfused);
    }
  else
    {
      s = g_strdup_printf ("%s"
                           "%s"
                           "<small>%s</small>",
                           udisks_object_info_get_description (info),
                           model->flags & GDU_DEVICE_TREE_MODEL_FLAGS_ONE_LINE_NAME ? " — " : "\n",
                           preferred_device);
    }

  jobs_running = block_has_jobs (model, block);

  gtk_tree_model_get (GTK_TREE_MODEL (model),
                      &iter,
                      GDU_DEVICE_TREE_MODEL_COLUMN_PULSE, &pulse,
                      -1);
  if (from_timer)
    pulse += 1;

  gtk_tree_store_set (GTK_TREE_STORE (model),
                      &iter,
                      GDU_DEVICE_TREE_MODEL_COLUMN_ICON, udisks_object_info_get_icon (info),
                      GDU_DEVICE_TREE_MODEL_COLUMN_NAME, s,
                      GDU_DEVICE_TREE_MODEL_COLUMN_SORT_KEY, udisks_object_info_get_sort_key (info),
                      GDU_DEVICE_TREE_MODEL_COLUMN_JOBS_RUNNING, jobs_running,
                      GDU_DEVICE_TREE_MODEL_COLUMN_PULSE, pulse,
                      GDU_DEVICE_TREE_MODEL_COLUMN_SIZE, size,
                      GDU_DEVICE_TREE_MODEL_COLUMN_BLOCK, block,
                      -1);

  /* update spinner, if jobs are running */
  if (jobs_running && (model->flags & GDU_DEVICE_TREE_MODEL_FLAGS_UPDATE_PULSE))
    {
      if (model->spinner_timeout == 0)
        {
          model->spinner_timeout = g_timeout_add (SPINNER_TIMEOUT_MSEC, on_spinner_timeout, model);
        }
    }

 out:
  g_clear_object (&info);
  g_free (s);
  g_free (size_str);
  return jobs_running;
}

static gboolean
should_include_block (UDisksObject *object)
{
  UDisksBlock *block;
  UDisksPartition *partition;
  UDisksLoop *loop;
  gboolean ret;
  const gchar *device;
  const gchar *drive;
  const gchar *crypto_backing_device;
  guint64 size;

  ret = FALSE;

  block = udisks_object_peek_block (object);
  partition = udisks_object_peek_partition (object);
  loop = udisks_object_peek_loop (object);

  /* RAM devices are useless */
  device = udisks_block_get_device (block);
  if (g_str_has_prefix (device, "/dev/ram"))
    goto out;

  /* MD-RAID devices (e.g. /dev/md0) with an associated org.fd.UDisks.MDRaid object are
   * shown in their own section so don't show them here
   */
  if (g_strcmp0 (udisks_block_get_mdraid (block), "/") != 0)
    goto out;

  /* Don't show loop devices of size zero - they're unused.
   *
   * Do show any other block device of size 0.
   *
   * Note that we _do_ want to show any other device of size 0 (for
   * exampleinactive MD-RAID devices) since that's a good hint that
   * the system is misconfigured and attention is needed.
   */
  size = udisks_block_get_size (block);
  if (size == 0 && loop != NULL)
    goto out;

  /* Only include devices if they are top-level */
  if (partition != NULL)
    goto out;

  /* Don't include if already shown in "Direct-Attached devices" */
  drive = udisks_block_get_drive (block);
  if (g_strcmp0 (drive, "/") != 0)
    goto out;

  /* Don't include if already shown in volume grid as an unlocked device */
  crypto_backing_device = udisks_block_get_crypto_backing_device (block);
  if (g_strcmp0 (crypto_backing_device, "/") != 0)
    goto out;

  ret = TRUE;

 out:
  return ret;
}

static void
update_blocks (GduDeviceTreeModel *model)
{
  GDBusObjectManager *object_manager;
  GList *objects;
  GList *blocks;
  GList *added_blocks;
  GList *removed_blocks;
  GList *l;

  object_manager = udisks_client_get_object_manager (model->client);
  objects = g_dbus_object_manager_get_objects (object_manager);

  blocks = NULL;
  for (l = objects; l != NULL; l = l->next)
    {
      UDisksObject *object = UDISKS_OBJECT (l->data);
      UDisksBlock *block;

      block = udisks_object_peek_block (object);
      if (block == NULL)
        continue;

      if (should_include_block (object))
        blocks = g_list_prepend (blocks, g_object_ref (object));
    }

  blocks = g_list_sort (blocks, (GCompareFunc) _g_dbus_object_compare);
  model->current_blocks = g_list_sort (model->current_blocks, (GCompareFunc) _g_dbus_object_compare);
  diff_sorted_lists (model->current_blocks,
                     blocks,
                     (GCompareFunc) _g_dbus_object_compare,
                     &added_blocks,
                     &removed_blocks);

  for (l = removed_blocks; l != NULL; l = l->next)
    {
      UDisksObject *object = UDISKS_OBJECT (l->data);

      g_assert (g_list_find (model->current_blocks, object) != NULL);

      model->current_blocks = g_list_remove (model->current_blocks, object);
      remove_block (model, object);
      g_object_unref (object);
    }
  for (l = added_blocks; l != NULL; l = l->next)
    {
      UDisksObject *object = UDISKS_OBJECT (l->data);
      model->current_blocks = g_list_prepend (model->current_blocks, g_object_ref (object));
      add_block (model, object, get_block_header_iter (model));
    }

  for (l = model->current_blocks; l != NULL; l = l->next)
    {
      UDisksObject *object = UDISKS_OBJECT (l->data);
      update_block (model, object, FALSE);
    }

  if (g_list_length (model->current_blocks) == 0)
    nuke_block_header (model);

  g_list_free (added_blocks);
  g_list_free (removed_blocks);
  g_list_foreach (blocks, (GFunc) g_object_unref, NULL);
  g_list_free (blocks);

  g_list_foreach (objects, (GFunc) g_object_unref, NULL);
  g_list_free (objects);
}

/* ---------------------------------------------------------------------------------------------------- */

static void
update_all (GduDeviceTreeModel *model)
{
  /* TODO: if this is CPU intensive we could coalesce all updates / schedule timeouts */
  update_mdraids (model);
  update_drives (model);
  update_blocks (model);
}

static void
coldplug (GduDeviceTreeModel *model)
{
  update_all (model);
}

static void
on_client_changed (UDisksClient  *client,
                   gpointer       user_data)
{
  GduDeviceTreeModel *model = GDU_DEVICE_TREE_MODEL (user_data);
  update_all (model);
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
clear_selected_cb (GtkTreeModel  *model,
                   GtkTreePath   *path,
                   GtkTreeIter   *iter,
                   gpointer       user_data)
{
  gtk_tree_store_set (GTK_TREE_STORE (model),
                      iter,
                      GDU_DEVICE_TREE_MODEL_COLUMN_SELECTED, FALSE,
                      -1);
  return FALSE; /* keep iterating */
}

void
gdu_device_tree_model_clear_selected (GduDeviceTreeModel *model)
{
  gtk_tree_model_foreach (GTK_TREE_MODEL (model), clear_selected_cb, NULL);
}

void
gdu_device_tree_model_toggle_selected (GduDeviceTreeModel *model,
                                       GtkTreeIter        *iter)
{
  gboolean selected = FALSE;

  g_return_if_fail (GDU_IS_DEVICE_TREE_MODEL (model));

  gtk_tree_model_get (GTK_TREE_MODEL (model),
                      iter,
                      GDU_DEVICE_TREE_MODEL_COLUMN_SELECTED, &selected,
                      -1);
  gtk_tree_store_set (GTK_TREE_STORE (model),
                      iter,
                      GDU_DEVICE_TREE_MODEL_COLUMN_SELECTED, !selected,
                      -1);
}

/* ---------------------------------------------------------------------------------------------------- */

/* It's pretty expensive to compute the UDisksObjectInfo on _each_
 * comparison when sorting ... so we use a simple memoization
 * technique
 */

static void
sort_begin (GduDeviceTreeModel *model)
{
  g_assert (model->sort_mz == NULL);
  model->sort_mz = g_hash_table_new_full (g_direct_hash,
                                          g_direct_equal,
                                          g_object_unref,
                                          g_object_unref);
}

static UDisksObjectInfo *
sort_get_object_info (GduDeviceTreeModel *model,
                      UDisksObject       *object)
{
  UDisksObjectInfo *ret;

  g_assert (model->sort_mz != NULL);

  ret = g_hash_table_lookup (model->sort_mz, object);
  if (ret != NULL)
    {
      g_object_ref (ret);
      goto out;
    }

  ret = udisks_client_get_object_info (model->client, object);
  g_hash_table_insert (model->sort_mz, g_object_ref (object), g_object_ref (ret));

 out:
  return ret;
}

static void
sort_end (GduDeviceTreeModel *model)
{
  g_assert (model->sort_mz != NULL);
  g_hash_table_destroy (model->sort_mz);
  model->sort_mz = NULL;
}

static gint
sort_func (gconstpointer a,
           gconstpointer b,
           gpointer      user_data,
           gboolean      is_block)
{
  GduDeviceTreeModel *model = GDU_DEVICE_TREE_MODEL (user_data);
  UDisksObjectInfo *ia = NULL, *ib = NULL;
  gint ret;

  if (is_block)
    {
      UDisksObject *oa, *ob;
      oa = (UDisksObject *) g_dbus_interface_get_object (G_DBUS_INTERFACE (UDISKS_BLOCK (a)));
      ob = (UDisksObject *) g_dbus_interface_get_object (G_DBUS_INTERFACE (UDISKS_BLOCK (b)));
      if (oa != NULL)
        ia = sort_get_object_info (model, oa);
      if (ob != NULL)
        ib = sort_get_object_info (model, ob);
    }
  else
    {
      ia = sort_get_object_info (model, UDISKS_OBJECT (a));
      ib = sort_get_object_info (model, UDISKS_OBJECT (b));
    }

  ret = g_strcmp0 (ia != NULL ? udisks_object_info_get_sort_key (ia) : NULL,
                   ib != NULL ? udisks_object_info_get_sort_key (ib) : NULL);

  g_clear_object (&ib);
  g_clear_object (&ia);
  return ret;
}

static gint
sort_func_object (gconstpointer a,
                  gconstpointer b,
                  gpointer      user_data)
{
  return sort_func (a, b, user_data, FALSE);
}

static gint
sort_func_block (gconstpointer a,
                 gconstpointer b,
                 gpointer      user_data)
{
  return sort_func (a, b, user_data, TRUE);
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
get_selected_cb (GtkTreeModel  *model,
                 GtkTreePath   *path,
                 GtkTreeIter   *iter,
                 gpointer       user_data)
{
  UDisksObject *object = NULL;
  gboolean selected = FALSE;
  GList **ret = user_data;

  gtk_tree_model_get (model,
                      iter,
                      GDU_DEVICE_TREE_MODEL_COLUMN_OBJECT, &object,
                      GDU_DEVICE_TREE_MODEL_COLUMN_SELECTED, &selected,
                      -1);

  if (selected)
    {
      *ret = g_list_prepend (*ret, object); /* adopts ownership of @object */
    }
  else
    {
      g_clear_object (&object);
    }

  return FALSE; /* keep iterating */
}

GList *
gdu_device_tree_model_get_selected (GduDeviceTreeModel *model)
{
  GList *ret = NULL;

  g_return_val_if_fail (GDU_IS_DEVICE_TREE_MODEL (model), NULL);

  gtk_tree_model_foreach (GTK_TREE_MODEL (model), get_selected_cb, &ret);

  sort_begin (model);
  ret = g_list_sort_with_data (ret, sort_func_object, model);
  sort_end (model);

  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
get_selected_blocks_cb (GtkTreeModel  *model,
                        GtkTreePath   *path,
                        GtkTreeIter   *iter,
                        gpointer       user_data)
{
  UDisksBlock *block = NULL;
  gboolean selected = FALSE;
  GList **ret = user_data;

  gtk_tree_model_get (model,
                      iter,
                      GDU_DEVICE_TREE_MODEL_COLUMN_BLOCK, &block,
                      GDU_DEVICE_TREE_MODEL_COLUMN_SELECTED, &selected,
                      -1);

  if (selected && block != NULL)
    {
      *ret = g_list_prepend (*ret, block); /* adopts ownership of @block */
    }
  else
    {
      g_clear_object (&block);
    }

  return FALSE; /* keep iterating */
}

GList *
gdu_device_tree_model_get_selected_blocks (GduDeviceTreeModel *model)
{
  GList *ret = NULL;

  g_return_val_if_fail (GDU_IS_DEVICE_TREE_MODEL (model), NULL);

  gtk_tree_model_foreach (GTK_TREE_MODEL (model), get_selected_blocks_cb, &ret);

  sort_begin (model);
  ret = g_list_sort_with_data (ret, sort_func_block, model);
  sort_end (model);

  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */
