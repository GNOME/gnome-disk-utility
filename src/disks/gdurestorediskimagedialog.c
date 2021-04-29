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
#include <gio/gunixfdlist.h>
#include <gio/gunixoutputstream.h>

#include <glib-unix.h>
#include <sys/ioctl.h>
#include <linux/fs.h>

#include <canberra-gtk.h>

#include "gduapplication.h"
#include "gduwindow.h"
#include "gdurestorediskimagedialog.h"
#include "gduvolumegrid.h"
#include "gduestimator.h"
#include "gdulocaljob.h"
#include "gdudevicetreemodel.h"
#include "gduxzdecompressor.h"

/* ---------------------------------------------------------------------------------------------------- */

typedef struct
{
  volatile gint ref_count;

  GduWindow *window;
  UDisksObject *object;
  gchar *disk_image_filename;
  gboolean switch_to_object;

  UDisksBlock *block;
  UDisksDrive *drive;

  GtkBuilder *builder;
  GtkWidget *dialog;

  GtkWidget *infobar_vbox;
  GtkWidget *warning_infobar;
  GtkWidget *warning_label;
  GtkWidget *error_infobar;
  GtkWidget *error_label;

  GtkWidget *image_key_label;
  GtkWidget *image_label;
  GtkWidget *selectable_image_label;
  GtkWidget *selectable_image_fcbutton;

  GtkWidget *image_size_key_label;
  GtkWidget *image_size_label;

  GtkWidget *destination_key_label;
  GtkWidget *destination_label;
  GtkWidget *selectable_destination_label;
  GtkWidget *selectable_destination_combobox;

  GtkWidget *start_copying_button;
  GtkWidget *cancel_button;

  guint64 block_size;
  gint64 start_time_usec;
  gint64 end_time_usec;

  GCancellable *cancellable;
  GOutputStream *block_stream;
  GInputStream *input_stream;
  guint64 input_size;

  guchar *buffer;
  guint64 total_bytes_read;
  guint64 buffer_bytes_written;
  guint64 buffer_bytes_to_write;

  /* must hold copy_lock when reading/writing these */
  GMutex copy_lock;
  GduEstimator *estimator;
  guint update_id;
  GError *copy_error;

  guint inhibit_cookie;

  gulong response_signal_handler_id;
  gboolean completed;

  GduLocalJob *local_job;
} DialogData;


static const struct {
  goffset offset;
  const gchar *name;
} widget_mapping[] = {
  {G_STRUCT_OFFSET (DialogData, infobar_vbox), "infobar-vbox"},

  {G_STRUCT_OFFSET (DialogData, image_key_label), "image-key-label"},
  {G_STRUCT_OFFSET (DialogData, image_label), "image-label"},
  {G_STRUCT_OFFSET (DialogData, selectable_image_label), "selectable-image-label"},
  {G_STRUCT_OFFSET (DialogData, selectable_image_fcbutton), "selectable-image-fcbutton"},

  {G_STRUCT_OFFSET (DialogData, image_size_key_label), "image-size-key-label"},
  {G_STRUCT_OFFSET (DialogData, image_size_label), "image-size-label"},

  {G_STRUCT_OFFSET (DialogData, destination_key_label), "destination-key-label"},
  {G_STRUCT_OFFSET (DialogData, destination_label), "destination-label"},
  {G_STRUCT_OFFSET (DialogData, selectable_destination_label), "selectable-destination-label"},
  {G_STRUCT_OFFSET (DialogData, selectable_destination_combobox), "selectable-destination-combobox"},

  {G_STRUCT_OFFSET (DialogData, start_copying_button), "start-copying-button"},
  {G_STRUCT_OFFSET (DialogData, cancel_button), "cancel-button"},
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
dialog_data_terminate_job (DialogData *data)
{
  if (data->local_job != NULL)
    {
      gdu_application_destroy_local_job (gdu_window_get_application (data->window), data->local_job);
      data->local_job = NULL;
    }
}

static void
dialog_data_uninhibit (DialogData *data)
{
  if (data->inhibit_cookie > 0)
    {
      gtk_application_uninhibit (GTK_APPLICATION (gdu_window_get_application (data->window)),
                                 data->inhibit_cookie);
      data->inhibit_cookie = 0;
    }
}

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
  if (g_atomic_int_dec_and_test (&data->ref_count))
    {
      dialog_data_terminate_job (data);
      dialog_data_uninhibit (data);
      dialog_data_hide (data);

      g_object_unref (data->warning_infobar);
      g_object_unref (data->error_infobar);
      g_object_unref (data->window);
      g_clear_object (&data->object);
      g_clear_object (&data->block);
      g_clear_object (&data->drive);
      g_free (data->disk_image_filename);
      if (data->builder != NULL)
        g_object_unref (data->builder);
      g_free (data->buffer);
      g_clear_object (&data->estimator);

      g_clear_object (&data->cancellable);
      g_clear_object (&data->input_stream);
      g_clear_object (&data->block_stream);
      g_mutex_clear (&data->copy_lock);
      g_free (data);
    }
}

/* ---------------------------------------------------------------------------------------------------- */

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
  dialog_data_uninhibit (data);
  dialog_data_hide (data);
  dialog_data_unref (data);
}

/* ---------------------------------------------------------------------------------------------------- */

static void
restore_disk_image_update (DialogData *data)
{
  gboolean can_proceed = FALSE;
  gchar *restore_warning = NULL;
  gchar *restore_error = NULL;
  gchar *image_size_str = NULL;
  GFile *restore_file = NULL;

  if (data->dialog == NULL)
    goto out;

  /* don't update if we're already copying */
  if (data->buffer != NULL)
    goto out;

  /* Check if we have a file */
  if (data->disk_image_filename != NULL)
    restore_file = g_file_new_for_commandline_arg (data->disk_image_filename);
  else
    restore_file = gtk_file_chooser_get_file (GTK_FILE_CHOOSER (data->selectable_image_fcbutton));

  if (restore_file != NULL)
    {
      gboolean is_xz_compressed = FALSE;
      GFileInfo *info;
      guint64 size;
      gchar *s;

      info = g_file_query_info (restore_file,
                                G_FILE_ATTRIBUTE_STANDARD_CONTENT_TYPE ","
                                G_FILE_ATTRIBUTE_STANDARD_SIZE,
                                G_FILE_QUERY_INFO_NONE,
                                NULL,
                                NULL);
      if (g_str_has_suffix (g_file_info_get_content_type (info), "-xz-compressed"))
        is_xz_compressed = TRUE;
      size = g_file_info_get_size (info);
      g_object_unref (info);

      if (is_xz_compressed)
        {
          gsize uncompressed_size;
          uncompressed_size = gdu_xz_decompressor_get_uncompressed_size (restore_file);
          if (uncompressed_size == 0)
            {
              restore_error = g_strdup (_("File does not appear to be XZ compressed"));
              size = 0;
            }
          else
            {
              s = udisks_client_get_size_for_display (gdu_window_get_client (data->window), uncompressed_size, FALSE, TRUE);
              /* Translators: Shown for a compressed disk image in the "Size" field.
               *              The %s is the uncompressed size as a long string, e.g. "4.2 MB (4,300,123 bytes)".
               */
              image_size_str = g_strdup_printf (_("%s when decompressed"), s);
              g_free (s);
              size = uncompressed_size;
            }
        }
      else
        {
          image_size_str = udisks_client_get_size_for_display (gdu_window_get_client (data->window), size, FALSE, TRUE);
        }

      if (data->block_size > 0)
        {
          if (size == 0)
            {
              /* if size is 0, error may be set already.. */
              if (restore_error == NULL)
                restore_error = g_strdup (_("Cannot restore image of size 0"));
            }
          else if (size < data->block_size)
            {
              /* Only complain if slack is bigger than 1MB */
              if (data->block_size - size > 1000L*1000L)
                {
                  s = udisks_client_get_size_for_display (gdu_window_get_client (data->window),
                                                          data->block_size - size, FALSE, FALSE);
                  restore_warning = g_strdup_printf (_("The disk image is %s smaller than the target device"), s);
                  g_free (s);
                }
              can_proceed = TRUE;
            }
          else if (size > data->block_size)
            {
              s = udisks_client_get_size_for_display (gdu_window_get_client (data->window),
                                                      size - data->block_size, FALSE, FALSE);
              restore_error = g_strdup_printf (_("The disk image is %s bigger than the target device"), s);
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

  gtk_label_set_text (GTK_LABEL (data->image_size_label), image_size_str != NULL ? image_size_str : "—");

  g_free (restore_warning);
  g_free (restore_error);
  g_clear_object (&restore_file);
  g_free (image_size_str);

  gtk_dialog_set_response_sensitive (GTK_DIALOG (data->dialog), GTK_RESPONSE_OK, can_proceed);

 out:
  ;
}

static void
on_file_set (GtkFileChooserButton   *button,
             gpointer                user_data)
{
  DialogData *data = user_data;
  if (data->dialog == NULL)
    goto out;
  restore_disk_image_update (data);
 out:
  ;
}

static void
on_notify (GObject    *object,
           GParamSpec *pspec,
           gpointer    user_data)
{
  DialogData *data = user_data;
  if (data->dialog == NULL)
    goto out;
  restore_disk_image_update (data);
 out:
  ;
}

/* ---------------------------------------------------------------------------------------------------- */

static void
destination_combobox_sensitive_cb (GtkCellLayout   *cell_layout,
                                   GtkCellRenderer *renderer,
                                   GtkTreeModel    *model,
                                   GtkTreeIter     *iter,
                                   gpointer         user_data)
{
  /* DialogData *data = user_data; */
  gboolean sensitive = FALSE;
  UDisksBlock *block = NULL;

  gtk_tree_model_get (model, iter,
                      GDU_DEVICE_TREE_MODEL_COLUMN_BLOCK, &block,
                      -1);

  if (block == NULL)
    sensitive = TRUE;

  if (block != NULL &&
      udisks_block_get_size (block) > 0 &&
      !udisks_block_get_read_only (block))
    sensitive = TRUE;

  gtk_cell_renderer_set_sensitive (renderer, sensitive);

  g_clear_object (&block);
}

static void
set_destination_object (DialogData *data,
                        UDisksObject *object)
{
  if (data->object != object)
    {
      g_clear_object (&data->object);
      g_clear_object (&data->block);
      g_clear_object (&data->drive);
      data->block_size = 0;
      if (object != NULL)
        {
          data->object = g_object_ref (object);
          data->block = udisks_object_get_block (data->object);
          g_assert (data->block != NULL);
          data->drive = udisks_client_get_drive_for_block (gdu_window_get_client (data->window), data->block);
          /* TODO: use a method call for this so it works on e.g. floppy drives where e.g. we don't know the size */
          data->block_size = udisks_block_get_size (data->block);
        }
    }
}

static void
on_destination_combobox_notify_active (GObject    *gobject,
                                       GParamSpec *pspec,
                                       gpointer    user_data)
{
  DialogData *data = user_data;
  UDisksObject *object = NULL;
  GtkTreeIter iter;
  GtkComboBox *combobox;

  combobox = GTK_COMBO_BOX (data->selectable_destination_combobox);
  if (gtk_combo_box_get_active_iter (combobox, &iter))
    {
      UDisksBlock *block = NULL;
      gtk_tree_model_get (gtk_combo_box_get_model (combobox),
                          &iter,
                          GDU_DEVICE_TREE_MODEL_COLUMN_BLOCK, &block,
                          -1);
      if (block != NULL)
        object = (UDisksObject *) g_dbus_interface_dup_object (G_DBUS_INTERFACE (block));
      g_clear_object (&block);
    }
  set_destination_object (data, object);
  restore_disk_image_update (data);
  g_clear_object (&object);
}

static void
populate_destination_combobox (DialogData *data)
{
  GduDeviceTreeModel *model;
  GtkComboBox *combobox;
  GtkCellRenderer *renderer;

  combobox = GTK_COMBO_BOX (data->selectable_destination_combobox);
  model = gdu_device_tree_model_new (gdu_window_get_application (data->window),
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
  g_object_set (G_OBJECT (renderer),
                "stock-size", GTK_ICON_SIZE_MENU,
                NULL);
  gtk_cell_layout_pack_start (GTK_CELL_LAYOUT (combobox), renderer, FALSE);
  gtk_cell_layout_set_attributes (GTK_CELL_LAYOUT (combobox), renderer,
                                  "gicon", GDU_DEVICE_TREE_MODEL_COLUMN_ICON,
                                  NULL);
  gtk_cell_layout_set_cell_data_func (GTK_CELL_LAYOUT (combobox), renderer,
                                      destination_combobox_sensitive_cb, data, NULL);

  renderer = gtk_cell_renderer_text_new ();
  gtk_cell_layout_pack_start (GTK_CELL_LAYOUT (combobox), renderer, FALSE);
  gtk_cell_layout_set_attributes (GTK_CELL_LAYOUT (combobox), renderer,
                                  "markup", GDU_DEVICE_TREE_MODEL_COLUMN_NAME,
                                  NULL);
  gtk_cell_layout_set_cell_data_func (GTK_CELL_LAYOUT (combobox), renderer,
                                      destination_combobox_sensitive_cb, data, NULL);

  g_signal_connect (combobox, "notify::active", G_CALLBACK (on_destination_combobox_notify_active), data);

  /* Select (None) item */
  gtk_combo_box_set_active (combobox, 0);
}

/* ---------------------------------------------------------------------------------------------------- */

static void
restore_disk_image_populate (DialogData *data)
{
  gdu_utils_configure_file_chooser_for_disk_images (GTK_FILE_CHOOSER (data->selectable_image_fcbutton),
                                                    TRUE,   /* set file types */
                                                    TRUE);  /* allow_compressed */

  /* Image: Show label if image is known, otherwise show a filechooser button */
  if (data->disk_image_filename != NULL)
    {
      gchar *s;
      s = gdu_utils_unfuse_path (data->disk_image_filename);
      gtk_label_set_text (GTK_LABEL (data->image_label), s);
      g_free (s);

      gtk_widget_hide (data->selectable_image_label);
      gtk_widget_hide (data->selectable_image_fcbutton);
    }
  else
    {
      gtk_widget_hide (data->image_key_label);
      gtk_widget_hide (data->image_label);
    }

  /* Destination: Show label if device is known, otherwise show a combobox */
  if (data->object != NULL)
    {
      UDisksObjectInfo *info;
      info = udisks_client_get_object_info (gdu_window_get_client (data->window), data->object);
      gtk_label_set_text (GTK_LABEL (data->destination_label), udisks_object_info_get_one_liner (info));
      g_clear_object (&info);

      gtk_widget_hide (data->selectable_destination_label);
      gtk_widget_hide (data->selectable_destination_combobox);
    }
  else
    {
      gtk_widget_hide (data->destination_key_label);
      gtk_widget_hide (data->destination_label);

      populate_destination_combobox (data);
    }
}

/* ---------------------------------------------------------------------------------------------------- */

static void
update_job (DialogData *data,
            gboolean    done)
{
  guint64 bytes_completed = 0;
  guint64 bytes_target = 0;
  guint64 bytes_per_sec = 0;
  guint64 usec_remaining = 0;
  gdouble progress = 0.0;

  g_mutex_lock (&data->copy_lock);
  if (data->estimator != NULL)
    {
      bytes_per_sec = gdu_estimator_get_bytes_per_sec (data->estimator);
      usec_remaining = gdu_estimator_get_usec_remaining (data->estimator);
      bytes_completed = gdu_estimator_get_completed_bytes (data->estimator);
      bytes_target = gdu_estimator_get_target_bytes (data->estimator);
    }
  data->update_id = 0;
  g_mutex_unlock (&data->copy_lock);

  if (data->local_job != NULL)
    {
      udisks_job_set_bytes (UDISKS_JOB (data->local_job), bytes_target);
      udisks_job_set_rate (UDISKS_JOB (data->local_job), bytes_per_sec);

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
      udisks_job_set_progress (UDISKS_JOB (data->local_job), progress);

      if (usec_remaining == 0)
        udisks_job_set_expected_end_time (UDISKS_JOB (data->local_job), 0);
      else
        udisks_job_set_expected_end_time (UDISKS_JOB (data->local_job), usec_remaining + g_get_real_time ());
    }
}

/* ---------------------------------------------------------------------------------------------------- */

static void
play_complete_sound (DialogData *data)
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
on_update_job (gpointer user_data)
{
  DialogData *data = user_data;
  update_job (data, FALSE);
  dialog_data_unref (data);
  return FALSE; /* remove source */
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
on_show_error (gpointer user_data)
{
  DialogData *data = user_data;

  play_complete_sound (data);
  dialog_data_uninhibit (data);

  g_assert (data->copy_error != NULL);
  gdu_utils_show_error (GTK_WINDOW (data->window),
                        _("Error restoring disk image"),
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

  update_job (data, TRUE);

  play_complete_sound (data);
  dialog_data_uninhibit (data);
  dialog_data_complete_and_unref (data);

  dialog_data_unref (data);
  return FALSE; /* remove source */
}

/* ---------------------------------------------------------------------------------------------------- */

static gpointer
copy_thread_func (gpointer user_data)
{
  DialogData *data = user_data;
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
  if (!udisks_block_call_open_for_restore_sync (data->block,
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
  data->block_size = block_device_size;

  page_size = sysconf (_SC_PAGESIZE);
  buffer_unaligned = g_new0 (guchar, buffer_size + page_size);
  buffer = (guchar*) (((gintptr) (buffer_unaligned + page_size)) & (~(page_size - 1)));

  g_mutex_lock (&data->copy_lock);
  data->estimator = gdu_estimator_new (data->input_size);
  data->update_id = 0;
  data->start_time_usec = g_get_real_time ();
  g_mutex_unlock (&data->copy_lock);

  /* Read huge (e.g. 1 MiB) blocks and write it to the output
   * device even if it was only partially read.
   */
  num_bytes_completed = 0;
  while (num_bytes_completed < data->input_size)
    {
      gsize num_bytes_to_read;
      gsize num_bytes_read;
      ssize_t num_bytes_written;
      gint64 now_usec;

      num_bytes_to_read = buffer_size;
      if (num_bytes_to_read + num_bytes_completed > data->input_size)
        num_bytes_to_read = data->input_size - num_bytes_completed;

      /* Update GUI - but only every 200 ms and only if last update isn't pending */
      g_mutex_lock (&data->copy_lock);
      now_usec = g_get_monotonic_time ();
      if (now_usec - last_update_usec > 200 * G_USEC_PER_SEC / 1000 || last_update_usec < 0)
        {
          if (num_bytes_completed > 0)
            gdu_estimator_add_sample (data->estimator, num_bytes_completed);
          if (data->update_id == 0)
            data->update_id = g_idle_add (on_update_job, dialog_data_ref (data));
          last_update_usec = now_usec;
        }
      g_mutex_unlock (&data->copy_lock);

      if (!g_input_stream_read_all (data->input_stream,
                                    buffer,
                                    num_bytes_to_read,
                                    &num_bytes_read,
                                    data->cancellable,
                                    &error))
        {
          g_prefix_error (&error,
                          "Error reading %" G_GSIZE_FORMAT " bytes from offset %" G_GUINT64_FORMAT ": ",
                          num_bytes_to_read,
                          num_bytes_completed);
          goto out;
        }
      if (num_bytes_read != num_bytes_to_read)
        {
          g_set_error (&error, G_IO_ERROR, G_IO_ERROR_FAILED,
                       "Requested %" G_GSIZE_FORMAT " bytes from offset %" G_GUINT64_FORMAT " but only read %" G_GSIZE_FORMAT " bytes",
                       num_bytes_read,
                       num_bytes_completed,
                       num_bytes_to_read);
          goto out;
        }

    copy_write_again:
      num_bytes_written = write (fd, buffer, num_bytes_read);
      if (num_bytes_written < 0)
        {
          if (errno == EAGAIN || errno == EINTR)
            goto copy_write_again;

          g_set_error (&error, G_IO_ERROR, G_IO_ERROR_FAILED,
                       "Error writing %" G_GSIZE_FORMAT " bytes to offset %" G_GUINT64_FORMAT ": %m",
                       num_bytes_read,
                       num_bytes_completed);
          goto out;
        }

      /*g_print ("copied %" G_GUINT64_FORMAT " bytes at offset %" G_GUINT64_FORMAT "\n",
               (guint64) num_bytes_written,
               num_bytes_completed);*/

      num_bytes_completed += num_bytes_written;
    }

 out:
  data->end_time_usec = g_get_real_time ();

  /* in either case, close the stream */
  if (!g_input_stream_close (G_INPUT_STREAM (data->input_stream),
                              NULL, /* cancellable */
                              &error2))
    {
      g_warning ("Error closing file input stream: %s (%s, %d)",
                 error2->message, g_quark_to_string (error2->domain), error2->code);
      g_clear_error (&error2);
    }
  g_clear_object (&data->input_stream);

  if (fd != -1 )
    {
      if (close (fd) != 0)
        g_warning ("Error closing fd: %m");
    }

  if (error != NULL)
    {
      gboolean wipe_after_error = TRUE;

      if (error->domain == UDISKS_ERROR && error->code == UDISKS_ERROR_NOT_AUTHORIZED_DISMISSED)
        {
          wipe_after_error = FALSE;
        }

      /* show error in GUI */
      if (!(error->domain == G_IO_ERROR && error->code == G_IO_ERROR_CANCELLED))
        {
          data->copy_error = error; error = NULL;
          g_idle_add (on_show_error, dialog_data_ref (data));
        }
      g_clear_error (&error);

      /* Wipe the device */
      if (wipe_after_error && !udisks_block_call_format_sync (data->block,
                                          "empty",
                                          g_variant_new ("a{sv}", NULL), /* options */
                                          NULL, /* cancellable */
                                          &error2))
        {
          g_warning ("Error wiping device on error path: %s (%s, %d)",
                     error2->message, g_quark_to_string (error2->domain), error2->code);
          g_clear_error (&error2);
        }
    }
  else
    {
      /* success */
      g_idle_add (on_success, dialog_data_ref (data));
    }

  g_free (buffer_unaligned);

  /* finally, request that the core OS / kernel rescans the device */
  if (!udisks_block_call_rescan_sync (data->block,
                                      g_variant_new ("a{sv}", NULL), /* options */
                                      NULL, /* cancellable */
                                      &error2))
    {
      g_warning ("Error rescanning device: %s (%s, %d)",
                 error2->message, g_quark_to_string (error2->domain), error2->code);
      g_clear_error (&error2);
    }

  dialog_data_unref_in_idle (data); /* unref on main thread */
  return NULL;
}

/* ---------------------------------------------------------------------------------------------------- */

static void
on_local_job_canceled (GduLocalJob  *job,
                       gpointer      user_data)
{
  DialogData *data = user_data;
  if (!data->completed)
    {
      dialog_data_terminate_job (data);
      dialog_data_complete_and_unref (data);
      update_job (data, FALSE);
    }
}

static gboolean
start_copying (DialogData *data)
{
  GFile *file = NULL;
  gboolean ret = FALSE;
  GFileInfo *info;
  GError *error;

  error = NULL;
  if (data->disk_image_filename != NULL)
    file = g_file_new_for_commandline_arg (data->disk_image_filename);
  else
    file = gtk_file_chooser_get_file (GTK_FILE_CHOOSER (data->selectable_image_fcbutton));

  data->input_stream = (GInputStream *) g_file_read (file, NULL, &error);
  if (data->input_stream == NULL)
    {
      if (!(error->domain == G_IO_ERROR && error->code == G_IO_ERROR_CANCELLED))
        gdu_utils_show_error (GTK_WINDOW (data->dialog), _("Error opening file for reading"), error);
      g_error_free (error);
      dialog_data_complete_and_unref (data);
      goto out;
    }

  error = NULL;
  info = g_file_query_info (file,
                            G_FILE_ATTRIBUTE_STANDARD_CONTENT_TYPE ","
                            G_FILE_ATTRIBUTE_STANDARD_SIZE,
                            G_FILE_QUERY_INFO_NONE,
                            NULL,
                            &error);
  if (info == NULL)
    {
      gdu_utils_show_error (GTK_WINDOW (data->dialog), _("Error determining size of file"), error);
      g_error_free (error);
      dialog_data_complete_and_unref (data);
      goto out;
    }
  data->input_size = g_file_info_get_size (info);
  if (g_str_has_suffix (g_file_info_get_content_type (info), "-xz-compressed"))
    {
      GduXzDecompressor *decompressor;
      GInputStream *decompressed_input_stream;

      data->input_size = gdu_xz_decompressor_get_uncompressed_size (file);

      decompressor = gdu_xz_decompressor_new ();
      decompressed_input_stream = g_converter_input_stream_new (G_INPUT_STREAM (data->input_stream),
                                                                G_CONVERTER (decompressor));
      g_clear_object (&decompressor);

      g_object_unref (data->input_stream);
      data->input_stream = decompressed_input_stream;
    }
  g_object_unref (info);

  data->inhibit_cookie = gtk_application_inhibit (GTK_APPLICATION (gdu_window_get_application (data->window)),
                                                  GTK_WINDOW (data->dialog),
                                                  GTK_APPLICATION_INHIBIT_SUSPEND |
                                                  GTK_APPLICATION_INHIBIT_LOGOUT,
                                                  /* Translators: Reason why suspend/logout is being inhibited */
                                                  C_("restore-inhibit-message", "Copying disk image to device"));

  data->local_job = gdu_application_create_local_job (gdu_window_get_application (data->window),
                                                      data->object);
  udisks_job_set_operation (UDISKS_JOB (data->local_job), "x-gdu-restore-disk-image");
  /* Translators: this is the description of the job */
  gdu_local_job_set_description (data->local_job, _("Restoring Disk Image"));
  udisks_job_set_progress_valid (UDISKS_JOB (data->local_job), TRUE);
  udisks_job_set_cancelable (UDISKS_JOB (data->local_job), TRUE);
  g_signal_connect (data->local_job, "canceled",
                    G_CALLBACK (on_local_job_canceled),
                    data);

  dialog_data_hide (data);

  if (data->switch_to_object)
    gdu_window_select_object (data->window, data->object);

  g_thread_new ("copy-disk-image-thread",
                copy_thread_func,
                dialog_data_ref (data));
  ret = TRUE;

 out:
  g_clear_object (&file);
  return ret;
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
  GList *objects = NULL;
  GFile *folder = NULL;

  if (data->dialog == NULL)
    goto out;

  objects = g_list_append (NULL, data->object);

  switch (response)
    {
    case GTK_RESPONSE_OK:
      if (!gdu_utils_show_confirmation (GTK_WINDOW (data->dialog),
                                        _("Are you sure you want to write the disk image to the device?"),
                                        _("All existing data will be lost"),
                                        _("_Restore"),
                                        NULL, NULL,
                                        gdu_window_get_client (data->window), objects))
        {
          dialog_data_complete_and_unref (data);
          goto out;
        }

      /* now that we know the user picked a folder, update file chooser settings */
      folder = gtk_file_chooser_get_current_folder_file (GTK_FILE_CHOOSER (data->selectable_image_fcbutton));
      gdu_utils_file_chooser_for_disk_images_set_default_folder (folder);

      /* ensure the device is unused (e.g. unmounted) before copying data to it... */
      gdu_window_ensure_unused (data->window,
                                data->object,
                                (GAsyncReadyCallback) ensure_unused_cb,
                                NULL, /* GCancellable */
                                data);
      break;

    default: /* explicit fallthrough */
    case GTK_RESPONSE_CANCEL:
      dialog_data_complete_and_unref (data);
      break;
    }
 out:
  g_list_free (objects);
  g_clear_object (&folder);
}

void
gdu_restore_disk_image_dialog_show (GduWindow    *window,
                                    UDisksObject *object,
                                    const gchar  *disk_image_filename)
{
  guint n;
  DialogData *data;

  data = g_new0 (DialogData, 1);
  data->ref_count = 1;
  g_mutex_init (&data->copy_lock);
  data->window = g_object_ref (window);
  set_destination_object (data, object);
  if (object == NULL)
    data->switch_to_object = TRUE;
  data->disk_image_filename = g_strdup (disk_image_filename);
  data->cancellable = g_cancellable_new ();

  data->dialog = GTK_WIDGET (gdu_application_new_widget (gdu_window_get_application (data->window),
                                                         "restore-disk-image-dialog.ui",
                                                         "restore-disk-image-dialog",
                                                         &data->builder));
  for (n = 0; widget_mapping[n].name != NULL; n++)
    {
      gpointer *p = (gpointer *) ((char *) data + widget_mapping[n].offset);
      *p = gtk_builder_get_object (data->builder, widget_mapping[n].name);
    }
  g_signal_connect (data->selectable_image_fcbutton, "file-set", G_CALLBACK (on_file_set), data);

  data->warning_infobar = gdu_utils_create_info_bar (GTK_MESSAGE_INFO, "", &data->warning_label);
  gtk_box_pack_start (GTK_BOX (data->infobar_vbox), data->warning_infobar, TRUE, TRUE, 0);
  gtk_widget_set_no_show_all (data->warning_infobar, TRUE);
  g_object_ref (data->warning_infobar);

  data->error_infobar = gdu_utils_create_info_bar (GTK_MESSAGE_ERROR, "", &data->error_label);
  gtk_box_pack_start (GTK_BOX (data->infobar_vbox), data->error_infobar, TRUE, TRUE, 0);
  gtk_widget_set_no_show_all (data->error_infobar, TRUE);
  g_object_ref (data->error_infobar);

  restore_disk_image_populate (data);
  restore_disk_image_update (data);

  /* unfortunately, GtkFileChooserButton:file-set is not emitted when the user
   * unselects a file but we can work around that.. (TODO: file bug against gtk+)
   */
  g_signal_connect (data->selectable_image_fcbutton, "notify",
                    G_CALLBACK (on_notify), data);

  data->response_signal_handler_id = g_signal_connect (data->dialog,
                                                       "response",
                                                       G_CALLBACK (on_dialog_response),
                                                       data);

  gtk_window_set_transient_for (GTK_WINDOW (data->dialog), GTK_WINDOW (window));
  gtk_window_present (GTK_WINDOW (data->dialog));

  /* The Destination combo-box is only shown if @object is NULL. */
  if (object == NULL)
    {
      gtk_widget_realize (data->selectable_destination_combobox);
      gtk_widget_grab_focus (data->selectable_destination_combobox);
    }
}

/* ---------------------------------------------------------------------------------------------------- */
