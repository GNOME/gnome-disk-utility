/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*-
 *
 * Copyright (C) 2008-2012 Red Hat, Inc.
 *
 * Licensed under GPL version 2 or later.
 *
 * Author: David Zeuthen <zeuthen@gmail.com>
 */

#include "config.h"

#include <glib/gi18n.h>

#include "gduapplication.h"
#include "gduwindow.h"
#include "gducreatepartitiondialog.h"
#include "gducreatefilesystemwidget.h"

/* ---------------------------------------------------------------------------------------------------- */


typedef struct
{
  GduWindow *window;
  UDisksObject *object;
  UDisksBlock *block;
  UDisksPartitionTable *table;
  UDisksDrive *drive;
  guint64 offset;
  guint64 max_size;

  GtkBuilder *builder;
  GtkWidget *dialog;
  GtkWidget *infobar_vbox;
  GtkWidget *dos_error_infobar;
  GtkWidget *dos_warning_infobar;
  GtkWidget *size_spinbutton;
  GtkAdjustment *size_adjustment;
  GtkAdjustment *free_following_adjustment;

  GtkWidget *contents_box;
  GtkWidget *create_filesystem_widget;
} CreatePartitionData;

static void
create_partition_data_free (CreatePartitionData *data)
{
  g_object_unref (data->window);
  g_object_unref (data->object);
  g_object_unref (data->block);
  g_object_unref (data->table);
  g_clear_object (&data->drive);
  if (data->dialog != NULL)
    {
      gtk_widget_hide (data->dialog);
      gtk_widget_destroy (data->dialog);
    }
  if (data->builder != NULL)
    g_object_unref (data->builder);
  g_free (data);
}

static guint
count_primary_dos_partitions (CreatePartitionData *data)
{
  GList *partitions, *l;
  guint ret = 0;
  partitions = udisks_client_get_partitions (gdu_window_get_client (data->window), data->table);
  for (l = partitions; l != NULL; l = l->next)
    {
      UDisksPartition *partition = UDISKS_PARTITION (l->data);
      if (!udisks_partition_get_is_contained (partition))
        ret += 1;
    }
  g_list_foreach (partitions, (GFunc) g_object_unref, NULL);
  g_list_free (partitions);
  return ret;
}

static gboolean
have_dos_extended (CreatePartitionData *data)
{
  GList *partitions, *l;
  gboolean ret = FALSE;
  partitions = udisks_client_get_partitions (gdu_window_get_client (data->window), data->table);
  for (l = partitions; l != NULL; l = l->next)
    {
      UDisksPartition *partition = UDISKS_PARTITION (l->data);
      if (udisks_partition_get_is_container (partition))
        {
          ret = TRUE;
          break;
        }
    }
  g_list_foreach (partitions, (GFunc) g_object_unref, NULL);
  g_list_free (partitions);
  return ret;
}

static gboolean
is_inside_dos_extended (CreatePartitionData *data, guint64 offset)
{
  GList *partitions, *l;
  gboolean ret = FALSE;
  partitions = udisks_client_get_partitions (gdu_window_get_client (data->window), data->table);
  for (l = partitions; l != NULL; l = l->next)
    {
      UDisksPartition *partition = UDISKS_PARTITION (l->data);
      if (udisks_partition_get_is_container (partition))
        {
          if (offset >= udisks_partition_get_offset (partition) &&
              offset < udisks_partition_get_offset (partition) + udisks_partition_get_size (partition))
            {
              ret = TRUE;
              break;
            }
        }
    }
  g_list_foreach (partitions, (GFunc) g_object_unref, NULL);
  g_list_free (partitions);
  return ret;
}

static void
create_partition_update (CreatePartitionData *data)
{
  gboolean can_proceed = FALSE;
  gboolean show_dos_error = FALSE;
  gboolean show_dos_warning = FALSE;

  /* MBR Partitioning sucks. So if we're trying to create a primary partition, then
   *
   *  - Show WARNING if trying to create 4th primary partition
   *  - Show ERROR if there are already 4 primary partitions
   */
  if (g_strcmp0 (udisks_partition_table_get_type_ (data->table), "dos") == 0)
    {
      if (!is_inside_dos_extended (data, data->offset))
        {
          guint num_primary;
          num_primary = count_primary_dos_partitions (data);
          if (num_primary == 4)
            show_dos_error = TRUE;
          else if (num_primary == 3)
            show_dos_warning = TRUE;
        }
    }

  if (gtk_adjustment_get_value (data->size_adjustment) > 0 &&
      gdu_create_filesystem_widget_get_has_info (GDU_CREATE_FILESYSTEM_WIDGET (data->create_filesystem_widget)))
    can_proceed = TRUE;

  if (show_dos_error)
    can_proceed = FALSE;

  if (!show_dos_warning)
    gtk_widget_set_no_show_all (data->dos_warning_infobar, TRUE);
  if (!show_dos_error)
    gtk_widget_set_no_show_all (data->dos_error_infobar, TRUE);
  gtk_dialog_set_response_sensitive (GTK_DIALOG (data->dialog), GTK_RESPONSE_OK, can_proceed);
}

static void
create_partition_property_changed (GObject     *object,
                                   GParamSpec  *pspec,
                                   gpointer     user_data)
{
  CreatePartitionData *data = user_data;
  create_partition_update (data);
}

static void
create_partition_populate (CreatePartitionData *data)
{
}

static gboolean
size_binding_func (GBinding     *binding,
                   const GValue *source_value,
                   GValue       *target_value,
                   gpointer      user_data)
{
  CreatePartitionData *data = user_data;
  g_value_set_double (target_value, data->max_size / (1000*1000) - g_value_get_double (source_value));
  return TRUE;
}

static void
format_cb (GObject      *source_object,
           GAsyncResult *res,
           gpointer      user_data)
{
  CreatePartitionData *data = user_data;
  GError *error;

  error = NULL;
  if (!udisks_block_call_format_finish (UDISKS_BLOCK (source_object),
                                        res,
                                        &error))
    {
      gdu_utils_show_error (GTK_WINDOW (data->window), _("Error formatting partition"), error);
      g_error_free (error);
    }
  create_partition_data_free (data);
}

static void
create_partition_cb (GObject      *source_object,
                     GAsyncResult *res,
                     gpointer      user_data)
{
  CreatePartitionData *data = user_data;
  GError *error;
  gchar *created_partition_object_path = NULL;
  UDisksObject *partition_object = NULL;
  UDisksBlock *partition_block;
  GVariantBuilder options_builder;
  const gchar *erase;
  const gchar *fstype;
  const gchar *name;
  const gchar *passphrase;

  error = NULL;
  if (!udisks_partition_table_call_create_partition_finish (UDISKS_PARTITION_TABLE (source_object),
                                                            &created_partition_object_path,
                                                            res,
                                                            &error))
    {
      gdu_utils_show_error (GTK_WINDOW (data->window), _("Error creating partition"), error);
      g_error_free (error);
      create_partition_data_free (data);
      goto out;
    }

  udisks_client_settle (gdu_window_get_client (data->window));

  partition_object = udisks_client_get_object (gdu_window_get_client (data->window), created_partition_object_path);
  gdu_window_select_object (data->window, partition_object);

  /* OK, cool, now format the created partition */
  partition_block = udisks_object_peek_block (partition_object);
  if (partition_block == NULL)
    {
      g_warning ("Created partition has no block interface");
      create_partition_data_free (data);
      goto out;
    }

  erase = gdu_create_filesystem_widget_get_erase (GDU_CREATE_FILESYSTEM_WIDGET (data->create_filesystem_widget));
  fstype = gdu_create_filesystem_widget_get_fstype (GDU_CREATE_FILESYSTEM_WIDGET (data->create_filesystem_widget));
  name = gdu_create_filesystem_widget_get_name (GDU_CREATE_FILESYSTEM_WIDGET (data->create_filesystem_widget));
  passphrase = gdu_create_filesystem_widget_get_passphrase (GDU_CREATE_FILESYSTEM_WIDGET (data->create_filesystem_widget));

  /* Not meaningful to create a filesystem if requested to create an extended partition */
  if (g_strcmp0 (fstype, "dos_extended") == 0)
    {
      create_partition_data_free (data);
    }
  else
    {
      g_variant_builder_init (&options_builder, G_VARIANT_TYPE_VARDICT);
      if (name != NULL && strlen (name) > 0)
        g_variant_builder_add (&options_builder, "{sv}", "label", g_variant_new_string (name));
      if (!(g_strcmp0 (fstype, "vfat") == 0 || g_strcmp0 (fstype, "ntfs") == 0))
        {
          /* TODO: need a better way to determine if this should be TRUE */
          g_variant_builder_add (&options_builder, "{sv}", "take-ownership", g_variant_new_boolean (TRUE));
        }
      if (passphrase != NULL && strlen (passphrase) > 0)
        g_variant_builder_add (&options_builder, "{sv}", "encrypt.passphrase", g_variant_new_string (passphrase));

      if (erase != NULL)
        g_variant_builder_add (&options_builder, "{sv}", "erase", g_variant_new_string (erase));

      udisks_block_call_format (partition_block,
                                fstype,
                                g_variant_builder_end (&options_builder),
                                NULL, /* GCancellable */
                                format_cb,
                                data);
    }

 out:
  g_free (created_partition_object_path);
  g_clear_object (&partition_object);
}

void
gdu_create_partition_dialog_show (GduWindow    *window,
                                  UDisksObject *object,
                                  guint64       offset,
                                  guint64       max_size)
{
  CreatePartitionData *data;
  guint64 max_size_mb;
  gint response;
  const gchar *additional_fstypes[3] = {NULL, NULL, NULL};
  gchar dos_extended_partition_name[256];

  data = g_new0 (CreatePartitionData, 1);
  data->window = g_object_ref (window);
  data->object = g_object_ref (object);
  data->block = udisks_object_get_block (object);
  g_assert (data->block != NULL);
  data->table = udisks_object_get_partition_table (object);
  g_assert (data->table != NULL);
  data->drive = udisks_client_get_drive_for_block (gdu_window_get_client (window), data->block);
  data->offset = offset;
  data->max_size = max_size;

  if (g_strcmp0 (udisks_partition_table_get_type_ (data->table), "dos") == 0)
    {
      if (!have_dos_extended (data))
        {
          snprintf (dos_extended_partition_name, sizeof dos_extended_partition_name,
                    "%s <span size=\"small\">(%s)</span>",
                    _("Extended partition"),
                    _("For logical partitions"));
          additional_fstypes[0] = "dos_extended";
          additional_fstypes[1] = dos_extended_partition_name;
        }
    }

  data->dialog = GTK_WIDGET (gdu_application_new_widget (gdu_window_get_application (window),
                                                         "create-partition-dialog.ui",
                                                         "create-partition-dialog",
                                                         &data->builder));

  data->infobar_vbox = GTK_WIDGET (gtk_builder_get_object (data->builder, "infobar-vbox"));;
  data->dos_error_infobar = gdu_utils_create_info_bar (GTK_MESSAGE_ERROR,
                                                       _("Cannot create a new partition. There are already four primary partitions."),
                                                       NULL);
  gtk_box_pack_start (GTK_BOX (data->infobar_vbox), data->dos_error_infobar, TRUE, TRUE, 0);
  data->dos_warning_infobar = gdu_utils_create_info_bar (GTK_MESSAGE_WARNING,
                                                         _("This is the last primary partition that can be created."),
                                                         NULL);
  gtk_box_pack_start (GTK_BOX (data->infobar_vbox), data->dos_warning_infobar, TRUE, TRUE, 0);
  data->size_spinbutton = GTK_WIDGET (gtk_builder_get_object (data->builder, "size-spinbutton"));
  data->size_adjustment = GTK_ADJUSTMENT (gtk_builder_get_object (data->builder, "size-adjustment"));
  g_signal_connect (data->size_adjustment, "notify::value", G_CALLBACK (create_partition_property_changed), data);
  data->free_following_adjustment = GTK_ADJUSTMENT (gtk_builder_get_object (data->builder, "free-following-adjustment"));
  data->contents_box = GTK_WIDGET (gtk_builder_get_object (data->builder, "contents-box"));
  data->create_filesystem_widget = gdu_create_filesystem_widget_new (gdu_window_get_application (window),
                                                                     data->drive,
                                                                     additional_fstypes);
  gtk_box_pack_start (GTK_BOX (data->contents_box),
                      data->create_filesystem_widget,
                      TRUE, TRUE, 0);
  g_signal_connect (data->create_filesystem_widget, "notify::has-info",
                    G_CALLBACK (create_partition_property_changed), data);

  /* The adjustments count MB, not bytes */
  max_size_mb = max_size / (1000L*1000L);
  gtk_adjustment_configure (data->size_adjustment,
                            max_size_mb,
                            0.0,                    /* lower */
                            max_size_mb,            /* upper */
                            1,                      /* step increment */
                            1000,                   /* page increment */
                            0.0);                   /* page_size */
  gtk_adjustment_configure (data->free_following_adjustment,
                            0,
                            0.0,                    /* lower */
                            max_size_mb,            /* upper */
                            1,                      /* step increment */
                            1000,                   /* page increment */
                            0.0);                   /* page_size */

  g_object_bind_property_full (data->size_adjustment,
                               "value",
                               data->free_following_adjustment,
                               "value",
                               G_BINDING_BIDIRECTIONAL,
                               size_binding_func,
                               size_binding_func,
                               data,
                               NULL);

  gtk_window_set_transient_for (GTK_WINDOW (data->dialog), GTK_WINDOW (window));
  gtk_dialog_set_default_response (GTK_DIALOG (data->dialog), GTK_RESPONSE_OK);

  create_partition_populate (data);
  create_partition_update (data);

  gtk_widget_show_all (data->dialog);
  gtk_widget_grab_focus (gdu_create_filesystem_widget_get_name_entry (GDU_CREATE_FILESYSTEM_WIDGET (data->create_filesystem_widget)));

  response = gtk_dialog_run (GTK_DIALOG (data->dialog));
  if (response == GTK_RESPONSE_OK)
    {
      guint64 size;
      const gchar *fstype;
      const gchar *partition_type = "";

      gtk_widget_hide (data->dialog);

      fstype = gdu_create_filesystem_widget_get_fstype (GDU_CREATE_FILESYSTEM_WIDGET (data->create_filesystem_widget));
      if (g_strcmp0 (fstype, "dos_extended") == 0)
        {
          partition_type = "0x05";
        }

      size = gtk_adjustment_get_value (data->size_adjustment) * 1000L * 1000L;
      udisks_partition_table_call_create_partition (data->table,
                                                    data->offset,
                                                    size,
                                                    partition_type, /* use default type */
                                                    "", /* use blank partition name */
                                                    g_variant_new ("a{sv}", NULL), /* options */
                                                    NULL, /* GCancellable */
                                                    create_partition_cb,
                                                    data);
      return;
    }

  create_partition_data_free (data);
}
