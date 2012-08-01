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
#include <gio/gunixfdlist.h>
#include <gio/gunixoutputstream.h>

#include <glib-unix.h>
#include <sys/ioctl.h>
#include <linux/fs.h>

#include "gduapplication.h"
#include "gduwindow.h"
#include "gdurestorediskimagedialog.h"
#include "gduvolumegrid.h"
#include "gduestimator.h"

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
  GtkWidget *source_file_fcbutton;

  GtkWidget *infobar_vbox;
  GtkWidget *warning_infobar;
  GtkWidget *warning_label;
  GtkWidget *error_infobar;
  GtkWidget *error_label;

  GtkWidget *copying_label;
  GtkWidget *copying_progressbar;
  GtkWidget *copying_progress_label;

  guint64 block_size;

  GCancellable *cancellable;
  GOutputStream *block_stream;
  GFileInputStream *input_file_stream;
  guint64 file_size;

  guchar *buffer;
  guint64 total_bytes_read;
  guint64 buffer_bytes_written;
  guint64 buffer_bytes_to_write;

  GduEstimator *estimator;

  gulong response_signal_handler_id;
  gboolean completed;
} RestoreDiskImageData;

static RestoreDiskImageData *
restore_disk_image_data_ref (RestoreDiskImageData *data)
{
  g_atomic_int_inc (&data->ref_count);
  return data;
}

static void
restore_disk_image_data_unref (RestoreDiskImageData *data)
{
  if (g_atomic_int_dec_and_test (&data->ref_count))
    {
      /* hide the dialog */
      if (data->dialog != NULL)
        {
          GtkWidget *dialog;
          if (data->response_signal_handler_id != 0)
            g_signal_handler_disconnect (data->dialog, data->response_signal_handler_id);
          dialog = data->dialog;
          data->dialog = NULL;
          gtk_widget_hide (dialog);
          gtk_widget_destroy (dialog);
        }
      g_clear_object (&data->cancellable);
      g_clear_object (&data->input_file_stream);
      g_clear_object (&data->block_stream);
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
rescan_cb (UDisksBlock   *block,
           GAsyncResult  *res,
           gpointer       user_data)
{
  RestoreDiskImageData *data = user_data;
  GError *error = NULL;
  if (!udisks_block_call_rescan_finish (block,
                                        res,
                                        &error))
    {
      gdu_utils_show_error (GTK_WINDOW (data->dialog), _("Error rescanning device"), error);
      g_clear_error (&error);
    }
  restore_disk_image_data_unref (data);
}

static void
restore_disk_image_data_complete (RestoreDiskImageData *data)
{
  if (!data->completed)
    {
      data->completed = TRUE;
      /* request that the core OS / kernel rescans the device */
      udisks_block_call_rescan (data->block,
                                g_variant_new ("a{sv}", NULL), /* options */
                                NULL, /* cancellable */
                                (GAsyncReadyCallback) rescan_cb,
                                restore_disk_image_data_ref (data));
      g_cancellable_cancel (data->cancellable);
      restore_disk_image_data_unref (data);
    }
}

/* ---------------------------------------------------------------------------------------------------- */

static void
restore_disk_image_update (RestoreDiskImageData *data)
{
  gboolean can_proceed = FALSE;
  gchar *restore_warning = NULL;
  gchar *restore_error = NULL;
  GFile *restore_file = NULL;

  if (data->dialog == NULL)
    goto out;

  /* don't update if we're already copying */
  if (data->buffer != NULL)
    goto out;

  /* Check if we have a file */
  restore_file = gtk_file_chooser_get_file (GTK_FILE_CHOOSER (data->source_file_fcbutton));
  if (restore_file != NULL)
    {
      GFileInfo *info;
      guint64 size;
      gchar *s;
      info = g_file_query_info (restore_file,
                                G_FILE_ATTRIBUTE_STANDARD_SIZE,
                                G_FILE_QUERY_INFO_NONE,
                                NULL,
                                NULL);
      size = g_file_info_get_size (info);
      g_object_unref (info);

      if (data->block_size > 0)
        {
          if (size == 0)
            {
              restore_error = g_strdup (_("Cannot restore image of size 0"));
            }
          else if (size < data->block_size)
            {
              /* Only complain if slack is bigger than 1MB */
              if (data->block_size - size > 1000L*1000L)
                {
                  s = udisks_client_get_size_for_display (gdu_window_get_client (data->window),
                                                          data->block_size - size, FALSE, FALSE);
                  restore_warning = g_strdup_printf (_("The selected image is %s smaller than the device"), s);
                  g_free (s);
                }
              can_proceed = TRUE;
            }
          else if (size > data->block_size)
            {
              s = udisks_client_get_size_for_display (gdu_window_get_client (data->window),
                                                      size - data->block_size, FALSE, FALSE);
              restore_error = g_strdup_printf (_("The selected image is %s bigger than the device"), s);
              g_free (s);
            }
          else
            {
              /* all good */
              can_proceed = TRUE;
            }
        }
    }

  if (restore_warning != NULL)
    {
      gtk_label_set_text (GTK_LABEL (data->warning_label), restore_warning);
      gtk_widget_show (data->warning_infobar);
    }
  else
    {
      gtk_widget_hide (data->warning_infobar);
    }
  if (restore_error != NULL)
    {
      gtk_label_set_text (GTK_LABEL (data->error_label), restore_error);
      gtk_widget_show (data->error_infobar);
    }
  else
    {
      gtk_widget_hide (data->error_infobar);
    }

  g_free (restore_warning);
  g_free (restore_error);
  g_clear_object (&restore_file);

  gtk_dialog_set_response_sensitive (GTK_DIALOG (data->dialog), GTK_RESPONSE_OK, can_proceed);

 out:
  ;
}

static void
on_file_set (GtkFileChooserButton   *button,
             gpointer                user_data)
{
  RestoreDiskImageData *data = user_data;
  restore_disk_image_update (data);
}

static void
on_notify (GObject    *object,
           GParamSpec *pspec,
           gpointer    user_data)
{
  RestoreDiskImageData *data = user_data;
  restore_disk_image_update (data);
}

/* ---------------------------------------------------------------------------------------------------- */

static void
restore_disk_image_populate (RestoreDiskImageData *data)
{
  gdu_utils_configure_file_chooser_for_disk_images (GTK_FILE_CHOOSER (data->source_file_fcbutton), TRUE);
}

/* ---------------------------------------------------------------------------------------------------- */

static void copy_more (RestoreDiskImageData *data);

static void write_more (RestoreDiskImageData *data);

static void
write_cb (GOutputStream  *output_stream,
          GAsyncResult  *res,
          gpointer       user_data)
{
  RestoreDiskImageData *data = user_data;
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
        gdu_utils_show_error (GTK_WINDOW (data->dialog),
                               _("Error writing to device"), error);
      g_error_free (error);
      restore_disk_image_data_complete (data);
      goto out;
    }

  data->buffer_bytes_written += bytes_written;
  data->buffer_bytes_to_write -= bytes_written;

  /* update progress bar and estimator */
  gtk_progress_bar_set_fraction (GTK_PROGRESS_BAR (data->copying_progressbar),
                                 ((gdouble) data->total_bytes_read) / ((gdouble) data->file_size));
  gdu_estimator_add_sample (data->estimator, data->total_bytes_read);
  bytes_per_sec = gdu_estimator_get_bytes_per_sec (data->estimator);
  usec_remaining = gdu_estimator_get_usec_remaining (data->estimator);
  if (bytes_per_sec > 0 && usec_remaining > 0)
    {
      s2 = g_format_size (data->total_bytes_read);
      s3 = g_format_size (data->file_size);
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
      s3 = g_format_size (data->file_size);
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
  restore_disk_image_data_unref (data);
}

static void
write_more (RestoreDiskImageData *data)
{
  if (data->buffer_bytes_to_write == 0)
    {
      copy_more (data);
    }
  else
    {
      g_output_stream_write_async (data->block_stream,
                                   data->buffer + data->buffer_bytes_written,
                                   data->buffer_bytes_to_write,
                                   G_PRIORITY_DEFAULT,
                                   data->cancellable,
                                   (GAsyncReadyCallback) write_cb,
                                   restore_disk_image_data_ref (data));
    }
}


static void
read_cb (GInputStream  *input_stream,
         GAsyncResult  *res,
         gpointer       user_data)
{
  RestoreDiskImageData *data = user_data;
  GError *error;
  gssize bytes_read;

  error = NULL;
  bytes_read = g_input_stream_read_finish (input_stream, res, &error);
  if (error != NULL)
    {
      gchar *s;
      s = g_strdup_printf (_("Error reading from offset %" G_GUINT64_FORMAT " of file"),
                           (guint64) data->total_bytes_read);
      if (!(error->domain == G_IO_ERROR && error->code == G_IO_ERROR_CANCELLED))
        gdu_utils_show_error (GTK_WINDOW (data->dialog), s, error);
      g_free (s);
      g_error_free (error);
      restore_disk_image_data_complete (data);
      goto out;
    }

  data->total_bytes_read += bytes_read;

  data->buffer_bytes_written = 0;
  data->buffer_bytes_to_write = bytes_read;
  write_more (data);

 out:
  restore_disk_image_data_unref (data);
}

static void
copy_more (RestoreDiskImageData *data)
{
  guint64 bytes_to_read;

  bytes_to_read = data->file_size - data->total_bytes_read;
  if (bytes_to_read == 0)
    {
      restore_disk_image_data_complete (data);
      goto out;
    }
  if (bytes_to_read > BUFFER_SIZE)
    bytes_to_read = BUFFER_SIZE;

  g_input_stream_read_async (G_INPUT_STREAM (data->input_file_stream),
                             data->buffer,
                             bytes_to_read,
                             G_PRIORITY_DEFAULT,
                             data->cancellable,
                             (GAsyncReadyCallback) read_cb,
                             restore_disk_image_data_ref (data));
 out:
  ;
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
start_copying (RestoreDiskImageData *data)
{
  GFile *file = NULL;
  gboolean ret = FALSE;
  GFileInfo *info;
  GError *error;

  error = NULL;
  file = gtk_file_chooser_get_file (GTK_FILE_CHOOSER (data->source_file_fcbutton));
  data->input_file_stream = g_file_read (file,
                                         NULL,
                                         &error);
  if (data->input_file_stream == NULL)
    {
      if (!(error->domain == G_IO_ERROR && error->code == G_IO_ERROR_CANCELLED))
        gdu_utils_show_error (GTK_WINDOW (data->dialog), _("Error opening file for reading"), error);
      g_error_free (error);
      restore_disk_image_data_complete (data);
      goto out;
    }

  error = NULL;
  info = g_file_query_info (file,
                            G_FILE_ATTRIBUTE_STANDARD_SIZE,
                            G_FILE_QUERY_INFO_NONE,
                            NULL,
                            &error);
  if (info == NULL)
    {
      gdu_utils_show_error (GTK_WINDOW (data->dialog), _("Error determing size of file"), error);
      g_error_free (error);
      restore_disk_image_data_complete (data);
      goto out;
    }
  data->file_size = g_file_info_get_size (info);
  g_object_unref (info);

  /* Alright, time to start copying! */
  data->cancellable = g_cancellable_new ();
  data->buffer = g_new0 (guchar, BUFFER_SIZE);
  data->estimator = gdu_estimator_new (data->file_size);
  copy_more (data);

  ret = TRUE;

 out:
  g_clear_object (&file);
  return ret;
}

static void
on_dialog_response (GtkDialog     *dialog,
                    gint           response,
                    gpointer       user_data)
{
  RestoreDiskImageData *data = user_data;
  if (response == GTK_RESPONSE_OK)
    {
      if (!gdu_utils_show_confirmation (GTK_WINDOW (data->dialog),
                                        _("Are you sure you want to write the disk image to the device?"),
                                        _("All existing data will be lost"),
                                        _("_Restore")))
        {
          restore_disk_image_data_complete (data);
          goto out;
        }

      /* now that we know the user picked a folder, update file chooser settings */
      gdu_utils_file_chooser_for_disk_images_update_settings (GTK_FILE_CHOOSER (data->source_file_fcbutton));

      gtk_label_set_markup (GTK_LABEL (data->copying_label), _("Copying data to device..."));

      /* Advance to the progress page and hide infobars, if any */
      gtk_notebook_set_current_page (GTK_NOTEBOOK (data->notebook), 1);
      gtk_widget_hide (data->start_copying_button);
      gtk_widget_hide (data->infobar_vbox);

      start_copying (data);
    }
  else
    {
      restore_disk_image_data_complete (data);
    }
 out:
  ;
}

static void
gdu_restore_disk_image_dialog_show2 (RestoreDiskImageData *data)
{
  gchar *s;

  data->dialog = GTK_WIDGET (gdu_application_new_widget (gdu_window_get_application (data->window),
                                                         "restore-disk-image-dialog.ui",
                                                         "restore-disk-image-dialog",
                                                         &data->builder));
  data->notebook = GTK_WIDGET (gtk_builder_get_object (data->builder, "notebook"));
  data->start_copying_button = GTK_WIDGET (gtk_builder_get_object (data->builder, "start_copying_button"));
  data->source_file_fcbutton = GTK_WIDGET (gtk_builder_get_object (data->builder, "source_file_fcbutton"));
  g_signal_connect (data->source_file_fcbutton, "file-set",
                    G_CALLBACK (on_file_set), data);
  data->infobar_vbox = GTK_WIDGET (gtk_builder_get_object (data->builder, "infobar-vbox"));
  data->warning_infobar = gdu_utils_create_info_bar (GTK_MESSAGE_INFO, "", &data->warning_label);
  gtk_box_pack_start (GTK_BOX (data->infobar_vbox), data->warning_infobar, TRUE, TRUE, 0);
  gtk_widget_set_no_show_all (data->warning_infobar, TRUE);
  data->error_infobar = gdu_utils_create_info_bar (GTK_MESSAGE_ERROR, "", &data->error_label);
  gtk_box_pack_start (GTK_BOX (data->infobar_vbox), data->error_infobar, TRUE, TRUE, 0);
  gtk_widget_set_no_show_all (data->error_infobar, TRUE);
  data->copying_label = GTK_WIDGET (gtk_builder_get_object (data->builder, "copying_label"));
  data->copying_progressbar = GTK_WIDGET (gtk_builder_get_object (data->builder, "copying_progressbar"));
  data->copying_progress_label = GTK_WIDGET (gtk_builder_get_object (data->builder, "copying_progress_label"));

  restore_disk_image_populate (data);
  restore_disk_image_update (data);

  /* unfortunately, GtkFileChooserButton:file-set is not emitted when the user
   * unselects a file but we can work around that.. (TODO: file bug against gtk+)
   */
  g_signal_connect (data->source_file_fcbutton, "notify",
                    G_CALLBACK (on_notify), data);


  /* Translators: This is the window title for the non-modal "Restore Disk Image" dialog. The %s is the device. */
  s = g_strdup_printf (_("Restore Disk Image (%s)"),
                       udisks_block_get_preferred_device (data->block));
  gtk_window_set_title (GTK_WINDOW (data->dialog), s);
  g_free (s);

  data->response_signal_handler_id = g_signal_connect (data->dialog,
                                                       "response",
                                                       G_CALLBACK (on_dialog_response),
                                                       data);
  gtk_widget_show_all (data->dialog);
  gtk_window_present (GTK_WINDOW (data->dialog));
}

/* ---------------------------------------------------------------------------------------------------- */

static void
open_cb (UDisksBlock  *block,
         GAsyncResult *res,
         gpointer      user_data)
{
  RestoreDiskImageData *data = user_data;
  GError *error;
  GUnixFDList *fd_list = NULL;
  GVariant *fd_index = NULL;
  int fd;

  error = NULL;
  if (!udisks_block_call_open_for_restore_finish (block,
                                                  &fd_index,
                                                  &fd_list,
                                                  res,
                                                  &error))
    {
      if (!(error->domain == G_IO_ERROR && error->code == G_IO_ERROR_CANCELLED))
        gdu_utils_show_error (GTK_WINDOW (data->dialog), _("Error opening device"), error);
      g_error_free (error);
      restore_disk_image_data_complete (data);
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
      gdu_utils_show_error (GTK_WINDOW (data->dialog), _("Error determining size of device"), error);
      g_error_free (error);
      restore_disk_image_data_complete (data);
      goto out;
    }
  data->block_stream = g_unix_output_stream_new (fd, TRUE);

  /* OK, we can now show the dialog */
  gdu_restore_disk_image_dialog_show2 (data);

 out:
  if (fd_index != NULL)
    g_variant_unref (fd_index);
  g_clear_object (&fd_list);
}


void
gdu_restore_disk_image_dialog_show (GduWindow    *window,
                                   UDisksObject *object)
{
  RestoreDiskImageData *data;

  data = g_new0 (RestoreDiskImageData, 1);
  data->ref_count = 1;
  data->window = g_object_ref (window);
  data->object = g_object_ref (object);
  data->block = udisks_object_get_block (object);
  g_assert (data->block != NULL);
  data->drive = udisks_client_get_drive_for_block (gdu_window_get_client (window), data->block);

  /* first, open the device */
  udisks_block_call_open_for_restore (data->block,
                                      g_variant_new ("a{sv}", NULL), /* options */
                                      NULL, /* fd_list */
                                      NULL, /* cancellable */
                                      (GAsyncReadyCallback) open_cb,
                                      data);
}
