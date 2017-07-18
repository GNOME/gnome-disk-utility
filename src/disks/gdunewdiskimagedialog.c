/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*-
 *
 * Copyright (C) 2017 Kai Lüke
 *
 * Licensed under GPL version 2 or later.
 *
 * Author: Kai Lüke <kailueke@riseup.net>
 */

#include "config.h"

#define _GNU_SOURCE
#include <fcntl.h>
#include <math.h>
#include <glib/gi18n.h>
#include <gio/gunixfdlist.h>
#include <gio/gunixinputstream.h>
#include <gio/gfiledescriptorbased.h>

#include <glib-unix.h>
#include <sys/ioctl.h>
#include <linux/fs.h>

#include "gduapplication.h"
#include "gduwindow.h"
#include "gdunewdiskimagedialog.h"
#include "gduutils.h"

/* TODOs / ideas for New Disk Image creation
 * include a radio toggle to create either
 * - a full disk image with a partition table or
 * - just a partition image with a filesystem
 * (embed respective widgets like gducreatepartitiondialog does)
 */

/* ---------------------------------------------------------------------------------------------------- */

typedef struct
{
  GduWindow *window;

  GtkBuilder *builder;
  GtkWidget *dialog;

  gint cur_unit_num;

  GtkWidget *size_spinbutton;
  GtkWidget *size_unit_combobox;
  GtkAdjustment *size_adjustment;
  GtkWidget *name_entry;
  GtkWidget *folder_fcbutton;

  GtkWidget *create_image_button;

  GFile *output_file;
  GFileOutputStream *output_file_stream;

  gulong response_signal_handler_id;

} DialogData;

static const struct {
  goffset offset;
  const gchar *name;
} widget_mapping[] = {
  {G_STRUCT_OFFSET (DialogData, size_spinbutton), "size-spinbutton"},
  {G_STRUCT_OFFSET (DialogData, size_unit_combobox), "size-unit-combobox"},
  {G_STRUCT_OFFSET (DialogData, size_adjustment), "size-adjustment"},
  {G_STRUCT_OFFSET (DialogData, name_entry), "name-entry"},
  {G_STRUCT_OFFSET (DialogData, folder_fcbutton), "folder-fcbutton"},
  {0, NULL}
};

/* ---------------------------------------------------------------------------------------------------- */

static void
dialog_data_hide (DialogData *data)
{
  if (data->dialog != NULL)
    {
      GtkWidget *dialog;
      if (data->response_signal_handler_id != 0)
        g_signal_handler_disconnect (data->dialog, data->response_signal_handler_id);
      dialog = data->dialog;
      data->dialog = NULL;
      gtk_widget_hide (dialog);
      gtk_widget_destroy (dialog);
      data->dialog = NULL;
    }
}

static void
dialog_data_unref (DialogData *data)
{
  dialog_data_hide (data);

  g_clear_object (&data->output_file_stream);
  g_object_unref (data->window);
  if (data->builder != NULL)
    g_object_unref (data->builder);
  g_free (data);
}

/* ---------------------------------------------------------------------------------------------------- */

static void
dialog_data_complete_and_unref (DialogData *data)
{
  dialog_data_hide (data);
  dialog_data_unref (data);
}

/* ---------------------------------------------------------------------------------------------------- */

static void
new_disk_image_update (DialogData *data)
{
  gboolean can_proceed = FALSE;

  if (gtk_adjustment_get_value (data->size_adjustment) > 0 &&
      strlen (gtk_entry_get_text (GTK_ENTRY (data->name_entry))) > 0)
    can_proceed = TRUE;

  gtk_dialog_set_response_sensitive (GTK_DIALOG (data->dialog), GTK_RESPONSE_OK, can_proceed);
}

static void
on_notify (GObject     *object,
           GParamSpec  *pspec,
           gpointer     user_data)
{
  DialogData *data = user_data;
  new_disk_image_update (data);
}


/* ---------------------------------------------------------------------------------------------------- */

static void
set_unit_num (DialogData *data,
              gint        unit_num)
{
  gdouble unit_size;
  gdouble value;
  gdouble value_units;

  g_assert (unit_num < NUM_UNITS);

  gtk_combo_box_set_active (GTK_COMBO_BOX (data->size_unit_combobox), unit_num);

  value = gtk_adjustment_get_value (data->size_adjustment) * ((gdouble) unit_sizes[data->cur_unit_num]);

  unit_size = unit_sizes[unit_num];
  value_units = value / unit_size;

  g_object_freeze_notify (G_OBJECT (data->size_adjustment));

  data->cur_unit_num = unit_num;

  gtk_adjustment_configure (data->size_adjustment,
                            value_units,
                            0.0,                    /* lower */
                            10000000000000.0,       /* upper */
                            1,                      /* step increment */
                            100,                    /* page increment */
                            0.0);                   /* page_size */

  gtk_spin_button_set_digits (GTK_SPIN_BUTTON (data->size_spinbutton), 3);

  gtk_adjustment_set_value (data->size_adjustment, value_units);

  g_object_thaw_notify (G_OBJECT (data->size_adjustment));
}

static void
new_disk_image_populate (DialogData *data)
{
  gchar *now_string;
  gchar *proposed_filename = NULL;
  GTimeZone *tz;
  GDateTime *now;

  tz = g_time_zone_new_local ();
  now = g_date_time_new_now (tz);
  now_string = g_date_time_format (now, "%Y-%m-%d %H%M");

  if (proposed_filename == NULL)
    {
      /* Translators: The suggested name for the disk image to create.
       *              The %s is today's date and time, e.g. "March 2, 1976 6:25AM".
       */
      proposed_filename = g_strdup_printf (_("Unnamed (%s).img"),
                                           now_string);
    }

  gtk_entry_set_text (GTK_ENTRY (data->name_entry), proposed_filename);
  g_free (proposed_filename);
  g_date_time_unref (now);
  g_time_zone_unref (tz);
  g_free (now_string);

  gdu_utils_configure_file_chooser_for_disk_images (GTK_FILE_CHOOSER (data->folder_fcbutton),
                                                    FALSE,   /* set file types */
                                                    FALSE);  /* allow_compressed */
  set_unit_num (data, data->cur_unit_num);
}

static void
on_size_unit_combobox_changed (GtkComboBox *combobox,
                               gpointer     user_data)
{
  DialogData *data = user_data;
  gint unit_num;

  unit_num = gtk_combo_box_get_active (GTK_COMBO_BOX (data->size_unit_combobox));
  set_unit_num (data, unit_num);

  new_disk_image_update (data);
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
start_creating (DialogData *data)
{
  gboolean ret = TRUE;
  const gchar *name;
  GFile *folder;
  GError *error;
  guint64 size;
  gchar *filename = NULL;

  name = gtk_entry_get_text (GTK_ENTRY (data->name_entry));
  folder = gtk_file_chooser_get_file (GTK_FILE_CHOOSER (data->folder_fcbutton));

  error = NULL;
  data->output_file = g_file_get_child (folder, name);
  filename = g_file_get_path(data->output_file);
  data->output_file_stream = g_file_replace (data->output_file,
                                             NULL, /* etag */
                                             FALSE, /* make_backup */
                                             G_FILE_CREATE_NONE,
                                             NULL,
                                             &error);
  if (data->output_file_stream == NULL)
    {
      gdu_utils_show_error (GTK_WINDOW (data->dialog), _("Error opening file for writing"), error);
      g_clear_error (&error);
      g_object_unref (folder);
      dialog_data_complete_and_unref (data);
      ret = FALSE;
      goto out;
    }

  /* now that we know the user picked a folder, update file chooser settings */
  gdu_utils_file_chooser_for_disk_images_set_default_folder (folder);

  size = gtk_adjustment_get_value (data->size_adjustment) * unit_sizes[data->cur_unit_num];
  /* will result in a sparse file if supported */
  if (!g_seekable_truncate (G_SEEKABLE(data->output_file_stream),
                            size,
                            NULL,
                            &error))
    {
      g_warning ("Error truncating file output stream: %s (%s, %d)",
                 error->message, g_quark_to_string (error->domain), error->code);
      gdu_utils_show_error (GTK_WINDOW (data->dialog), _("Error writing file"), error);
      g_clear_error (&error);
    }

  if (!g_output_stream_close (G_OUTPUT_STREAM (data->output_file_stream),
                              NULL, /* cancellable */
                              &error))
    {
      g_warning ("Error closing file output stream: %s (%s, %d)",
                 error->message, g_quark_to_string (error->domain), error->code);
      g_clear_error (&error);
    }
  g_clear_object (&data->output_file_stream);

  /* load loop device*/
  gdu_window_attach_disk_image_helper (data->window, filename, FALSE);

  dialog_data_hide (data);
 out:
  g_clear_object (&(data->output_file));
  g_free (filename);
  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */


/* returns TRUE if OK to overwrite or file doesn't exist */
static gboolean
check_overwrite (DialogData *data)
{
  GFile *folder = NULL;
  const gchar *name;
  gboolean ret = TRUE;
  GFile *file = NULL;
  GFileInfo *folder_info = NULL;
  GtkWidget *dialog;
  gint response;

  name = gtk_entry_get_text (GTK_ENTRY (data->name_entry));
  folder = gtk_file_chooser_get_file (GTK_FILE_CHOOSER (data->folder_fcbutton));
  file = g_file_get_child (folder, name);
  if (!g_file_query_exists (file, NULL))
    goto out;

  folder_info = g_file_query_info (folder,
                                   G_FILE_ATTRIBUTE_STANDARD_DISPLAY_NAME,
                                   G_FILE_QUERY_INFO_NONE,
                                   NULL,
                                   NULL);
  if (folder_info == NULL)
    goto out;

  dialog = gtk_message_dialog_new (GTK_WINDOW (data->dialog),
                                   GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                   GTK_MESSAGE_QUESTION,
                                   GTK_BUTTONS_NONE,
                                   _("A file named “%s” already exists.  Do you want to replace it?"),
                                   name);
  gtk_message_dialog_format_secondary_text (GTK_MESSAGE_DIALOG (dialog),
                                            _("The file already exists in “%s”.  Replacing it will overwrite its contents."),
                                            g_file_info_get_display_name (folder_info));
  gtk_dialog_add_button (GTK_DIALOG (dialog), _("_Cancel"), GTK_RESPONSE_CANCEL);
  gtk_dialog_add_button (GTK_DIALOG (dialog), _("_Replace"), GTK_RESPONSE_ACCEPT);
  gtk_dialog_set_default_response (GTK_DIALOG (dialog), GTK_RESPONSE_ACCEPT);
  response = gtk_dialog_run (GTK_DIALOG (dialog));

  if (response != GTK_RESPONSE_ACCEPT)
    ret = FALSE;

  gtk_widget_destroy (dialog);

 out:
  g_clear_object (&folder_info);
  g_clear_object (&file);
  g_clear_object (&folder);
  return ret;
}


static void
on_dialog_response (GtkDialog     *dialog,
                    gint           response,
                    gpointer       user_data)
{
  DialogData *data = user_data;

  switch (response)
    {
    case GTK_RESPONSE_OK:
      if (check_overwrite (data))
        {
          start_creating (data);
        }
      break;

    case GTK_RESPONSE_CLOSE:
      dialog_data_complete_and_unref (data);
      break;

    default: /* explicit fallthrough */
    case GTK_RESPONSE_CANCEL:
      dialog_data_complete_and_unref (data);
      break;
    }
}

void
gdu_new_disk_image_dialog_show (GduWindow *window)
{
  DialogData *data;
  guint n;

  data = g_new0 (DialogData, 1);
  data->window = g_object_ref (window);
  data->cur_unit_num = 3; /* initalize with GB as unit */

  data->dialog = GTK_WIDGET (gdu_application_new_widget (gdu_window_get_application (window),
                                                         "new-disk-image-dialog.ui",
                                                         "new-disk-image-dialog",
                                                         &data->builder));
  for (n = 0; widget_mapping[n].name != NULL; n++)
    {
      gpointer *p = (gpointer *) ((char *) data + widget_mapping[n].offset);
      *p = gtk_builder_get_object (data->builder, widget_mapping[n].name);
    }
  g_signal_connect (data->name_entry, "notify::text", G_CALLBACK (on_notify), data);
  g_signal_connect (data->size_adjustment, "notify::value", G_CALLBACK (on_notify), data);
  g_signal_connect (data->size_unit_combobox, "changed", G_CALLBACK (on_size_unit_combobox_changed), data);

  new_disk_image_populate (data);
  new_disk_image_update (data);

  gtk_dialog_set_default_response (GTK_DIALOG (data->dialog), GTK_RESPONSE_OK);

  data->response_signal_handler_id = g_signal_connect (data->dialog,
                                                       "response",
                                                       G_CALLBACK (on_dialog_response),
                                                       data);

  gtk_window_set_transient_for (GTK_WINDOW (data->dialog), GTK_WINDOW (window));
  gtk_window_present (GTK_WINDOW (data->dialog));

  /* Only select the precomputed filename, not the .img extension */
  gtk_editable_select_region (GTK_EDITABLE (data->name_entry), 0,
                              strlen (gtk_entry_get_text (GTK_ENTRY (data->name_entry))) - 4);
}

