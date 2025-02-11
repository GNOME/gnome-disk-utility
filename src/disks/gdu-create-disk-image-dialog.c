/*
 * Copyright (C) 2008-2013 Red Hat, Inc.
 *
 * Licensed under GPL version 2 or later.
 *
 * Author: David Zeuthen <zeuthen@gmail.com>
 */

#include "config.h"

#define _GNU_SOURCE
#include <fcntl.h>

#include <glib/gi18n.h>
#include <gio/gunixfdlist.h>
#include <gio/gunixinputstream.h>
#include <gio/gfiledescriptorbased.h>

#include <glib-unix.h>
#include <sys/ioctl.h>
#include <linux/fs.h>

#include "gdu-application.h"
#include "gdu-create-disk-image-dialog.h"
#include "gduestimator.h"
#include "gdulocaljob.h"

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

struct _GduCreateDiskImageDialog
{
  AdwDialog parent_instance;

  GtkWidget    *name_entry;
  GtkWidget    *location_entry;
  GtkWidget    *source_label;

  UDisksObject *object;
  UDisksBlock  *block;
  UDisksDrive  *drive;
  UDisksClient *client;

  GCancellable *cancellable;
  GFile *output_file;
  GFile *directory;
  GFileOutputStream *output_file_stream;

  /* must hold copy_lock when reading/writing these */
  GMutex copy_lock;
  GduEstimator *estimator;

  gboolean allocating_file;
  gboolean retrieving_dvd_keys;
  guint64 num_error_bytes;
  gint64 start_time_usec;
  gint64 end_time_usec;
  gboolean played_read_error_sound;

  guint update_id;
  GError *copy_error;

  gulong response_signal_handler_id;
  gboolean completed;

  guint inhibit_cookie;

  GduLocalJob *local_job;
};

G_DEFINE_TYPE (GduCreateDiskImageDialog, gdu_create_disk_image_dialog, ADW_TYPE_DIALOG)
/* ---------------------------------------------------------------------------------------------------- */

static gpointer
create_disk_dialog_get_window (GduCreateDiskImageDialog *self)
{
  return gtk_widget_get_ancestor (GTK_WIDGET (self), GTK_TYPE_WINDOW);
}

static void
dialog_data_terminate_job (GduCreateDiskImageDialog *self)
{
  if (self->local_job != NULL)
    {
      gdu_application_destroy_local_job ((gpointer)g_application_get_default (), self->local_job);
      self->local_job = NULL;
    }
}

static void
dialog_data_uninhibit (GduCreateDiskImageDialog *self)
{
  if (self->inhibit_cookie > 0)
    {
      gtk_application_uninhibit (GTK_APPLICATION ((gpointer)g_application_get_default ()),
                                 self->inhibit_cookie);
      self->inhibit_cookie = 0;
    }
}

static void
dialog_data_complete_and_unref (GduCreateDiskImageDialog *self)
{
  if (!self->completed)
    {
      self->completed = TRUE;
      g_cancellable_cancel (self->cancellable);
    }
  dialog_data_uninhibit (self);
}

/* ---------------------------------------------------------------------------------------------------- */

static void
play_read_error_sound (GduCreateDiskImageDialog *self)
{
  const gchar *sound_message;

  /* Translators: A descriptive string for the sound played when
   * there's a read error that's being ignored, see
   * CA_PROP_EVENT_DESCRIPTION
   */
  sound_message = _("Disk image read error");
  /* gtk4 todo : Find a replacement for this
  ca_gtk_play_for_widget (GTK_WIDGET (self->window), 0,
                          CA_PROP_EVENT_ID, "dialog-warning",
                          CA_PROP_EVENT_DESCRIPTION, sound_message,
                          NULL);
  */
}

/* ---------------------------------------------------------------------------------------------------- */

static void
update_job (GduCreateDiskImageDialog *self,
            gboolean    done)
{
  gchar *extra_markup = NULL;
  guint64 bytes_completed = 0;
  guint64 bytes_target = 0;
  guint64 bytes_per_sec = 0;
  guint64 usec_remaining = 0;
  guint64 num_error_bytes = 0;
  gdouble progress = 0.0;
  gchar *s2, *s3;

  g_mutex_lock (&self->copy_lock);
  if (self->estimator != NULL)
    {
      bytes_per_sec = gdu_estimator_get_bytes_per_sec (self->estimator);
      usec_remaining = gdu_estimator_get_usec_remaining (self->estimator);
      bytes_completed = gdu_estimator_get_completed_bytes (self->estimator);
      bytes_target = gdu_estimator_get_target_bytes (self->estimator);
      num_error_bytes = self->num_error_bytes;
    }
  self->update_id = 0;
  g_mutex_unlock (&self->copy_lock);

  if (self->allocating_file)
    {
      extra_markup = g_strdup (_("Allocating Disk Image"));
    }
  else if (self->retrieving_dvd_keys)
    {
      extra_markup = g_strdup (_("Retrieving DVD keys"));
    }

  if (num_error_bytes > 0)
    {
      s2 = g_format_size (num_error_bytes);
      /* Translators: Shown when there are read errors and we skip some data.
       *              The first %s is the amount of unreadable data (ex. "512 kB").
       */
      s3 = g_strdup_printf (_("%s unreadable (replaced with zeroes)"), s2);
      /* TODO: once https://bugzilla.gnome.org/show_bug.cgi?id=657194 is resolved, use that instead
       * of hard-coding the color
       */
      g_free (extra_markup);
      extra_markup = g_strdup_printf ("<span foreground=\"#ff0000\">%s</span>", s3);
      g_free (s3);
      g_free (s2);
    }

  if (self->local_job != NULL)
    {
      udisks_job_set_bytes (UDISKS_JOB (self->local_job), bytes_target);
      udisks_job_set_rate (UDISKS_JOB (self->local_job), bytes_per_sec);

      if (done)
        {
          progress = 1.0;
        }
      else
        {
          if (bytes_target != 0)
            progress = ((gdouble) bytes_completed) / ((gdouble) bytes_target);
          else
            progress = 0.0;
        }
      udisks_job_set_progress (UDISKS_JOB (self->local_job), progress);

      if (usec_remaining == 0)
        udisks_job_set_expected_end_time (UDISKS_JOB (self->local_job), 0);
      else
        udisks_job_set_expected_end_time (UDISKS_JOB (self->local_job), usec_remaining + g_get_real_time ());

      gdu_local_job_set_extra_markup (self->local_job, extra_markup);
    }

  /* Play a sound the first time we encounter a read error */
  if (num_error_bytes > 0 && !self->played_read_error_sound)
    {
      play_read_error_sound (self);
      self->played_read_error_sound = TRUE;
    }

  g_free (extra_markup);
}

/* ---------------------------------------------------------------------------------------------------- */

static void
play_complete_sound (GduCreateDiskImageDialog *self)
{
  const gchar *sound_message;

  /* Translators: A descriptive string for the 'complete' sound, see CA_PROP_EVENT_DESCRIPTION */
  sound_message = _("Disk image copying complete");
  /* gtk4 todo : Find a replacement for this
  ca_gtk_play_for_widget (GTK_WIDGET (self->window), 0,
                          CA_PROP_EVENT_ID, "complete",
                          CA_PROP_EVENT_DESCRIPTION, sound_message,
                          NULL);
  */
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
on_update_job (gpointer user_data)
{
  GduCreateDiskImageDialog *self = user_data;
  update_job (self, FALSE);
  return FALSE; /* remove source */
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
on_show_error (gpointer user_data)
{
  GduCreateDiskImageDialog *self = user_data;

  dialog_data_uninhibit (self);

  g_assert (self->copy_error != NULL);
  gdu_utils_show_error (create_disk_dialog_get_window (self),
                        _("Error creating disk image"),
                        self->copy_error);
  g_clear_error (&self->copy_error);

  dialog_data_complete_and_unref (self);

  return FALSE; /* remove source */
}

/* ---------------------------------------------------------------------------------------------------- */

static void
on_delete_response (GObject       *object,
                    GAsyncResult  *response,
                    gpointer       userdata)
{
  AdwAlertDialog *dialog = ADW_ALERT_DIALOG (object);
  GduCreateDiskImageDialog *self = userdata;
  g_autoptr(GError) error = NULL;
  g_autoptr(GFile) file = NULL;
  const char *name;

  if (g_strcmp0 (adw_alert_dialog_choose_finish(dialog, response), "cancel") == 0)
    return;
  name = gtk_editable_get_text (GTK_EDITABLE (self->name_entry));

  file = g_file_get_child (self->directory, name);
  if (!g_file_delete (file, NULL, &error))
    {
      g_warning ("Error deleting file: %s (%s, %d)",
                  error->message, g_quark_to_string (error->domain), error->code);
    }
}

static gboolean
on_success (gpointer user_data)
{
  AdwDialog *dialog;
  gdouble percentage;
  g_autofree gchar *s = NULL;
  GduCreateDiskImageDialog *self = user_data;

  update_job (self, TRUE);

  play_complete_sound (self);
  dialog_data_uninhibit (self);
  dialog_data_complete_and_unref (self);

  /* OK, we're done but we had to replace unreadable data with
   * zeroes. Bring up a modal dialog to inform the user of this and
   * allow him to delete the file, if so desired.
   */
  if (self->num_error_bytes > 0)
    {
      dialog = adw_alert_dialog_new (/* Translators: Heading in dialog shown if some data was unreadable while creating a disk image */
                                       _("Unrecoverable Read Errors"),
                                       NULL);

      s = g_format_size (self->num_error_bytes);
      percentage = 100.0 * ((gdouble) self->num_error_bytes) / ((gdouble) gdu_estimator_get_target_bytes (self->estimator));

      adw_alert_dialog_format_body (ADW_ALERT_DIALOG (dialog),
                                    /* Translators: Body in dialog shown if some data was unreadable while creating a disk image.
                                     * The %f is the percentage of unreadable data (ex. 13.0).
                                     * The first %s is the amount of unreadable data (ex. "4.2 MB").
                                     * The second %s is the name of the device (ex "/dev/").
                                     */
                                    _("%2.1f%% (%s) of the data on the device “%s” was unreadable and replaced with zeroes in the created disk image file. This typically happens if the medium is scratched or if there is physical damage to the drive."),
                                    percentage,
                                    s,
                                    gtk_label_get_text (GTK_LABEL (self->source_label)));

      adw_alert_dialog_add_responses (ADW_ALERT_DIALOG (dialog),
                                      "cancel",  _("_Cancel"),
                                      "confirm", _("_Delete Disk Image File"),
                                      NULL);

      adw_alert_dialog_set_default_response (ADW_ALERT_DIALOG (dialog), "cancel");
      adw_alert_dialog_set_response_appearance (ADW_ALERT_DIALOG (dialog),
                                                "confirm",
                                                ADW_RESPONSE_DESTRUCTIVE);

      adw_alert_dialog_choose (ADW_ALERT_DIALOG (dialog),
                               NULL,
                               NULL,
                               on_delete_response,
                               self);
    }

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
                      "Error writing %" G_GSIZE_FORMAT " bytes to offset %" G_GUINT64_FORMAT ": ",
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
  GduCreateDiskImageDialog *self = user_data;
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
  if (g_str_has_prefix (udisks_block_get_device (self->block), "/dev/sr"))
    {
      const gchar *device_file = udisks_block_get_device (self->block);
      fd = open (device_file, O_RDONLY);

      /* Use libdvdcss (if available on the system) on DVDs with UDF
       * filesystems - otherwise the backup process may fail because
       * of unreadable/scrambled sectors
       */
      if (g_strcmp0 (udisks_block_get_id_usage (self->block), "filesystem") == 0 &&
          g_strcmp0 (udisks_block_get_id_type (self->block), "udf") == 0 &&
          g_str_has_prefix (udisks_drive_get_media (self->drive), "optical_dvd"))
        {
          g_mutex_lock (&self->copy_lock);
          self->retrieving_dvd_keys = TRUE;
          g_mutex_unlock (&self->copy_lock);
          g_idle_add (on_update_job, self);

          dvd_support = gdu_dvd_support_new (device_file, udisks_block_get_size (self->block));

          g_mutex_lock (&self->copy_lock);
          self->retrieving_dvd_keys = FALSE;
          g_mutex_unlock (&self->copy_lock);
          g_idle_add (on_update_job, self);
        }
    }

  /* Otherwise, request the fd from udisks */
  if (fd == -1)
    {
      GUnixFDList *fd_list = NULL;
      GVariant *fd_index = NULL;
      if (!udisks_block_call_open_for_backup_sync (self->block,
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

  /* If supported, allocate space at once to ensure blocks are laid
   * out contigously, see http://lwn.net/Articles/226710/
   */
  if (G_IS_FILE_DESCRIPTOR_BASED (self->output_file_stream))
    {
      gint output_fd = g_file_descriptor_based_get_fd (G_FILE_DESCRIPTOR_BASED (self->output_file_stream));
      gint rc;

      g_mutex_lock (&self->copy_lock);
      self->allocating_file = TRUE;
      g_mutex_unlock (&self->copy_lock);
      g_idle_add (on_update_job, self);

      rc = fallocate (output_fd,
                      0, /* mode */
                      (off_t) 0,
                      (off_t) block_device_size);

      if (rc != 0)
        {
          if (errno == ENOSYS || errno == EOPNOTSUPP)
            {
              /* If the kernel or filesystem does not support it, too
               * bad. Just continue.
               */
            }
          else
            {
              error = g_error_new (G_IO_ERROR, g_io_error_from_errno (errno), "%s", strerror (errno));
              g_prefix_error (&error, _("Error allocating space for disk image file: "));
              goto out;
            }
        }

      g_mutex_lock (&self->copy_lock);
      self->allocating_file = FALSE;
      g_mutex_unlock (&self->copy_lock);
      g_idle_add (on_update_job, self);
    }

  page_size = sysconf (_SC_PAGESIZE);
  buffer_unaligned = g_new0 (guchar, buffer_size + page_size);
  buffer = (guchar*) (((gintptr) (buffer_unaligned + page_size)) & (~(page_size - 1)));

  g_mutex_lock (&self->copy_lock);
  self->estimator = gdu_estimator_new (block_device_size);
  self->update_id = 0;
  self->num_error_bytes = 0;
  self->start_time_usec = g_get_real_time ();
  g_mutex_unlock (&self->copy_lock);

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
      g_mutex_lock (&self->copy_lock);
      now_usec = g_get_monotonic_time ();
      if (now_usec - last_update_usec > 200 * G_USEC_PER_SEC / 1000 || last_update_usec < 0)
        {
          if (num_bytes_completed > 0)
            gdu_estimator_add_sample (self->estimator, num_bytes_completed);
          if (self->update_id == 0)
            self->update_id = g_idle_add (on_update_job, self);
          last_update_usec = now_usec;
        }
      g_mutex_unlock (&self->copy_lock);

      num_bytes_read = copy_span (fd,
                                  G_OUTPUT_STREAM (self->output_file_stream),
                                  num_bytes_completed,
                                  num_bytes_to_read,
                                  buffer,
                                  TRUE, /* pad_with_zeroes */
                                  dvd_support,
                                  self->cancellable,
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
          g_mutex_lock (&self->copy_lock);
          self->num_error_bytes += num_bytes_skipped;
          g_mutex_unlock (&self->copy_lock);
        }
      num_bytes_completed += num_bytes_to_read;
    }

 out:
  if (dvd_support != NULL)
    gdu_dvd_support_free (dvd_support);

  self->end_time_usec = g_get_real_time ();

  /* in either case, close the stream */
  if (!g_output_stream_close (G_OUTPUT_STREAM (self->output_file_stream),
                              NULL, /* cancellable */
                              &error2))
    {
      g_warning ("Error closing file output stream: %s (%s, %d)",
                 error2->message, g_quark_to_string (error2->domain), error2->code);
      g_clear_error (&error2);
    }
  g_clear_object (&self->output_file_stream);

  if (error != NULL)
    {
      /* show error in GUI */
      if (!(error->domain == G_IO_ERROR && error->code == G_IO_ERROR_CANCELLED))
        {
          self->copy_error = error; error = NULL;
          g_idle_add (on_show_error, self);
        }
      g_clear_error (&error);

      /* Cleanup */
      if (!g_file_delete (self->output_file, NULL, &error))
        {
          g_warning ("Error deleting file: %s (%s, %d)",
                     error->message, g_quark_to_string (error->domain), error->code);
          g_clear_error (&error);
        }
    }
  else
    {
      /* success */
      g_idle_add (on_success, self);
    }
  if (fd != -1 )
    {
      if (close (fd) != 0)
        g_warning ("Error closing fd: %m");
    }

  g_free (buffer_unaligned);

  return NULL;
}

/* ---------------------------------------------------------------------------------------------------- */

static void
on_local_job_canceled (GduLocalJob  *job,
                       gpointer      user_data)
{
  GduCreateDiskImageDialog *self = user_data;
  if (!self->completed)
    {
      dialog_data_terminate_job (self);
      dialog_data_complete_and_unref (self);
      update_job (self, FALSE);
    }
}

static void
start_copying (GduCreateDiskImageDialog *self)
{
  const gchar *name;
  g_autoptr(GError) error = NULL;

  name = gtk_editable_get_text (GTK_EDITABLE (self->name_entry));

  self->output_file = g_file_get_child (self->directory, name);
  self->output_file_stream = g_file_replace (self->output_file,
                                             NULL, /* etag */
                                             FALSE, /* make_backup */
                                             G_FILE_CREATE_NONE,
                                             NULL,
                                             &error);
  if (self->output_file_stream == NULL)
    {
      gdu_utils_show_error (create_disk_dialog_get_window (self), _("Error opening file for writing"), error);
      return;
    }

  /* gtk4 todo */
  /* now that we know the user picked a folder, update file chooser settings */
  // gdu_utils_file_chooser_for_disk_images_set_default_folder (folder);

  self->inhibit_cookie = gtk_application_inhibit ((gpointer)g_application_get_default (),
                                                  create_disk_dialog_get_window (self),
                                                  GTK_APPLICATION_INHIBIT_SUSPEND |
                                                  GTK_APPLICATION_INHIBIT_LOGOUT,
                                                  /* Translators: Reason why suspend/logout is being inhibited */
                                                  C_("create-inhibit-message", "Copying device to disk image"));

  self->local_job = gdu_application_create_local_job ((gpointer)g_application_get_default (),
                                                      self->object);
  udisks_job_set_operation (UDISKS_JOB (self->local_job), "x-gdu-create-disk-image");
  /* Translators: this is the description of the job */
  gdu_local_job_set_description (self->local_job, _("Creating Disk Image"));
  udisks_job_set_progress_valid (UDISKS_JOB (self->local_job), TRUE);
  udisks_job_set_cancelable (UDISKS_JOB (self->local_job), TRUE);
  g_signal_connect (self->local_job, "canceled",
                    G_CALLBACK (on_local_job_canceled),
                    self);

  g_thread_new ("copy-disk-image-thread",
                copy_thread_func,
                self);
}

static void
ensure_unused_cb (GtkWindow     *window,
                  GAsyncResult  *res,
                  gpointer       user_data)
{
  GduCreateDiskImageDialog *self = user_data;
  if (!gdu_utils_ensure_unused_finish (self->client, res, NULL))
    {
      dialog_data_complete_and_unref (self);
      return;
    }

  start_copying (self);
}

static void
create_disk_image (GduCreateDiskImageDialog *self)
{
  /* If it's a optical drive, we don't need to try and
  * manually unmount etc.  everything as we're attempting to
  * open it O_RDONLY anyway - see copy_thread_func() for
  * details.
  */
  if (g_str_has_prefix (udisks_block_get_device (self->block), "/dev/sr"))
  {
    start_copying (self);
    return;
  }

  /* ensure the device is unused (e.g. unmounted) before copying data from it... */
  gdu_utils_ensure_unused (self->client,
                           create_disk_dialog_get_window (self),
                           self->object,
                           (GAsyncReadyCallback) ensure_unused_cb,
                           NULL, /* GCancellable */
                           self);
}

static void
overwrite_response_cb (GObject          *object,
                       GAsyncResult     *response,
                       gpointer          user_data)
{
  GduCreateDiskImageDialog *self = GDU_CREATE_DISK_IMAGE_DIALOG (user_data);
  AdwAlertDialog *dialog = ADW_ALERT_DIALOG (object);

  if (g_strcmp0 (adw_alert_dialog_choose_finish(dialog, response), "cancel") == 0)
    return;

  create_disk_image (self);

  adw_dialog_close (ADW_DIALOG (self));
}

static void
on_create_image_button_clicked_cb (GduCreateDiskImageDialog *self,
                                   GtkButton                *button)
{
  const gchar *name;
  g_autoptr(GFile) file = NULL;
  ConfirmationDialogData *data;

  name = gtk_editable_get_text (GTK_EDITABLE (self->name_entry));
  file = g_file_get_child(self->directory, name);

  if (!g_file_query_exists (file, NULL))
    {
      create_disk_image (self);
      adw_dialog_close (ADW_DIALOG (self));
      return;
    }

  data = g_new0 (ConfirmationDialogData, 1);
  data->message = _("Replace File?");
  data->description = g_strdup_printf (_("A file named “%s” already exists in %s"), name, gdu_utils_unfuse_path (g_file_get_path (self->directory)));
  data->response_verb = _("Replace");
  data->response_appearance = ADW_RESPONSE_DESTRUCTIVE;
  data->callback = overwrite_response_cb;
  data->user_data = self;

  gdu_utils_show_confirmation (create_disk_dialog_get_window (self),
                               data, NULL);
}

static void
create_disk_image_dialog_update_directory (GduCreateDiskImageDialog *self)
{
  g_autofree char *path = NULL;

  path = gdu_utils_unfuse_path (g_file_get_path (self->directory));

  adw_action_row_set_subtitle (ADW_ACTION_ROW (self->location_entry), path);
}

static void
file_dialog_open_cb (GObject      *object,
                     GAsyncResult *res,
                     gpointer      user_data)
{
  GduCreateDiskImageDialog *self = GDU_CREATE_DISK_IMAGE_DIALOG (user_data);
  g_autofree char *path = NULL;
  GFile *directory = NULL;
  GtkFileDialog *file_dialog = GTK_FILE_DIALOG (object);

  directory = gtk_file_dialog_select_folder_finish (file_dialog, res, NULL);
  if (directory)
    {
      g_clear_object (&self->directory);
      self->directory = directory;
      create_disk_image_dialog_update_directory (self);
    }
}


static void
on_choose_folder_button_clicked_cb (GduCreateDiskImageDialog *self)
{
  GtkFileDialog *file_dialog;
  GtkWindow *toplevel;

  toplevel = create_disk_dialog_get_window (self);
  if (toplevel == NULL)
    {
      g_info("Could not get native window for dialog");
    }

  file_dialog = gtk_file_dialog_new ();
  gtk_file_dialog_set_title (file_dialog, _("Choose a location to save the disk image."));

  gtk_file_dialog_select_folder (file_dialog,
                                 toplevel,
                                 NULL,
                                 file_dialog_open_cb,
                                 self);
}

static void
create_disk_image_dialog_set_default_name (GduCreateDiskImageDialog *self)
{
  g_autoptr(GTimeZone) tz = NULL;
  g_autoptr(GDateTime) now = NULL;
  g_autofree char *now_string = NULL;
  g_autofree char *proposed_filename = NULL;
  GString *device_name;
  const gchar *fstype;
  const gchar *fslabel;

  tz = g_time_zone_new_local ();
  now = g_date_time_new_now (tz);
  now_string = g_date_time_format (now, "%Y-%m-%d %H%M");

  device_name = g_string_new (udisks_block_dup_preferred_device (self->block));
  g_string_replace (device_name, "/dev/", "", 1);
  g_string_replace (device_name, "/", "_", 0);

  /* If it's an ISO/UDF filesystem, suggest a filename ending in .iso */
  fstype = udisks_block_get_id_type (self->block);
  fslabel = udisks_block_get_id_label (self->block);
  if ((g_strcmp0 (fstype, "iso9660") == 0 || g_strcmp0 (fstype, "udf") == 0)
    && fslabel != NULL && strlen (fslabel) > 0)
    {
      proposed_filename = g_strdup_printf ("%s.iso", fslabel);
    }
  else
  {
    /* Translators: The suggested name for the disk image to create.
      *              The first %s is a name for the disk (e.g. 'sdb').
      *              The second %s is today's date and time, e.g. "March 2, 1976 6:25AM".
      */
    proposed_filename = g_strdup_printf (_("Disk Image of %s (%s).img"),
                                          g_string_free_and_steal (device_name),
                                          now_string);
  }

  gtk_editable_set_text (GTK_EDITABLE (self->name_entry), proposed_filename);
}

static void
create_disk_image_set_source_label (GduCreateDiskImageDialog *self)
{
  g_autoptr(UDisksObjectInfo) info = NULL;

  info = udisks_client_get_object_info (self->client, self->object);
  adw_action_row_set_subtitle (ADW_ACTION_ROW (self->source_label), udisks_object_info_get_one_liner (info));
}

static void
gdu_create_disk_image_dialog_finalize (GObject *object)
{
  G_OBJECT_CLASS (gdu_create_disk_image_dialog_parent_class)->finalize (object);
}

void
gdu_create_disk_image_dialog_class_init (GduCreateDiskImageDialogClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->finalize = gdu_create_disk_image_dialog_finalize;

  gtk_widget_class_set_template_from_resource (widget_class,
                                               "/org/gnome/DiskUtility/ui/"
                                               "gdu-create-disk-image-dialog.ui");

  gtk_widget_class_bind_template_child (widget_class, GduCreateDiskImageDialog, name_entry);
  gtk_widget_class_bind_template_child (widget_class, GduCreateDiskImageDialog, location_entry);
  gtk_widget_class_bind_template_child (widget_class, GduCreateDiskImageDialog, source_label);

  gtk_widget_class_bind_template_callback (widget_class, on_choose_folder_button_clicked_cb);
  gtk_widget_class_bind_template_callback (widget_class, on_create_image_button_clicked_cb);
}

void
gdu_create_disk_image_dialog_init (GduCreateDiskImageDialog *self)
{
  gtk_widget_init_template (GTK_WIDGET (self));

  g_mutex_init (&self->copy_lock);
  self->cancellable = g_cancellable_new ();

  self->directory = g_file_new_for_path (g_get_user_special_dir (G_USER_DIRECTORY_DOCUMENTS));
}

void
gdu_create_disk_image_dialog_show (GtkWindow    *parent_window,
                                   UDisksObject *object,
                                   UDisksClient *client)
{
  GduCreateDiskImageDialog *self;

  self = g_object_new (GDU_TYPE_CREATE_DISK_IMAGE_DIALOG, NULL);

  self->client = client;
  self->object = g_object_ref (object);
  self->block = udisks_object_get_block (object);
  g_assert (self->block != NULL);
  self->drive = udisks_client_get_drive_for_block (client, self->block);

  create_disk_image_set_source_label (self);
  create_disk_image_dialog_set_default_name (self);
  create_disk_image_dialog_update_directory (self);

  // gtk4 todo
  // gdu_utils_configure_file_chooser_for_disk_images (GTK_FILE_CHOOSER (self->folder_fcbutton),
  //                                                   FALSE,   /* set file types */
  //                                                   FALSE);  /* allow_compressed */

  adw_dialog_present (ADW_DIALOG (self), GTK_WIDGET (parent_window));
}
