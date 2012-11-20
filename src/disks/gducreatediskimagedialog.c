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
#include <gio/gunixinputstream.h>

#include <glib-unix.h>
#include <sys/ioctl.h>
#include <linux/fs.h>

#include <canberra-gtk.h>

#include "gduapplication.h"
#include "gduwindow.h"
#include "gducreatediskimagedialog.h"
#include "gduvolumegrid.h"
#include "gducreatefilesystemwidget.h"
#include "gduestimator.h"

#include "gdudvdsupport.h"

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

typedef struct
{
  volatile gint ref_count;

  GduWindow *window;
  UDisksObject *object;
  UDisksBlock *block;
  UDisksDrive *drive;

  GtkBuilder *builder;
  GtkWidget *dialog;

  GtkWidget *source_label;
  GtkWidget *name_label;
  GtkWidget *name_entry;
  GtkWidget *folder_label;
  GtkWidget *folder_fcbutton;
  GtkWidget *destination_key_label;
  GtkWidget *destination_label;

  GtkWidget *copying_key_label;
  GtkWidget *copying_vbox;
  GtkWidget *copying_progressbar;
  GtkWidget *copying_label;

  GtkWidget *start_copying_button;
  GtkWidget *cancel_button;
  GtkWidget *close_button;
  GtkWidget *show_in_folder_button;

  GCancellable *cancellable;
  GFile *output_file;
  GFileOutputStream *output_file_stream;

  /* must hold copy_lock when reading/writing these */
  GMutex copy_lock;
  GduEstimator *estimator;

  gboolean retrieving_dvd_keys;
  guint64 num_error_bytes;
  gint64 start_time_usec;
  gint64 end_time_usec;

  guint update_id;
  GError *copy_error;

  gulong response_signal_handler_id;
  gboolean completed;

  guint inhibit_cookie;
} DialogData;

static const struct {
  goffset offset;
  const gchar *name;
} widget_mapping[] = {
  {G_STRUCT_OFFSET (DialogData, source_label), "source-label"},
  {G_STRUCT_OFFSET (DialogData, name_label), "name-label"},
  {G_STRUCT_OFFSET (DialogData, name_entry), "name-entry"},
  {G_STRUCT_OFFSET (DialogData, folder_label), "folder-label"},
  {G_STRUCT_OFFSET (DialogData, folder_fcbutton), "folder-fcbutton"},
  {G_STRUCT_OFFSET (DialogData, destination_key_label), "destination-key-label"},
  {G_STRUCT_OFFSET (DialogData, destination_label), "destination-label"},

  {G_STRUCT_OFFSET (DialogData, copying_key_label), "copying-key-label"},
  {G_STRUCT_OFFSET (DialogData, copying_vbox), "copying-vbox"},
  {G_STRUCT_OFFSET (DialogData, copying_progressbar), "copying-progressbar"},
  {G_STRUCT_OFFSET (DialogData, copying_label), "copying-label"},

  {G_STRUCT_OFFSET (DialogData, start_copying_button), "start-copying-button"},
  {G_STRUCT_OFFSET (DialogData, cancel_button), "cancel-button"},
  {G_STRUCT_OFFSET (DialogData, close_button), "close-button"},
  {G_STRUCT_OFFSET (DialogData, show_in_folder_button), "show-in-folder-button"},
  {0, NULL}
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
      g_clear_object (&data->output_file_stream);
      g_object_unref (data->window);
      g_object_unref (data->object);
      g_object_unref (data->block);
      g_clear_object (&data->drive);
      if (data->builder != NULL)
        g_object_unref (data->builder);
      g_clear_object (&data->estimator);
      g_mutex_clear (&data->copy_lock);
      g_free (data);
    }
}

static gboolean
unref_in_idle (gpointer user_data)
{
  DialogData *data = user_data;
  dialog_data_unref (data);
  return FALSE; /* remove source */
}

static void
dialog_data_unref_in_idle (DialogData *data)
{
  g_idle_add (unref_in_idle, data);
}

/* ---------------------------------------------------------------------------------------------------- */

static void
dialog_data_complete_and_unref (DialogData *data)
{
  if (!data->completed)
    {
      data->completed = TRUE;
      g_cancellable_cancel (data->cancellable);
    }
  if (data->inhibit_cookie > 0)
    {
      gtk_application_uninhibit (GTK_APPLICATION (gdu_window_get_application (data->window)),
                                 data->inhibit_cookie);
      data->inhibit_cookie = 0;
    }
  gtk_widget_hide (data->dialog);
  dialog_data_unref (data);
}

/* ---------------------------------------------------------------------------------------------------- */

static void
create_disk_image_update (DialogData *data)
{
  gboolean can_proceed = FALSE;

  if (strlen (gtk_entry_get_text (GTK_ENTRY (data->name_entry))) > 0)
    can_proceed = TRUE;

  gtk_dialog_set_response_sensitive (GTK_DIALOG (data->dialog), GTK_RESPONSE_OK, can_proceed);
}

static void
on_notify (GObject     *object,
           GParamSpec  *pspec,
           gpointer     user_data)
{
  DialogData *data = user_data;
  create_disk_image_update (data);
}


/* ---------------------------------------------------------------------------------------------------- */

static void
create_disk_image_populate (DialogData *data)
{
  UDisksObjectInfo *info = NULL;
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
  if (g_strcmp0 (fstype, "iso9660") == 0 || g_strcmp0 (fstype, "udf") == 0)
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

  gtk_entry_set_text (GTK_ENTRY (data->name_entry), proposed_filename);
  g_free (proposed_filename);
  g_free (device_name);
  g_date_time_unref (now);
  g_time_zone_unref (tz);
  g_free (now_string);

  gdu_utils_configure_file_chooser_for_disk_images (GTK_FILE_CHOOSER (data->folder_fcbutton), FALSE);

  /* Source label */
  info = udisks_client_get_object_info (gdu_window_get_client (data->window), data->object);
  gtk_label_set_text (GTK_LABEL (data->source_label), udisks_object_info_get_one_liner (info));
  g_clear_object (&info);
}

/* ---------------------------------------------------------------------------------------------------- */

static void
update_gui (DialogData *data,
            gboolean    done)
{
  guint64 bytes_completed = 0;
  guint64 bytes_target = 1;
  guint64 bytes_per_sec = 0;
  guint64 usec_remaining = 1;
  guint64 num_error_bytes = 0;
  gchar *s, *s2, *s3, *s4, *s5;
  gdouble progress;

  g_mutex_lock (&data->copy_lock);
  if (data->estimator != NULL)
    {
      bytes_per_sec = gdu_estimator_get_bytes_per_sec (data->estimator);
      usec_remaining = gdu_estimator_get_usec_remaining (data->estimator);
      bytes_completed = gdu_estimator_get_completed_bytes (data->estimator);
      bytes_target = gdu_estimator_get_target_bytes (data->estimator);
      num_error_bytes = data->num_error_bytes;
    }
  data->update_id = 0;
  g_mutex_unlock (&data->copy_lock);

  if (done)
    {
      gint64 duration_usec = data->end_time_usec - data->start_time_usec;
      s2 = g_format_size (bytes_target);
      s3 = gdu_utils_format_duration_usec (duration_usec, GDU_FORMAT_DURATION_FLAGS_SUBSECOND_PRECISION);
      s4 = g_format_size (G_USEC_PER_SEC * bytes_target / duration_usec);
      /* Translators: string used for conveying how long the copy took.
       *              The first %s is the amount of bytes copied (ex. "650 MB").
       *              The second %s is the time it took to copy (ex. "1 minute", or "Less than a minute").
       *              The third %s is the average amount of bytes transfered per second (ex. "8.9 MB").
       */
      s = g_strdup_printf (_("%s copied in %s (%s/sec)"), s2, s3, s4);
      g_free (s4);
      g_free (s3);
      g_free (s2);
    }
  else if (data->retrieving_dvd_keys)
    {
      s = g_strdup (_("Retrieving DVD keys"));
    }
  else if (bytes_per_sec > 0 && usec_remaining > 0)
    {
      s2 = g_format_size (bytes_completed);
      s3 = g_format_size (bytes_target);
      s4 = gdu_utils_format_duration_usec (usec_remaining,
                                           GDU_FORMAT_DURATION_FLAGS_NO_SECONDS);
      s5 = g_format_size (bytes_per_sec);
      /* Translators: string used for conveying progress of copy operation when there are no errors.
       *              The first %s is the amount of bytes copied (ex. "650 MB").
       *              The second %s is the size of the device (ex. "8.5 GB").
       *              The third %s is the estimated amount of time remaining (ex. "1 minute" or "5 minutes").
       *              The fourth %s is the average amount of bytes transfered per second (ex. "8.9 MB").
       */
      s = g_strdup_printf (_("%s of %s copied – %s remaining (%s/sec)"), s2, s3, s4, s5);
      g_free (s5);
      g_free (s4);
      g_free (s3);
      g_free (s2);
    }
  else
    {
      s2 = g_format_size (bytes_completed);
      s3 = g_format_size (bytes_target);
      /* Translators: string used for convey progress of a copy operation where we don't know time remaining / speed.
       * The first two %s are strings with the amount of bytes (ex. "3.4 MB" and "300 MB").
       */
      s = g_strdup_printf (_("%s of %s copied"), s2, s3);
      g_free (s2);
      g_free (s3);
    }

  if (num_error_bytes > 0)
    {
      s2 = g_format_size (num_error_bytes);
      /* Translators: Shown when there are read errors and we skip some data.
       *              The first %s is the amount of unreadable data (ex. "512 kB").
       */
      s3 = g_strdup_printf (_("%s replaced with zeroes because of read errors"), s2);
      /* TODO: once https://bugzilla.gnome.org/show_bug.cgi?id=657194 is resolved, use that instead
       * of hard-coding the color
       */
      s4 = g_strdup_printf ("%s\n<span foreground=\"#ff0000\">%s</span>", s, s3);
      g_free (s3);
      g_free (s2);
      g_free (s);
      s = s4;
    }

  s2 = g_strconcat ("<small>", s, "</small>", NULL);
  gtk_label_set_markup (GTK_LABEL (data->copying_label), s2);
  g_free (s);

  if (done)
    progress = 1.0;
  else
    progress = ((gdouble) bytes_completed) / ((gdouble) bytes_target);
  gtk_progress_bar_set_fraction (GTK_PROGRESS_BAR (data->copying_progressbar), progress);
}

/* ---------------------------------------------------------------------------------------------------- */

static void
play_complete_sound_and_uninhibit (DialogData *data)
{
  const gchar *sound_message;

  /* Translators: A descriptive string for the 'complete' sound, see CA_PROP_EVENT_DESCRIPTION */
  sound_message = _("Disk image copying complete");
  ca_gtk_play_for_widget (GTK_WIDGET (data->dialog), 0,
                          CA_PROP_EVENT_ID, "complete",
                          CA_PROP_EVENT_DESCRIPTION, sound_message,
                          NULL);

  if (data->inhibit_cookie > 0)
    {
      gtk_application_uninhibit (GTK_APPLICATION (gdu_window_get_application (data->window)),
                                 data->inhibit_cookie);
      data->inhibit_cookie = 0;
    }
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
on_update_ui (gpointer user_data)
{
  DialogData *data = user_data;
  update_gui (data, FALSE);
  dialog_data_unref (data);
  return FALSE; /* remove source */
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
on_show_error (gpointer user_data)
{
  DialogData *data = user_data;

  play_complete_sound_and_uninhibit (data);

  g_assert (data->copy_error != NULL);
  gdu_utils_show_error (GTK_WINDOW (data->dialog),
                        _("Error creating disk image"),
                        data->copy_error);
  g_clear_error (&data->copy_error);

  dialog_data_complete_and_unref (data);

  dialog_data_unref (data);
  return FALSE; /* remove source */
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
on_success (gpointer user_data)
{
  DialogData *data = user_data;

  update_gui (data, TRUE);

  gtk_widget_hide (data->cancel_button);
  gtk_widget_show (data->close_button);
  gtk_widget_show (data->show_in_folder_button);

  play_complete_sound_and_uninhibit (data);

  dialog_data_unref (data);
  return FALSE; /* remove source */
}

/* ---------------------------------------------------------------------------------------------------- */

/* Note that error on reading is *not* considered an error - instead 0
 * is returned.
 *
 * Error conditions include failure to seek or write to output.
 *
 * Returns: Number of bytes actually read (e.g. not include padding) -1 if @error is set.
 */
static gssize
copy_span (int              fd,
           GOutputStream   *output_stream,
           guint64          offset,
           guint64          size,
           guchar          *buffer,
           gboolean         pad_with_zeroes,
           GduDVDSupport   *dvd_support,
           GCancellable    *cancellable,
           GError         **error)
{
  gint64 ret = -1;
  ssize_t num_bytes_read;
  gsize num_bytes_to_write;

  g_return_val_if_fail (-1, buffer != NULL);
  g_return_val_if_fail (-1, G_IS_OUTPUT_STREAM (output_stream));
  g_return_val_if_fail (-1, buffer != NULL);
  g_return_val_if_fail (-1, cancellable == NULL || G_IS_CANCELLABLE (cancellable));
  g_return_val_if_fail (-1, error == NULL || *error == NULL);

  if (dvd_support != NULL)
    {
      num_bytes_read = gdu_dvd_support_read (dvd_support, fd, buffer, offset, size);
    }
  else
    {
      if (lseek (fd, offset, SEEK_SET) == (off_t) -1)
        {
          g_set_error (error,
                       G_IO_ERROR, g_io_error_from_errno (errno),
                       "Error seeking to offset %" G_GUINT64_FORMAT ": %s",
                       offset, strerror (errno));
          goto out;
        }
    read_again:
      num_bytes_read = read (fd, buffer, size);
      if (num_bytes_read < 0)
        {
          if (errno == EAGAIN || errno == EINTR)
            goto read_again;
        }
      else
        {
          /* EOF */
          if (num_bytes_read == 0)
            {
              g_set_error (error,
                           G_IO_ERROR, G_IO_ERROR_FAILED,
                           "Reading from offset %" G_GUINT64_FORMAT " returned zero bytes",
                           offset);
              goto out;
            }
        }
    }

  if (num_bytes_read < 0)
    {
      /* do not consider this an error - treat as zero bytes read */
      num_bytes_read = 0;
    }

  num_bytes_to_write = num_bytes_read;
  if (pad_with_zeroes && (guint64) num_bytes_read < size)
    {
      memset (buffer + num_bytes_read, 0, size - num_bytes_read);
      num_bytes_to_write = size;
    }

  if (!g_seekable_seek (G_SEEKABLE (output_stream),
                        offset,
                        G_SEEK_SET,
                        cancellable,
                        error))
    {
      g_prefix_error (error,
                      "Error seeking to offset %" G_GUINT64_FORMAT ": ",
                      offset);
      goto out;
    }

  if (!g_output_stream_write_all (G_OUTPUT_STREAM (output_stream),
                                  buffer,
                                  num_bytes_to_write,
                                  G_PRIORITY_DEFAULT,
                                  cancellable,
                                  error))
    {
      g_prefix_error (error,
                      "Error writing %" G_GUINT64_FORMAT " bytes to offset %" G_GUINT64_FORMAT ": ",
                      num_bytes_to_write,
                      offset);
      goto out;
    }

  ret = num_bytes_read;

 out:

  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

static gpointer
copy_thread_func (gpointer user_data)
{
  DialogData *data = user_data;
  GduDVDSupport *dvd_support = NULL;
  guchar *buffer_unaligned = NULL;
  guchar *buffer = NULL;
  guint64 block_device_size = 0;
  long page_size;
  GError *error = NULL;
  GError *error2 = NULL;
  gint64 last_update_usec = -1;
  gint fd = -1;
  gint buffer_size;
  guint64 num_bytes_completed = 0;

  /* default to 1 MiB blocks */
  buffer_size = (1 * 1024 * 1024);

  /* Most OSes put ACLs for logged-in users on /dev/sr* nodes (this is
   * so CD burning tools etc. work) so see if we can open the device
   * file ourselves. If so, great, since this avoids a polkit dialog.
   *
   * As opposed to udisks' OpenForBackup() we also avoid O_EXCL since
   * the disc is read-only by its very nature. As a side-effect this
   * allows creating a disk image of a mounted disc.
   */
  if (g_str_has_prefix (udisks_block_get_device (data->block), "/dev/sr"))
    {
      const gchar *device_file = udisks_block_get_device (data->block);
      fd = open (device_file, O_RDONLY);

      /* Use libdvdcss (if available on the system) on DVDs with UDF
       * filesystems - otherwise the backup process may fail because
       * of unreadable/scrambled sectors
       */
      if (g_strcmp0 (udisks_block_get_id_usage (data->block), "filesystem") == 0 &&
          g_strcmp0 (udisks_block_get_id_type (data->block), "udf") == 0 &&
          g_str_has_prefix (udisks_drive_get_media (data->drive), "optical_dvd"))
        {
          g_mutex_lock (&data->copy_lock);
          data->retrieving_dvd_keys = TRUE;
          g_mutex_unlock (&data->copy_lock);
          g_idle_add (on_update_ui, dialog_data_ref (data));

          dvd_support = gdu_dvd_support_new (device_file, udisks_block_get_size (data->block));

          g_mutex_lock (&data->copy_lock);
          data->retrieving_dvd_keys = FALSE;
          g_mutex_unlock (&data->copy_lock);
          g_idle_add (on_update_ui, dialog_data_ref (data));
        }
    }

  /* Otherwise, request the fd from udisks */
  if (fd == -1)
    {
      GUnixFDList *fd_list = NULL;
      GVariant *fd_index = NULL;
      if (!udisks_block_call_open_for_backup_sync (data->block,
                                                   g_variant_new ("a{sv}", NULL), /* options */
                                                   NULL, /* fd_list */
                                                   &fd_index,
                                                   &fd_list,
                                                   NULL, /* cancellable */
                                                   &error))
        goto out;

      fd = g_unix_fd_list_get (fd_list, g_variant_get_handle (fd_index), &error);
      if (error != NULL)
        {
          g_prefix_error (&error,
                          "Error extracing fd with handle %d from D-Bus message: ",
                          g_variant_get_handle (fd_index));
          goto out;
        }
      if (fd_index != NULL)
        g_variant_unref (fd_index);
      g_clear_object (&fd_list);
    }

  g_assert (fd != -1);

  /* We can't use udisks_block_get_size() because the media may have
   * changed and udisks may not have noticed. TODO: maybe have a
   * Block.GetSize() method instead...
   */
  if (ioctl (fd, BLKGETSIZE64, &block_device_size) != 0)
    {
      error = g_error_new (G_IO_ERROR, g_io_error_from_errno (errno),
                           "%s", strerror (errno));
      g_prefix_error (&error, _("Error determining size of device: "));
      goto out;
    }

  if (block_device_size == 0)
    {
      error = g_error_new (G_IO_ERROR, G_IO_ERROR_FAILED,
                           _("Device is size 0"));
      goto out;
    }

  page_size = sysconf (_SC_PAGESIZE);
  buffer_unaligned = g_new0 (guchar, buffer_size + page_size);
  buffer = (guchar*) (((gintptr) (buffer_unaligned + page_size)) & (~(page_size - 1)));

  g_mutex_lock (&data->copy_lock);
  data->estimator = gdu_estimator_new (block_device_size);
  data->update_id = 0;
  data->num_error_bytes = 0;
  data->start_time_usec = g_get_real_time ();
  g_mutex_unlock (&data->copy_lock);

  /* Read huge (e.g. 1 MiB) blocks and write it to the output
   * file even if it was only partially read.
   */
  num_bytes_completed = 0;
  while (num_bytes_completed < block_device_size)
    {
      gssize num_bytes_to_read;
      gssize num_bytes_read;
      gint64 now_usec;

      num_bytes_to_read = buffer_size;
      if (num_bytes_to_read + num_bytes_completed > block_device_size)
        num_bytes_to_read = block_device_size - num_bytes_completed;

      /* Update GUI - but only every 200 ms and only if last update isn't pending */
      g_mutex_lock (&data->copy_lock);
      now_usec = g_get_monotonic_time ();
      if (now_usec - last_update_usec > 200 * G_USEC_PER_SEC / 1000 || last_update_usec < 0)
        {
          if (num_bytes_completed > 0)
            gdu_estimator_add_sample (data->estimator, num_bytes_completed);
          if (data->update_id == 0)
            data->update_id = g_idle_add (on_update_ui, dialog_data_ref (data));
          last_update_usec = now_usec;
        }
      g_mutex_unlock (&data->copy_lock);

      num_bytes_read = copy_span (fd,
                                  G_OUTPUT_STREAM (data->output_file_stream),
                                  num_bytes_completed,
                                  num_bytes_to_read,
                                  buffer,
                                  TRUE, /* pad_with_zeroes */
                                  dvd_support,
                                  data->cancellable,
                                  &error);
      if (num_bytes_read < 0)
        goto out;

      /*g_print ("read %" G_GUINT64_FORMAT " bytes (requested %" G_GUINT64_FORMAT ") from offset %" G_GUINT64_FORMAT "\n",
               num_bytes_read,
               num_bytes_to_read,
               num_bytes_completed);*/

      if (num_bytes_read < num_bytes_to_read)
        {
          guint64 num_bytes_skipped = num_bytes_to_read - num_bytes_read;
          g_mutex_lock (&data->copy_lock);
          data->num_error_bytes += num_bytes_skipped;
          g_mutex_unlock (&data->copy_lock);
        }
      num_bytes_completed += num_bytes_to_read;
    }

 out:
  if (dvd_support != NULL)
    gdu_dvd_support_free (dvd_support);

  data->end_time_usec = g_get_real_time ();

  /* in either case, close the stream */
  if (!g_output_stream_close (G_OUTPUT_STREAM (data->output_file_stream),
                              NULL, /* cancellable */
                              &error2))
    {
      g_warning ("Error closing file output stream: %s (%s, %d)",
                 error2->message, g_quark_to_string (error2->domain), error2->code);
      g_clear_error (&error2);
    }
  g_clear_object (&data->output_file_stream);

  if (error != NULL)
    {
      /* show error in GUI */
      if (!(error->domain == G_IO_ERROR && error->code == G_IO_ERROR_CANCELLED))
        {
          data->copy_error = error; error = NULL;
          g_idle_add (on_show_error, dialog_data_ref (data));
        }
      g_clear_error (&error);

      /* Cleanup */
      if (!g_file_delete (data->output_file, NULL, &error))
        {
          g_warning ("Error deleting file: %s (%s, %d)",
                     error->message, g_quark_to_string (error->domain), error->code);
          g_clear_error (&error);
        }
    }
  else
    {
      /* success */
      g_idle_add (on_success, dialog_data_ref (data));
    }
  if (fd != -1 )
    {
      if (close (fd) != 0)
        g_warning ("Error closing fd: %m");
    }

  g_free (buffer_unaligned);

  dialog_data_unref_in_idle (data); /* unref on main thread */
  return NULL;
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
start_copying (DialogData *data)
{
  gboolean ret = TRUE;
  const gchar *name;
  GFile *folder;
  GError *error;
  gchar *uri = NULL;

  name = gtk_entry_get_text (GTK_ENTRY (data->name_entry));
  folder = gtk_file_chooser_get_file (GTK_FILE_CHOOSER (data->folder_fcbutton));

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
      gdu_utils_show_error (GTK_WINDOW (data->dialog), _("Error opening file for writing"), error);
      g_clear_error (&error);
      g_object_unref (folder);
      dialog_data_complete_and_unref (data);
      ret = FALSE;
      goto out;
    }

  uri = gdu_utils_get_pretty_uri (data->output_file);
  gtk_label_set_text (GTK_LABEL (data->destination_label), uri);

  /* now that we know the user picked a folder, update file chooser settings */
  gdu_utils_file_chooser_for_disk_images_update_settings (GTK_FILE_CHOOSER (data->folder_fcbutton));

  data->inhibit_cookie = gtk_application_inhibit (GTK_APPLICATION (gdu_window_get_application (data->window)),
                                                  GTK_WINDOW (data->dialog),
                                                  GTK_APPLICATION_INHIBIT_SUSPEND |
                                                  GTK_APPLICATION_INHIBIT_LOGOUT,
                                                  /* Translators: Reason why suspend/logout is being inhibited */
                                                  C_("create-inhibit-message", "Copying device to disk image"));

  g_thread_new ("copy-disk-image-thread",
                copy_thread_func,
                dialog_data_ref (data));

 out:
  g_clear_object (&folder);
  g_free (uri);
  return ret;
}

static void
show_items_cb (GObject       *source_object,
               GAsyncResult  *res,
               gpointer       user_data)
{
  DialogData *data = user_data;
  GError *error;
  error = NULL;
  if (!g_dbus_connection_call_finish (G_DBUS_CONNECTION (source_object),
                                      res,
                                      &error))
    {
      gdu_utils_show_error (GTK_WINDOW (data->dialog), _("Error showing item"), error);
      g_error_free (error);
    }
  dialog_data_unref (data);
}

/* http://www.freedesktop.org/wiki/Specifications/file-manager-interface */
static void
show_in_folder (DialogData *data)
{
  gchar *uris[2] = {NULL, NULL};
  GDBusConnection *session_bus;
  const gchar *startup_id;

  /* TODO: figure out how to set this */
  startup_id = "";

  uris[0] = g_file_get_uri (data->output_file);

  session_bus = g_bus_get_sync (G_BUS_TYPE_SESSION, NULL, NULL);
  g_dbus_connection_call (session_bus,
                          "org.freedesktop.FileManager1",
                          "/org/freedesktop/FileManager1",
                          "org.freedesktop.FileManager1",
                          "ShowItems",
                          g_variant_new ("(^ass)", uris, startup_id),
                          NULL, /* reply-type */
                          G_DBUS_CALL_FLAGS_NONE,
                          -1, /* timeout */
                          NULL,
                          show_items_cb,
                          dialog_data_ref (data));
  g_clear_object (&session_bus);
  g_free (uris[0]);
}



static void
ensure_unused_cb (GduWindow     *window,
                  GAsyncResult  *res,
                  gpointer       user_data)
{
  DialogData *data = user_data;
  if (gdu_window_ensure_unused_finish (window, res, NULL))
    {
      start_copying (data);
    }
  else
    {
      dialog_data_complete_and_unref (data);
    }
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
          /* Hide name, entry widgets, "Start Creating" button and show destination, progress widgets */
          gtk_widget_hide (data->name_label);
          gtk_widget_hide (data->name_entry);
          gtk_widget_hide (data->folder_label);
          gtk_widget_hide (data->folder_fcbutton);
          gtk_widget_show (data->destination_key_label);
          gtk_widget_show (data->destination_label);
          gtk_widget_show (data->copying_key_label);
          gtk_widget_show (data->copying_vbox);

          gtk_widget_hide (data->start_copying_button);

          /* ensure the device is unused (e.g. unmounted) before copying data from it... */
          gdu_window_ensure_unused (data->window,
                                    data->object,
                                    (GAsyncReadyCallback) ensure_unused_cb,
                                    NULL, /* GCancellable */
                                    data);
        }
      break;

    case GTK_RESPONSE_CLOSE:
      dialog_data_complete_and_unref (data);
      break;

    case 1: /* show_in_folder */
      show_in_folder (data);
      break;

    default: /* explicit fallthrough */
    case GTK_RESPONSE_CANCEL:
      dialog_data_complete_and_unref (data);
      break;
    }
}

void
gdu_create_disk_image_dialog_show (GduWindow    *window,
                                   UDisksObject *object)
{
  DialogData *data;
  guint n;

  data = g_new0 (DialogData, 1);
  data->ref_count = 1;
  g_mutex_init (&data->copy_lock);
  data->window = g_object_ref (window);
  data->object = g_object_ref (object);
  data->block = udisks_object_get_block (object);
  g_assert (data->block != NULL);
  data->drive = udisks_client_get_drive_for_block (gdu_window_get_client (window), data->block);
  data->cancellable = g_cancellable_new ();

  data->dialog = GTK_WIDGET (gdu_application_new_widget (gdu_window_get_application (window),
                                                         "create-disk-image-dialog.ui",
                                                         "create-disk-image-dialog",
                                                         &data->builder));
  for (n = 0; widget_mapping[n].name != NULL; n++)
    {
      gpointer *p = (gpointer *) ((char *) data + widget_mapping[n].offset);
      *p = gtk_builder_get_object (data->builder, widget_mapping[n].name);
    }
  g_signal_connect (data->name_entry, "notify::text", G_CALLBACK (on_notify), data);

  create_disk_image_populate (data);
  create_disk_image_update (data);

  gtk_dialog_set_default_response (GTK_DIALOG (data->dialog), GTK_RESPONSE_OK);

  data->response_signal_handler_id = g_signal_connect (data->dialog,
                                                       "response",
                                                       G_CALLBACK (on_dialog_response),
                                                       data);

  /* initially hide the destination, errors and progress widgets + close buttons */
  gtk_widget_hide (data->destination_key_label);
  gtk_widget_hide (data->destination_label);
  gtk_widget_hide (data->copying_key_label);
  gtk_widget_hide (data->copying_vbox);
  gtk_widget_show (data->dialog);
  gtk_widget_hide (data->close_button);
  gtk_widget_hide (data->show_in_folder_button);
  gtk_window_present (GTK_WINDOW (data->dialog));

  /* Only select the precomputed filename, not the .img / .iso extension */
  gtk_editable_select_region (GTK_EDITABLE (data->name_entry), 0,
                              strlen (gtk_entry_get_text (GTK_ENTRY (data->name_entry))) - 4);
}
