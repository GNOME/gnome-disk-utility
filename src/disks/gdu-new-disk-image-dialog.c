/*
 * Copyright (C) 2008-2013 Red Hat, Inc.
 * Copyright (C) 2017 Kai Lüke
 * Copyright (C) 2022 Purism SPC
 *
 * Licensed under GPL version 2 or later.
 *
 * Author(s):
 *   Kai Lüke <kailueke@riseup.net>
 *   Mohammed Sadiq <sadiq@sadiqpk.org>
 */

#include "config.h"

#define _GNU_SOURCE
#include <fcntl.h>
#include <glib/gi18n.h>
#include <math.h>

#include <linux/fs.h>

#include "gdu-new-disk-image-dialog.h"
#include "gduutils.h"

/* TODOs: ideas for New Disk Image creation
 * include a radio toggle to create either
 * - a full disk image with a partition table or
 * - just a partition image with a filesystem
 * (embed respective widgets like gducreatepartitiondialog does)
 */

struct _GduNewDiskImageDialog
{
  AdwDialog      parent_instance;

  GtkWidget     *create_image_button;

  GtkWidget     *name_entry;
  GtkWidget     *location_entry;
  GtkWidget     *size_entry;
  GtkWidget     *size_unit_combo;

  GFile         *directory;

  UDisksClient  *client;
  gint           cur_unit_num;
};

G_DEFINE_TYPE (GduNewDiskImageDialog, gdu_new_disk_image_dialog, ADW_TYPE_DIALOG)

static gpointer
gdu_new_disk_image_dialog_get_window (GduNewDiskImageDialog *self)
{
  return gtk_widget_get_ancestor (GTK_WIDGET (self), GTK_TYPE_WINDOW);
}

/* adapted from
 * https://gitlab.gnome.org/GNOME/gnome-disk-utility/-/blob/3eccf2b5fec7200cb16c46dd5d047c083ac318f7/src/disks/gduwindow.c#L729
 */
static void
loop_setup_cb (UDisksManager *manager, GAsyncResult *res, gpointer user_data)
{
  GduNewDiskImageDialog *self;
  g_autoptr(GTask) task = user_data;
  g_autoptr(GError) error = NULL;
  g_autofree char *uri = NULL;
  g_autofree char *out_loop_device_object_path = NULL;
  char *filename;

  self = g_task_get_source_object (task);
  filename = g_task_get_task_data (task);

  if (!udisks_manager_call_loop_setup_finish (manager,
                                              &out_loop_device_object_path,
                                              NULL, res, &error))
    {
      gdu_utils_show_error (gdu_new_disk_image_dialog_get_window (self),
                            _("Error attaching disk image"),
                            error);
      return;
    }

  /* This is to make it appear in the file chooser's "Recently Used" list
   */
  uri = g_strdup_printf ("file://%s", filename);
  gtk_recent_manager_add_item (gtk_recent_manager_get_default (), uri);

  udisks_client_settle (self->client);
}

static gboolean
dialog_attach_disk_image_helper (GduNewDiskImageDialog *self,
                                 char *filename,
                                 gboolean readonly)
{
  g_autoptr(GTask) task = NULL;
  g_autoptr(GUnixFDList) fd_list = NULL;
  GVariantBuilder options_builder;
  gint fd = -1;

  task = g_task_new (self, NULL, NULL, NULL);
  g_task_set_task_data (task, g_strdup (filename), g_free);

  fd = open (filename, O_RDWR);
  if (fd == -1)
    {
      fd = open (filename, O_RDONLY);
    }
  if (fd == -1)
    {
      g_autoptr(GError) error = NULL;

      error = g_error_new (G_IO_ERROR, g_io_error_from_errno (errno), "%s",
                           strerror (errno));
      gdu_utils_show_error (gdu_new_disk_image_dialog_get_window (self),
                            _("Error attaching disk image"),
                            error);
      return FALSE;
    }

  g_variant_builder_init (&options_builder, G_VARIANT_TYPE_VARDICT);
  if (readonly)
    {
      g_variant_builder_add (&options_builder, "{sv}", "read-only",
                        g_variant_new_boolean (TRUE));
    }
  fd_list = g_unix_fd_list_new_from_array (&fd, 1); /* adopts the fd */
  udisks_manager_call_loop_setup (udisks_client_get_manager (self->client),
                                  g_variant_new_handle (0),
                                  g_variant_builder_end (&options_builder),
                                  fd_list, NULL, /* GCancellable */
                                  (GAsyncReadyCallback)loop_setup_cb,
                                  g_steal_pointer (&task));

  return TRUE;
}

static void
create_new_disk (GduNewDiskImageDialog *self)
{
  g_autoptr(GFileOutputStream) out_file_stream = NULL;
  g_autoptr(GFile) out_file = NULL;
  g_autoptr(GError) error = NULL;
  g_autofree char *out_filename = NULL;
  const char *filename;
  guint64 size;

  filename = gtk_editable_get_text (GTK_EDITABLE (self->name_entry));

  out_file = g_file_get_child (self->directory, filename);
  out_filename = g_file_get_path (out_file);
  out_file_stream = g_file_replace (out_file, NULL, /* etag */
                                    FALSE,          /* make_backup */
                                    G_FILE_CREATE_NONE, NULL, &error);
  if (!out_file_stream)
    {
      gdu_utils_show_error (gdu_new_disk_image_dialog_get_window (self),
                            _("Error opening file for writing"), error);
      return;
    }

  size = adw_spin_row_get_value (ADW_SPIN_ROW (self->size_entry)) * unit_sizes[self->cur_unit_num];

  /* will result in a sparse file if supported */
  if (!g_seekable_truncate (G_SEEKABLE (out_file_stream), size, NULL, &error))
    {
      if (error)
        {
          g_warning ("Error truncating file output stream: %s (%s, %d)",
                     error->message, g_quark_to_string (error->domain), error->code);
        }
      gdu_utils_show_error (gdu_new_disk_image_dialog_get_window (self), _("Error writing file"), error);
    }

  if (!g_output_stream_close (G_OUTPUT_STREAM (out_file_stream), NULL, &error))
    {
      g_warning ("Error closing file output stream: %s (%s, %d)",
                 error->message, g_quark_to_string (error->domain),
                 error->code);
    }

  /* load loop device */
  dialog_attach_disk_image_helper (self, out_filename, FALSE);

  adw_dialog_close (ADW_DIALOG (self));
}

static void
new_disk_image_set_directory (GduNewDiskImageDialog *self)
{
  g_autofree char *path = NULL;

  if (self->directory == NULL)
    {
      self->directory = g_file_new_for_path (g_get_user_special_dir (G_USER_DIRECTORY_DOCUMENTS));
    }

  path = gdu_utils_unfuse_path (g_file_get_path (self->directory));

  adw_action_row_set_subtitle (ADW_ACTION_ROW (self->location_entry), path);
}

static void
file_dialog_open_cb (GObject *source_object, GAsyncResult *res,
                     gpointer user_data)
{
  GduNewDiskImageDialog *self = GDU_NEW_DISK_IMAGE_DIALOG (user_data);
  GtkFileDialog *file_dialog = GTK_FILE_DIALOG (source_object);
  g_autoptr (GError) error = NULL;

  self->directory = gtk_file_dialog_select_folder_finish (file_dialog, res, &error);
  if (!self->directory)
    {
      g_warning ("Error selecting folder: %s", error->message);
      return;
    }

  new_disk_image_set_directory (self);
}

static gboolean
set_size_entry_unit_cb (AdwSpinRow *spin_row, gpointer *user_data)
{
  GduNewDiskImageDialog *self = GDU_NEW_DISK_IMAGE_DIALOG (user_data);
  GtkAdjustment *adjustment;
  GObject *object = NULL;
  const char *unit = NULL;
  g_autofree char *s = NULL;

  adjustment = adw_spin_row_get_adjustment (spin_row);

  object = adw_combo_row_get_selected_item (ADW_COMBO_ROW (self->size_unit_combo));
  unit = gtk_string_object_get_string (GTK_STRING_OBJECT (object));

  s = g_strdup_printf ("%.2f %s", gtk_adjustment_get_value (adjustment), unit);
  gtk_editable_set_text (GTK_EDITABLE (spin_row), s);

  return TRUE;
}

static void
on_choose_folder_button_clicked_cb (GduNewDiskImageDialog *self)
{
  g_autoptr (GFile) documents_folder = NULL;
  GtkFileDialog *file_dialog;
  GtkWindow *toplevel;

  toplevel = gdu_new_disk_image_dialog_get_window (self);
  if (toplevel == NULL)
    {
      g_info ("Could not get native window for dialog");
    }

  file_dialog = gtk_file_dialog_new ();
  gtk_file_dialog_set_title (file_dialog,
                             _("Choose a location to save the disk image."));
  gtk_file_dialog_set_modal (file_dialog, TRUE);

  documents_folder = g_file_new_for_path (g_get_user_special_dir (G_USER_DIRECTORY_DOCUMENTS));
  gtk_file_dialog_set_initial_folder (file_dialog, documents_folder);

  gtk_file_dialog_select_folder (file_dialog,
                                 toplevel, NULL,
                                 file_dialog_open_cb,
                                 self);
}

static void
new_disk_image_confirm_response_cb (GObject      *object,
                                    GAsyncResult *response,
                                    gpointer      user_data)
{
  GduNewDiskImageDialog *self = GDU_NEW_DISK_IMAGE_DIALOG (user_data);
  AdwAlertDialog *dialog = ADW_ALERT_DIALOG (object);

  if (g_strcmp0 (adw_alert_dialog_choose_finish (dialog, response), "cancel") == 0)
    return;

  create_new_disk (self);
}

static void
on_create_image_button_clicked_cb (GduNewDiskImageDialog *self)
{
  const char *filename = NULL;
  g_autoptr(GFile) file = NULL;
  ConfirmationDialogData *data;

  filename = gtk_editable_get_text (GTK_EDITABLE (self->name_entry));
  file = g_file_get_child (self->directory, filename);

  if (!g_file_query_exists (file, NULL))
    {
      create_new_disk (self);
      return;
    }

  data = g_new0 (ConfirmationDialogData, 1);
  data->message = _("Replace File?");
  data->description = g_strdup_printf (_("A file named “%s” already exists in %s"), filename, gdu_utils_unfuse_path (g_file_get_path (self->directory)));
  data->response_verb = _("Replace");
  data->response_appearance = ADW_RESPONSE_DESTRUCTIVE;
  data->callback = new_disk_image_confirm_response_cb;
  data->user_data = self;

  gdu_utils_show_confirmation (GTK_WIDGET (self),
                               data, NULL);
}

static void
new_disk_image_set_default_name (GduNewDiskImageDialog *self)
{
  g_autofree char *now_string = NULL;
  g_autofree char *filename = NULL;
  g_autoptr(GDateTime) now = NULL;
  g_autoptr(GTimeZone) tz = NULL;

  tz = g_time_zone_new_local ();
  now = g_date_time_new_now (tz);
  now_string = g_date_time_format (now, "%Y-%m-%d %H%M");

  /* Translators: The suggested name for the disk image to create.
   *              The %s is today's date and time, e.g. "March 2, 1976 6:25AM".
   */
  filename = g_strdup_printf (_("Unnamed (%s).img"), now_string);
  gtk_editable_set_text (GTK_EDITABLE (self->name_entry), filename);
}

static void
new_disk_image_details_changed_cb (GduNewDiskImageDialog *self)
{
  const char *filename;
  gboolean can_proceed = FALSE;
  double size_value;

  filename = gtk_editable_get_text (GTK_EDITABLE (self->name_entry));
  size_value = adw_spin_row_get_value (ADW_SPIN_ROW (self->size_entry));

  if (filename && *filename && size_value > 0)
    {
      can_proceed = TRUE;
    }

  if (filename && *filename)
    {
      gtk_widget_remove_css_class (self->name_entry, "error");
    }
  else
    {
      gtk_widget_add_css_class (self->name_entry, "error");
    }

  if (size_value == 0)
    {
      gtk_widget_add_css_class (self->size_entry, "error");
    }
  else
    {
      gtk_widget_remove_css_class (self->size_entry, "error");
    }

  gtk_widget_set_sensitive (self->create_image_button, can_proceed);
}

static void
on_size_unit_changed_cb (GduNewDiskImageDialog *self)
{
  GtkAdjustment *adjustment;
  gint unit_num;
  gdouble value;
  gdouble value_units;

  unit_num = adw_combo_row_get_selected (ADW_COMBO_ROW (self->size_unit_combo));

  adjustment = adw_spin_row_get_adjustment (ADW_SPIN_ROW (self->size_entry));
  value = gtk_adjustment_get_value (adjustment) * ((gdouble) unit_sizes[self->cur_unit_num]);
  value_units = value / unit_sizes[unit_num];

  self->cur_unit_num = unit_num;

  gtk_adjustment_set_value (adjustment, value_units);
  set_size_entry_unit_cb (ADW_SPIN_ROW (self->size_entry), (void *)self);
}

static void
gdu_new_disk_image_dialog_class_init (GduNewDiskImageDialogClass *klass)
{
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  gtk_widget_class_set_template_from_resource (widget_class,
                                               "/org/gnome/DiskUtility/ui/"
                                               "gdu-new-disk-image-dialog.ui");

  gtk_widget_class_bind_template_child (widget_class, GduNewDiskImageDialog, create_image_button);

  gtk_widget_class_bind_template_child (widget_class, GduNewDiskImageDialog, name_entry);
  gtk_widget_class_bind_template_child (widget_class, GduNewDiskImageDialog, location_entry);
  gtk_widget_class_bind_template_child (widget_class, GduNewDiskImageDialog, size_entry);
  gtk_widget_class_bind_template_child (widget_class, GduNewDiskImageDialog, size_unit_combo);

  gtk_widget_class_bind_template_callback (widget_class, set_size_entry_unit_cb);
  gtk_widget_class_bind_template_callback (widget_class, on_size_unit_changed_cb);
  gtk_widget_class_bind_template_callback (widget_class, on_choose_folder_button_clicked_cb);
  gtk_widget_class_bind_template_callback (widget_class, on_create_image_button_clicked_cb);
  gtk_widget_class_bind_template_callback (widget_class, new_disk_image_details_changed_cb);
}

static void
gdu_new_disk_image_dialog_init (GduNewDiskImageDialog *self)
{
  gtk_widget_init_template (GTK_WIDGET (self));

  self->cur_unit_num = 3; /* GB */
  adw_combo_row_set_selected (ADW_COMBO_ROW (self->size_unit_combo), self->cur_unit_num);

  new_disk_image_set_default_name (self);
  new_disk_image_set_directory (self);
}

void
gdu_new_disk_image_dialog_show (UDisksClient *client, GtkWindow *parent_window)
{
  GduNewDiskImageDialog *self;

  self = g_object_new (GDU_TYPE_NEW_DISK_IMAGE_DIALOG, NULL);

  g_return_if_fail (client != NULL);
  self->client = client;

  g_return_if_fail (client != NULL);
  self->client = client;

  adw_dialog_present (ADW_DIALOG (self), GTK_WIDGET (parent_window));
}
