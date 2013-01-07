/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*-
 *
 * Copyright (C) 2008-2013 Red Hat, Inc.
 *
 * Licensed under GPL version 2 or later.
 *
 * Author: David Zeuthen <zeuthen@gmail.com>
 */

#include "config.h"

#include <glib/gi18n.h>

#include <math.h>

#include "gduapplication.h"
#include "gduwindow.h"
#include "gdumdraiddisksdialog.h"
#include "gduatasmartdialog.h"

/* ---------------------------------------------------------------------------------------------------- */

typedef struct
{
  volatile gint ref_count;

  gboolean in_update;

  UDisksClient *client;
  UDisksObject *object;
  UDisksMDRaid *mdraid;
  guint num_devices;
  guint64 member_size;

  GduWindow *window;
  GtkBuilder *builder;

  GtkWidget *dialog;

  GtkWidget *close_button;
  GtkWidget *scrolledwindow;
  GtkWidget *treeview;

  GtkWidget *toolbar;
  GtkWidget *add_disk_button;
  GtkWidget *remove_disk_button;
  GtkWidget *goto_disk_button;

  GtkWidget *device_label;
  GtkWidget *serial_label;
  GtkWidget *assessment_label;

  GtkListStore *store;
} DialogData;

static const struct {
  goffset offset;
  const gchar *name;
} widget_mapping[] = {
  {G_STRUCT_OFFSET (DialogData, close_button), "close-button"},

  {G_STRUCT_OFFSET (DialogData, scrolledwindow), "scrolledwindow"},
  {G_STRUCT_OFFSET (DialogData, treeview), "treeview"},

  {G_STRUCT_OFFSET (DialogData, toolbar), "toolbar"},
  {G_STRUCT_OFFSET (DialogData, add_disk_button), "add-disk-button"},
  {G_STRUCT_OFFSET (DialogData, remove_disk_button), "remove-disk-button"},
  {G_STRUCT_OFFSET (DialogData, goto_disk_button), "goto-disk-button"},

  {G_STRUCT_OFFSET (DialogData, device_label), "device-label"},
  {G_STRUCT_OFFSET (DialogData, serial_label), "serial-label"},
  {G_STRUCT_OFFSET (DialogData, assessment_label), "assessment-label"},

  {0, NULL}
};

static void update_dialog (DialogData *data);


static void on_client_changed (UDisksClient  *client,
                               gpointer       user_data);

static void on_tree_selection_changed (GtkTreeSelection *selection,
                                       gpointer          user_data);

static void on_add_disk_button_clicked (GtkButton   *button,
                                        gpointer     user_data);

enum
{
  COLUMN_SLOT,
  COLUMN_BLOCK,
  COLUMN_STATES,
  COLUMN_NUM_ERRORS,
  COLUMN_EXPANSION,
};

/* ---------------------------------------------------------------------------------------------------- */

static DialogData *
dialog_data_ref (DialogData *data)
{
  g_atomic_int_inc (&data->ref_count);
  return data;
}

static void
dialog_data_unref (DialogData *data)
{
  if (g_atomic_int_dec_and_test (&data->ref_count))
    {
      g_signal_handlers_disconnect_by_func (data->treeview,
                                            G_CALLBACK (on_tree_selection_changed),
                                            data);

      if (data->dialog != NULL)
        {
          gtk_widget_hide (data->dialog);
          gtk_widget_destroy (data->dialog);
          data->dialog = NULL;
        }

      g_signal_handlers_disconnect_by_func (data->client,
                                            G_CALLBACK (on_client_changed),
                                            data);

      g_clear_object (&data->object);
      g_clear_object (&data->window);
      g_clear_object (&data->builder);

      g_clear_object (&data->store);

      g_free (data);
    }
}

static void
dialog_data_close (DialogData *data)
{
  gtk_dialog_response (GTK_DIALOG (data->dialog), GTK_RESPONSE_CANCEL);
}

/* ---------------------------------------------------------------------------------------------------- */

static void
update_dialog_treeview (DialogData *data)
{
  GVariantIter iter;
  gint disk_slot;
  const gchar *disk_block_objpath;
  const gchar **disk_states;
  guint64 disk_num_errors;
  GtkTreeIter titer;
  UDisksBlock *selected_block = NULL;
  GtkTreeIter *iter_to_select = NULL;

  /* ---------- */

  /* preserve focus */
  if (gtk_tree_selection_get_selected (gtk_tree_view_get_selection (GTK_TREE_VIEW (data->treeview)),
                                       NULL, /* out_model */
                                       &titer))
    {
      gtk_tree_model_get (GTK_TREE_MODEL (data->store),
                          &titer,
                          COLUMN_BLOCK, &selected_block,
                          -1);
    }
  gtk_list_store_clear (data->store);
  g_variant_iter_init (&iter, udisks_mdraid_get_active_devices (data->mdraid));
  while (g_variant_iter_next (&iter, "(&oi^a&sta{sv})",
                              &disk_block_objpath,
                              &disk_slot,
                              &disk_states,
                              &disk_num_errors,
                              NULL)) /* expansion */
    {
      UDisksObject *object = NULL;
      UDisksBlock *block = NULL;
      GtkTreeIter inserted_iter;

      object = udisks_client_peek_object (data->client, disk_block_objpath);
      if (object != NULL)
        {
          block = udisks_object_peek_block (object);
        }

      gtk_list_store_insert_with_values (data->store,
                                         &inserted_iter, /* out_iter */
                                         -1,   /* position */
                                         COLUMN_SLOT, disk_slot,
                                         COLUMN_BLOCK, block,
                                         COLUMN_STATES, disk_states,
                                         COLUMN_NUM_ERRORS, (guint64) disk_num_errors,
                                         -1);
      if (block == selected_block)
        iter_to_select = gtk_tree_iter_copy (&inserted_iter);

      g_free (disk_states);
    }
  if (iter_to_select == NULL)
    {
      GtkTreeIter first_iter;
      if (gtk_tree_model_get_iter_first (GTK_TREE_MODEL (data->store), &first_iter))
        iter_to_select = gtk_tree_iter_copy (&first_iter);
    }
  if (iter_to_select != NULL)
    {
      gtk_tree_selection_select_iter (gtk_tree_view_get_selection (GTK_TREE_VIEW (data->treeview)),
                                      iter_to_select);
      gtk_tree_iter_free (iter_to_select);
    }
  g_clear_object (&selected_block);
}

/* ---------------------------------------------------------------------------------------------------- */

static void
update_dialog_labels (DialogData *data)
{
  GtkTreeIter titer;
  UDisksObject *object = NULL;
  UDisksBlock *block = NULL;
  UDisksDrive *drive = NULL;
  gchar *device_markup = NULL;
  gchar *serial_markup = NULL;
  gchar *assessment_markup = NULL;
  UDisksObjectInfo *info = NULL;
  UDisksObject *drive_object = NULL;
  UDisksDriveAta *ata = NULL;

  if (gtk_tree_selection_get_selected (gtk_tree_view_get_selection (GTK_TREE_VIEW (data->treeview)),
                                       NULL, /* out_model */
                                       &titer))
    {
      gtk_tree_model_get (GTK_TREE_MODEL (data->store),
                          &titer,
                          COLUMN_BLOCK, &block,
                          -1);
    }
  if (block == NULL)
    goto out;

  object = (UDisksObject *) g_dbus_interface_dup_object (G_DBUS_INTERFACE (block));
  if (object == NULL)
    goto out;

  if (udisks_block_get_read_only (block))
    {
      /* Translators: Shown for a read-only device. The %s is the device file, e.g. /dev/sdb1 */
      device_markup = g_strdup_printf (_("%s <span size=\"smaller\">(Read-Only)</span>"),
                                       udisks_block_get_preferred_device (block));
    }
  else
    {
      device_markup = g_strdup (udisks_block_get_preferred_device (block));
    }

  info = udisks_client_get_object_info (data->client, object);
  drive = udisks_client_get_drive_for_block (data->client, block);
  if (drive != NULL)
    {
      drive_object = (UDisksObject *) g_dbus_interface_get_object (G_DBUS_INTERFACE (drive));
      ata = udisks_object_peek_drive_ata (drive_object);
      serial_markup = udisks_drive_dup_serial (drive);
      if (ata != NULL && !udisks_drive_get_media_removable (drive))
        {
          assessment_markup = gdu_ata_smart_get_one_liner_assessment (ata,
                                                                      NULL,  /* out_smart_is_supported */
                                                                      NULL); /* out_warning */
        }
    }

  device_markup = g_strdup (udisks_object_info_get_one_liner (info));

 out:
  gtk_label_set_markup (GTK_LABEL (data->device_label),     device_markup != NULL ?     device_markup : "—");
  gtk_label_set_markup (GTK_LABEL (data->serial_label),     serial_markup != NULL ?     serial_markup : "—");
  gtk_label_set_markup (GTK_LABEL (data->assessment_label), assessment_markup != NULL ? assessment_markup : "—");

  g_free (device_markup);
  g_free (serial_markup);
  g_free (assessment_markup);
  g_clear_object (&info);
  g_clear_object (&drive);
  g_clear_object (&object);
  g_clear_object (&block);
}

/* ---------------------------------------------------------------------------------------------------- */

static void
update_dialog (DialogData *data)
{
  /* don't recurse */
  if (data->in_update)
    goto out;

  data->in_update = TRUE;
  update_dialog_treeview (data);
  update_dialog_labels (data);
  data->in_update = FALSE;

 out:
  ;
}

/* ---------------------------------------------------------------------------------------------------- */

G_GNUC_UNUSED static void
on_property_changed (GObject     *object,
                     GParamSpec  *pspec,
                     gpointer     user_data)
{
  DialogData *data = user_data;
  update_dialog (data);
}

/* ---------------------------------------------------------------------------------------------------- */

static void
remove_device_cb (GObject      *source_object,
                  GAsyncResult *res,
                  gpointer      user_data)
{
  DialogData *data = user_data;
  GError *error = NULL;
  error = NULL;
  if (!udisks_mdraid_call_remove_device_finish (UDISKS_MDRAID (source_object),
                                                res,
                                                &error))
    {
      gdu_utils_show_error (GTK_WINDOW (data->window),
                            _("An error occurred when removing a disk from the RAID Array"),
                            error);
      g_clear_error (&error);
    }
  dialog_data_unref (data);
}

static void
on_remove_disk_button_clicked (GtkButton   *button,
                               gpointer     user_data)
{
  DialogData *data = user_data;
  GVariantBuilder options_builder;
  UDisksBlock *selected_block = NULL;
  UDisksObject *selected_block_object = NULL;
  GtkTreeIter titer;
  GList *objects = NULL;

  if (gtk_tree_selection_get_selected (gtk_tree_view_get_selection (GTK_TREE_VIEW (data->treeview)),
                                       NULL, /* out_model */
                                       &titer))
    {
      gtk_tree_model_get (GTK_TREE_MODEL (data->store),
                          &titer,
                          COLUMN_BLOCK, &selected_block,
                          -1);
      if (selected_block != NULL)
        selected_block_object = (UDisksObject *) g_dbus_interface_dup_object (G_DBUS_INTERFACE (selected_block));
    }

  if (selected_block_object == NULL)
    {
      g_warning ("Cannot determine device to remove");
      goto out;
    }

  objects = g_list_append (NULL, selected_block_object);
  if (!gdu_utils_show_confirmation (GTK_WINDOW (data->dialog),
                                    C_("mdraid-disks", "Are you sure you want to remove the disk?"),
                                    C_("mdraid-disks", "Removing a disk from a RAID array may degrade it"),
                                    C_("mdraid-disks", "_Remove"),
                                    NULL, NULL,
                                    gdu_window_get_client (data->window), objects))
    goto out;

  g_variant_builder_init (&options_builder, G_VARIANT_TYPE_VARDICT);
  g_variant_builder_add (&options_builder, "{sv}", "wipe", g_variant_new_boolean (TRUE));

  udisks_mdraid_call_remove_device (data->mdraid,
                                    g_dbus_object_get_object_path (G_DBUS_OBJECT (selected_block_object)),
                                    g_variant_builder_end (&options_builder),
                                    NULL, /* cancellable */
                                    (GAsyncReadyCallback) remove_device_cb,
                                    dialog_data_ref (data));

 out:
  g_clear_object (&selected_block_object);
  g_clear_object (&selected_block);
  g_list_free (objects);
}

/* ---------------------------------------------------------------------------------------------------- */

static void
on_goto_disk_button_clicked (GtkButton   *button,
                             gpointer     user_data)
{
  DialogData *data = user_data;
  UDisksBlock *selected_block = NULL;
  UDisksObject *selected_block_object = NULL;
  GtkTreeIter titer;

  if (gtk_tree_selection_get_selected (gtk_tree_view_get_selection (GTK_TREE_VIEW (data->treeview)),
                                       NULL, /* out_model */
                                       &titer))
    {
      gtk_tree_model_get (GTK_TREE_MODEL (data->store),
                          &titer,
                          COLUMN_BLOCK, &selected_block,
                          -1);
      if (selected_block != NULL)
        selected_block_object = (UDisksObject *) g_dbus_interface_dup_object (G_DBUS_INTERFACE (selected_block));
    }

  if (selected_block_object == NULL)
    {
      g_warning ("Cannot determine device to go to");
      goto out;
    }

  dialog_data_close (data);
  gdu_window_select_object (data->window, selected_block_object);

 out:
  g_clear_object (&selected_block_object);
  g_clear_object (&selected_block);
}

/* ---------------------------------------------------------------------------------------------------- */

static void
slot_cell_func (GtkTreeViewColumn *column,
                GtkCellRenderer   *renderer,
                GtkTreeModel      *model,
                GtkTreeIter       *iter,
                gpointer           user_data)
{
  /* DialogData *data = user_data; */
  gint slot = -1;
  gchar *markup = NULL;

  gtk_tree_model_get (model,
                      iter,
                      COLUMN_SLOT, &slot,
                      -1);

  if (slot == -1)
    {
      markup = g_strdup_printf ("—");
    }
  else
    {
      markup = g_strdup_printf ("%d", slot);
    }

  g_object_set (renderer,
                "markup", markup,
                NULL);

  g_free (markup);
}

static void
pixbuf_cell_func (GtkTreeViewColumn *column,
                  GtkCellRenderer   *renderer,
                  GtkTreeModel      *model,
                  GtkTreeIter       *iter,
                  gpointer           user_data)
{
  DialogData *data = user_data;
  UDisksBlock *block = NULL;
  UDisksObject *object = NULL;
  UDisksObjectInfo *info = NULL;
  GIcon *icon = NULL;

  gtk_tree_model_get (model,
                      iter,
                      COLUMN_BLOCK, &block,
                      -1);

  if (block == NULL)
    goto out;

  object = (UDisksObject *) g_dbus_interface_dup_object (G_DBUS_INTERFACE (block));
  if (object == NULL)
    goto out;

  info = udisks_client_get_object_info (data->client, object);
  icon = g_object_ref (udisks_object_info_get_icon (info));

 out:
  g_object_set (renderer,
                "gicon", icon,
                NULL);

  g_clear_object (&icon);
  g_clear_object (&object);
  g_clear_object (&block);
  g_clear_object (&info);
}

static void
name_cell_func (GtkTreeViewColumn *column,
                GtkCellRenderer   *renderer,
                GtkTreeModel      *model,
                GtkTreeIter       *iter,
                gpointer           user_data)
{
  DialogData *data = user_data;
  UDisksBlock *block = NULL;
  UDisksObject *object = NULL;
  UDisksObjectInfo *info = NULL;
  gchar *markup = NULL;

  gtk_tree_model_get (model,
                      iter,
                      COLUMN_BLOCK, &block,
                      -1);

  if (block == NULL)
    goto out;

  object = (UDisksObject *) g_dbus_interface_dup_object (G_DBUS_INTERFACE (block));
  if (object == NULL)
    goto out;

  info = udisks_client_get_object_info (data->client, object);
  markup = g_strdup (udisks_object_info_get_description (info));

 out:

  g_object_set (renderer,
                "markup", markup,
                NULL);

  g_free (markup);
  g_clear_object (&object);
  g_clear_object (&block);
  g_clear_object (&info);
}

static void
num_errors_cell_func (GtkTreeViewColumn *column,
                      GtkCellRenderer   *renderer,
                      GtkTreeModel      *model,
                      GtkTreeIter       *iter,
                      gpointer           user_data)
{
  /* DialogData *data = user_data; */
  UDisksBlock *block = NULL;
  guint64 num_errors = 0;
  gchar *markup = NULL;

  gtk_tree_model_get (model,
                      iter,
                      COLUMN_BLOCK, &block,
                      COLUMN_NUM_ERRORS, &num_errors,
                      -1);

  if (block == NULL)
    {
      markup = g_strdup ("—");
    }
  else
    {
      markup = g_strdup_printf ("%" G_GUINT64_FORMAT, num_errors);
    }

  g_object_set (renderer,
                "markup", markup,
                NULL);

  g_free (markup);
  g_clear_object (&block);
}

static void
state_cell_func (GtkTreeViewColumn *column,
                 GtkCellRenderer   *renderer,
                 GtkTreeModel      *model,
                 GtkTreeIter       *iter,
                 gpointer           user_data)
{
  /* DialogData *data = user_data; */
  gint slot = -1;
  gchar **states = NULL;
  gchar *markup = NULL;

  gtk_tree_model_get (model,
                      iter,
                      COLUMN_SLOT, &slot,
                      COLUMN_STATES, &states,
                      -1);

  if (states == NULL || g_strv_length (states) == 0)
    {
      markup = g_strdup ("—");
    }
  else
    {
      GString *str = g_string_new (NULL);
      guint n;
      for (n = 0; states[n] != NULL; n++)
        {
          const gchar *state = states[n];
          if (g_strcmp0 (state, "faulty") == 0)
            {
              if (str->len > 0)
                g_string_append (str, ", ");
              /* Translators: MD-RAID member state for 'faulty' */
              g_string_append_printf (str, "<span foreground=\"#ff0000\">%s</span>",
                                      C_("mdraid-disks-state", "FAILED"));
            }
          else if (g_strcmp0 (state, "in_sync") == 0)
            {
              if (str->len > 0)
                g_string_append (str, ", ");
              /* Translators: MD-RAID member state for 'in_sync' */
              g_string_append (str, C_("mdraid-disks-state", "In Sync"));
            }
          else if (g_strcmp0 (state, "spare") == 0)
            {
              if (slot < 0)
                {
                  if (str->len > 0)
                    g_string_append (str, ", ");
                  /* Translators: MD-RAID member state for 'spare' */
                  g_string_append (str, C_("mdraid-disks-state", "Spare"));
                }
              else
                {
                  if (str->len > 0)
                    g_string_append (str, ", ");
                  /* Translators: MD-RAID member state for 'spare' but is being recovered to  */
                  g_string_append (str, C_("mdraid-disks-state", "Recovering"));
                }
            }
          else if (g_strcmp0 (state, "write_mostly") == 0)
            {
              if (str->len > 0)
                g_string_append (str, ", ");
              /* Translators: MD-RAID member state for 'writemostly' */
              g_string_append (str, C_("mdraid-disks-state", "Write-mostly"));
            }
          else if (g_strcmp0 (state, "blocked") == 0)
            {
              if (str->len > 0)
                g_string_append (str, ", ");
              /* Translators: MD-RAID member state for 'blocked' */
              g_string_append (str, C_("mdraid-disks-state", "Blocked"));
            }
          else
            {
              if (str->len > 0)
                g_string_append (str, ", ");
              /* Translators: MD-RAID member state unknown. The %s is the raw state from sysfs */
              g_string_append_printf (str, C_("mdraid-disks-state", "Unknown (%s)"), state);
            }
        } /* for all states */
      markup = g_string_free (str, FALSE);
    }

  g_object_set (renderer,
                "markup", markup,
                NULL);

  g_free (markup);
  g_strfreev (states);
}


/* ---------------------------------------------------------------------------------------------------- */

static gint
row_sort_func (GtkTreeModel *model,
               GtkTreeIter *a,
               GtkTreeIter *b,
               gpointer user_data)
{
  gint ret;
  gint slot_a, slot_b;
  UDisksBlock *block_a = NULL, *block_b = NULL;

  gtk_tree_model_get (model, a,
                      COLUMN_SLOT, &slot_a,
                      COLUMN_BLOCK, &block_a,
                      -1);
  gtk_tree_model_get (model, a,
                      COLUMN_SLOT, &slot_b,
                      COLUMN_BLOCK, &block_b,
                      -1);

  ret = slot_a - slot_b;
  if (ret != 0)
    goto out;

  ret = g_strcmp0 (udisks_block_get_preferred_device (block_a),
                   udisks_block_get_preferred_device (block_b));

 out:
  g_clear_object (&block_a);
  g_clear_object (&block_b);
  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

static void
init_dialog (DialogData *data)
{
  GtkTreeViewColumn *column;
  GtkCellRenderer *renderer;
  GtkStyleContext *context;

  context = gtk_widget_get_style_context (data->scrolledwindow);
  gtk_style_context_set_junction_sides (context, GTK_JUNCTION_BOTTOM);
  context = gtk_widget_get_style_context (data->toolbar);
  gtk_style_context_add_class (context, GTK_STYLE_CLASS_INLINE_TOOLBAR);
  gtk_style_context_set_junction_sides (context, GTK_JUNCTION_TOP);

  data->store = gtk_list_store_new (5,
                                    G_TYPE_INT,
                                    UDISKS_TYPE_BLOCK,
                                    G_TYPE_STRV,
                                    G_TYPE_UINT64,
                                    G_TYPE_VARIANT);

  gtk_tree_view_set_model (GTK_TREE_VIEW (data->treeview), GTK_TREE_MODEL (data->store));
  gtk_tree_sortable_set_sort_column_id (GTK_TREE_SORTABLE (data->store),
                                        COLUMN_SLOT,
                                        GTK_SORT_ASCENDING);
  gtk_tree_sortable_set_sort_func (GTK_TREE_SORTABLE (data->store),
                                   COLUMN_SLOT,
                                   row_sort_func,
                                   data, /* user_data */
                                   NULL); /* GDestroyNotify */

  column = gtk_tree_view_column_new ();
  gtk_tree_view_append_column (GTK_TREE_VIEW (data->treeview), column);
  /* Translators: column name for the position of the disk in the RAID array */
  gtk_tree_view_column_set_title (column, C_("mdraid-disks", "Position"));
  /* -- */
  renderer = gtk_cell_renderer_text_new ();
  gtk_tree_view_column_pack_start (column, renderer, TRUE);
  g_object_set (G_OBJECT (renderer),
                "yalign", 0.5,
                NULL);
  gtk_tree_view_column_set_cell_data_func (column,
                                           renderer,
                                           slot_cell_func,
                                           data,  /* user_data */
                                           NULL); /* user_data GDestroyNotify */

  column = gtk_tree_view_column_new ();
  gtk_tree_view_append_column (GTK_TREE_VIEW (data->treeview), column);
  /* Translators: column name for the disk in the RAID array */
  gtk_tree_view_column_set_title (column, C_("mdraid-disks", "Disk"));
  gtk_tree_view_column_set_expand (column, TRUE);
  /* -- */
  renderer = gtk_cell_renderer_pixbuf_new ();
  g_object_set (G_OBJECT (renderer),
                "stock-size", GTK_ICON_SIZE_SMALL_TOOLBAR,
                NULL);
  gtk_tree_view_column_pack_start (column, renderer, FALSE);
  gtk_tree_view_column_set_cell_data_func (column,
                                           renderer,
                                           pixbuf_cell_func,
                                           data,  /* user_data */
                                           NULL); /* user_data GDestroyNotify */
  /* -- */
  renderer = gtk_cell_renderer_text_new ();
  gtk_tree_view_column_pack_start (column, renderer, TRUE);
  g_object_set (G_OBJECT (renderer),
                "yalign", 0.5,
                "ellipsize", PANGO_ELLIPSIZE_MIDDLE,
                NULL);
  gtk_tree_view_column_set_cell_data_func (column,
                                           renderer,
                                           name_cell_func,
                                           data,  /* user_data */
                                           NULL); /* user_data GDestroyNotify */


  column = gtk_tree_view_column_new ();
  gtk_tree_view_append_column (GTK_TREE_VIEW (data->treeview), column);
  /* Translators: column name for the state of the disk in the RAID array */
  gtk_tree_view_column_set_title (column, C_("mdraid-disks", "State"));
  /* -- */
  renderer = gtk_cell_renderer_text_new ();
  gtk_tree_view_column_pack_start (column, renderer, TRUE);
  g_object_set (G_OBJECT (renderer),
                "yalign", 0.5,
                NULL);
  gtk_tree_view_column_set_cell_data_func (column,
                                           renderer,
                                           state_cell_func,
                                           data,  /* user_data */
                                           NULL); /* user_data GDestroyNotify */

  column = gtk_tree_view_column_new ();
  gtk_tree_view_append_column (GTK_TREE_VIEW (data->treeview), column);
  /* Translators: column name for the number of read errors of the disk in the RAID array */
  gtk_tree_view_column_set_title (column, C_("mdraid-disks", "Errors"));
  /* -- */
  renderer = gtk_cell_renderer_text_new ();
  gtk_tree_view_column_pack_start (column, renderer, TRUE);
  g_object_set (G_OBJECT (renderer),
                "yalign", 0.5,
                NULL);
  gtk_tree_view_column_set_cell_data_func (column,
                                           renderer,
                                           num_errors_cell_func,
                                           data,  /* user_data */
                                           NULL); /* user_data GDestroyNotify */

  /* ---------- */

  g_signal_connect (data->add_disk_button,
                    "clicked",
                    G_CALLBACK (on_add_disk_button_clicked),
                    data);

  g_signal_connect (data->remove_disk_button,
                    "clicked",
                    G_CALLBACK (on_remove_disk_button_clicked),
                    data);

  g_signal_connect (data->goto_disk_button,
                    "clicked",
                    G_CALLBACK (on_goto_disk_button_clicked),
                    data);


  /* ---------- */

  g_signal_connect (data->client,
                    "changed",
                    G_CALLBACK (on_client_changed),
                    data);

  g_signal_connect (gtk_tree_view_get_selection (GTK_TREE_VIEW (data->treeview)),
                    "changed",
                    G_CALLBACK (on_tree_selection_changed),
                    data);

  update_dialog (data);
}

static void
on_client_changed (UDisksClient   *client,
                   gpointer        user_data)
{
  DialogData *data = user_data;
  update_dialog (data);
}

static void
on_tree_selection_changed (GtkTreeSelection *selection,
                           gpointer          user_data)
{
  DialogData *data = user_data;
  update_dialog (data);
}

/* ---------------------------------------------------------------------------------------------------- */


void
gdu_mdraid_disks_dialog_show (GduWindow    *window,
                              UDisksObject *object)
{
  DialogData *data;
  GList *members, *l;
  guint n;

  data = g_new0 (DialogData, 1);
  data->ref_count = 1;
  data->object = g_object_ref (object);
  data->mdraid = udisks_object_peek_mdraid (data->object);
  data->num_devices = udisks_mdraid_get_num_devices (data->mdraid);
  data->window = g_object_ref (window);
  data->client = gdu_window_get_client (data->window);

  /* figure out member_size */
  data->member_size = G_MAXUINT64;
  members = udisks_client_get_members_for_mdraid (data->client, data->mdraid);
  for (l = members; l != NULL; l = l->next)
    {
      UDisksBlock *member = UDISKS_BLOCK (l->data);
      guint64 size = udisks_block_get_size (member);
      if (data->member_size > size)
        data->member_size = size;
    }
  g_list_free_full (members, g_object_unref);

  data->dialog = GTK_WIDGET (gdu_application_new_widget (gdu_window_get_application (window),
                                                         "md-raid-disks-dialog.ui",
                                                         "dialog",
                                                         &data->builder));
  for (n = 0; widget_mapping[n].name != NULL; n++)
    {
      gpointer *p = (gpointer *) ((char *) data + widget_mapping[n].offset);
      *p = gtk_builder_get_object (data->builder, widget_mapping[n].name);
    }

  gtk_window_set_transient_for (GTK_WINDOW (data->dialog), GTK_WINDOW (window));

  init_dialog (data);

  while (TRUE)
    {
      gint response;
      response = gtk_dialog_run (GTK_DIALOG (data->dialog));
      /* Keep in sync with .ui file */
      switch (response)
        {
        case GTK_RESPONSE_CLOSE: /* Close */
          goto out;
          break;

        default:
          goto out;
        }
    }
 out:
  dialog_data_close (data);
  dialog_data_unref (data);
}

/* ---------------------------------------------------------------------------------------------------- */

static void
add_device_cb (GObject      *source_object,
               GAsyncResult *res,
               gpointer      user_data)
{
  DialogData *data = user_data;
  GError *error = NULL;
  error = NULL;
  if (!udisks_mdraid_call_add_device_finish (UDISKS_MDRAID (source_object),
                                             res,
                                             &error))
    {
      gdu_utils_show_error (GTK_WINDOW (data->window),
                            _("An error occurred when adding a disk to the RAID Array"),
                            error);
      g_clear_error (&error);
    }
  dialog_data_unref (data);
}

static void
add_on_menu_item_activated (GtkMenuItem *item,
                            gpointer     user_data)
{
  DialogData *data = user_data;
  GList *objects = NULL;
  UDisksBlock *block;
  UDisksObject *object;
  GVariantBuilder options_builder;

  block = UDISKS_BLOCK (g_object_get_data (G_OBJECT (item), "x-udisks-block"));
  object = UDISKS_OBJECT (g_dbus_interface_dup_object (G_DBUS_INTERFACE (block)));

  objects = g_list_append (NULL, object);
  if (!gdu_utils_show_confirmation (GTK_WINDOW (data->dialog),
                                    C_("mdraid-disks", "Are you sure you want to add the disk to the array?"),
                                    C_("mdraid-disks", "All existing data on the disk will be lost"),
                                    C_("mdraid-disks", "_Add"),
                                    NULL, NULL,
                                    gdu_window_get_client (data->window), objects))
    goto out;

  g_variant_builder_init (&options_builder, G_VARIANT_TYPE_VARDICT);
  udisks_mdraid_call_add_device (data->mdraid,
                                 g_dbus_object_get_object_path (G_DBUS_OBJECT (object)),
                                 g_variant_builder_end (&options_builder),
                                 NULL, /* cancellable */
                                 (GAsyncReadyCallback) add_device_cb,
                                 dialog_data_ref (data));

 out:
  g_clear_object (&object);
  g_list_free (objects);
}

static void
menu_position_func (GtkMenu       *menu,
                    gint          *x,
                    gint          *y,
                    gboolean      *push_in,
                    gpointer       user_data)
{
  GtkWidget *align_widget = GTK_WIDGET (user_data);
  GtkRequisition menu_req;
  GtkTextDirection direction;
  GdkRectangle monitor;
  gint monitor_num;
  GdkScreen *screen;
  GdkWindow *gdk_window;
  GtkAllocation allocation, arrow_allocation;
  GtkAlign align;
  GtkWidget *toplevel;

  align = gtk_widget_get_halign (GTK_WIDGET (menu));
  direction = gtk_widget_get_direction (align_widget);
  gdk_window = gtk_widget_get_window (align_widget);

  gtk_widget_get_preferred_size (GTK_WIDGET (menu),
                                 &menu_req,
                                 NULL);

  toplevel = gtk_widget_get_toplevel (GTK_WIDGET (menu));
  gtk_window_set_type_hint (GTK_WINDOW (toplevel), GDK_WINDOW_TYPE_HINT_DROPDOWN_MENU);

  screen = gtk_widget_get_screen (GTK_WIDGET (menu));
  monitor_num = gdk_screen_get_monitor_at_window (screen, gdk_window);
  if (monitor_num < 0)
    monitor_num = 0;
  gdk_screen_get_monitor_workarea (screen, monitor_num, &monitor);

  gtk_widget_get_allocation (align_widget, &allocation);
  gtk_widget_get_allocation (align_widget, &arrow_allocation);

  gdk_window_get_origin (gdk_window, x, y);
  *x += allocation.x;
  *y += allocation.y;

  /* treat the default align value like START */
  if (align == GTK_ALIGN_FILL)
    align = GTK_ALIGN_START;

  if (align == GTK_ALIGN_CENTER)
    *x -= (menu_req.width - allocation.width) / 2;
  else if ((align == GTK_ALIGN_START && direction == GTK_TEXT_DIR_LTR) ||
           (align == GTK_ALIGN_END && direction == GTK_TEXT_DIR_RTL))
    *x += MAX (allocation.width - menu_req.width, 0);
  else if (menu_req.width > allocation.width)
    *x -= menu_req.width - allocation.width;

  if ((*y + arrow_allocation.height + menu_req.height) <= monitor.y + monitor.height)
    *y += arrow_allocation.height;
  else if ((*y - menu_req.height) >= monitor.y)
    *y -= menu_req.height;
  else if (monitor.y + monitor.height - (*y + arrow_allocation.height) > *y)
    *y += arrow_allocation.height;
  else
    *y -= menu_req.height;

  *push_in = FALSE;
}

static void
on_add_disk_button_clicked (GtkButton   *button,
                            gpointer     user_data)
{
  DialogData *data = user_data;
  GdkEventButton *event = NULL;
  GtkWidget *menu = NULL;
  GtkWidget *item = NULL;
  GList *object_proxies = NULL;
  GList *l;
  guint num_candidates = 0;

  /* TODO: I think, down the road, we want *some* kind of popup "menu"
   *       that allows the user to choose a device. This popup "menu"
   *       should be backed by GduDeviceTreeModel to properly convey
   *       the information in there, e.g.  naming, icons, warnings,
   *       progress, classification and so on.
   *
   *       For now, we just use a GtkMenu. It's not pretty but it
   *       works...
   */

  menu = gtk_menu_new ();

  /* Go through all block devices */
  object_proxies = g_dbus_object_manager_get_objects (udisks_client_get_object_manager (data->client));
  for (l = object_proxies; l != NULL; l = l->next)
    {
      UDisksObject *object = UDISKS_OBJECT (l->data);
      UDisksObjectInfo *info = NULL;
      UDisksBlock *block = NULL;
      guint64 block_size = 0;
      GVariantIter active_devices_iter;
      const gchar *disk_block_objpath;

      block = udisks_object_peek_block (object);
      if (block == NULL)
        goto skip;

      block_size = udisks_block_get_size (block);

      /* Don't include empty devices or partitions */
      if (block_size == 0 || udisks_object_peek_partition (object) != NULL)
        goto skip;

      /* Size must match within 1% or 1MiB */
      if (block_size < data->member_size || block_size > (data->member_size * 101LL / 100LL))
        goto skip;

      /* Must not be an active member of this running array */
      g_variant_iter_init (&active_devices_iter, udisks_mdraid_get_active_devices (data->mdraid));
      while (g_variant_iter_next (&active_devices_iter, "(&oi^a&sta{sv})",
                                  &disk_block_objpath,
                                  NULL,  /* disk_slot */
                                  NULL,  /* disk_states */
                                  NULL,  /* disk_num_errors */
                                  NULL)) /* expansion */
        {
          if (g_strcmp0 (disk_block_objpath, g_dbus_object_get_object_path (G_DBUS_OBJECT (object))) == 0)
            {
              goto skip;
            }
        }

      info = udisks_client_get_object_info (data->client, object);

      item = gtk_image_menu_item_new ();
      gtk_menu_item_set_label (GTK_MENU_ITEM (item), udisks_object_info_get_one_liner (info));
      gtk_image_menu_item_set_image (GTK_IMAGE_MENU_ITEM (item),
                                     gtk_image_new_from_gicon (udisks_object_info_get_icon (info), GTK_ICON_SIZE_MENU));
      gtk_image_menu_item_set_always_show_image (GTK_IMAGE_MENU_ITEM (item), TRUE);
      gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);

      /* yeah, this use of GObject data blows.. It Is What It Is(tm) */
      g_object_set_data_full (G_OBJECT (item),
                              "x-udisks-block",
                              g_object_ref (block),
                              g_object_unref);
      g_signal_connect (item,
                        "activate",
                        G_CALLBACK (add_on_menu_item_activated),
                        data);

      g_clear_object (&info);
      num_candidates++;
    skip:
      ;
    }

  if (num_candidates == 0)
    {
      /* Translators: Shown in sole item in popup menu for the "+" button when there are no disks of the
       *              right size available
       */
      item = gtk_menu_item_new_with_label (C_("mdraid-add", "No disks of suitable size available"));
      gtk_widget_set_sensitive (item, FALSE);
      gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);
    }
  else
    {
      item = gtk_separator_menu_item_new ();
      gtk_menu_shell_prepend (GTK_MENU_SHELL (menu), item);
      /* Translators: Top-most item in popup menu for the "+" button. Other items in the menu include
       *              disks that can be added to the array
       */
      item = gtk_menu_item_new_with_label (C_("mdraid-add", "Select disk to add"));
      gtk_widget_set_sensitive (item, FALSE);
      gtk_menu_shell_prepend (GTK_MENU_SHELL (menu), item);
    }

  gtk_widget_show_all (menu);
  gtk_menu_popup_for_device (GTK_MENU (menu),
                             event != NULL ? event->device : NULL,
                             NULL, /* parent_menu_shell */
                             NULL, /* parent_menu_item */
                             menu_position_func,
                             data->add_disk_button,
                             NULL, /* GDestroyNotify for user data */
                             event != NULL ? event->button : 0,
                             event != NULL ? event->time : gtk_get_current_event_time ());

  g_list_free_full (object_proxies, g_object_unref);
}
