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

#include <math.h>

#include "gduapplication.h"
#include "gduwindow.h"
#include "gdumdraiddisksdialog.h"

/* ---------------------------------------------------------------------------------------------------- */

typedef struct
{
  volatile gint ref_count;

  UDisksClient *client;
  UDisksObject *object;
  UDisksMDRaid *mdraid;
  guint num_devices;

  GduWindow *window;
  GtkBuilder *builder;

  GtkWidget *dialog;

  GtkWidget *close_button;
  GtkWidget *scrolledwindow;
  GtkWidget *treeview;
  GtkWidget *toolbar;
  GtkWidget *add_toolbutton;
  GtkWidget *remove_toolbutton;

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
  {G_STRUCT_OFFSET (DialogData, add_toolbutton), "add-toolbutton"},
  {G_STRUCT_OFFSET (DialogData, remove_toolbutton), "remove-toolbutton"},

  {0, NULL}
};

static void update_dialog (DialogData *data);


static void on_client_changed (UDisksClient  *client,
                               gpointer       user_data);

enum
{
  COLUMN_SLOT,
  COLUMN_BLOCK,
  COLUMN_STATE,
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
update_dialog (DialogData *data)
{
  GVariantIter iter;
  gint disk_slot;
  const gchar *disk_block_objpath;
  const gchar *disk_state;
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
  while (g_variant_iter_next (&iter, "(&oi&sta{sv})",
                              &disk_block_objpath,
                              &disk_slot,
                              &disk_state,
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
                                         COLUMN_STATE, disk_state,
                                         COLUMN_NUM_ERRORS, (guint64) disk_num_errors,
                                         -1);
      if (block == selected_block)
        iter_to_select = gtk_tree_iter_copy (&inserted_iter);
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
                            _("An error occurred when removing a disk from RAID Array"),
                            error);
      g_clear_error (&error);
    }
  dialog_data_unref (data);
}

static void
on_remove_toolbutton_clicked (GtkToolButton   *tool_button,
                              gpointer         user_data)
{
  DialogData *data = user_data;
  GtkWidget *dialog;
  GtkWidget *check_button;
  gint response;
  const gchar *message;
  const gchar *secondary_message;
  const gchar *affirmative_verb;
  gboolean opt_wipe = FALSE;
  GVariantBuilder options_builder;
  UDisksBlock *selected_block = NULL;
  UDisksObject *selected_block_object = NULL;
  GtkTreeIter titer;

  message = C_("mdraid-disks", "Are you sure you want to remove the disk?");
  secondary_message = C_("mdraid-disks", "Removing a disk from a RAID array may degrade it");
  affirmative_verb = C_("mdraid-disks", "_Remove");

  dialog = gtk_message_dialog_new_with_markup (GTK_WINDOW (data->dialog),
                                               GTK_DIALOG_MODAL,
                                               GTK_MESSAGE_INFO,
                                               GTK_BUTTONS_CANCEL,
                                               "<big><b>%s</b></big>",
                                               message);
  gtk_message_dialog_format_secondary_markup (GTK_MESSAGE_DIALOG (dialog),
                                              "%s",
                                              secondary_message);

  check_button = gtk_check_button_new_with_mnemonic (C_("mdraid-disks", "_Wipe disk after removal"));
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (check_button), TRUE);
  gtk_box_pack_start (GTK_BOX (gtk_message_dialog_get_message_area (GTK_MESSAGE_DIALOG (dialog))),
                      check_button,
                      FALSE, FALSE, 0);

  gtk_dialog_add_button (GTK_DIALOG (dialog),
                         affirmative_verb,
                         GTK_RESPONSE_OK);

  gtk_widget_show_all (dialog);
  response = gtk_dialog_run (GTK_DIALOG (dialog));

  opt_wipe = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (check_button));

  gtk_widget_destroy (dialog);

  if (response != GTK_RESPONSE_OK)
    goto out;

  g_variant_builder_init (&options_builder, G_VARIANT_TYPE_VARDICT);
  if (opt_wipe)
    g_variant_builder_add (&options_builder, "{sv}", "wipe", g_variant_new_boolean (TRUE));
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

  udisks_mdraid_call_remove_device (data->mdraid,
                                    g_dbus_object_get_object_path (G_DBUS_OBJECT (selected_block_object)),
                                    g_variant_builder_end (&options_builder),
                                    NULL, /* cancellable */
                                    (GAsyncReadyCallback) remove_device_cb,
                                    dialog_data_ref (data));

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
  GIcon *icon = NULL;

  gtk_tree_model_get (model,
                      iter,
                      COLUMN_BLOCK, &block,
                      -1);

  if (block == NULL)
    {
      icon = NULL;
    }
  else
    {
      UDisksDrive *drive;
      drive = udisks_client_get_drive_for_block (data->client, block);
      if (drive != NULL)
        {
          udisks_client_get_drive_info (data->client,
                                        drive,
                                        NULL,  /* out_name */
                                        NULL,  /* out_description */
                                        &icon, /* out_drive_icon */
                                        NULL,  /* out_media_description */
                                        NULL); /* out_media_icon */
          g_object_unref (drive);
        }
      else
        {
          icon = g_themed_icon_new ("drive-removable-media"); /* for now */
        }
    }

  g_object_set (renderer,
                "gicon", icon,
                NULL);

  g_clear_object (&icon);
  g_clear_object (&block);
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
  gchar *markup = NULL;
  gchar *s;

  gtk_tree_model_get (model,
                      iter,
                      COLUMN_BLOCK, &block,
                      -1);

  if (block == NULL)
    {
      /* Translators: Shown in 'Disk' column when there is no disk for the position in question */
      markup = g_strdup_printf ("<i>%s</i>",
                                C_("mdraid-disks", "None"));
    }
  else
    {
      UDisksDrive *drive = NULL;
      gchar *name = NULL;
      gchar *desc = NULL;

      drive = udisks_client_get_drive_for_block (data->client, block);
      if (drive != NULL)
        {
          UDisksPartition *partition = NULL;
          UDisksObject *object = NULL;

          udisks_client_get_drive_info (data->client,
                                        drive,
                                        &name,  /* out_name */
                                        &desc,  /* out_description */
                                        NULL,   /* out_drive_icon */
                                        NULL,   /* out_media_description */
                                        NULL);  /* out_media_icon */

          object = (UDisksObject *) g_dbus_interface_get_object (G_DBUS_INTERFACE (block));
          if (object != NULL)
            partition = udisks_object_peek_partition (object);

          if (partition != NULL)
            {
              s = g_strdup_printf (C_("mdraid-disks", "Partition %d of %s"),
                                   udisks_partition_get_number (partition),
                                   desc);
              g_free (desc); desc = s;
            }

          s = g_strdup_printf ("%s — %s", name, udisks_block_get_preferred_device (block));
          g_free (name); name = s;

          g_object_unref (drive);
        }
      else
        {
          s = udisks_client_get_size_for_display (data->client, udisks_block_get_size (block), FALSE, FALSE);
          /* Translators: Shown in list for a disk/member that is not a drive.
           *              The %s is the size (e.g. "42.0 GB").
           */
          desc = g_strdup_printf (C_("mdraid-disks", "%s Block Device"), s);
          g_free (s);
          name = udisks_block_dup_preferred_device (block);
        }

      /* TODO: once https://bugzilla.gnome.org/show_bug.cgi?id=657194 is resolved, use that instead
       * of hard-coding the color
       */
      markup = g_strdup_printf ("%s\n"
                                "<small><span foreground=\"#555555\">%s</span></small>",
                                desc,
                                name);
      g_free (desc);
      g_free (name);
    }

  g_object_set (renderer,
                "markup", markup,
                NULL);

  g_free (markup);
  g_clear_object (&block);
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
  gchar *state = NULL;
  gchar *markup = NULL;

  gtk_tree_model_get (model,
                      iter,
                      COLUMN_STATE, &state,
                      -1);

  if (state == NULL || strlen (state) == 0)
    {
      markup = g_strdup ("—");
    }
  else if (g_strcmp0 (state, "faulty") == 0)
    {
      /* Translators: MD-RAID member state for 'faulty' */
      markup = g_strdup_printf ("<span foreground=\"#ff0000\">%s</span>\n",
                                C_("mdraid-disks-state", "FAILING"));
    }
  else if (g_strcmp0 (state, "in_sync") == 0)
    {
      /* Translators: MD-RAID member state for 'in_sync' */
      markup = g_strdup (C_("mdraid-disks-state", "In Sync"));
    }
  else if (g_strcmp0 (state, "writemostly") == 0)
    {
      /* Translators: MD-RAID member state for 'writemostly' */
      markup = g_strdup (C_("mdraid-disks-state", "In Sync (Write-Mostly)"));
    }
  else if (g_strcmp0 (state, "blocked") == 0)
    {
      /* Translators: MD-RAID member state for 'blocked' */
      markup = g_strdup (C_("mdraid-disks-state", "Blocked"));
    }
  else if (g_strcmp0 (state, "spare") == 0)
    {
      /* Translators: MD-RAID member state for 'spare' */
      markup = g_strdup (C_("mdraid-disks-state", "Spare"));
    }
  else
    {
      /* Translators: MD-RAID member state unknown. The %s is the raw state from sysfs */
      markup = g_strdup_printf (C_("mdraid-disks-state", "Unknown (%s)"), state);
    }

  g_object_set (renderer,
                "markup", markup,
                NULL);

  g_free (markup);
  g_free (state);
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
  gtk_widget_set_name (data->toolbar, "mdraid-disks-toolbar");

  data->store = gtk_list_store_new (5,
                                    G_TYPE_INT,
                                    UDISKS_TYPE_BLOCK,
                                    G_TYPE_STRING,
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
                "stock-size", GTK_ICON_SIZE_DND,
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

  g_signal_connect (data->remove_toolbutton,
                    "clicked",
                    G_CALLBACK (on_remove_toolbutton_clicked),
                    data);


  /* ---------- */

  g_signal_connect (data->client,
                    "changed",
                    G_CALLBACK (on_client_changed),
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

/* ---------------------------------------------------------------------------------------------------- */


void
gdu_mdraid_disks_dialog_show (GduWindow    *window,
                              UDisksObject *object)
{
  DialogData *data;
  guint n;

  data = g_new0 (DialogData, 1);
  data->ref_count = 1;
  data->object = g_object_ref (object);
  data->mdraid = udisks_object_peek_mdraid (data->object);
  data->num_devices = udisks_mdraid_get_num_devices (data->mdraid);
  data->window = g_object_ref (window);
  data->client = gdu_window_get_client (data->window);

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

