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

  GList *objects;

  UDisksClient *client;
  GduWindow *window;
  GtkBuilder *builder;

  GtkWidget *dialog;

  GtkWidget *infobar_vbox;

  GtkWidget *level_combobox;
  GtkWidget *chunk_combobox;
  GtkWidget *name_entry;
  GtkWidget *num_disks_label;
  GtkWidget *size_label;
} DialogData;

static const struct {
  goffset offset;
  const gchar *name;
} widget_mapping[] = {

  {G_STRUCT_OFFSET (DialogData, infobar_vbox), "infobar-vbox"},

  {G_STRUCT_OFFSET (DialogData, level_combobox), "level-combobox"},
  {G_STRUCT_OFFSET (DialogData, chunk_combobox), "chunk-combobox"},
  {G_STRUCT_OFFSET (DialogData, name_entry), "name-entry"},
  {G_STRUCT_OFFSET (DialogData, num_disks_label), "num-disks-label"},
  {G_STRUCT_OFFSET (DialogData, size_label), "size-label"},

  {0, NULL}
};

static void update_dialog (DialogData *data);


static void on_client_changed (UDisksClient  *client,
                               gpointer       user_data);


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

      g_list_free_full (data->objects, g_object_unref);
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
  /* don't recurse */
  if (data->in_update)
    goto out;

  data->in_update = TRUE;
  /* TODO */
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

enum
{
  COMBOBOX_MODEL_COLUMN_ID,
  COMBOBOX_MODEL_COLUMN_MARKUP,
  COMBOBOX_MODEL_COLUMN_SEPARATOR,
  COMBOBOX_MODEL_COLUMN_SENSITIVE,
  COMBOBOX_MODEL_N_COLUMNS,
};


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
  gchar *s;
  GtkListStore *model;

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
  /* defaults: RAID6, 512 KiB Chunk */
  gtk_combo_box_set_active_id (GTK_COMBO_BOX (data->level_combobox), "raid6");
  gtk_combo_box_set_active_id (GTK_COMBO_BOX (data->chunk_combobox), "chunk_512");

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

/* ---------------------------------------------------------------------------------------------------- */


void
gdu_create_raid_array_dialog_show (GduWindow *window,
                                   GList     *objects)
{
  DialogData *data;
  guint n;

  data = g_new0 (DialogData, 1);
  data->ref_count = 1;
  data->window = g_object_ref (window);
  data->client = gdu_window_get_client (data->window);
  data->objects = g_list_copy_deep (objects, (GCopyFunc) g_object_ref, NULL);

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

