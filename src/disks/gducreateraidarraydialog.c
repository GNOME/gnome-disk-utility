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
#include <stdlib.h>

#include "gduapplication.h"
#include "gduwindow.h"
#include "gducreateraidarraydialog.h"
#include "gduatasmartdialog.h"

/* ---------------------------------------------------------------------------------------------------- */

typedef struct
{
  volatile gint ref_count;

  GList *blocks;
  guint64 disk_size;
  guint num_disks;

  UDisksClient *client;
  GduWindow *window;
  GtkBuilder *builder;

  GtkWidget *dialog;

  GtkWidget *grid;
  GtkWidget *level_combobox;
  GtkWidget *chunk_combobox;
  GtkWidget *name_entry;
  GtkWidget *num_disks_label;
  GtkWidget *size_label;

  GList *blocks_ensure_iter;
} DialogData;

static const struct {
  goffset offset;
  const gchar *name;
} widget_mapping[] = {

  {G_STRUCT_OFFSET (DialogData, grid), "grid"},
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

      g_clear_object (&data->window);
      g_clear_object (&data->builder);

      g_list_free_full (data->blocks, g_object_unref);
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
  gboolean create_sensitive = FALSE;
  gboolean chunk_level_sensitive = TRUE;
  const gchar *raid_level;
  guint64 raid_size;
  gchar *s;

  create_sensitive = TRUE;

  raid_level = gtk_combo_box_get_active_id (GTK_COMBO_BOX (data->level_combobox));
  if (g_strcmp0 (raid_level, "raid0") == 0)
    {
      raid_size = data->disk_size * data->num_disks;
    }
  else if (g_strcmp0 (raid_level, "raid1") == 0)
    {
      chunk_level_sensitive = FALSE; /* does not make sense on RAID-1 */
      raid_size = data->disk_size;
    }
  else if (g_strcmp0 (raid_level, "raid4") == 0)
    {
      raid_size = data->disk_size * (data->num_disks - 1);
    }
  else if (g_strcmp0 (raid_level, "raid5") == 0)
    {
      raid_size = data->disk_size * (data->num_disks - 1);
    }
  else if (g_strcmp0 (raid_level, "raid6") == 0)
    {
      raid_size = data->disk_size * (data->num_disks - 2);
    }
  else if (g_strcmp0 (raid_level, "raid10") == 0)
    {
      /* Yes, MD RAID-10 makes sense with two drives - also see drivers/md/raid10.c
       * function raid10_size() for the formula
       *
       * The constants below stems from the fact that the default for
       * RAID-10 creation is "n2", e.g. two near copies.
       */
      gint num_far_copies = 1;
      gint num_near_copies = 2;
      raid_size  = data->disk_size / num_far_copies;
      raid_size *= data->num_disks;
      raid_size /= num_near_copies;
    }
  else
    {
      g_assert_not_reached ();
    }

  s = udisks_client_get_size_for_display (data->client, raid_size, FALSE, FALSE);
  gtk_label_set_text (GTK_LABEL (data->size_label), s);
  g_free (s);

  gtk_dialog_set_response_sensitive (GTK_DIALOG (data->dialog),
                                     GTK_RESPONSE_OK,
                                     create_sensitive);
  gtk_widget_set_sensitive (data->chunk_combobox, chunk_level_sensitive);
}

/* ---------------------------------------------------------------------------------------------------- */

static void
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
                   const gchar  *id,
                   gboolean      sensitive)
{
  gtk_list_store_insert_with_values (model, NULL /* out_iter */, G_MAXINT, /* position */
                                     COMBOBOX_MODEL_COLUMN_ID, id,
                                     COMBOBOX_MODEL_COLUMN_MARKUP, markup,
                                     COMBOBOX_MODEL_COLUMN_SENSITIVE, sensitive,
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
  gchar *s, *s2, *s3;
  GtkListStore *model;
  guint64 min_size;
  guint64 max_size;
  GList *l;

  /* ---------- */
  /* check disk size and get block objects */

  min_size = G_MAXUINT64;
  max_size = 0;
  data->num_disks = 0;
  for (l = data->blocks; l != NULL; l = l->next)
    {
      UDisksBlock *block = UDISKS_BLOCK (l->data);
      guint64 block_size = udisks_block_get_size (block);
      if (block_size > max_size)
        max_size = block_size;
      if (block_size < min_size)
        min_size = block_size;
      data->num_disks += 1;
    }

  data->disk_size = min_size;

  /* Translators: Shown in "Create RAID Array" dialog.
   *              The %d is number of disks and is always >= 2.
   */
  s2 = g_strdup_printf (dngettext (GETTEXT_PACKAGE,
                                   "%d disk",
                                   "%d disks",
                                   (gint) data->num_disks),
                        (gint) data->num_disks);
  s3 = udisks_client_get_size_for_display (data->client, data->disk_size, FALSE, FALSE);
  /* Translators: Shown in "Create RAID Array" dialog.
   *              The first %s is the number of disks e.g. '3 disks'.
   *              The second %s is the size of the disk e.g. '42 GB' or '3 TB'.
   */
  s = g_strdup_printf (_("%s of %s each"), s2, s3);
  gtk_label_set_text (GTK_LABEL (data->num_disks_label), s);
  /* size_label is set in update_dialog() */
  g_free (s3);
  g_free (s2);
  g_free (s);

  /* ---------- */
  /* 'RAID Level' combobox */
  model = combobox_init (data, data->level_combobox);
  s = gdu_utils_format_mdraid_level ("raid0", TRUE, TRUE);
  combobox_add_item (model, s, "raid0", TRUE);  g_free (s);
  s = gdu_utils_format_mdraid_level ("raid1", TRUE, TRUE);
  combobox_add_item (model, s, "raid1", TRUE);  g_free (s);
  s = gdu_utils_format_mdraid_level ("raid4", TRUE, TRUE);
  combobox_add_item (model, s, "raid4", TRUE);  g_free (s);
  s = gdu_utils_format_mdraid_level ("raid5", TRUE, TRUE);
  combobox_add_item (model, s, "raid5", TRUE);  g_free (s);
  s = gdu_utils_format_mdraid_level ("raid6", TRUE, TRUE);
  combobox_add_item (model, s, "raid6", data->num_disks > 2);  g_free (s);
  s = gdu_utils_format_mdraid_level ("raid10", TRUE, TRUE);
  combobox_add_item (model, s, "raid10", TRUE); g_free (s);

  /* ---------- */
  /* 'Chunk Size' combobox */
  model = combobox_init (data, data->chunk_combobox);
  s = udisks_client_get_size_for_display (data->client,   4*1024, TRUE, FALSE);
  combobox_add_item (model, s, "chunk_4", TRUE); g_free (s);
  s = udisks_client_get_size_for_display (data->client,   8*1024, TRUE, FALSE);
  combobox_add_item (model, s, "chunk_8", TRUE); g_free (s);
  s = udisks_client_get_size_for_display (data->client,  16*1024, TRUE, FALSE);
  combobox_add_item (model, s, "chunk_16", TRUE); g_free (s);
  s = udisks_client_get_size_for_display (data->client,  32*1024, TRUE, FALSE);
  combobox_add_item (model, s, "chunk_32", TRUE); g_free (s);
  s = udisks_client_get_size_for_display (data->client,  64*1024, TRUE, FALSE);
  combobox_add_item (model, s, "chunk_64", TRUE); g_free (s);
  s = udisks_client_get_size_for_display (data->client, 128*1024, TRUE, FALSE);
  combobox_add_item (model, s, "chunk_128", TRUE); g_free (s);
  s = udisks_client_get_size_for_display (data->client, 256*1024, TRUE, FALSE);
  combobox_add_item (model, s, "chunk_256", TRUE); g_free (s);
  s = udisks_client_get_size_for_display (data->client, 512*1024, TRUE, FALSE);
  combobox_add_item (model, s, "chunk_512", TRUE); g_free (s);
  s = udisks_client_get_size_for_display (data->client,1024*1024, TRUE, FALSE);
  combobox_add_item (model, s, "chunk_1024", TRUE); g_free (s);
  s = udisks_client_get_size_for_display (data->client,2048*1024, TRUE, FALSE);
  combobox_add_item (model, s, "chunk_2048", TRUE); g_free (s);


  /* ---------- */
  /* defaults: RAID-1 for two disks, RAID-5 for three disks, RAID-6 otherwise, 512 KiB Chunk */
  if (data->num_disks == 2)
    gtk_combo_box_set_active_id (GTK_COMBO_BOX (data->level_combobox), "raid1");
  else if (data->num_disks == 3)
    gtk_combo_box_set_active_id (GTK_COMBO_BOX (data->level_combobox), "raid5");
  else
    gtk_combo_box_set_active_id (GTK_COMBO_BOX (data->level_combobox), "raid6");
  gtk_combo_box_set_active_id (GTK_COMBO_BOX (data->chunk_combobox), "chunk_512");

  /* ---------- */

  g_signal_connect (data->level_combobox,
                    "notify::active",
                    G_CALLBACK (on_property_changed),
                    data);

  g_signal_connect (data->chunk_combobox,
                    "notify::active",
                    G_CALLBACK (on_property_changed),
                    data);

  g_signal_connect (data->name_entry,
                    "notify::text",
                    G_CALLBACK (on_property_changed),
                    data);

  g_signal_connect (data->client,
                    "changed",
                    G_CALLBACK (on_client_changed),
                    data);

  /* ---------- */

  gtk_widget_grab_focus (data->name_entry);

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

static void
mdraid_create_cb (GObject      *source_object,
                  GAsyncResult *res,
                  gpointer      user_data)
{
  DialogData *data = user_data;
  GError *error;
  gchar *array_objpath = NULL;

  error = NULL;
  if (!udisks_manager_call_mdraid_create_finish (UDISKS_MANAGER (source_object),
                                                 &array_objpath,
                                                 res,
                                                 &error))
    {
      gdu_utils_show_error (GTK_WINDOW (data->window), _("Error creating RAID array"), error);
      g_error_free (error);
    }
  else
    {
      UDisksObject *object;
      udisks_client_settle (data->client);
      object = udisks_client_get_object (data->client, array_objpath);
      gdu_window_select_object (data->window, object);
      g_object_unref (object);
    }
  dialog_data_unref (data);
  g_free (array_objpath);
}

static void ensure_next (DialogData *data);

static void
ensure_unused_cb (GduWindow     *window,
                  GAsyncResult  *res,
                  gpointer       user_data)
{
  DialogData *data = user_data;
  GVariantBuilder options_builder;
  const gchar *name;
  const gchar *level;
  guint64 chunk;
  GPtrArray *p;
  GList *l;

  if (!gdu_window_ensure_unused_finish (data->window, res, NULL))
    {
      /* fail and error dialog has already been presented */
      goto out;
    }
  else
    {
      if (data->blocks_ensure_iter != NULL)
        {
          ensure_next (dialog_data_ref (data));
          goto out;
        }
    }

  /* done ensuring all devices are not in use... now create the RAID array... */
  name = gtk_entry_get_text (GTK_ENTRY (data->name_entry));
  level = gtk_combo_box_get_active_id (GTK_COMBO_BOX (data->level_combobox));
  chunk = atoi (gtk_combo_box_get_active_id (GTK_COMBO_BOX (data->chunk_combobox)) + strlen ("chunk_"));
  chunk *= 1024;
  if (g_strcmp0 (level, "raid1") == 0)
    chunk = 0;

  p = g_ptr_array_new ();
  for (l = data->blocks; l != NULL; l = l->next)
    {
      UDisksBlock *block = UDISKS_BLOCK (l->data);
      GDBusObject *object;
      object = g_dbus_interface_get_object (G_DBUS_INTERFACE (block));
      g_ptr_array_add (p, (gpointer) g_dbus_object_get_object_path (object));
    }
  g_ptr_array_add (p, NULL);

  g_variant_builder_init (&options_builder, G_VARIANT_TYPE_VARDICT);
  udisks_manager_call_mdraid_create (udisks_client_get_manager (data->client),
                                     (const gchar* const *) p->pdata,
                                     level,
                                     name,
                                     chunk,
                                     g_variant_builder_end (&options_builder),
                                     NULL,                       /* GCancellable */
                                     (GAsyncReadyCallback) mdraid_create_cb,
                                     dialog_data_ref (data));
  g_ptr_array_free (p, TRUE);

 out:
  dialog_data_unref (data);
}

static void
ensure_next (DialogData *data)
{
  UDisksBlock *block;
  UDisksObject *object;

  g_assert (data->blocks_ensure_iter != NULL);

  block = UDISKS_BLOCK (data->blocks_ensure_iter->data);
  data->blocks_ensure_iter = data->blocks_ensure_iter->next;

  object = (UDisksObject *) g_dbus_interface_get_object (G_DBUS_INTERFACE (block));
  gdu_window_ensure_unused (data->window,
                            object,
                            (GAsyncReadyCallback) ensure_unused_cb,
                            NULL, /* GCancellable */
                            data);
}

gboolean
gdu_create_raid_array_dialog_show (GduWindow *window,
                                   GList     *blocks)
{
  gboolean ret = FALSE;
  DialogData *data;
  guint n;

  data = g_new0 (DialogData, 1);
  data->ref_count = 1;
  data->window = g_object_ref (window);
  data->client = gdu_window_get_client (data->window);
  data->blocks = g_list_copy_deep (blocks, (GCopyFunc) g_object_ref, NULL);
  data->num_disks = g_list_length (data->blocks);
  g_assert_cmpint (data->num_disks, >, 0);

  if (!gdu_util_is_same_size (blocks, &data->disk_size))
    {
      g_warning ("Disks are not the same size");
      goto out;
    }

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
  gtk_dialog_set_default_response (GTK_DIALOG (data->dialog), GTK_RESPONSE_OK);

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

        case GTK_RESPONSE_OK:
          {
            GList *objects = NULL, *l;

            for (l = data->blocks; l != NULL; l = l->next)
              {
                UDisksObject *object = (UDisksObject *) g_dbus_interface_get_object (G_DBUS_INTERFACE (l->data));
                if (object != NULL)
                  objects = g_list_append (objects, object);
              }
            if (!gdu_utils_show_confirmation (GTK_WINDOW (data->window),
                                              _("Are you sure you want to use the disks for a RAID array?"),
                                              _("Existing content on the devices will be erased"),
                                              _("C_reate"),
                                              NULL, NULL,
                                              gdu_window_get_client (data->window), objects))
              {
                g_list_free (objects);
                continue;
              }
            g_list_free (objects);

            /* First ensure all disks are unused... then if all that works, we
             * create the array - see ensure_unused_cb() above...
             */
            data->blocks_ensure_iter = data->blocks;
            ensure_next (dialog_data_ref (data));
            ret = TRUE;
            goto out;
          }
          break;

        default:
          goto out;
        }
    }
 out:
  dialog_data_close (data);
  dialog_data_unref (data);
  return ret;
}
