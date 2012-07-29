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
#include <gio/gunixfdlist.h>
#include <gio/gunixinputstream.h>

#include <glib-unix.h>
#include <sys/ioctl.h>
#include <linux/fs.h>

#include "gduapplication.h"
#include "gduwindow.h"
#include "gducreatediskimagedialog.h"
#include "gduvolumegrid.h"
#include "gduutils.h"
#include "gducreatefilesystemwidget.h"
#include "gduestimator.h"

/* TODOs / ideas for Disk Image creation
 *
 * - Be tolerant of I/O errors like dd_rescue(1), see http://www.gnu.org/s/ddrescue/ddrescue.html
 * - Create images useful for Virtualization, e.g. vdi, vmdk, qcow2. Maybe use libguestfs for
 *   this. See http://libguestfs.org/
 * - Support a Apple DMG-ish format
 * - Sliding buffer size
 * - Update time remaining / speed exactly every 1/10th second instead of when we've read a full buffer
 *
 */

/* ---------------------------------------------------------------------------------------------------- */

/* TODO: make dynamic? */
#define BUFFER_SIZE (1*1024*1024)

typedef struct
{
  volatile gint ref_count;

  GduWindow *window;
  UDisksObject *object;
  UDisksBlock *block;
  UDisksDrive *drive;

  GtkBuilder *builder;
  GtkWidget *dialog;

  GtkWidget *notebook;
  GtkWidget *start_copying_button;
  GtkWidget *destination_name_entry;
  GtkWidget *destination_name_fcbutton;

  GtkWidget *copying_label;
  GtkWidget *copying_progressbar;
  GtkWidget *copying_progress_label;

  GCancellable *cancellable;
  GInputStream *block_stream;
  GFile *output_file;
  GFileOutputStream *output_file_stream;
  guint64 block_size;
  gboolean delete_on_free;

  guchar *buffer;
  guint64 total_bytes_read;
  guint64 buffer_bytes_written;
  guint64 buffer_bytes_to_write;

  GduEstimator *estimator;
} CreateDiskImageData;

static CreateDiskImageData *
create_disk_image_data_ref (CreateDiskImageData *data)
{
  g_atomic_int_inc (&data->ref_count);
  return data;
}

static void
create_disk_image_data_unref (CreateDiskImageData *data)
{
  if (g_atomic_int_dec_and_test (&data->ref_count))
    {
      if (data->dialog != NULL)
        {
          gtk_widget_hide (data->dialog);
          gtk_widget_destroy (data->dialog);
        }
      g_clear_object (&data->cancellable);
      g_clear_object (&data->output_file_stream);
      g_clear_object (&data->block_stream);
      if (data->delete_on_free)
        {
          GError *error = NULL;
          if (!g_file_delete (data->output_file, NULL, &error))
            {
              g_warning ("Error deleting file: %s (%s, %d)",
                         error->message, g_quark_to_string (error->domain), error->code);
              g_error_free (error);
            }
        }
      g_object_unref (data->window);
      g_object_unref (data->object);
      g_object_unref (data->block);
      g_clear_object (&data->drive);
      if (data->builder != NULL)
        g_object_unref (data->builder);
      g_free (data->buffer);
      g_clear_object (&data->estimator);
      g_free (data);
    }
}

/* ---------------------------------------------------------------------------------------------------- */

static void
create_disk_image_data_complete (CreateDiskImageData *data)
{
  g_cancellable_cancel (data->cancellable);
  gtk_dialog_response (GTK_DIALOG (data->dialog), GTK_RESPONSE_CANCEL);
}

/* ---------------------------------------------------------------------------------------------------- */

static void
create_disk_image_update (CreateDiskImageData *data)
{
  gboolean can_proceed = FALSE;

  if (strlen (gtk_entry_get_text (GTK_ENTRY (data->destination_name_entry))) > 0)
    can_proceed = TRUE;

  gtk_dialog_set_response_sensitive (GTK_DIALOG (data->dialog), GTK_RESPONSE_OK, can_proceed);
}

static void
on_notify (GObject     *object,
           GParamSpec  *pspec,
           gpointer     user_data)
{
  CreateDiskImageData *data = user_data;
  create_disk_image_update (data);
}


/* ---------------------------------------------------------------------------------------------------- */

static void
create_disk_image_populate (CreateDiskImageData *data)
{
  gchar *device_name;
  gchar *now_string;
  gchar *proposed_filename = NULL;
  guint n;
  GTimeZone *tz;
  GDateTime *now;
  const gchar *fstype;
  const gchar *fslabel;

  device_name = udisks_block_dup_preferred_device (data->block);
  if (g_str_has_prefix (device_name, "/dev/"))
    memmove (device_name, device_name + 5, strlen (device_name) - 5 + 1);
  for (n = 0; device_name[n] != '\0'; n++)
    {
      if (device_name[n] == '/')
        device_name[n] = '_';
    }

  tz = g_time_zone_new_local ();
  now = g_date_time_new_now (tz);
  now_string = g_date_time_format (now, "%Y-%m-%d %H%M");

  /* If it's an ISO/UDF filesystem, suggest a filename ending in .iso */
  fstype = udisks_block_get_id_type (data->block);
  fslabel = udisks_block_get_id_label (data->block);
  if (g_strcmp0 (fstype, "udf") == 0 || g_strcmp0 (fstype, "udf") == 0)
    {
      if (fslabel != NULL && strlen (fslabel) > 0)
        proposed_filename = g_strdup_printf ("%s.iso", fslabel);
    }

  if (proposed_filename == NULL)
    {
      /* Translators: The suggested name for the disk image to create.
       *              The first %s is a name for the disk (e.g. 'sdb').
       *              The second %s is today's date and time, e.g. "March 2, 1976 6:25AM".
       */
      proposed_filename = g_strdup_printf (_("Disk Image of %s (%s).img"),
                                           device_name,
                                           now_string);
    }

  gtk_entry_set_text (GTK_ENTRY (data->destination_name_entry), proposed_filename);
  g_free (proposed_filename);
  g_free (device_name);
  g_date_time_unref (now);
  g_time_zone_unref (tz);
  g_free (now_string);

  gdu_utils_configure_file_chooser_for_disk_images (GTK_FILE_CHOOSER (data->destination_name_fcbutton), FALSE);
}

/* ---------------------------------------------------------------------------------------------------- */

static void copy_more (CreateDiskImageData *data);

static void write_more (CreateDiskImageData *data);

static void
write_cb (GOutputStream  *output_stream,
          GAsyncResult  *res,
          gpointer       user_data)
{
  CreateDiskImageData *data = user_data;
  GError *error;
  gssize bytes_written;
  guint64 bytes_per_sec;
  guint64 usec_remaining;
  gchar *s, *s2, *s3, *s4, *s5;

  error = NULL;
  bytes_written = g_output_stream_write_finish (output_stream, res, &error);
  if (error != NULL)
    {
      if (!(error->domain == G_IO_ERROR && error->code == G_IO_ERROR_CANCELLED))
        gdu_window_show_error (data->window,
                               _("Error writing to backup image"), error);
      g_error_free (error);
      create_disk_image_data_complete (data);
      goto out;
    }

  data->buffer_bytes_written += bytes_written;
  data->buffer_bytes_to_write -= bytes_written;

  /* update progress bar and estimator */
  gtk_progress_bar_set_fraction (GTK_PROGRESS_BAR (data->copying_progressbar),
                                 ((gdouble) data->total_bytes_read) / ((gdouble) data->block_size));

  gdu_estimator_add_sample (data->estimator, data->total_bytes_read);
  bytes_per_sec = gdu_estimator_get_bytes_per_sec (data->estimator);
  usec_remaining = gdu_estimator_get_usec_remaining (data->estimator);
  if (bytes_per_sec > 0 && usec_remaining > 0)
    {
      s2 = g_format_size (data->total_bytes_read);
      s3 = g_format_size (data->block_size);
      s4 = gdu_utils_format_duration_usec (usec_remaining,
                                           GDU_FORMAT_DURATION_FLAGS_NO_SECONDS);
      s5 = g_format_size (bytes_per_sec);
      /* Translators: string used for conveying progress of copy operation.
       * The first two %s are strings with the amount of bytes (ex. "3.4 MB" and "300 MB").
       * The third %s is the estimated amount of time remaining (ex. "1 minute", "5 minutes" or "Less than a minute").
       * The fourth %s is the average amount of bytes transfered per second (ex. "8.9 MB").
       */
      s = g_strdup_printf (_("%s of %s copied – %s remaining (%s/sec)"), s2, s3, s4, s5);
      g_free (s5);
      g_free (s4);
      g_free (s3);
      g_free (s2);
    }
  else
    {
      s2 = g_format_size (data->total_bytes_read);
      s3 = g_format_size (data->block_size);
      /* Translators: string used for convey progress of a copy operation where we don't know time remaining / speed.
       * The first two %s are strings with the amount of bytes (ex. "3.4 MB" and "300 MB").
       */
      s = g_strdup_printf (_("%s of %s copied"), s2, s3);
      g_free (s2);
      g_free (s3);
    }
  s2 = g_strconcat ("<small>", s, "</small>", NULL);
  gtk_label_set_markup (GTK_LABEL (data->copying_progress_label), s2);
  g_free (s);

  write_more (data);

 out:
  create_disk_image_data_unref (data);
}

static void
write_more (CreateDiskImageData *data)
{
  if (data->buffer_bytes_to_write == 0)
    {
      copy_more (data);
    }
  else
    {
      g_output_stream_write_async (G_OUTPUT_STREAM (data->output_file_stream),
                                   data->buffer + data->buffer_bytes_written,
                                   data->buffer_bytes_to_write,
                                   G_PRIORITY_DEFAULT,
                                   data->cancellable,
                                   (GAsyncReadyCallback) write_cb,
                                   create_disk_image_data_ref (data));
    }
}


static void
read_cb (GInputStream  *input_stream,
         GAsyncResult  *res,
         gpointer       user_data)
{
  CreateDiskImageData *data = user_data;
  GError *error;
  gssize bytes_read;

  error = NULL;
  bytes_read = g_input_stream_read_finish (input_stream, res, &error);
  if (error != NULL)
    {
      gchar *s;
      s = g_strdup_printf (_("Error reading from offset %" G_GUINT64_FORMAT " of device %s"),
                           (guint64) data->total_bytes_read,
                           udisks_block_get_preferred_device (data->block));
      if (!(error->domain == G_IO_ERROR && error->code == G_IO_ERROR_CANCELLED))
        gdu_window_show_error (data->window, s, error);
      g_free (s);
      g_error_free (error);
      create_disk_image_data_complete (data);
      goto out;
    }

  data->total_bytes_read += bytes_read;

  data->buffer_bytes_written = 0;
  data->buffer_bytes_to_write = bytes_read;
  write_more (data);

 out:
  create_disk_image_data_unref (data);
}

static void
copy_more (CreateDiskImageData *data)
{
  guint64 bytes_to_read;

  bytes_to_read = data->block_size - data->total_bytes_read;
  if (bytes_to_read == 0)
    {
      data->delete_on_free = FALSE;
      create_disk_image_data_complete (data);
      goto out;
    }
  if (bytes_to_read > BUFFER_SIZE)
    bytes_to_read = BUFFER_SIZE;

  g_input_stream_read_async (data->block_stream,
                             data->buffer,
                             bytes_to_read,
                             G_PRIORITY_DEFAULT,
                             data->cancellable,
                             (GAsyncReadyCallback) read_cb,
                             create_disk_image_data_ref (data));
 out:
  ;
}

/* ---------------------------------------------------------------------------------------------------- */

static void
open_cb (UDisksBlock  *block,
         GAsyncResult *res,
         gpointer      user_data)
{
  CreateDiskImageData *data = user_data;
  GError *error;
  GUnixFDList *fd_list = NULL;
  GVariant *fd_index = NULL;
  int fd;

  error = NULL;
  if (!udisks_block_call_open_for_backup_finish (block,
                                                 &fd_index,
                                                 &fd_list,
                                                 res,
                                                 &error))
    {
      if (!(error->domain == G_IO_ERROR && error->code == G_IO_ERROR_CANCELLED))
        gdu_window_show_error (data->window, _("Error opening device"), error);
      g_error_free (error);
      create_disk_image_data_complete (data);
      goto out;
    }

  fd = g_unix_fd_list_get (fd_list, g_variant_get_handle (fd_index), NULL);

  /* We can't use udisks_block_get_size() because the media may have
   * changed and udisks may not have noticed. TODO: maybe have a
   * Block.GetSize() method instead...
   */
  if (ioctl (fd, BLKGETSIZE64, &data->block_size) != 0)
    {
      error = g_error_new (G_IO_ERROR, g_io_error_from_errno (errno), "%s", strerror (errno));
      gdu_window_show_error (data->window, _("Error determining size of device"), error);
      g_error_free (error);
      create_disk_image_data_complete (data);
      goto out;
    }

  /* now that we know the user picked a folder, update file chooser settings */
  gdu_utils_file_chooser_for_disk_images_update_settings (GTK_FILE_CHOOSER (data->destination_name_fcbutton));

  data->block_stream = g_unix_input_stream_new (fd, TRUE);

  /* Alright, time to start copying! */
  data->cancellable = g_cancellable_new ();
  data->buffer = g_new0 (guchar, BUFFER_SIZE);
  data->estimator = gdu_estimator_new (data->block_size);
  copy_more (data);

 out:
  if (fd_index != NULL)
    g_variant_unref (fd_index);
  g_clear_object (&fd_list);
}

/* returns TRUE if OK to overwrite or file doesn't exist */
static gboolean
check_overwrite (CreateDiskImageData *data)
{
  GFile *folder = NULL;
  const gchar *name;
  gboolean ret = TRUE;
  GFile *file = NULL;
  GFileInfo *folder_info = NULL;
  GtkWidget *dialog;
  gint response;

  name = gtk_entry_get_text (GTK_ENTRY (data->destination_name_entry));
  folder = gtk_file_chooser_get_file (GTK_FILE_CHOOSER (data->destination_name_fcbutton));
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
                                   _("A file named \"%s\" already exists.  Do you want to replace it?"),
                                   name);
  gtk_message_dialog_format_secondary_text (GTK_MESSAGE_DIALOG (dialog),
                                            _("The file already exists in \"%s\".  Replacing it will "
                                              "overwrite its contents."),
                                            g_file_info_get_display_name (folder_info));
  gtk_dialog_add_button (GTK_DIALOG (dialog), GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL);
  gtk_dialog_add_button (GTK_DIALOG (dialog), _("_Replace"), GTK_RESPONSE_ACCEPT);
  gtk_dialog_set_alternative_button_order (GTK_DIALOG (dialog),
                                           GTK_RESPONSE_ACCEPT,
                                           GTK_RESPONSE_CANCEL,
                                           -1);
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

static gboolean
start_copying (CreateDiskImageData *data)
{
  gboolean ret = TRUE;
  const gchar *name;
  GFile *folder;
  GError *error;

  name = gtk_entry_get_text (GTK_ENTRY (data->destination_name_entry));
  folder = gtk_file_chooser_get_file (GTK_FILE_CHOOSER (data->destination_name_fcbutton));

  error = NULL;
  data->output_file = g_file_get_child (folder, name);
  data->output_file_stream = g_file_replace (data->output_file,
                                             NULL, /* etag */
                                             FALSE, /* make_backup */
                                             G_FILE_CREATE_NONE,
                                             NULL,
                                             &error);
  if (data->output_file_stream == NULL)
    {
      if (!(error->domain == G_IO_ERROR && error->code == G_IO_ERROR_CANCELLED))
        gdu_window_show_error (data->window, _("Error opening file for writing"), error);
      g_error_free (error);
      g_object_unref (folder);
      create_disk_image_data_complete (data);
      ret = FALSE;
      goto out;
    }
  data->delete_on_free = TRUE;

  udisks_block_call_open_for_backup (data->block,
                                     g_variant_new ("a{sv}", NULL), /* options */
                                     NULL, /* fd_list */
                                     NULL, /* cancellable */
                                     (GAsyncReadyCallback) open_cb,
                                     data);

 out:
  g_clear_object (&folder);
  return ret;
}

void
gdu_create_disk_image_dialog_show (GduWindow    *window,
                                   UDisksObject *object)
{
  CreateDiskImageData *data;
  gint response;
  gchar *s;

  data = g_new0 (CreateDiskImageData, 1);
  data->ref_count = 1;
  data->window = g_object_ref (window);
  data->object = g_object_ref (object);
  data->block = udisks_object_get_block (object);
  g_assert (data->block != NULL);
  data->drive = udisks_client_get_drive_for_block (gdu_window_get_client (window), data->block);

  data->dialog = GTK_WIDGET (gdu_application_new_widget (gdu_window_get_application (window),
                                                         "create-disk-image-dialog.ui",
                                                         "create-disk-image-dialog",
                                                         &data->builder));
  data->notebook = GTK_WIDGET (gtk_builder_get_object (data->builder, "notebook"));
  data->start_copying_button = GTK_WIDGET (gtk_builder_get_object (data->builder, "start_copying_button"));
  data->destination_name_entry = GTK_WIDGET (gtk_builder_get_object (data->builder, "destination_name_entry"));
  g_signal_connect (data->destination_name_entry, "notify::text", G_CALLBACK (on_notify), data);
  data->destination_name_fcbutton = GTK_WIDGET (gtk_builder_get_object (data->builder, "destination_folder_fcbutton"));
  data->copying_label = GTK_WIDGET (gtk_builder_get_object (data->builder, "copying_label"));
  data->copying_progressbar = GTK_WIDGET (gtk_builder_get_object (data->builder, "copying_progressbar"));
  data->copying_progress_label = GTK_WIDGET (gtk_builder_get_object (data->builder, "copying_progress_label"));

  create_disk_image_populate (data);
  create_disk_image_update (data);

  /* Make sure we attach to parent */
  gtk_window_set_transient_for (GTK_WINDOW (data->dialog), GTK_WINDOW (window));
  gtk_dialog_set_default_response (GTK_DIALOG (data->dialog), GTK_RESPONSE_OK);
  gtk_widget_show_all (data->dialog);
  /* Only select the precomputed filename, not the .img extension */
  gtk_editable_select_region (GTK_EDITABLE (data->destination_name_entry), 0,
                              strlen (gtk_entry_get_text (GTK_ENTRY (data->destination_name_entry))) - 4);

 again:
  response = gtk_dialog_run (GTK_DIALOG (data->dialog));
  if (response != GTK_RESPONSE_OK)
    goto out;

  if (!check_overwrite (data))
    goto again;

  s = g_strdup_printf (_("Copying data from device <i>%s</i>..."),
                       udisks_block_get_preferred_device (data->block));
  gtk_label_set_markup (GTK_LABEL (data->copying_label), s);
  g_free (s);

  /* Advance to the progress page */
  gtk_notebook_set_current_page (GTK_NOTEBOOK (data->notebook), 1);
  gtk_widget_hide (data->start_copying_button);

  if (!start_copying (data))
    goto out;

  gtk_dialog_run (GTK_DIALOG (data->dialog));
  create_disk_image_data_complete (data);

 out:
  create_disk_image_data_unref (data);
}
