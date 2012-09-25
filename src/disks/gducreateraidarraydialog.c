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
#include "gducreateraidarraydialog.h"
#include "gduselectdiskdialog.h"
#include "gduatasmartdialog.h"

/* ---------------------------------------------------------------------------------------------------- */

typedef struct
{
  volatile gint ref_count;

  gboolean in_update;

  UDisksClient *client;
  GduWindow *window;
  GtkBuilder *builder;

  GtkWidget *dialog;

  GtkWidget *scrolledwindow;
  GtkWidget *treeview;

  GtkWidget *toolbar;
  GtkWidget *add_toolbutton;
  GtkWidget *remove_toolbutton;
  GtkWidget *up_toolbutton;
  GtkWidget *down_toolbutton;

  GtkWidget *level_combobox;
  GtkWidget *chunk_combobox;
  GtkWidget *name_entry;

  GtkWidget *model_label;
  GtkWidget *device_label;
  GtkWidget *serial_label;
  GtkWidget *assessment_label;

  GtkListStore *store;
} DialogData;

static const struct {
  goffset offset;
  const gchar *name;
} widget_mapping[] = {
  {G_STRUCT_OFFSET (DialogData, scrolledwindow), "scrolledwindow"},
  {G_STRUCT_OFFSET (DialogData, treeview), "treeview"},

  {G_STRUCT_OFFSET (DialogData, toolbar), "toolbar"},
  {G_STRUCT_OFFSET (DialogData, add_toolbutton), "add-toolbutton"},
  {G_STRUCT_OFFSET (DialogData, remove_toolbutton), "remove-toolbutton"},
  {G_STRUCT_OFFSET (DialogData, up_toolbutton), "up-toolbutton"},
  {G_STRUCT_OFFSET (DialogData, down_toolbutton), "down-toolbutton"},

  {G_STRUCT_OFFSET (DialogData, level_combobox), "level-combobox"},
  {G_STRUCT_OFFSET (DialogData, chunk_combobox), "chunk-combobox"},
  {G_STRUCT_OFFSET (DialogData, name_entry), "name-entry"},

  {G_STRUCT_OFFSET (DialogData, model_label), "model-label"},
  {G_STRUCT_OFFSET (DialogData, device_label), "device-label"},
  {G_STRUCT_OFFSET (DialogData, serial_label), "serial-label"},
  {G_STRUCT_OFFSET (DialogData, assessment_label), "assessment-label"},

  {0, NULL}
};

enum
{
  COMBOBOX_MODEL_COLUMN_ID,
  COMBOBOX_MODEL_COLUMN_MARKUP,
  COMBOBOX_MODEL_COLUMN_SEPARATOR,
  COMBOBOX_MODEL_COLUMN_SENSITIVE,
  COMBOBOX_MODEL_N_COLUMNS,
};

enum
{
  DISKS_MODEL_COLUMN_SLOT,
  DISKS_MODEL_COLUMN_BLOCK,
  DISKS_MODEL_N_COLUMNS
};

static void update_dialog (DialogData *data);


static void on_client_changed (UDisksClient  *client,
                               gpointer       user_data);


static void on_tree_selection_changed (GtkTreeSelection *selection,
                                       gpointer          user_data);

/* ---------------------------------------------------------------------------------------------------- */

G_GNUC_UNUSED static DialogData *
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

      g_clear_object (&data->window);
      g_clear_object (&data->builder);

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
update_dialog_labels (DialogData *data)
{
  GtkTreeIter titer;
  UDisksObject *object = NULL;
  UDisksBlock *block = NULL;
  UDisksDrive *drive = NULL;
  gchar *model_markup = NULL;
  gchar *device_markup = NULL;
  gchar *serial_markup = NULL;
  gchar *assessment_markup = NULL;
  const gchar *drive_revision = NULL;
  UDisksObjectInfo *info = NULL;
  UDisksObject *drive_object = NULL;
  UDisksDriveAta *ata = NULL;

  if (gtk_tree_selection_get_selected (gtk_tree_view_get_selection (GTK_TREE_VIEW (data->treeview)),
                                       NULL, /* out_model */
                                       &titer))
    {
      gtk_tree_model_get (GTK_TREE_MODEL (data->store),
                          &titer,
                          DISKS_MODEL_COLUMN_BLOCK, &block,
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
      drive_revision = udisks_drive_get_revision (drive);
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

  if (drive_revision != NULL && strlen (drive_revision) > 0)
    {
      /* Translators: Shown for "Model" field.
       *              The first %s is the name of the object (e.g. "INTEL SSDSA2MH080G1GC").
       *              The second %s is the fw revision (e.g "45ABX21").
       */
      model_markup = g_strdup_printf (C_("mdraid-disks", "%s (%s)"), info->name, drive_revision);
    }
  else
    {
      model_markup = g_strdup (info->name);
    }

 out:
  gtk_label_set_markup (GTK_LABEL (data->model_label),      model_markup != NULL ?      model_markup : "—");
  gtk_label_set_markup (GTK_LABEL (data->device_label),     device_markup != NULL ?     device_markup : "—");
  gtk_label_set_markup (GTK_LABEL (data->serial_label),     serial_markup != NULL ?     serial_markup : "—");
  gtk_label_set_markup (GTK_LABEL (data->assessment_label), assessment_markup != NULL ? assessment_markup : "—");

  g_free (model_markup);
  g_free (device_markup);
  g_free (serial_markup);
  g_free (assessment_markup);
  if (info != NULL)
    udisks_object_info_unref (info);
  g_clear_object (&drive);
  g_clear_object (&object);
  g_clear_object (&block);
}

static void
update_dialog_toolbuttons (DialogData *data)
{
  gboolean remove_sensitive = FALSE;
  gboolean up_sensitive = FALSE;
  gboolean down_sensitive = FALSE;
  GtkTreeIter iter;

  if (gtk_tree_selection_get_selected (gtk_tree_view_get_selection (GTK_TREE_VIEW (data->treeview)),
                                       NULL, /* out_model */
                                       &iter))
    {
      GtkTreeIter iter2;
      remove_sensitive = TRUE;
      iter2 = iter;
      if (gtk_tree_model_iter_previous (GTK_TREE_MODEL (data->store), &iter2))
        up_sensitive = TRUE;
      iter2 = iter;
      if (gtk_tree_model_iter_next (GTK_TREE_MODEL (data->store), &iter2))
        down_sensitive = TRUE;
    }

  gtk_widget_set_sensitive (data->remove_toolbutton, remove_sensitive);
  gtk_widget_set_sensitive (data->up_toolbutton, up_sensitive);
  gtk_widget_set_sensitive (data->down_toolbutton, down_sensitive);
}


static void
update_dialog (DialogData *data)
{
  /* don't recurse */
  if (data->in_update)
    goto out;

  data->in_update = TRUE;
  update_dialog_labels (data);
  update_dialog_toolbuttons (data);
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

static gboolean
has_block (DialogData  *data,
           UDisksBlock *block)
{
  GtkTreeIter iter;
  gboolean ret = FALSE;

  if (gtk_tree_model_get_iter_first (GTK_TREE_MODEL (data->store), &iter))
    {
      do
        {
          UDisksBlock *iter_block = NULL;
          gtk_tree_model_get (GTK_TREE_MODEL (data->store), &iter,
                              DISKS_MODEL_COLUMN_BLOCK, &iter_block,
                              -1);
          if (iter_block != NULL)
            {
              if (iter_block == block)
                {
                  ret = TRUE;
                  g_object_unref (iter_block);
                  goto out;
                }
              else
                {
                  g_object_unref (iter_block);
                }
            }
        }
      while (gtk_tree_model_iter_next (GTK_TREE_MODEL (data->store), &iter));
    }

 out:
  return ret;
}

static gint
get_num_blocks (DialogData *data)
{
  GtkTreeIter iter;
  gint ret = 0;

  if (gtk_tree_model_get_iter_first (GTK_TREE_MODEL (data->store), &iter))
    {
      do
        {
          ret += 1;
        }
      while (gtk_tree_model_iter_next (GTK_TREE_MODEL (data->store), &iter));
    }

  return ret;
}

static void
on_add_toolbutton_clicked (GtkToolButton   *tool_button,
                           gpointer         user_data)
{
  DialogData *data = user_data;
  GList *blocks, *l;

  blocks = gdu_select_disk_dialog_show (gdu_window_get_application (data->window),
                                        GTK_WINDOW (data->dialog),
                                        0, /* TODO: size */
                                        GDU_SELECT_DISK_FLAGS_ALLOW_MULTIPLE);
  for (l = blocks; l != NULL; l = l->next)
    {
      UDisksBlock *block = UDISKS_BLOCK (l->data);
      gint next_slot;

      if (has_block (data, block))
        continue;

      next_slot = get_num_blocks (data);

      gtk_list_store_insert_with_values (data->store, NULL /* out_iter */, G_MAXINT, /* position */
                                         DISKS_MODEL_COLUMN_SLOT, next_slot,
                                         DISKS_MODEL_COLUMN_BLOCK, block,
                                         -1);

    }
  g_list_free_full (blocks, g_object_unref);
}

/* ---------------------------------------------------------------------------------------------------- */

static void
on_remove_toolbutton_clicked (GtkToolButton   *tool_button,
                              gpointer         user_data)
{
  DialogData *data = user_data;
  GtkTreeIter iter;

  if (gtk_tree_selection_get_selected (gtk_tree_view_get_selection (GTK_TREE_VIEW (data->treeview)),
                                       NULL, /* out_model */
                                       &iter))
    {
      if (gtk_list_store_remove (data->store, &iter))
        {
          gtk_tree_selection_select_iter (gtk_tree_view_get_selection (GTK_TREE_VIEW (data->treeview)),
                                          &iter);
        }
    }
}

/* ---------------------------------------------------------------------------------------------------- */

static gint
disks_row_sort_func (GtkTreeModel *model,
                     GtkTreeIter  *a,
                     GtkTreeIter  *b,
                     gpointer      user_data);

static void
on_updown_toolbutton_clicked (GtkToolButton   *tool_button,
                              DialogData      *data,
                              gboolean         is_up)
{
  GtkTreeIter iter1;
  GtkTreeIter iter2;
  gint slot1, slot2;

  if (!gtk_tree_selection_get_selected (gtk_tree_view_get_selection (GTK_TREE_VIEW (data->treeview)),
                                        NULL, /* out_model */
                                        &iter1))
    goto out;

  iter2 = iter1;
  if (is_up)
    {
      if (!gtk_tree_model_iter_previous (GTK_TREE_MODEL (data->store), &iter2))
        goto out;
    }
  else
    {
      if (!gtk_tree_model_iter_next (GTK_TREE_MODEL (data->store), &iter2))
        goto out;
    }

  gtk_tree_model_get (GTK_TREE_MODEL (data->store), &iter1,
                      DISKS_MODEL_COLUMN_SLOT, &slot1,
                      -1);
  gtk_tree_model_get (GTK_TREE_MODEL (data->store), &iter2,
                      DISKS_MODEL_COLUMN_SLOT, &slot2,
                      -1);

  gtk_list_store_set (data->store, &iter1,
                      DISKS_MODEL_COLUMN_SLOT, slot2,
                      -1);
  gtk_list_store_set (data->store, &iter2,
                      DISKS_MODEL_COLUMN_SLOT, slot1,
                      -1);

  update_dialog (data);
 out:
  ;
}

static void
on_down_toolbutton_clicked (GtkToolButton   *tool_button,
                            gpointer         user_data)
{
  DialogData *data = user_data;
  on_updown_toolbutton_clicked (tool_button, data, FALSE);
}

static void
on_up_toolbutton_clicked (GtkToolButton   *tool_button,
                          gpointer         user_data)
{
  DialogData *data = user_data;
  on_updown_toolbutton_clicked (tool_button, data, TRUE);
}

/* ---------------------------------------------------------------------------------------------------- */

static gint
disks_row_sort_func (GtkTreeModel *model,
                     GtkTreeIter  *a,
                     GtkTreeIter  *b,
                     gpointer      user_data)
{
  gint ret;
  gint slot_a, slot_b;
  UDisksBlock *block_a = NULL, *block_b = NULL;

  gtk_tree_model_get (model, a,
                      DISKS_MODEL_COLUMN_SLOT, &slot_a,
                      DISKS_MODEL_COLUMN_BLOCK, &block_a,
                      -1);
  gtk_tree_model_get (model, b,
                      DISKS_MODEL_COLUMN_SLOT, &slot_b,
                      DISKS_MODEL_COLUMN_BLOCK, &block_b,
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

static void
disks_slot_cell_func (GtkTreeViewColumn *column,
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
                      DISKS_MODEL_COLUMN_SLOT, &slot,
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
disks_pixbuf_cell_func (GtkTreeViewColumn *column,
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
                      DISKS_MODEL_COLUMN_BLOCK, &block,
                      -1);

  if (block == NULL)
    goto out;

  object = (UDisksObject *) g_dbus_interface_dup_object (G_DBUS_INTERFACE (block));
  if (object == NULL)
    goto out;

  info = udisks_client_get_object_info (data->client, object);
  if (info->icon != NULL)
    icon = g_object_ref (info->icon);

  if (icon == NULL)
    icon = g_themed_icon_new ("drive-removable-media"); /* fallback - for now */

 out:
  g_object_set (renderer,
                "gicon", icon,
                NULL);

  g_clear_object (&icon);
  g_clear_object (&object);
  g_clear_object (&block);
  if (info != NULL)
    udisks_object_info_unref (info);
}

static void
disks_name_cell_func (GtkTreeViewColumn *column,
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
                      DISKS_MODEL_COLUMN_BLOCK, &block,
                      -1);

  if (block == NULL)
    goto out;

  object = (UDisksObject *) g_dbus_interface_dup_object (G_DBUS_INTERFACE (block));
  if (object == NULL)
    goto out;

  info = udisks_client_get_object_info (data->client, object);
  markup = g_strdup (info->description);

 out:

  g_object_set (renderer,
                "markup", markup,
                NULL);

  g_free (markup);
  g_clear_object (&object);
  g_clear_object (&block);
  if (info != NULL)
    udisks_object_info_unref (info);
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
combobox_separator_func (GtkTreeModel *model,
                         GtkTreeIter  *iter,
                         gpointer      user_data)
{
  gboolean is_separator;
  gtk_tree_model_get (model, iter,
                      COMBOBOX_MODEL_COLUMN_SEPARATOR, &is_separator,
                      -1);
  return is_separator;
}

static GtkListStore *
combobox_init (DialogData  *data,
               GtkWidget   *combobox)
{
  GtkListStore *model;
  GtkCellRenderer *renderer;

  {G_STATIC_ASSERT (COMBOBOX_MODEL_N_COLUMNS == 4);}
  model = gtk_list_store_new (COMBOBOX_MODEL_N_COLUMNS,
                              G_TYPE_STRING,
                              G_TYPE_STRING,
                              G_TYPE_BOOLEAN,
                              G_TYPE_BOOLEAN);
  gtk_combo_box_set_model (GTK_COMBO_BOX (combobox), GTK_TREE_MODEL (model));
  g_object_unref (model);

  renderer = gtk_cell_renderer_text_new ();
  gtk_cell_layout_pack_start (GTK_CELL_LAYOUT (combobox), renderer, FALSE);
  gtk_cell_layout_set_attributes (GTK_CELL_LAYOUT (combobox), renderer,
                                  "markup", COMBOBOX_MODEL_COLUMN_MARKUP,
                                  "sensitive", COMBOBOX_MODEL_COLUMN_SENSITIVE,
                                  NULL);

  gtk_combo_box_set_row_separator_func (GTK_COMBO_BOX (combobox),
                                        combobox_separator_func,
                                        data,
                                        NULL); /* GDestroyNotify */

  return model;
}

static void
combobox_add_item (GtkListStore *model,
                   const gchar  *markup,
                   const gchar  *id)
{
  gtk_list_store_insert_with_values (model, NULL /* out_iter */, G_MAXINT, /* position */
                                     COMBOBOX_MODEL_COLUMN_ID, id,
                                     COMBOBOX_MODEL_COLUMN_MARKUP, markup,
                                     COMBOBOX_MODEL_COLUMN_SENSITIVE, TRUE,
                                     -1);
}

G_GNUC_UNUSED static void
combobox_add_separator (GtkListStore *model)
{
  gtk_list_store_insert_with_values (model, NULL /* out_iter */, G_MAXINT, /* position */
                                     COMBOBOX_MODEL_COLUMN_SEPARATOR, TRUE,
                                     COMBOBOX_MODEL_COLUMN_SENSITIVE, TRUE,
                                     -1);
}

/* ---------------------------------------------------------------------------------------------------- */

static void
init_dialog (DialogData *data)
{
  GtkTreeViewColumn *column;
  GtkCellRenderer *renderer;
  GtkStyleContext *context;
  GtkListStore *model;
  gchar *s;

  context = gtk_widget_get_style_context (data->scrolledwindow);
  gtk_style_context_set_junction_sides (context, GTK_JUNCTION_BOTTOM);
  context = gtk_widget_get_style_context (data->toolbar);
  gtk_style_context_add_class (context, GTK_STYLE_CLASS_INLINE_TOOLBAR);
  gtk_style_context_set_junction_sides (context, GTK_JUNCTION_TOP);

  /* ---------- */
  /* 'RAID Level' combobox */
  model = combobox_init (data, data->level_combobox);
  s = gdu_utils_format_mdraid_level ("raid0", TRUE);  combobox_add_item (model, s, "raid0");  g_free (s);
  s = gdu_utils_format_mdraid_level ("raid1", TRUE);  combobox_add_item (model, s, "raid1");  g_free (s);
  s = gdu_utils_format_mdraid_level ("raid4", TRUE);  combobox_add_item (model, s, "raid4");  g_free (s);
  s = gdu_utils_format_mdraid_level ("raid5", TRUE);  combobox_add_item (model, s, "raid5");  g_free (s);
  s = gdu_utils_format_mdraid_level ("raid6", TRUE);  combobox_add_item (model, s, "raid6");  g_free (s);
  s = gdu_utils_format_mdraid_level ("raid10", TRUE); combobox_add_item (model, s, "raid10"); g_free (s);

  /* ---------- */
  /* 'Chunk Size' combobox */
  model = combobox_init (data, data->chunk_combobox);
  s = udisks_client_get_size_for_display (data->client,   4*1024, TRUE, FALSE); combobox_add_item (model, s, "chunk_4"); g_free (s);
  s = udisks_client_get_size_for_display (data->client,   8*1024, TRUE, FALSE); combobox_add_item (model, s, "chunk_8"); g_free (s);
  s = udisks_client_get_size_for_display (data->client,  16*1024, TRUE, FALSE); combobox_add_item (model, s, "chunk_16"); g_free (s);
  s = udisks_client_get_size_for_display (data->client,  32*1024, TRUE, FALSE); combobox_add_item (model, s, "chunk_32"); g_free (s);
  s = udisks_client_get_size_for_display (data->client,  64*1024, TRUE, FALSE); combobox_add_item (model, s, "chunk_64"); g_free (s);
  s = udisks_client_get_size_for_display (data->client, 128*1024, TRUE, FALSE); combobox_add_item (model, s, "chunk_128"); g_free (s);
  s = udisks_client_get_size_for_display (data->client, 256*1024, TRUE, FALSE); combobox_add_item (model, s, "chunk_256"); g_free (s);
  s = udisks_client_get_size_for_display (data->client, 512*1024, TRUE, FALSE); combobox_add_item (model, s, "chunk_512"); g_free (s);
  s = udisks_client_get_size_for_display (data->client,1024*1024, TRUE, FALSE); combobox_add_item (model, s, "chunk_1024"); g_free (s);
  s = udisks_client_get_size_for_display (data->client,2048*1024, TRUE, FALSE); combobox_add_item (model, s, "chunk_2048"); g_free (s);


  /* ---------- */
  /* Disks list */

  {G_STATIC_ASSERT (DISKS_MODEL_N_COLUMNS == 2);}
  data->store = gtk_list_store_new (2,
                                    G_TYPE_INT,
                                    UDISKS_TYPE_BLOCK);

  gtk_tree_view_set_model (GTK_TREE_VIEW (data->treeview), GTK_TREE_MODEL (data->store));
  gtk_tree_sortable_set_sort_column_id (GTK_TREE_SORTABLE (data->store),
                                        DISKS_MODEL_COLUMN_SLOT,
                                        GTK_SORT_ASCENDING);
  gtk_tree_sortable_set_sort_func (GTK_TREE_SORTABLE (data->store),
                                   DISKS_MODEL_COLUMN_SLOT,
                                   disks_row_sort_func,
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
                                           disks_slot_cell_func,
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
                                           disks_pixbuf_cell_func,
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
                                           disks_name_cell_func,
                                           data,  /* user_data */
                                           NULL); /* user_data GDestroyNotify */

  /* ---------- */
  /* defaults: RAID6, 512 KiB Chunk */
  gtk_combo_box_set_active_id (GTK_COMBO_BOX (data->level_combobox), "raid6");
  gtk_combo_box_set_active_id (GTK_COMBO_BOX (data->chunk_combobox), "chunk_512");

  /* ---------- */

  g_signal_connect (data->add_toolbutton,
                    "clicked",
                    G_CALLBACK (on_add_toolbutton_clicked),
                    data);

  g_signal_connect (data->remove_toolbutton,
                    "clicked",
                    G_CALLBACK (on_remove_toolbutton_clicked),
                    data);

  g_signal_connect (data->up_toolbutton,
                    "clicked",
                    G_CALLBACK (on_up_toolbutton_clicked),
                    data);

  g_signal_connect (data->down_toolbutton,
                    "clicked",
                    G_CALLBACK (on_down_toolbutton_clicked),
                    data);

  g_signal_connect (gtk_tree_view_get_selection (GTK_TREE_VIEW (data->treeview)),
                    "changed",
                    G_CALLBACK (on_tree_selection_changed),
                    data);

  /* ---------- */

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
gdu_create_raid_array_dialog_show (GduWindow    *window)
{
  DialogData *data;
  guint n;

  data = g_new0 (DialogData, 1);
  data->ref_count = 1;
  data->window = g_object_ref (window);
  data->client = gdu_window_get_client (data->window);

  data->dialog = GTK_WIDGET (gdu_application_new_widget (gdu_window_get_application (window),
                                                         "create-raid-array-dialog.ui",
                                                         "create-raid-array-dialog",
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

