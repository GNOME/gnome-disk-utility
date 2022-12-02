/*
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

struct _GduNewDiskImageDialog
{
  GtkDialog             parent_instance;

  GtkSpinButton        *size_spin_button;
  GtkComboBoxText      *size_unit_combobox;
  GtkEntry             *name_entry;
  /* GtkFileChooserButton *choose_folder_button; */

  GtkButton            *cancel_button;
  GtkButton            *create_image_button;
  GtkAdjustment        *size_adjustment;

  UnitSizeIndices       size_type;
};

G_DEFINE_TYPE (GduNewDiskImageDialog, gdu_new_disk_image_dialog, GTK_TYPE_DIALOG)

static void
create_new_disk (GduNewDiskImageDialog *self)
{
  g_autoptr(GFileOutputStream) out_file_stream = NULL;
  g_autoptr(GFile) out_file = NULL;
  g_autoptr(GFile) folder = NULL;
  g_autoptr(GError) error = NULL;
  g_autofree char *out_filename = NULL;
  const char *filename;
  GtkWindow *window;
  guint64 size;

  filename = gtk_editable_get_text (GTK_EDITABLE (self->name_entry));
  /* folder = gtk_file_chooser_get_file (GTK_FILE_CHOOSER (self->choose_folder_button)); */

  out_file = g_file_get_child (folder, filename);
  out_filename = g_file_get_path (out_file);
  out_file_stream = g_file_replace (out_file,
                                    NULL, /* etag */
                                    FALSE, /* make_backup */
                                    G_FILE_CREATE_NONE,
                                    NULL,
                                    &error);
  if (!out_file_stream)
    {
      gdu_utils_show_error (GTK_WINDOW (self), _("Error opening file for writing"), error);
      return;
    }

  /* now that we know the user picked a folder, update file chooser settings */
  gdu_utils_file_chooser_for_disk_images_set_default_folder (folder);

  size = gtk_adjustment_get_value (self->size_adjustment) * unit_sizes[self->size_type];

  /* will result in a sparse file if supported */
  if (!g_seekable_truncate (G_SEEKABLE (out_file_stream), size, NULL, &error))
    {
      if (error)
        g_warning ("Error truncating file output stream: %s (%s, %d)",
                   error->message, g_quark_to_string (error->domain), error->code);
      gdu_utils_show_error (GTK_WINDOW (self), _("Error writing file"), error);
      g_clear_error (&error);
    }

  if (!g_output_stream_close (G_OUTPUT_STREAM (out_file_stream), NULL, &error))
    {
      g_warning ("Error closing file output stream: %s (%s, %d)",
                 error->message, g_quark_to_string (error->domain), error->code);
    }

  window = gtk_window_get_transient_for (GTK_WINDOW (self));

  /* load loop device */
  gdu_window_attach_disk_image_helper (GDU_WINDOW (window), out_filename, FALSE);

  gtk_widget_hide (GTK_WIDGET (self));
  gtk_window_destroy (GTK_WINDOW (self));
}

static void
new_disk_image_confirm_response_cb (GduNewDiskImageDialog *self,
                                    int                    response_id,
                                    GtkWidget             *dialog)
{
  g_assert (GDU_IS_NEW_DISK_IMAGE_DIALOG (self));
  g_assert (GTK_IS_DIALOG (dialog));

  gtk_widget_hide (dialog);
  gtk_window_destroy (GTK_WINDOW (dialog));

  if (response_id == GTK_RESPONSE_ACCEPT)
    create_new_disk (self);
}

static void
confirm_and_create_new_disk (GduNewDiskImageDialog *self)
{
  g_autoptr(GFileInfo) folder_info = NULL;
  g_autoptr(GFile) folder = NULL;
  g_autoptr(GFile) file = NULL;
  const char *filename;
  GtkWidget *dialog;

  filename = gtk_editable_get_text (GTK_EDITABLE (self->name_entry));
  /* folder = gtk_file_chooser_get_file (GTK_FILE_CHOOSER (self->choose_folder_button)); */
  file = g_file_get_child (folder, filename);
  if (!g_file_query_exists (file, NULL))
    {
      create_new_disk (self);
      return;
    }

  folder_info = g_file_query_info (folder,
                                   G_FILE_ATTRIBUTE_STANDARD_DISPLAY_NAME,
                                   G_FILE_QUERY_INFO_NONE,
                                   NULL,
                                   NULL);
  if (!folder_info)
    {
      create_new_disk (self);
      return;
    }

  dialog = gtk_message_dialog_new (GTK_WINDOW (self),
                                   GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                   GTK_MESSAGE_QUESTION,
                                   GTK_BUTTONS_NONE,
                                   _("A file named “%s” already exists.  Do you want to replace it?"),
                                   filename);
  gtk_message_dialog_format_secondary_text (GTK_MESSAGE_DIALOG (dialog),
                                            _("The file already exists in “%s”.  Replacing it will overwrite its contents."),
                                            g_file_info_get_display_name (folder_info));
  gtk_dialog_add_button (GTK_DIALOG (dialog), _("_Cancel"), GTK_RESPONSE_CANCEL);
  gtk_dialog_add_button (GTK_DIALOG (dialog), _("_Replace"), GTK_RESPONSE_ACCEPT);
  gtk_dialog_set_default_response (GTK_DIALOG (dialog), GTK_RESPONSE_CANCEL);

  g_signal_connect_object (dialog, "response",
                           G_CALLBACK (new_disk_image_confirm_response_cb),
                           self, G_CONNECT_SWAPPED);
  gtk_window_present (GTK_WINDOW (dialog));
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
new_disk_image_dialog_response_cb (GduNewDiskImageDialog *self,
                                   int                    response_id)
{
  g_assert (GDU_IS_NEW_DISK_IMAGE_DIALOG (self));

  if (response_id == GTK_RESPONSE_OK)
    {
      confirm_and_create_new_disk (self);
    }
  else
    {
      gtk_widget_hide (GTK_WIDGET (self));
      gtk_window_destroy (GTK_WINDOW (self));
    }
}

static void
new_disk_image_details_changed_cb (GduNewDiskImageDialog *self)
{
  const char *filename;
  gboolean can_proceed = FALSE;

  filename = gtk_editable_get_text (GTK_EDITABLE (self->name_entry));

  if (filename && *filename &&
      gtk_adjustment_get_value (self->size_adjustment) > 0)
    can_proceed = TRUE;

  gtk_widget_set_sensitive (GTK_WIDGET (self->create_image_button), can_proceed);
}

static void
new_disk_image_size_unit_changed_cb (GduNewDiskImageDialog *self,
                                     GtkComboBox           *combo_box)
{
  UnitSizeIndices old_size_type;
  gdouble old_size, new_size, bytes;

  g_assert (GDU_IS_NEW_DISK_IMAGE_DIALOG (self));
  g_assert (GTK_IS_COMBO_BOX (combo_box));

  old_size_type = self->size_type;
  self->size_type = gtk_combo_box_get_active (combo_box);

  if (self->size_type == old_size_type)
    return;

  /* Convert size from one type to another */
  /* Say if the user changed from GB to MB and the spin button has a value of 1.0
   * it'll be changed to 1000.0 */

  old_size = gtk_adjustment_get_value (self->size_adjustment);
  bytes = old_size * unit_sizes[old_size_type];

  new_size = bytes / unit_sizes[self->size_type];
  gtk_adjustment_set_value (self->size_adjustment, new_size);
}

static void
gdu_new_disk_image_dialog_class_init (GduNewDiskImageDialogClass *klass)
{
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  gtk_widget_class_set_template_from_resource (widget_class,
                                               "/org/gnome/Disks/"
                                               "ui/new-disk-image-dialog.ui");

  gtk_widget_class_bind_template_child (widget_class, GduNewDiskImageDialog, size_spin_button);
  gtk_widget_class_bind_template_child (widget_class, GduNewDiskImageDialog, size_unit_combobox);
  gtk_widget_class_bind_template_child (widget_class, GduNewDiskImageDialog, name_entry);
  /* gtk_widget_class_bind_template_child (widget_class, GduNewDiskImageDialog, choose_folder_button); */

  gtk_widget_class_bind_template_child (widget_class, GduNewDiskImageDialog, cancel_button);
  gtk_widget_class_bind_template_child (widget_class, GduNewDiskImageDialog, create_image_button);
  gtk_widget_class_bind_template_child (widget_class, GduNewDiskImageDialog, size_adjustment);

  gtk_widget_class_bind_template_callback (widget_class, new_disk_image_dialog_response_cb);
  gtk_widget_class_bind_template_callback (widget_class, new_disk_image_details_changed_cb);
  gtk_widget_class_bind_template_callback (widget_class, new_disk_image_size_unit_changed_cb);
}

static void
gdu_new_disk_image_dialog_init (GduNewDiskImageDialog *self)
{
  gtk_widget_init_template (GTK_WIDGET (self));

  gtk_combo_box_set_active (GTK_COMBO_BOX (self->size_unit_combobox), GByte);
  new_disk_image_set_default_name (self);

  /* gdu_utils_configure_file_chooser_for_disk_images (GTK_FILE_CHOOSER (self->choose_folder_button), */
  /*                                                   FALSE,   /\* set file types *\/ */
  /*                                                   FALSE);  /\* allow_compressed *\/ */
}

static GduNewDiskImageDialog *
gdu_new_disk_image_dialog_new (void)
{
  return g_object_new (GDU_TYPE_NEW_DISK_IMAGE_DIALOG, NULL);
}

void
gdu_new_disk_image_dialog_show (GduWindow *window)
{
  GduNewDiskImageDialog *self;

  self = gdu_new_disk_image_dialog_new ();
  gtk_window_set_transient_for (GTK_WINDOW (self), GTK_WINDOW (window));

  gtk_window_present (GTK_WINDOW (self));
}
