/*
 * Copyright (C) 2008-2013 Red Hat, Inc.
 * Copyright (C) 2017 Kai Lüke
 * Copyright (C) 2023 Mohammed Sadiq
 *
 * Licensed under GPL version 2 or later.
 *
 * Author(s):
 *   David Zeuthen <zeuthen@gmail.com>
 *   Kai Lüke <kailueke@riseup.net>
 *   Mohammed Sadiq <sadiq@sadiqpk.org>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <glib/gi18n.h>

#include "gdu-application.h"
#include "gdu-create-confirm-page.h"
#include "gdu-create-filesystem-page.h"
#include "gdu-create-other-page.h"
#include "gdu-create-partition-page.h"
#include "gdu-create-password-page.h"
#include "gdu-format-volume-dialog.h"

#define PARTITION_PAGE "partition"
#define FORMAT_PAGE "format"
#define OTHER_PAGE "other"
#define PASSWORD_PAGE "password"
#define CONFIRM_PAGE "confirm"

#include "gdu-format-volume-dialog.h"

struct _GduFormatVolumeDialog
{
  AdwDialog                parent_instance;

  GtkWidget               *pages_stack;
  GduCreatePartitionPage  *partition_page;
  GduCreateFilesystemPage *filesystem_page;
  GduCreateOtherPage      *other_page;
  GduCreatePasswordPage   *password_page;
  GduCreateConfirmPage    *confirm_page;

  GtkWidget               *back_button;
  GtkWidget               *forward_button;

  UDisksClient            *udisks_client;
  UDisksObject            *udisks_object;
  UDisksBlock             *udisks_block;
  UDisksDrive             *udisks_drive;
  UDisksPartitionTable    *udisks_table;

  const char              *current; /* page names */
  const char              *prev;
  const char              *next;

  gboolean                 add_partition; /* mode: format vs add partition and format */
  guint64                  add_partition_offset;
  guint64                  add_partition_maxsize;
};

G_DEFINE_TYPE (GduFormatVolumeDialog, gdu_format_volume_dialog, ADW_TYPE_DIALOG)

static gpointer
gdu_format_volume_dialog_get_window (GduFormatVolumeDialog *self)
{
  return gtk_widget_get_ancestor (GTK_WIDGET (self), GTK_TYPE_WINDOW);
}

static const gchar *
gdu_format_volume_dialog_get_fs_type (GduFormatVolumeDialog *self)
{
  if (self->add_partition && gdu_create_partition_page_is_extended (self->partition_page))
    {
      return "dos_extended";
    }

  return g_strcmp0 (gdu_create_filesystem_page_get_fs (self->filesystem_page), "other") != 0 ?
         gdu_create_filesystem_page_get_fs (self->filesystem_page) :
         gdu_create_other_page_get_fs (self->other_page);
}

static gboolean
gdu_format_volume_dialog_get_encrypt (GduFormatVolumeDialog *self)
{
  return gdu_create_filesystem_page_is_other (self->filesystem_page)
             ? gdu_create_other_page_is_encrypted (self->other_page)
             : gdu_create_filesystem_page_is_encrypted (self->filesystem_page);
}

static void
gdu_format_volume_dialog_set_dialog_title (GduFormatVolumeDialog *self)
{
  const char *title;
  GtkWidget *child;
  GtkStackPage *page;

  child = gtk_stack_get_child_by_name (GTK_STACK (self->pages_stack),
                                       self->current);
  page = gtk_stack_get_page (GTK_STACK (self->pages_stack), child);
  title = gtk_stack_page_get_title (page);

  adw_dialog_set_title (ADW_DIALOG (self), title);
}

static GtkWidget *
gdu_format_volume_dialog_update_stack (GduFormatVolumeDialog *self)
{
  GtkWidget *page;

  if (g_strcmp0 (self->current, PARTITION_PAGE) == 0)
    {
      page = GTK_WIDGET (self->partition_page);
      self->prev = NULL;
      self->next = !gdu_create_partition_page_is_extended (self->partition_page)
                      ? FORMAT_PAGE
                      : NULL;
      return page;
    }

  if (g_strcmp0 (self->current, FORMAT_PAGE) == 0)
    {
      page = GTK_WIDGET (self->filesystem_page);
      self->prev = self->add_partition ? PARTITION_PAGE : NULL;
      self->next = !self->add_partition ? CONFIRM_PAGE : NULL;

      if (gdu_create_filesystem_page_is_encrypted (self->filesystem_page))
        {
          self->next = PASSWORD_PAGE;
        }

      if (gdu_create_filesystem_page_is_other (self->filesystem_page))
        {
          self->next = OTHER_PAGE;
        }
      return page;
    }

  if (g_strcmp0 (self->current, OTHER_PAGE) == 0)
    {
      page = GTK_WIDGET (self->other_page);
      self->prev = FORMAT_PAGE;
      self->next = gdu_create_other_page_is_encrypted (self->other_page)
                       ? PASSWORD_PAGE
                       : CONFIRM_PAGE;
      return page;
    }

  if (g_strcmp0 (self->current, PASSWORD_PAGE) == 0)
    {
      page = GTK_WIDGET (self->password_page);
      self->next = CONFIRM_PAGE;
      if (gdu_create_filesystem_page_is_other (self->filesystem_page)
          && gdu_create_other_page_is_encrypted (self->other_page))
        {
          self->prev = OTHER_PAGE;
        }
      else if (gdu_create_filesystem_page_is_encrypted (self->filesystem_page))
        {
          self->prev = FORMAT_PAGE;
        }
      return page;
    }

  if (g_strcmp0 (self->current, CONFIRM_PAGE) == 0)
    {
      page = GTK_WIDGET (self->confirm_page);
      self->next = NULL;
      self->prev = gdu_create_filesystem_page_is_other (self->filesystem_page)
                       ? OTHER_PAGE
                       : FORMAT_PAGE;

      if (gdu_format_volume_dialog_get_encrypt (self))
        {
          self->prev = PASSWORD_PAGE;
        }
      return page;
    }

  return NULL;
}

static void
update_dialog (GtkWidget             *widget,
               GParamSpec            *child_property,
               GduFormatVolumeDialog *self)
{
  gboolean complete = FALSE;
  GtkWidget *page;

  page = gdu_format_volume_dialog_update_stack (self);

  gtk_button_set_label (GTK_BUTTON (self->back_button), (self->prev == NULL)
                                                            ? _ ("_Cancel")
                                                            : _ ("_Previous"));

  if (self->next == NULL)
    {
      gtk_button_set_label (GTK_BUTTON (self->forward_button),
                            self->add_partition ? _ ("Cre_ate")
                                                : _ ("Form_at"));
      gtk_widget_add_css_class (self->forward_button,
                                self->add_partition ? "suggested-action"
                                                    : "destructive-action");
    }
  else
    {
      gtk_button_set_label (GTK_BUTTON (self->forward_button), _ ("N_ext"));
      gtk_widget_remove_css_class (self->forward_button, "suggested-action");
      gtk_widget_remove_css_class (self->forward_button, "destructive-action");
    }

  g_object_get (page, "complete", &complete, NULL);
  gtk_widget_set_sensitive (GTK_WIDGET (self->forward_button), complete);
  gtk_stack_set_visible_child_name (GTK_STACK (self->pages_stack),
                                    self->current);
  gdu_format_volume_dialog_set_dialog_title (self);
}

static void
format_cb (GObject      *source_object,
           GAsyncResult *res,
           gpointer      user_data)
{
  GduFormatVolumeDialog *self = user_data;
  g_autoptr (GError) error = NULL;

  if (!udisks_block_call_format_finish (UDISKS_BLOCK (source_object), res,
                                        &error))
    gdu_utils_show_error (gdu_format_volume_dialog_get_window (self),
                          _("Error formatting volume"), error);

  g_object_unref (self);
}

static void
ensure_unused_cb (UDisksClient          *client,
                  GAsyncResult          *res,
                  GduFormatVolumeDialog *self)
{
  GVariantBuilder options_builder;
  const gchar *fs_type;
  const gchar *erase_type;

  if (!gdu_utils_ensure_unused_finish (client, res, NULL))
    {
      return;
    }

  fs_type = gdu_format_volume_dialog_get_fs_type (self);
  erase_type = gdu_create_filesystem_page_get_erase (self->filesystem_page);

  g_variant_builder_init (&options_builder, G_VARIANT_TYPE_VARDICT);
  if (g_strcmp0 (fs_type, "empty") != 0)
    {
      g_variant_builder_add (
          &options_builder, "{sv}", "label",
          g_variant_new_string (gdu_create_filesystem_page_get_name (self->filesystem_page)));
    }

  if (g_strcmp0 (fs_type, "vfat") != 0
      && g_strcmp0 (fs_type, "ntfs") != 0
      && g_strcmp0 (fs_type, "exfat") != 0)
    {
      g_variant_builder_add (&options_builder, "{sv}", "take-ownership",
                             g_variant_new_boolean (TRUE));
    }

  if (gdu_format_volume_dialog_get_encrypt (self))
    {
      g_variant_builder_add (
          &options_builder, "{sv}", "encrypt.passphrase",
          g_variant_new_string (gdu_create_password_page_get_password (self->password_page)));

      g_variant_builder_add (&options_builder, "{sv}", "encrypt.type",
                             g_variant_new_string ("luks2"));
    }

  if (erase_type != NULL)
    g_variant_builder_add (&options_builder, "{sv}", "erase",
                           g_variant_new_string (erase_type));

  g_variant_builder_add (&options_builder, "{sv}", "update-partition-type",
                         g_variant_new_boolean (TRUE));

  udisks_block_call_format (self->udisks_block,
                            fs_type,
                            g_variant_builder_end (&options_builder),
                            NULL, /* GCancellable */
                            format_cb,
                            self);
}

static void
create_partition_cb (GObject *source_object, GAsyncResult *res, gpointer user_data);

static void
on_forward_button_clicked_cb (GduFormatVolumeDialog *self,
                              GtkWidget             *widget)
{
  guint64 size;
  const char *partition_type = "";
  GVariantBuilder options_builder;

  /* step to next page */
  if (self->next != NULL)
    {
      self->current = self->next;
      update_dialog (NULL, NULL, self);
      return;
    }

  if (!self->add_partition)
    {
      /* ensure the volume is unused (e.g. unmounted) before formatting it */
      gdu_utils_ensure_unused (self->udisks_client,
                               gdu_format_volume_dialog_get_window (self),
                               self->udisks_object,
                               (GAsyncReadyCallback)ensure_unused_cb,
                               NULL, /* GCancellable */
                               g_object_ref (self));
      adw_dialog_close (ADW_DIALOG (self));
      return;
    }

  g_variant_builder_init (&options_builder, G_VARIANT_TYPE_VARDICT);

  if (g_strcmp0 (gdu_format_volume_dialog_get_fs_type (self), "dos_extended") == 0)
    {
      partition_type = "0x05";
      g_variant_builder_add (&options_builder, "{sv}", "partition-type",
                             g_variant_new_string ("extended"));
    }
  else if (g_strcmp0 (udisks_partition_table_get_type_ (self->udisks_table), "dos") == 0)
    {
      if (gdu_utils_is_inside_dos_extended (self->udisks_client,
                                            self->udisks_table,
                                            self->add_partition_offset))
        {
          g_variant_builder_add (&options_builder, "{sv}", "partition-type",
                                 g_variant_new_string ("logical"));
        }
      else
        {
          g_variant_builder_add (&options_builder, "{sv}", "partition-type",
                                 g_variant_new_string ("primary"));
        }
    }

  size = gdu_create_partition_page_get_size (self->partition_page);
  /* Normal alignments of a few MB are expected to happen and normally it works
   * well to pass a slightly larger size. Yet sometimes there are huge amounts
   * of alignment enforced by libblockdev/libparted and it's unclear how many
   * MB should be left when creating a partition on a 1 TB drive. The maximal
   * size we present to the user is based on the size of unallocated space
   * which may be too large. To address this issue, rely on the maximal size
   * being found automatically by passing the special argument 0. However, this
   * won't cover cases where the user substracts a few MB and now wonders why
   * the partition can't be created. Heuristics for these corner cases could be
   * implemented or libblockdev improved to handle these cases better but it
   * would be easier and more accurate to create a partition with a maximal
   * size first and then shrink it to the user's desired size if needed.
   */
  if (size == self->add_partition_maxsize)
    {
      size = 0;
    }
  udisks_partition_table_call_create_partition (self->udisks_table,
                                                self->add_partition_offset,
                                                size,
                                                partition_type, /* use default type */
                                                "",             /* use blank partition name */
                                                g_variant_builder_end (&options_builder),
                                                NULL,           /* GCancellable */
                                                create_partition_cb,
                                                g_object_ref (self));
}

static void
create_partition_cb (GObject      *source_object,
                     GAsyncResult *res,
                     gpointer      user_data)
{
  g_autoptr(GduFormatVolumeDialog) self = user_data;
  g_autoptr(GError) error = NULL;
  g_autofree char *created_partition_object_path = NULL;
  UDisksObject *partition_object = NULL;
  UDisksBlock *partition_block;

  if (!udisks_partition_table_call_create_partition_finish (
          UDISKS_PARTITION_TABLE (source_object),
          &created_partition_object_path, res, &error))
    {
      gdu_utils_show_error (gdu_format_volume_dialog_get_window (self),
                            _("Error creating partition"), error);
      return;
    }

  udisks_client_settle (self->udisks_client);

  partition_object = udisks_client_get_object (self->udisks_client,
                                               created_partition_object_path);
  /* gtk4 todo: to be done with GtkSelectionModel */
  /* gdu_window_select_object (GDU_WINDOW (self->parent_window),
   * partition_object); */

  partition_block = udisks_object_get_block (partition_object);
  if (partition_block == NULL)
    {
      g_warning ("Created partition has no block interface");
      adw_dialog_close (ADW_DIALOG (self));
      g_clear_object (&partition_object);
      return;
    }

  /* Create a filesystem now on partition if not an extended partition */
  if (g_strcmp0 (gdu_format_volume_dialog_get_fs_type (self), "dos_extended") != 0)
    {
      self->add_partition = FALSE;
      g_object_unref (self->udisks_block);
      self->udisks_block = partition_block;
      g_object_unref (self->udisks_object);
      self->udisks_object = partition_object;
      on_forward_button_clicked_cb (self, NULL);
    }
}

static void
on_back_button_clicked_cb (GduFormatVolumeDialog *self, GtkWidget *widget)
{
  if (self->prev == NULL)
    {
      adw_dialog_close (ADW_DIALOG (self));
      return;
    }
  self->current = self->prev;
  update_dialog (NULL, NULL, self);
}

static void
gdu_format_volume_dialog_finalize (GObject *object)
{
  GduFormatVolumeDialog *self = (GduFormatVolumeDialog *)object;

  g_clear_object (&self->udisks_object);
  g_clear_object (&self->udisks_block);
  g_clear_object (&self->udisks_drive);

  G_OBJECT_CLASS (gdu_format_volume_dialog_parent_class)->finalize (object);
}

static void
gdu_format_volume_dialog_class_init (GduFormatVolumeDialogClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->finalize = gdu_format_volume_dialog_finalize;

  gtk_widget_class_set_template_from_resource (widget_class,
                                               "/org/gnome/DiskUtility/ui/"
                                               "gdu-format-volume-dialog.ui");

  gtk_widget_class_bind_template_child (widget_class, GduFormatVolumeDialog, pages_stack);
  gtk_widget_class_bind_template_child (widget_class, GduFormatVolumeDialog, back_button);
  gtk_widget_class_bind_template_child (widget_class, GduFormatVolumeDialog, forward_button);

  gtk_widget_class_bind_template_callback (widget_class, on_back_button_clicked_cb);
  gtk_widget_class_bind_template_callback (widget_class, on_forward_button_clicked_cb);
}

static void
gdu_format_volume_dialog_init (GduFormatVolumeDialog *self)
{
  gtk_widget_init_template (GTK_WIDGET (self));
}

void
gdu_create_format_show (UDisksClient  *client,
                        GtkWindow     *parent_window,
                        UDisksObject  *object,
                        gboolean       add_partition, /* format vs add partition and format */
                        guint64        add_partition_offset,
                        guint64        add_partition_maxsize)
{
  GduFormatVolumeDialog *self;

  g_return_if_fail (UDISKS_IS_CLIENT (client));
  g_return_if_fail (GTK_IS_WINDOW (parent_window));
  g_return_if_fail (UDISKS_IS_OBJECT (object));

  self = g_object_new (GDU_TYPE_FORMAT_VOLUME_DIALOG, NULL);

  self->udisks_client = client;
  self->udisks_object = g_object_ref (object);
  self->udisks_drive = udisks_object_get_drive (object);
  self->udisks_block = udisks_object_get_block (object);

  // in case the object passed was from a gdu drive get block from drive
  if (self->udisks_block == NULL && self->udisks_drive != NULL)
    self->udisks_block = udisks_client_get_block_for_drive (self->udisks_client,
                                                     self->udisks_drive,
                                                     FALSE);

  // in case the object passed was from gdu-block get drive from block
  self->udisks_drive = udisks_client_get_drive_for_block (client, self->udisks_block);

  // get dbus inteface for the block
  UDisksObject* block_object = (gpointer) g_dbus_interface_get_object ((gpointer) self->udisks_block);
  self->udisks_table = udisks_object_get_partition_table (block_object);
  g_assert (self->udisks_block != NULL);

  self->add_partition = add_partition;
  self->add_partition_offset = add_partition_offset;
  self->add_partition_maxsize = add_partition_maxsize;

  self->current = add_partition ? PARTITION_PAGE : FORMAT_PAGE;

  if (add_partition)
    {
      self->partition_page = gdu_create_partition_page_new (client,
                                                            self->udisks_table,
                                                            add_partition_maxsize,
                                                            add_partition_offset);
      gtk_stack_add_titled (GTK_STACK (self->pages_stack),
                            GTK_WIDGET (self->partition_page),
                            PARTITION_PAGE, _("Create Partition"));
      g_signal_connect (self->partition_page, "notify::complete", G_CALLBACK (update_dialog), self);
    }

  self->filesystem_page = gdu_create_filesystem_page_new (client, self->udisks_drive);
  gtk_stack_add_titled (GTK_STACK (self->pages_stack),
                        GTK_WIDGET (self->filesystem_page),
                        FORMAT_PAGE, _("Format Volume"));
  g_signal_connect (self->filesystem_page, "notify::complete", G_CALLBACK (update_dialog), self);

  self->other_page = gdu_create_other_page_new (client);
  gtk_stack_add_titled (GTK_STACK (self->pages_stack),
                        GTK_WIDGET (self->other_page),
                        OTHER_PAGE, _("Custom Format"));
  g_signal_connect (self->other_page, "notify::complete", G_CALLBACK (update_dialog), self);

  self->password_page = gdu_create_password_page_new ();
  gtk_stack_add_titled (GTK_STACK (self->pages_stack),
                        GTK_WIDGET (self->password_page),
                        PASSWORD_PAGE, _("Set Password"));
  g_signal_connect (self->password_page, "notify::complete", G_CALLBACK (update_dialog), self);

  self->confirm_page = gdu_create_confirm_page_new (client,
                                                    self->udisks_object,
                                                    self->udisks_block);
  gtk_stack_add_titled (GTK_STACK (self->pages_stack),
                        GTK_WIDGET (self->confirm_page),
                        CONFIRM_PAGE, _("Confirm Details"));

  update_dialog (NULL, NULL, self);

  adw_dialog_present (ADW_DIALOG (self), GTK_WIDGET (parent_window));
}
