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
#include "gduselectdiskdialog.h"
#include "gdudevicetreemodel.h"

/* ---------------------------------------------------------------------------------------------------- */

typedef struct
{
  volatile gint ref_count;

  UDisksClient *client;
  GduApplication *application;
  GtkBuilder *builder;

  guint64 size;
  GduSelectDiskFlags flags;

  GtkWidget *dialog;

  GtkWidget *scrolledwindow;
  GtkWidget *treeview;

  GduDeviceTreeModel *model;

  GList *ret;
} DialogData;

static const struct {
  goffset offset;
  const gchar *name;
} widget_mapping[] = {
  {G_STRUCT_OFFSET (DialogData, scrolledwindow), "scrolledwindow"},
  {G_STRUCT_OFFSET (DialogData, treeview), "treeview"},
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

      g_clear_object (&data->application);
      g_clear_object (&data->builder);

      g_clear_object (&data->model);

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
  gboolean select_sensitive = FALSE;

  if (gtk_tree_selection_count_selected_rows (gtk_tree_view_get_selection (GTK_TREE_VIEW (data->treeview))) > 0)
    select_sensitive = TRUE;

  gtk_dialog_set_response_sensitive (GTK_DIALOG (data->dialog),
                                     GTK_RESPONSE_OK,
                                     select_sensitive);
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

static gint
device_sort_function (GtkTreeModel *model,
                      GtkTreeIter *a,
                      GtkTreeIter *b,
                      gpointer user_data)
{
  gchar *sa, *sb;

  gtk_tree_model_get (model, a,
                      GDU_DEVICE_TREE_MODEL_COLUMN_SORT_KEY, &sa,
                      -1);
  gtk_tree_model_get (model, b,
                      GDU_DEVICE_TREE_MODEL_COLUMN_SORT_KEY, &sb,
                      -1);

  return g_strcmp0 (sa, sb);
}

static void
on_row_inserted (GtkTreeModel *tree_model,
                 GtkTreePath  *path,
                 GtkTreeIter  *iter,
                 gpointer      user_data)
{
  DialogData *data = user_data;
  gtk_tree_view_expand_all (GTK_TREE_VIEW (data->treeview));
}

static void
on_tree_selection_changed (GtkTreeSelection *tree_selection,
                           gpointer          user_data)
{
  DialogData *data = user_data;
  update_dialog (data);
}

static gboolean
treeview_select_func (GtkTreeSelection *selection,
                      GtkTreeModel     *model,
                      GtkTreePath      *path,
                      gboolean          selected,
                      gpointer          user_data)
{
  /* DialogData *data = user_data; */
  gboolean selectable = FALSE;
  gboolean is_heading = FALSE;
  GtkTreeIter iter;

  gtk_tree_model_get_iter (model, &iter, path);
  gtk_tree_model_get (model,
                      &iter,
                      GDU_DEVICE_TREE_MODEL_COLUMN_IS_HEADING, &is_heading,
                      -1);

  /* headers are never selectable */
  if (is_heading)
    goto out;

  selectable = TRUE;

 out:
  return selectable;
}

static void
init_dialog (DialogData *data)
{
  GtkTreeViewColumn *column;
  GtkCellRenderer *renderer;

  data->model = gdu_device_tree_model_new (data->client);

  gtk_tree_view_set_model (GTK_TREE_VIEW (data->treeview), GTK_TREE_MODEL (data->model));
  gtk_tree_sortable_set_sort_column_id (GTK_TREE_SORTABLE (data->model),
                                        GDU_DEVICE_TREE_MODEL_COLUMN_SORT_KEY,
                                        GTK_SORT_ASCENDING);
  /* Force g_strcmp0() as the sort function otherwise ___aa won't come before ____b ... */
  gtk_tree_sortable_set_sort_func (GTK_TREE_SORTABLE (data->model),
                                   GDU_DEVICE_TREE_MODEL_COLUMN_SORT_KEY,
                                   device_sort_function,
                                   NULL, /* user_data */
                                   NULL); /* GDestroyNotify */

  column = gtk_tree_view_column_new ();
  gtk_tree_view_append_column (GTK_TREE_VIEW (data->treeview), column);

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
                "ellipsize", PANGO_ELLIPSIZE_MIDDLE,
                NULL);
  gtk_tree_view_column_pack_start (column, renderer, FALSE);
  gtk_tree_view_column_set_attributes (column,
                                       renderer,
                                       "markup", GDU_DEVICE_TREE_MODEL_COLUMN_NAME,
                                       NULL);
  renderer = gtk_cell_renderer_spinner_new ();
  g_object_set (G_OBJECT (renderer),
                "xalign", 1.0,
                NULL);
  gtk_tree_view_column_pack_end (column, renderer, FALSE);
  gtk_tree_view_column_set_attributes (column,
                                       renderer,
                                       "visible", GDU_DEVICE_TREE_MODEL_COLUMN_JOBS_RUNNING,
                                       "active", GDU_DEVICE_TREE_MODEL_COLUMN_JOBS_RUNNING,
                                       "pulse", GDU_DEVICE_TREE_MODEL_COLUMN_PULSE,
                                       NULL);

  gtk_tree_selection_set_mode (gtk_tree_view_get_selection (GTK_TREE_VIEW (data->treeview)),
                               data->flags & GDU_SELECT_DISK_FLAGS_ALLOW_MULTIPLE ?
                                 GTK_SELECTION_MULTIPLE : GTK_SELECTION_SINGLE);

  gtk_tree_selection_unselect_all (gtk_tree_view_get_selection (GTK_TREE_VIEW (data->treeview)));

  gtk_tree_selection_set_select_function (gtk_tree_view_get_selection (GTK_TREE_VIEW (data->treeview)),
                                          treeview_select_func,
                                          data,
                                          NULL); /* GDestroyNotify */
  g_signal_connect (gtk_tree_view_get_selection (GTK_TREE_VIEW (data->treeview)),
                    "changed",
                    G_CALLBACK (on_tree_selection_changed),
                    data);

  /* expand on insertion - hmm, I wonder if there's an easier way to do this */
  g_signal_connect (data->model,
                    "row-inserted",
                    G_CALLBACK (on_row_inserted),
                    data);
  gtk_tree_view_expand_all (GTK_TREE_VIEW (data->treeview));


  /* Dialog title */
  if (data->flags & GDU_SELECT_DISK_FLAGS_ALLOW_MULTIPLE)
    {
      gtk_window_set_title (GTK_WINDOW (data->dialog), C_("select-disk-dialog", "Select Disks"));
    }
  else
    {
      gtk_window_set_title (GTK_WINDOW (data->dialog), C_("select-disk-dialog", "Select Disk"));
    }

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
add_block_func (GtkTreeModel *model,
                GtkTreePath  *path,
                GtkTreeIter  *iter,
                gpointer      user_data)
{
  DialogData *data = user_data;
  UDisksBlock *block = NULL;

  gtk_tree_model_get (model, iter,
                      GDU_DEVICE_TREE_MODEL_COLUMN_BLOCK, &block,
                      -1);
  if (block != NULL)
    {
      data->ret = g_list_prepend (data->ret, block); /* adopts block */
    }
}

GList *
gdu_select_disk_dialog_show (GduApplication     *application,
                             GtkWindow          *parent_window,
                             guint64             size,
                             GduSelectDiskFlags  flags)
{
  GList *ret = NULL;
  DialogData *data;
  guint n;

  data = g_new0 (DialogData, 1);
  data->ref_count = 1;
  data->application = g_object_ref (application);
  data->client = gdu_application_get_client (data->application);
  data->size = size;
  data->flags = flags;

  data->dialog = GTK_WIDGET (gdu_application_new_widget (data->application,
                                                         "select-disk-dialog.ui",
                                                         "select-disk-dialog",
                                                         &data->builder));
  for (n = 0; widget_mapping[n].name != NULL; n++)
    {
      gpointer *p = (gpointer *) ((char *) data + widget_mapping[n].offset);
      *p = gtk_builder_get_object (data->builder, widget_mapping[n].name);
    }

  gtk_window_set_transient_for (GTK_WINDOW (data->dialog), parent_window);

  init_dialog (data);

  while (TRUE)
    {
      gint response;
      response = gtk_dialog_run (GTK_DIALOG (data->dialog));
      /* Keep in sync with .ui file */
      switch (response)
        {
        case GTK_RESPONSE_CLOSE: /* Close */
          break;

        case GTK_RESPONSE_OK:
          gtk_tree_selection_selected_foreach (gtk_tree_view_get_selection (GTK_TREE_VIEW (data->treeview)),
                                               add_block_func,
                                               data);
          ret = g_list_reverse (data->ret);
          /* TODO: ensure @ret is sorted according to sort function.
           *       Or does selected_foreach() guarantee that?
           */
          goto out;

        default:
          goto out;
        }
    }
 out:
  dialog_data_close (data);
  dialog_data_unref (data);

  return ret;
}

