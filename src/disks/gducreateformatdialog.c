/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*-
 *
 * Copyright (C) 2008-2013 Red Hat, Inc.
 * Copyright (C) 2017 Kai Lüke
 *
 * Licensed under GPL version 2 or later.
 *
 * Author: David Zeuthen <zeuthen@gmail.com>, Kai Lüke <kailueke@riseup.net>
 */

#include "config.h"

#include <glib/gi18n.h>

#include "gduapplication.h"
#include "gduwindow.h"
#include "gducreateformatdialog.h"
#include "gducreatepartitionpage.h"
#include "gducreatefilesystempage.h"
#include "gducreateotherpage.h"
#include "gducreatepasswordpage.h"
#include "gducreateconfirmpage.h"
#include "gduvolumegrid.h"

#define PARTITION_PAGE "partition"
#define FORMAT_PAGE "format"
#define OTHER_PAGE "other"
#define PASSWORD_PAGE "password"
#define CONFIRM_PAGE "confirm"

typedef struct
{
  GtkWindow *parent_window;
  UDisksObject *object;
  UDisksBlock *block;
  UDisksDrive *drive;
  UDisksPartitionTable *table;
  UDisksClient *client;

  GtkBuilder *builder;
  GtkDialog *dialog;
  GtkStack *stack;
  GtkButton *back;
  GtkButton *forward;
  const gchar *current; /* page names */
  const gchar *prev;
  const gchar *next;
  gboolean add_partition; /* mode: format vs add partition and format */
  guint64 add_partition_offset;
  guint64 add_partition_maxsize;

  GduCreatePartitionPage *partition_page;    /* create partition page */
  GduCreateFilesystemPage *filesystem_page;  /* main format page */
  GduCreateOtherPage *other_page;            /* custom filesystem page */
  GduCreatePasswordPage *password_page;      /* set password page */
  GduCreateConfirmPage *confirm_page;        /* confirm format page */

  GCallback finished_cb;
  gpointer  cb_data;
} CreateFormatData;

static void
create_format_data_free (CreateFormatData *data)
{
  if (data->finished_cb)
    ((GDestroyNotify) data->finished_cb) (data->cb_data);
  g_clear_object (&data->parent_window);
  g_object_unref (data->object);
  g_object_unref (data->block);
  g_clear_object (&data->drive);
  if (data->dialog != NULL)
    {
      gtk_widget_hide (GTK_WIDGET (data->dialog));
      gtk_widget_destroy (GTK_WIDGET (data->dialog));
    }
  if (data->builder != NULL)
    g_object_unref (data->builder);
  g_free (data);
}

static const gchar *
get_filesystem (CreateFormatData *data)
{
  if (data->add_partition && gdu_create_partition_page_is_extended (data->partition_page))
    return "dos_extended";
  else if (gdu_create_filesystem_page_get_fs (data->filesystem_page) != NULL)
    return gdu_create_filesystem_page_get_fs (data->filesystem_page);
  else
    return gdu_create_other_page_get_fs (data->other_page);
}

static gboolean
get_encrypt (CreateFormatData *data)
{
  return gdu_create_filesystem_page_is_encrypted (data->filesystem_page) ||
         (gdu_create_filesystem_page_is_other (data->filesystem_page) &&
          gdu_create_other_page_is_encrypted (data->other_page));
}

static void
update_dialog (GtkWidget *widget, GParamSpec *child_property, CreateFormatData *data)
{
  GValue title = G_VALUE_INIT;
  gboolean complete = FALSE;
  GtkWidget *child;
  gpointer page = NULL;

  g_value_init (&title, G_TYPE_STRING);
  child = gtk_stack_get_child_by_name (data->stack, data->current);
  gtk_container_child_get_property (GTK_CONTAINER (data->stack), child, "title", &title);

  gtk_window_set_title (GTK_WINDOW (data->dialog), g_value_get_string (&title));
  data->prev = NULL;
  data->next = CONFIRM_PAGE;

  if (data->add_partition)
    data->next = NULL;

  if (g_strcmp0 (data->current, PARTITION_PAGE) == 0)
    {
      page = data->partition_page;
      data->next = FORMAT_PAGE;
      if (gdu_create_partition_page_is_extended (data->partition_page))
        data->next = NULL;
    }
  else if (g_strcmp0 (data->current, FORMAT_PAGE) == 0)
    {
      page = data->filesystem_page;
      if (data->add_partition)
        data->prev = PARTITION_PAGE;

      if (gdu_create_filesystem_page_is_other (data->filesystem_page))
        data->next = OTHER_PAGE;

      if (gdu_create_filesystem_page_is_encrypted (data->filesystem_page))
        data->next = PASSWORD_PAGE;
    }
  else if (g_strcmp0 (data->current, OTHER_PAGE) == 0)
    {
      page = data->other_page;
      data->prev = FORMAT_PAGE;

      if (gdu_create_other_page_is_encrypted (data->other_page))
        data->next = PASSWORD_PAGE;
    }
  else if (g_strcmp0 (data->current, PASSWORD_PAGE) == 0)
    {
      page = data->password_page;
      if (gdu_create_filesystem_page_is_encrypted (data->filesystem_page))
        data->prev = FORMAT_PAGE;
      else if (gdu_create_filesystem_page_is_other (data->filesystem_page) &&
               gdu_create_other_page_is_encrypted (data->other_page))
        data->prev = OTHER_PAGE;
    }
  else if (g_strcmp0 (data->current, CONFIRM_PAGE) == 0)
    {
      page = data->confirm_page;
      data->next = NULL;

      if (gdu_create_filesystem_page_is_encrypted (data->filesystem_page) ||
          (gdu_create_filesystem_page_is_other (data->filesystem_page) &&
           gdu_create_other_page_is_encrypted (data->other_page)))
        data->prev = PASSWORD_PAGE;
      else if (gdu_create_filesystem_page_is_other (data->filesystem_page))
        data->prev = OTHER_PAGE;
      else
        data->prev = FORMAT_PAGE;

      gdu_create_confirm_page_fill_confirmation (data->confirm_page);
    }

  if (data->prev == NULL)
    gtk_button_set_label (data->back, _("_Cancel"));
  else
    gtk_button_set_label (data->back, _("_Previous"));

  if (data->next == NULL)
    {
      if (data->add_partition)
        {
          gtk_button_set_label (data->forward, _("Cre_ate"));
          gtk_style_context_add_class (gtk_widget_get_style_context (GTK_WIDGET (data->forward)),
                                       "suggested-action");
        }
      else
        {
          gtk_button_set_label (data->forward, _("Form_at"));
          gtk_style_context_add_class (gtk_widget_get_style_context (GTK_WIDGET (data->forward)),
                                       "destructive-action");
        }
    }
  else
    {
      gtk_button_set_label (data->forward, _("N_ext"));
      gtk_style_context_remove_class (gtk_widget_get_style_context (GTK_WIDGET (data->forward)),
                                      "suggested-action");
      gtk_style_context_remove_class (gtk_widget_get_style_context (GTK_WIDGET (data->forward)),
                                      "destructive-action");
    }

  g_object_get (page, "complete", &complete, NULL);
  gtk_widget_set_sensitive (GTK_WIDGET (data->forward), complete);
  gtk_stack_set_visible_child (data->stack, child);
}

static void
cancel_cb (GtkDialog *dialog, CreateFormatData *data)
{
  create_format_data_free (data);
}

static void
format_cb (GObject *source_object, GAsyncResult *res, gpointer user_data)
{
  CreateFormatData *data = user_data;
  GError *error;

  error = NULL;
  if (!udisks_block_call_format_finish (UDISKS_BLOCK (source_object), res, &error))
    {
      gdu_utils_show_error (GTK_WINDOW (data->parent_window), _("Error formatting volume"), error);
      g_error_free (error);
    }
  create_format_data_free (data);
}

static void
ensure_unused_cb (UDisksClient *client, GAsyncResult *res, CreateFormatData *data)
{
  GVariantBuilder options_builder;
  const gchar *fs_type;
  const gchar *erase_type;

  if (!gdu_utils_ensure_unused_finish (client, res, NULL))
    {
      create_format_data_free (data);
      return;
    }

  fs_type = get_filesystem (data);
  erase_type = gdu_create_filesystem_page_get_erase (data->filesystem_page);

  g_variant_builder_init (&options_builder, G_VARIANT_TYPE_VARDICT);
  if (g_strcmp0 (fs_type, "empty") != 0)
    g_variant_builder_add (&options_builder, "{sv}", "label",
                           g_variant_new_string (gdu_create_filesystem_page_get_name (data->filesystem_page)));

  if (g_strcmp0 (fs_type, "vfat") != 0 && g_strcmp0 (fs_type, "ntfs") != 0 && g_strcmp0 (fs_type, "exfat") != 0)
    {
      g_variant_builder_add (&options_builder, "{sv}", "take-ownership", g_variant_new_boolean (TRUE));
    }

  if (get_encrypt (data))
    g_variant_builder_add (&options_builder, "{sv}", "encrypt.passphrase",
                           g_variant_new_string (gdu_create_password_page_get_password (data->password_page)));

  if (erase_type != NULL)
    g_variant_builder_add (&options_builder, "{sv}", "erase", g_variant_new_string (erase_type));

  g_variant_builder_add (&options_builder, "{sv}", "update-partition-type", g_variant_new_boolean (TRUE));

  udisks_block_call_format (data->block,
                            fs_type,
                            g_variant_builder_end (&options_builder),
                            NULL, /* GCancellable */
                            format_cb,
                            data);
}

void
finish_cb (GtkDialog *assistant, gint response_id, CreateFormatData *data);

static void
create_partition_cb (GObject *source_object, GAsyncResult *res, gpointer user_data)
{
  CreateFormatData *data = user_data;
  GError *error;
  gchar *created_partition_object_path = NULL;
  UDisksObject *partition_object = NULL;
  UDisksBlock *partition_block;

  error = NULL;
  if (!udisks_partition_table_call_create_partition_finish (UDISKS_PARTITION_TABLE (source_object),
                                                            &created_partition_object_path,
                                                            res,
                                                            &error))
    {
      gdu_utils_show_error (GTK_WINDOW (data->parent_window), _("Error creating partition"), error);
      g_error_free (error);
      create_format_data_free (data);
      return;
    }

  udisks_client_settle (data->client);

  partition_object = udisks_client_get_object (data->client, created_partition_object_path);
  g_free (created_partition_object_path);
  gdu_window_select_object (GDU_WINDOW (data->parent_window), partition_object);

  partition_block = udisks_object_get_block (partition_object);
  if (partition_block == NULL)
    {
      g_warning ("Created partition has no block interface");
      create_format_data_free (data);
      g_clear_object (&partition_object);
      return;
    }

  /* Create a filesystem now on partition if not an extended partition */
  if (g_strcmp0 (get_filesystem (data), "dos_extended") != 0)
    {
      data->add_partition = FALSE;
      g_object_unref (data->block);
      data->block = partition_block;
      g_object_unref (data->object);
      data->object = partition_object;
      finish_cb (data->dialog, GTK_RESPONSE_APPLY, data);
    }
}

void
finish_cb (GtkDialog *dialog, gint response_id, CreateFormatData *data) /* the assistant is done */
{
  guint64 size;
  const gchar *partition_type = "";
  GVariantBuilder options_builder;

  if (response_id != GTK_RESPONSE_APPLY)
    {
      /* step back or cancel */
      if (data->prev != NULL)
        {
          data->current = data->prev;
          update_dialog (NULL, NULL, data);
        }
      else
        cancel_cb (dialog, data);
      return;
    }

  /* step to next page */
  if (data->next != NULL)
    {
      data->current = data->next;
      update_dialog (NULL, NULL, data);
      return;
    }

  if (data->add_partition)
    {
      g_variant_builder_init (&options_builder, G_VARIANT_TYPE_VARDICT);

      if (g_strcmp0 (get_filesystem (data), "dos_extended") == 0)
        {
          partition_type = "0x05";
          g_variant_builder_add (&options_builder, "{sv}", "partition-type", g_variant_new_string ("extended"));
        }
      else if (g_strcmp0 (udisks_partition_table_get_type_ (data->table), "dos") == 0)
        {
          if (gdu_utils_is_inside_dos_extended (data->client, data->table, data->add_partition_offset))
            {
              g_variant_builder_add (&options_builder, "{sv}", "partition-type", g_variant_new_string ("logical"));
            }
          else
            {
              g_variant_builder_add (&options_builder, "{sv}", "partition-type", g_variant_new_string ("primary"));
            }
        }

      size = gdu_create_partition_page_get_size (data->partition_page);
      udisks_partition_table_call_create_partition (data->table,
                                                    data->add_partition_offset,
                                                    size,
                                                    partition_type, /* use default type */
                                                    "", /* use blank partition name */
                                                    g_variant_builder_end (&options_builder),
                                                    NULL, /* GCancellable */
                                                    create_partition_cb,
                                                    data);
    }
  else
    {
      /* ensure the volume is unused (e.g. unmounted) before formatting it... */
      gdu_utils_ensure_unused (data->client,
                               GTK_WINDOW (data->parent_window),
                               data->object,
                               (GAsyncReadyCallback) ensure_unused_cb,
                               NULL, /* GCancellable */
                               data);
    }

  gtk_widget_hide (GTK_WIDGET (data->dialog));
}

void
gdu_create_format_show (UDisksClient *client,
                        GtkWindow    *parent_window,
                        UDisksObject *object,
                        gboolean      show_custom,  /* e.g. hide custom format page from nautilus */
                        gboolean      add_partition, /* format vs add partition and format */
                        guint64       add_partition_offset,
                        guint64       add_partition_maxsize,
                        GCallback     finished_cb,
                        gpointer      cb_data)
{
  GduApplication *app;
  CreateFormatData *data;

  app = GDU_APPLICATION (g_application_get_default ());
  data = g_new0 (CreateFormatData, 1);
  data->finished_cb = finished_cb;
  data->cb_data = cb_data;
  data->client = client;
  data->parent_window = (parent_window != NULL) ? g_object_ref (parent_window) : NULL;
  data->object = g_object_ref (object);
  data->block = udisks_object_get_block (object);
  g_assert (data->block != NULL);
  data->drive = udisks_client_get_drive_for_block (client, data->block);
  data->table = udisks_object_get_partition_table (object);

  data->dialog = GTK_DIALOG (gdu_application_new_widget (app,
                                                         "create-format.ui",
                                                         "create-format",
                                                         &data->builder));
  data->stack = GTK_STACK (gtk_builder_get_object (data->builder, "pages-stack"));

  data->add_partition = add_partition;
  data->add_partition_offset = add_partition_offset;
  data->add_partition_maxsize = add_partition_maxsize;

  if (data->add_partition)
    {
      data->partition_page = gdu_create_partition_page_new (data->client, data->table,
                                                            add_partition_maxsize, add_partition_offset);
      gtk_stack_add_titled (data->stack, GTK_WIDGET (data->partition_page), PARTITION_PAGE, _("Create Partition"));
      g_signal_connect (data->partition_page, "notify::complete", G_CALLBACK (update_dialog), data);
    }
  else
    {
      data->partition_page = NULL;
    }

  data->filesystem_page = gdu_create_filesystem_page_new (data->client, show_custom, data->drive);
  gtk_stack_add_titled (data->stack, GTK_WIDGET (data->filesystem_page), FORMAT_PAGE, _("Format Volume"));
  g_signal_connect (data->filesystem_page, "notify::complete", G_CALLBACK (update_dialog), data);
  data->other_page = gdu_create_other_page_new (data->client);
  gtk_stack_add_titled (data->stack, GTK_WIDGET (data->other_page), OTHER_PAGE, _("Custom Format"));
  g_signal_connect (data->other_page, "notify::complete", G_CALLBACK (update_dialog), data);
  data->password_page = gdu_create_password_page_new ();
  gtk_stack_add_titled (data->stack, GTK_WIDGET (data->password_page), PASSWORD_PAGE, _("Set Password"));
  g_signal_connect (data->password_page, "notify::complete", G_CALLBACK (update_dialog), data);
  data->confirm_page = gdu_create_confirm_page_new (data->client, data->object, data->block);
  gtk_stack_add_titled (data->stack, GTK_WIDGET (data->confirm_page), CONFIRM_PAGE, _("Confirm Details"));

  data->back = GTK_BUTTON (gtk_dialog_add_button (data->dialog, _("_Cancel"), GTK_RESPONSE_CANCEL));
  data->forward = GTK_BUTTON (gtk_dialog_add_button (data->dialog, _("_Format"), GTK_RESPONSE_APPLY));
  gtk_widget_grab_default (GTK_WIDGET (data->forward));

  g_signal_connect (data->dialog, "close", G_CALLBACK (cancel_cb), data);
  g_signal_connect (data->dialog, "response", G_CALLBACK (finish_cb), data);

  if (add_partition)
    data->current = PARTITION_PAGE;
  else
    data->current = FORMAT_PAGE;

  if (parent_window != NULL)
    {
      gtk_window_set_transient_for (GTK_WINDOW (data->dialog), parent_window);
    }

  update_dialog (NULL, NULL, data);
  gtk_widget_show (GTK_WIDGET (data->dialog));
}
