/*
 * Copyright (C) 2008-2013 Red Hat, Inc.
 *
 * Licensed under GPL version 2 or later.
 *
 * Author: David Zeuthen <zeuthen@gmail.com>
 */

#include "config.h"

#include <glib/gi18n.h>

#include <linux/fs.h>
#include <sys/ioctl.h>

#include "gdu-application.h"
#include "gdu-restore-disk-image-dialog.h"
#include "gdudevicetreemodel.h"
#include "gduestimator.h"
#include "gdulocaljob.h"
#include "gduxzdecompressor.h"

struct _GduRestoreDiskImageDialog
{
  AdwWindow      parent_instance;

  UDisksClient  *client;
  UDisksObject  *object;
  UDisksDrive   *drive;
  UDisksBlock   *block;
  gchar         *disk_image_filename;
  GFile         *restore_file;
  gboolean       switch_to_object;

  GtkWidget     *start_restore_button;

  GtkWidget     *warning_banner;
  GtkWidget     *error_banner;

  GtkWidget     *image_label;
  GtkWidget     *file_chooser_button;
  GtkWidget     *size_label;
  GtkWidget     *destination_label;
  GtkWidget     *destination_row;

  GtkWidget     *selectable_image_fcbutton;
  GtkWidget     *selectable_destination_combobox;

  guint64        block_size;
  gint64         start_time_usec;
  gint64         end_time_usec;

  GCancellable  *cancellable;
  GOutputStream *block_stream;
  GInputStream  *input_stream;
  guint64        input_size;

  guchar        *buffer;
  guint64        total_bytes_read;
  guint64        buffer_bytes_written;
  guint64        buffer_bytes_to_write;

  /* must hold copy_lock when reading/writing these */
  GMutex         copy_lock;
  GduEstimator  *estimator;
  guint          update_id;
  GError        *copy_error;

  guint          inhibit_cookie;

  gulong         response_signal_handler_id;
  gboolean       completed;

  GduLocalJob   *local_job;
};

G_DEFINE_TYPE (GduRestoreDiskImageDialog, gdu_restore_disk_image_dialog, ADW_TYPE_WINDOW)

static gpointer
restore_disk_image_dialog_get_window (GduRestoreDiskImageDialog *self)
{
  return gtk_widget_get_ancestor (GTK_WIDGET (self), GTK_TYPE_WINDOW);
}

static void
dialog_data_terminate_job (GduRestoreDiskImageDialog *self)
{
  if (self->local_job != NULL)
    {
      gdu_application_destroy_local_job ((gpointer)g_application_get_default (), self->local_job);
      self->local_job = NULL;
    }
}

static void
dialog_data_uninhibit (GduRestoreDiskImageDialog *self)
{
  if (self->inhibit_cookie > 0)
    {
      gtk_application_uninhibit ((gpointer)g_application_get_default (),
                                 self->inhibit_cookie);
      self->inhibit_cookie = 0;
    }
}

static void
dialog_data_hide (GduRestoreDiskImageDialog *self)
{
  /*
  if (self->dialog != NULL)
    {
      GtkWidget *dialog;
      if (self->response_signal_handler_id != 0)
        g_signal_handler_disconnect (self->dialog, self->response_signal_handler_id);
      dialog = self->dialog;
      self->dialog = NULL;
      gtk_widget_set_visible (dialog, FALSE);
      gtk_window_close (GTK_WINDOW (dialog));
      self->dialog = NULL;
    }
  */
}

static gboolean
unref_in_idle (gpointer user_data)
{
  GduRestoreDiskImageDialog *self = user_data;
  // dialog_data_unref (self);
  return FALSE; /* remove source */
}

static void
dialog_data_unref_in_idle (GduRestoreDiskImageDialog *self)
{
  g_idle_add (unref_in_idle, self);
}

static void
dialog_data_complete_and_unref (GduRestoreDiskImageDialog *self)
{
  if (!self->completed)
    {
      self->completed = TRUE;
      g_cancellable_cancel (self->cancellable);
    }
  dialog_data_uninhibit (self);
  dialog_data_hide (self);
}

static void
gdu_restore_disk_image_dialog_update (GduRestoreDiskImageDialog *self)
{
  g_autofree char *restore_warning = NULL;
  g_autofree char *restore_error = NULL;
  g_autofree char *size_str = NULL;
  const char *name;
  gboolean is_xz_compressed = FALSE;
  g_autoptr(GFileInfo) info = NULL;
  guint64 size;
  g_autofree char *s = NULL;

  /* don't update if we're already copying */
  if (self->buffer != NULL || self->restore_file == NULL || self->block_size <= 0)
    {
      gtk_widget_set_sensitive (self->start_restore_button, FALSE);
      return;
    }

  info = g_file_query_info (self->restore_file,
                            G_FILE_ATTRIBUTE_STANDARD_CONTENT_TYPE ","
                            G_FILE_ATTRIBUTE_STANDARD_DISPLAY_NAME ","
                            G_FILE_ATTRIBUTE_STANDARD_SIZE,
                            G_FILE_QUERY_INFO_NONE, NULL, NULL);
  name = g_file_info_get_display_name (info);
  size = g_file_info_get_size (info);
  is_xz_compressed = g_str_has_suffix (g_file_info_get_content_type (info),
                                       "-xz-compressed");

  if (is_xz_compressed)
    {
      size = gdu_xz_decompressor_get_uncompressed_size (self->restore_file);
      if (size == 0)
        {
          restore_error = g_strdup (_("File does not appear to be XZ compressed"));
        }
    }

  /* Translators: When shown for a compressed disk image in the "Size" field.
   * The %s is the uncompressed size as a long string e.g. "4.2 MB (4,300,123 bytes)".
   */
  size_str = g_strdup_printf (
      is_xz_compressed ? _ ("%s when compressed") : _ ("%s"),
      udisks_client_get_size_for_display (self->client, size, FALSE, TRUE));

  if (size == 0 && restore_error == NULL) 
    {
      /* if size is 0, error may be set already.. */
      restore_error = g_strdup (_ ("Cannot restore image of size 0"));
    }
  else if (self->block_size - size > 1000L * 1000L)
    {
      /* Only complain if slack is bigger than 1MB */
      s = udisks_client_get_size_for_display (
          self->client, self->block_size - size, FALSE, FALSE);
      restore_warning = g_strdup_printf (_("The disk image is %s smaller than the target device"), s);
    }
  else if (size > self->block_size)
    {
      s = udisks_client_get_size_for_display (
          self->client, size - self->block_size, FALSE, FALSE);
      restore_error = g_strdup_printf (_("The disk image is %s bigger than the target device"), s);
    }

  if (restore_error != NULL)
    {
      adw_banner_set_title (ADW_BANNER (self->error_banner), restore_error);
    }
  if (restore_warning != NULL)
    {
      adw_banner_set_title (ADW_BANNER (self->warning_banner),
                            restore_warning);
    }
  adw_banner_set_revealed (ADW_BANNER (self->error_banner), restore_error != NULL);
  adw_banner_set_revealed (ADW_BANNER (self->warning_banner), restore_warning != NULL);

  gtk_widget_set_sensitive (self->start_restore_button, restore_error == NULL);
  gtk_label_set_text (GTK_LABEL (self->image_label), name);
  gtk_label_set_label (GTK_LABEL (self->size_label),
                       size_str != NULL ? size_str : "—");
}

static void
destination_combobox_sensitive_cb (GtkCellLayout   *cell_layout,
                                   GtkCellRenderer *renderer,
                                   GtkTreeModel    *model,
                                   GtkTreeIter     *iter,
                                   gpointer         user_data)
{
  /* GduRestoreDiskImageDialog *self = user_data; */
  gboolean sensitive = FALSE;
  g_autoptr(UDisksBlock) block = NULL;

  gtk_tree_model_get (model, iter, GDU_DEVICE_TREE_MODEL_COLUMN_BLOCK, &block, -1);

  if (block == NULL || (udisks_block_get_size (block) > 0
      && !udisks_block_get_read_only (block)))
    {
      sensitive = TRUE;
    }

  gtk_cell_renderer_set_sensitive (renderer, sensitive);
}

static void
set_destination_object (GduRestoreDiskImageDialog *self,
                        UDisksObject              *object)
{
  if (self->object != object)
    {
      g_clear_object (&self->object);
      g_clear_object (&self->block);
      g_clear_object (&self->drive);
      self->block_size = 0;
      if (object != NULL)
        {
          self->object = g_object_ref (object);
          self->block = udisks_object_get_block (self->object);
          g_assert (self->block != NULL);
          self->drive = udisks_client_get_drive_for_block (self->client, self->block);
          /* TODO: use a method call for this so it works on e.g. floppy drives
           * where e.g. we don't know the size */
          self->block_size = udisks_block_get_size (self->block);
        }
    }
}

static void
on_destination_combobox_notify_active (GObject    *gobject,
                                       GParamSpec *pspec,
                                       gpointer    user_data)
{
  GduRestoreDiskImageDialog *self = user_data;
  g_autoptr(UDisksObject ) object = NULL;
  GtkTreeIter iter;
  GtkComboBox *combobox;

  combobox = GTK_COMBO_BOX (self->selectable_destination_combobox);
  if (gtk_combo_box_get_active_iter (combobox, &iter))
    {
      UDisksBlock *block = NULL;
      gtk_tree_model_get (gtk_combo_box_get_model (combobox), &iter,
                          GDU_DEVICE_TREE_MODEL_COLUMN_BLOCK, &block, -1);
      if (block != NULL)
        {
          object = (UDisksObject *)g_dbus_interface_dup_object (G_DBUS_INTERFACE (block));
        }
      g_clear_object (&block);
    }
  set_destination_object (self, object);
  gdu_restore_disk_image_dialog_update (self);
  g_clear_object (&object);
}

static void
populate_destination_combobox (GduRestoreDiskImageDialog *self)
{
  GduDeviceTreeModel *model;
  GtkComboBox *combobox;
  GtkCellRenderer *renderer;

  combobox = GTK_COMBO_BOX (self->selectable_destination_combobox);
  model = gdu_device_tree_model_new ((gpointer)g_application_get_default (),
                                     GDU_DEVICE_TREE_MODEL_FLAGS_FLAT |
                                     GDU_DEVICE_TREE_MODEL_FLAGS_ONE_LINE_NAME |
                                     GDU_DEVICE_TREE_MODEL_FLAGS_INCLUDE_DEVICE_NAME |
                                     GDU_DEVICE_TREE_MODEL_FLAGS_INCLUDE_NONE_ITEM);
  gtk_tree_sortable_set_sort_column_id (GTK_TREE_SORTABLE (model),
                                        GDU_DEVICE_TREE_MODEL_COLUMN_SORT_KEY,
                                        GTK_SORT_ASCENDING);
  gtk_combo_box_set_model (combobox, GTK_TREE_MODEL (model));
  g_object_unref (model);

  renderer = gtk_cell_renderer_pixbuf_new ();
  /*
  g_object_set (G_OBJECT (renderer),
                "stock-size", GTK_ICON_SIZE_MENU,
                NULL);
  */
  gtk_cell_layout_pack_start (GTK_CELL_LAYOUT (combobox), renderer, FALSE);
  gtk_cell_layout_set_attributes (GTK_CELL_LAYOUT (combobox), renderer,
                                  "gicon", GDU_DEVICE_TREE_MODEL_COLUMN_ICON,
                                  NULL);
  gtk_cell_layout_set_cell_data_func (GTK_CELL_LAYOUT (combobox), renderer,
                                      destination_combobox_sensitive_cb, self,
                                      NULL);

  renderer = gtk_cell_renderer_text_new ();
  gtk_cell_layout_pack_start (GTK_CELL_LAYOUT (combobox), renderer, FALSE);
  gtk_cell_layout_set_attributes (GTK_CELL_LAYOUT (combobox), renderer,
                                  "markup", GDU_DEVICE_TREE_MODEL_COLUMN_NAME,
                                  NULL);
  gtk_cell_layout_set_cell_data_func (GTK_CELL_LAYOUT (combobox), renderer,
                                      destination_combobox_sensitive_cb, self,
                                      NULL);

  g_signal_connect (combobox, "notify::active",
                    G_CALLBACK (on_destination_combobox_notify_active), self);

  /* Select (None) item */
  gtk_combo_box_set_active (combobox, 0);
}

static void
update_job (GduRestoreDiskImageDialog *self,
            gboolean                   done)
{
  guint64 bytes_completed = 0;
  guint64 bytes_target = 0;
  guint64 bytes_per_sec = 0;
  guint64 usec_remaining = 0;
  gdouble progress = 0.0;

  g_mutex_lock (&self->copy_lock);
  if (self->estimator != NULL)
    {
      bytes_per_sec = gdu_estimator_get_bytes_per_sec (self->estimator);
      usec_remaining = gdu_estimator_get_usec_remaining (self->estimator);
      bytes_completed = gdu_estimator_get_completed_bytes (self->estimator);
      bytes_target = gdu_estimator_get_target_bytes (self->estimator);
    }
  self->update_id = 0;
  g_mutex_unlock (&self->copy_lock);

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
            progress = ((gdouble)bytes_completed) / ((gdouble)bytes_target);
          else
            progress = 0.0;
        }
      udisks_job_set_progress (UDISKS_JOB (self->local_job), progress);

      if (usec_remaining == 0)
        udisks_job_set_expected_end_time (UDISKS_JOB (self->local_job), 0);
      else
        udisks_job_set_expected_end_time (UDISKS_JOB (self->local_job),
                                          usec_remaining + g_get_real_time ());
    }
}

static void
play_complete_sound (GduRestoreDiskImageDialog *self)
{
  const gchar *sound_message;

  /* Translators: A descriptive string for the 'complete' sound, see
   * CA_PROP_EVENT_DESCRIPTION */
  sound_message = _ ("Disk image copying complete");
  /* gtk4 todo : Find a replacement for this
  ca_gtk_play_for_widget (GTK_WIDGET (self->dialog), 0,
                          CA_PROP_EVENT_ID, "complete",
                          CA_PROP_EVENT_DESCRIPTION, sound_message,
                          NULL);
  */

  if (self->inhibit_cookie > 0)
    {
      gtk_application_uninhibit ((gpointer)g_application_get_default (),
                                 self->inhibit_cookie);
      self->inhibit_cookie = 0;
    }
}

static gboolean
on_update_job (gpointer user_data)
{
  GduRestoreDiskImageDialog *self = user_data;
  update_job (self, FALSE);
  return FALSE; /* remove source */
}

static gboolean
on_show_error (gpointer user_data)
{
  GduRestoreDiskImageDialog *self = user_data;

  play_complete_sound (self);
  dialog_data_uninhibit (self);

  g_assert (self->copy_error != NULL);
  gdu_utils_show_error (restore_disk_image_dialog_get_window (self),
                        _("Error restoring disk image"), self->copy_error);
  g_clear_error (&self->copy_error);

  dialog_data_complete_and_unref (self);

  return FALSE; /* remove source */
}

static gboolean
on_success (gpointer user_data)
{
  GduRestoreDiskImageDialog *self = user_data;

  update_job (self, TRUE);

  play_complete_sound (self);
  dialog_data_uninhibit (self);
  dialog_data_complete_and_unref (self);

  return FALSE; /* remove source */
}

static gpointer
copy_thread_func (gpointer user_data)
{
  GduRestoreDiskImageDialog *self = user_data;
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
  GUnixFDList *fd_list = NULL;
  GVariant *fd_index = NULL;

  /* default to 1 MiB blocks */
  buffer_size = (1 * 1024 * 1024);

  /* request the fd from udisks */
  if (!udisks_block_call_open_for_restore_sync (
          self->block,
          g_variant_new ("a{sv}", NULL), /* options */
          NULL,                          /* fd_list */
          &fd_index,
          &fd_list,
          NULL,                          /* cancellable */
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
    {
      g_variant_unref (fd_index);
    }
  g_clear_object (&fd_list);

  g_assert (fd != -1);

  /* We can't use udisks_block_get_size() because the media may have
   * changed and udisks may not have noticed. TODO: maybe have a
   * Block.GetSize() method instead...
   */
  if (ioctl (fd, BLKGETSIZE64, &block_device_size) != 0)
    {
      error = g_error_new (G_IO_ERROR, g_io_error_from_errno (errno), "%s",
                           strerror (errno));
      g_prefix_error (&error, _("Error determining size of device: "));
      goto out;
    }

  if (block_device_size == 0)
    {
      error = g_error_new (G_IO_ERROR, G_IO_ERROR_FAILED,
                           _("Device is size 0"));
      goto out;
    }
  self->block_size = block_device_size;

  page_size = sysconf (_SC_PAGESIZE);
  buffer_unaligned = g_new0 (guchar, buffer_size + page_size);
  buffer = (guchar *)(((gintptr)(buffer_unaligned + page_size)) & (~(page_size - 1)));

  g_mutex_lock (&self->copy_lock);
  self->estimator = gdu_estimator_new (self->input_size);
  self->update_id = 0;
  self->start_time_usec = g_get_real_time ();
  g_mutex_unlock (&self->copy_lock);

  /* Read huge (e.g. 1 MiB) blocks and write it to the output
   * device even if it was only partially read.
   */
  num_bytes_completed = 0;
  while (num_bytes_completed < self->input_size)
    {
      gsize num_bytes_to_read;
      gsize num_bytes_read;
      ssize_t num_bytes_written;
      gint64 now_usec;

      num_bytes_to_read = buffer_size;
      if (num_bytes_to_read + num_bytes_completed > self->input_size)
        num_bytes_to_read = self->input_size - num_bytes_completed;

      /* Update GUI - but only every 200 ms and only if last update isn't
       * pending */
      g_mutex_lock (&self->copy_lock);
      now_usec = g_get_monotonic_time ();
      if (now_usec - last_update_usec > 200 * G_USEC_PER_SEC / 1000
          || last_update_usec < 0)
        {
          if (num_bytes_completed > 0)
            gdu_estimator_add_sample (self->estimator, num_bytes_completed);
          if (self->update_id == 0)
            self->update_id = g_idle_add (on_update_job, self);
          last_update_usec = now_usec;
        }
      g_mutex_unlock (&self->copy_lock);

      if (!g_input_stream_read_all (self->input_stream, buffer,
                                    num_bytes_to_read, &num_bytes_read,
                                    self->cancellable, &error))
        {
          g_prefix_error (&error,
                          "Error reading %" G_GSIZE_FORMAT
                          " bytes from offset %" G_GUINT64_FORMAT ": ",
                          num_bytes_to_read, num_bytes_completed);
          goto out;
        }
      if (num_bytes_read != num_bytes_to_read)
        {
          g_set_error (&error, G_IO_ERROR, G_IO_ERROR_FAILED,
                       "Requested %" G_GSIZE_FORMAT
                       " bytes from offset %" G_GUINT64_FORMAT
                       " but only read %" G_GSIZE_FORMAT " bytes",
                       num_bytes_read, num_bytes_completed, num_bytes_to_read);
          goto out;
        }

    copy_write_again:
      num_bytes_written = write (fd, buffer, num_bytes_read);
      if (num_bytes_written < 0)
        {
          if (errno == EAGAIN || errno == EINTR)
            goto copy_write_again;

          g_set_error (&error, G_IO_ERROR, G_IO_ERROR_FAILED,
                       "Error writing %" G_GSIZE_FORMAT
                       " bytes to offset %" G_GUINT64_FORMAT ": %m",
                       num_bytes_read, num_bytes_completed);
          goto out;
        }

      /*g_print ("copied %" G_GUINT64_FORMAT " bytes at offset %"
         G_GUINT64_FORMAT "\n", (guint64) num_bytes_written,
               num_bytes_completed);*/

      num_bytes_completed += num_bytes_written;
    }

out:
  self->end_time_usec = g_get_real_time ();

  /* in either case, close the stream */
  if (!g_input_stream_close (G_INPUT_STREAM (self->input_stream),
                             NULL, /* cancellable */
                             &error2))
    {
      g_warning ("Error closing file input stream: %s (%s, %d)",
                 error2->message, g_quark_to_string (error2->domain),
                 error2->code);
      g_clear_error (&error2);
    }
  g_clear_object (&self->input_stream);

  if (fd != -1)
    {
      if (close (fd) != 0)
        g_warning ("Error closing fd: %m");
    }

  if (error != NULL)
    {
      gboolean wipe_after_error = TRUE;

      if (error->domain == UDISKS_ERROR
          && error->code == UDISKS_ERROR_NOT_AUTHORIZED_DISMISSED)
        {
          wipe_after_error = FALSE;
        }

      /* show error in GUI */
      if (!(error->domain == G_IO_ERROR
            && error->code == G_IO_ERROR_CANCELLED))
        {
          self->copy_error = error;
          error = NULL;
          g_idle_add (on_show_error, self);
        }
      g_clear_error (&error);

      /* Wipe the device */
      if (wipe_after_error
          && !udisks_block_call_format_sync (
              self->block, "empty",
              g_variant_new ("a{sv}", NULL), /* options */
              NULL,                          /* cancellable */
              &error2))
        {
          g_warning ("Error wiping device on error path: %s (%s, %d)",
                     error2->message, g_quark_to_string (error2->domain),
                     error2->code);
          g_clear_error (&error2);
        }
    }
  else
    {
      /* success */
      g_idle_add (on_success, self);
    }

  g_free (buffer_unaligned);

  /* finally, request that the core OS / kernel rescans the device */
  if (!udisks_block_call_rescan_sync (
          self->block, g_variant_new ("a{sv}", NULL), /* options */
          NULL,                                       /* cancellable */
          &error2))
    {
      g_warning ("Error rescanning device: %s (%s, %d)", error2->message,
                 g_quark_to_string (error2->domain), error2->code);
      g_clear_error (&error2);
    }

  dialog_data_unref_in_idle (self); /* unref on main thread */
  return NULL;
}

static void
on_local_job_canceled (GduLocalJob *job,
                       gpointer     user_data)
{
  GduRestoreDiskImageDialog *self = user_data;
  if (!self->completed)
    {
      dialog_data_terminate_job (self);
      dialog_data_complete_and_unref (self);
      update_job (self, FALSE);
    }
}

static void
start_copying (GduRestoreDiskImageDialog *self)
{
  g_autoptr(GFileInfo) info;
  g_autoptr(GError) error = NULL;
  gboolean is_xz_compressed = FALSE;

  info = g_file_query_info (self->restore_file,
                            G_FILE_ATTRIBUTE_STANDARD_CONTENT_TYPE ","
                            G_FILE_ATTRIBUTE_STANDARD_SIZE,
                            G_FILE_QUERY_INFO_NONE, NULL, &error);
  if (info == NULL)
    {
      gdu_utils_show_error (GTK_WINDOW (self),
                            _ ("Error determining size of file"), error);
      return;
    }

  self->input_size = g_file_info_get_size (info);
  self->input_stream = (GInputStream *)g_file_read (self->restore_file, NULL, &error);

  if (self->input_stream == NULL)
    {
      if (!(error->domain == G_IO_ERROR
            && error->code == G_IO_ERROR_CANCELLED))
        {
          gdu_utils_show_error (GTK_WINDOW (self),
                                _ ("Error opening file for reading"), error);
        }

      return;
    }

  is_xz_compressed = g_str_has_suffix (g_file_info_get_content_type (info),
                                       "-xz-compressed");
  if (is_xz_compressed)
    {
      GduXzDecompressor *decompressor;

      decompressor = gdu_xz_decompressor_new ();

      self->input_size = gdu_xz_decompressor_get_uncompressed_size (self->restore_file);
      self->input_stream = g_converter_input_stream_new (G_INPUT_STREAM (self->input_stream),
                                                         G_CONVERTER (decompressor));

      g_object_unref (decompressor);
    }

  self->inhibit_cookie = gtk_application_inhibit ((gpointer)g_application_get_default (),
                                                  GTK_WINDOW (self),
                                                  GTK_APPLICATION_INHIBIT_SUSPEND |
                                                  GTK_APPLICATION_INHIBIT_LOGOUT,
                                                  /* Translators: Reason why suspend/logout is being inhibited */
                                                  C_("restore-inhibit-message", "Copying disk image to device"));

  self->local_job = gdu_application_create_local_job ((gpointer)g_application_get_default (), self->object);
  udisks_job_set_operation (UDISKS_JOB (self->local_job), "x-gdu-restore-disk-image");
  /* Translators: this is the description of the job */
  gdu_local_job_set_description (self->local_job, _("Restoring Disk Image"));
  udisks_job_set_progress_valid (UDISKS_JOB (self->local_job), TRUE);
  udisks_job_set_cancelable (UDISKS_JOB (self->local_job), TRUE);
  g_signal_connect (self->local_job,
                    "canceled",
                    G_CALLBACK (on_local_job_canceled),
                    self);

  /* gtk4 todo: after using GtkSelectionModel or so */
  /* if (self->switch_to_object && GDU_IS_WINDOW (self->window)) */
  /*   gdu_window_select_object (GDU_WINDOW (self->window), self->object); */

  g_thread_new ("copy-disk-image-thread", copy_thread_func, self);
}

static void
ensure_unused_cb (GtkWindow     *window,
                  GAsyncResult  *res,
                  gpointer       user_data)
{
  GduRestoreDiskImageDialog *self = user_data;
  if (!gdu_utils_ensure_unused_finish (self->client, res, NULL))
    {
      return;
    }

  start_copying (self);
}

static void
on_confirmation_response_cb (GObject        *object,
                             GAsyncResult   *response,
                             gpointer        user_data)
{
  GduRestoreDiskImageDialog *self = user_data;
  AdwMessageDialog *dialog = ADW_MESSAGE_DIALOG (object);

  if (g_strcmp0 (adw_message_dialog_choose_finish (dialog, response), "cancel") == 0)
    {
      return;
    }

  /* ensure the device is unused (e.g. unmounted) before copying data to it... */
  gdu_utils_ensure_unused (self->client,
                           restore_disk_image_dialog_get_window (self), self->object,
                           (GAsyncReadyCallback) ensure_unused_cb,
                           NULL, /* GCancellable */
                           self);
}

static void
on_start_restore_button_clicked_cb (GduRestoreDiskImageDialog *self)
{
  GList *objects = NULL;

  objects = g_list_append (NULL, self->object);

  gdu_utils_show_confirmation (GTK_WINDOW (self),
                               _("Are you sure you want to write the disk image to the device?"),
                               _("All existing data will be lost"),
                               _("_Restore"),
                               NULL, NULL,
                               self->client, objects,
                               on_confirmation_response_cb,
                               self,
                               ADW_RESPONSE_DESTRUCTIVE);
}

static void
file_dialog_open_cb (GObject *object, GAsyncResult *res, gpointer user_data)
{
  GduRestoreDiskImageDialog *self = user_data;
  GFile *file = NULL;
  GtkFileDialog *file_dialog = GTK_FILE_DIALOG (object);

  file = gtk_file_dialog_open_finish (file_dialog, res, NULL);
  if (file)
    {
      self->restore_file = g_steal_pointer (&file);
      gdu_restore_disk_image_dialog_update (self);
    }
}

static void
on_file_chooser_button_clicked_cb (GduRestoreDiskImageDialog *self)
{
  GtkFileDialog *file_dialog;

  file_dialog = gtk_file_dialog_new ();
  gtk_file_dialog_set_title (file_dialog,
                             _("Choose a disk image to restore."));

  gtk_file_dialog_open (file_dialog,
                        GTK_WINDOW (self),
                        NULL, /* Cancellable */
                        file_dialog_open_cb,
                        self);
}

static void
gdu_restore_disk_image_dialog_finalize (GObject *object)
{
  GduRestoreDiskImageDialog *self = (GduRestoreDiskImageDialog *)object;
  dialog_data_terminate_job (self);
  dialog_data_uninhibit (self);
  dialog_data_hide (self);

  G_OBJECT_CLASS (gdu_restore_disk_image_dialog_parent_class)->finalize (object);
}

static void
gdu_restore_disk_image_dialog_class_init (GduRestoreDiskImageDialogClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->finalize = gdu_restore_disk_image_dialog_finalize;

  gtk_widget_class_set_template_from_resource (widget_class,
                                               "/org/gnome/DiskUtility/ui/"
                                               "gdu-restore-disk-image-dialog.ui");

  gtk_widget_class_bind_template_child (widget_class, GduRestoreDiskImageDialog, size_label);
  gtk_widget_class_bind_template_child (widget_class, GduRestoreDiskImageDialog, start_restore_button);
  gtk_widget_class_bind_template_child (widget_class, GduRestoreDiskImageDialog, image_label);
  gtk_widget_class_bind_template_child (widget_class, GduRestoreDiskImageDialog, file_chooser_button);
  gtk_widget_class_bind_template_child (widget_class, GduRestoreDiskImageDialog, destination_row);
  gtk_widget_class_bind_template_child (widget_class, GduRestoreDiskImageDialog, destination_label);
  gtk_widget_class_bind_template_child (widget_class, GduRestoreDiskImageDialog, error_banner);
  gtk_widget_class_bind_template_child (widget_class, GduRestoreDiskImageDialog, warning_banner);

  gtk_widget_class_bind_template_callback (widget_class, on_file_chooser_button_clicked_cb);
  gtk_widget_class_bind_template_callback (widget_class, on_start_restore_button_clicked_cb);
}

static void
gdu_restore_disk_image_dialog_init (GduRestoreDiskImageDialog *self)
{
  gtk_widget_init_template (GTK_WIDGET (self));

  self->cancellable = g_cancellable_new ();
}

void
gdu_restore_disk_image_dialog_show (GtkWindow    *parent_window,
                                    UDisksObject *object,
                                    UDisksClient *client,
                                    const gchar  *disk_image_filename)
{
  GduRestoreDiskImageDialog *self;

  self = g_object_new (GDU_TYPE_RESTORE_DISK_IMAGE_DIALOG,
                       "transient-for", parent_window,
                       NULL);

  g_mutex_init (&self->copy_lock);
  self->client = client;
  set_destination_object (self, object);
  if (object == NULL)
    {
      self->switch_to_object = TRUE;
    }

  if (disk_image_filename != NULL)
    {
      self->restore_file = g_file_new_for_commandline_arg (disk_image_filename);
    }

  /* Image: Show label if image is known, otherwise show a filechooser button */
  if (disk_image_filename != NULL)
    {
      g_autofree char *s;
      s = gdu_utils_unfuse_path (disk_image_filename);
      gtk_label_set_text (GTK_LABEL (self->image_label), s);
      gtk_widget_set_visible (self->file_chooser_button, FALSE);
    }

  /* Destination: Show label if device is known, otherwise show a combobox */
  if (self->object != NULL)
    {
      g_autoptr (UDisksObjectInfo) info;
      info = udisks_client_get_object_info (self->client, self->object);
      gtk_label_set_text (GTK_LABEL (self->destination_label),
                          udisks_object_info_get_one_liner (info));
    }
  else
    {
      gtk_widget_set_visible (self->destination_row, FALSE);
      populate_destination_combobox (self);
    }
  gdu_restore_disk_image_dialog_update (self);

  gtk_window_present (GTK_WINDOW (self));
}
