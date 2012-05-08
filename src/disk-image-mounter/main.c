/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*-
 *
 * Copyright (C) 2012 Red Hat, Inc.
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

#include <glib-unix.h>
#include <gio/gunixfdlist.h>

#include <gtk/gtk.h>

#include <udisks/udisks.h>

static gboolean have_gtk = FALSE;
static UDisksClient *udisks_client = NULL;
static GMainLoop *main_loop = NULL;

/* ---------------------------------------------------------------------------------------------------- */

static void
on_udisks_client_changed_check_loop_cleared (UDisksClient *client,
                                             gpointer      user_data)
{
  GList *loop_device_objpaths = user_data;
  GList *l;
  guint num_loops = 0;
  guint num_cleared = 0;

  for (l = loop_device_objpaths; l != NULL; l = l->next)
    {
      const gchar *loop_object_path = l->data;
      UDisksObject *object;
      UDisksBlock *block;

      num_loops++;
      num_cleared++; /* assume clear */

      object = udisks_client_peek_object (udisks_client, loop_object_path);
      if (object == NULL)
        continue;

      block = udisks_object_peek_block (object);
      if (block == NULL)
        continue;

      if (udisks_block_get_size (block) > 0)
        {
          /* nope, not clear */
          num_cleared--;
        }
    }

  if (num_cleared == num_loops)
    g_main_loop_quit (main_loop);
}

/* ---------------------------------------------------------------------------------------------------- */

static void
show_error (const gchar *format, ...)
{
  va_list var_args;
  gchar *s;

  va_start (var_args, format);

  s = g_strdup_vprintf (format, var_args);

  if (have_gtk)
    {
      GtkWidget *dialog;
      dialog = gtk_message_dialog_new_with_markup (NULL,
                                                   GTK_DIALOG_MODAL,
                                                   GTK_MESSAGE_ERROR,
                                                   GTK_BUTTONS_CLOSE,
                                                   "<big><b>%s</b></big>",
                                                   _("An error occurred"));
      gtk_message_dialog_format_secondary_markup (GTK_MESSAGE_DIALOG (dialog), s);
      gtk_window_set_title (GTK_WINDOW (dialog), _("Disk Image Mounter"));
      gtk_dialog_run (GTK_DIALOG (dialog));
      gtk_widget_destroy (dialog);
    }
  else
    {
      g_printerr ("%s\n", s);
    }

  g_free (s);
  va_end (var_args);
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean  opt_writable = FALSE;
static gboolean  opt_wait_until_clear = FALSE;

static const GOptionEntry opt_entries[] =
{
  { "writable", 'w', 0, G_OPTION_ARG_NONE, &opt_writable, N_("Allow writing to the image"), NULL},
  { "wait-until-clear", 0, 0, G_OPTION_ARG_NONE, &opt_wait_until_clear, N_("Wait until created loop devices are cleared"), NULL},
  { NULL }
};

/* ---------------------------------------------------------------------------------------------------- */

/* TODO: keep in sync with src/disks/gduutils.c (ideally in shared lib) */
static void
_gdu_utils_configure_file_chooser_for_disk_images (GtkFileChooser *file_chooser)
{
  GtkFileFilter *filter;
  const gchar *folder;

  /* Default to the "Documents" folder since that's where we save such images */
  folder = g_get_user_special_dir (G_USER_DIRECTORY_DOCUMENTS);
  if (folder != NULL)
    gtk_file_chooser_set_current_folder (file_chooser, folder);

  /* TODO: define proper mime-types */
  filter = gtk_file_filter_new ();
  gtk_file_filter_set_name (filter, _("All Files"));
  gtk_file_filter_add_pattern (filter, "*");
  gtk_file_chooser_add_filter (file_chooser, filter); /* adopts filter */
  filter = gtk_file_filter_new ();
  gtk_file_filter_set_name (filter, _("Disk Images (*.img, *.iso)"));
  gtk_file_filter_add_pattern (filter, "*.img");
  gtk_file_filter_add_pattern (filter, "*.iso");
  gtk_file_chooser_add_filter (file_chooser, filter); /* adopts filter */
  gtk_file_chooser_set_filter (file_chooser, filter);
}

static GSList *
do_filechooser (void)
{
  GSList *ret = NULL;
  GtkWidget *dialog;
  GtkWidget *ro_checkbutton;

  ret = NULL;

  dialog = gtk_file_chooser_dialog_new (_("Select Disk Image(s) to Mount"),
                                        NULL, /* parent window */
                                        GTK_FILE_CHOOSER_ACTION_OPEN,
                                        GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
                                        _("_Mount"), GTK_RESPONSE_ACCEPT,
                                        NULL);
  _gdu_utils_configure_file_chooser_for_disk_images (GTK_FILE_CHOOSER (dialog));
  gtk_file_chooser_set_local_only (GTK_FILE_CHOOSER (dialog), FALSE);

  /* Add a RO check button that defaults to RO */
  ro_checkbutton = gtk_check_button_new_with_mnemonic (_("Set up _read-only mount"));
  gtk_widget_set_tooltip_markup (ro_checkbutton, _("If checked, the mount will be read-only. This is useful if you don't want the underlying disk image to be modified"));
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (ro_checkbutton), !opt_writable);
  gtk_file_chooser_set_select_multiple (GTK_FILE_CHOOSER (dialog), TRUE);
  gtk_file_chooser_set_extra_widget (GTK_FILE_CHOOSER (dialog), ro_checkbutton);

  //gtk_widget_show_all (dialog);
  if (gtk_dialog_run (GTK_DIALOG (dialog)) != GTK_RESPONSE_ACCEPT)
    goto out;

  ret = gtk_file_chooser_get_uris (GTK_FILE_CHOOSER (dialog));
  opt_writable = ! gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (ro_checkbutton));

 out:
  gtk_widget_destroy (dialog);
  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

int
main (int argc, char *argv[])
{
  gint ret = 1;
  GError *error = NULL;
  gchar *s = NULL;
  GOptionContext *o = NULL;
  guint n;
  GSList *uris = NULL;
  GSList *l;
  GList *loop_device_objpaths = NULL;

  bindtextdomain (GETTEXT_PACKAGE, GNOMELOCALEDIR);
  bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
  textdomain (GETTEXT_PACKAGE);

  g_type_init ();
  have_gtk = gtk_init_check (&argc, &argv);

  main_loop = g_main_loop_new (NULL, FALSE);

  udisks_client = udisks_client_new_sync (NULL, &error);
  if (udisks_client == NULL)
    {
      g_printerr (_("Error connecting to udisks daemon: %s (%s, %d)"),
                  error->message, g_quark_to_string (error->domain), error->code);
      g_error_free (error);
      goto out;
    }

  o = g_option_context_new (NULL);
  g_option_context_set_help_enabled (o, FALSE);
  g_option_context_set_summary (o, _("Attach and mount one or more disk image files."));
  g_option_context_add_main_entries (o, opt_entries, GETTEXT_PACKAGE);

  if (!g_option_context_parse (o, &argc, &argv, NULL))
    {
      s = g_option_context_get_help (o, FALSE, NULL);
      g_printerr ("%s", s);
      g_free (s);
      goto out;
    }

  if (argc > 1)
    {
      for (n = 1; n < argc; n++)
        uris = g_slist_prepend (uris, g_strdup (argv[n]));
      uris = g_slist_reverse (uris);
    }
  else
    {
      if (!have_gtk)
        {
          show_error ("No files given and GTK+ not available");
          goto out;
        }
      else
        {
          uris = do_filechooser ();
        }
    }

  /* Files to attach are positional arguments */
  for (l = uris; l != NULL; l = l->next)
    {
      const gchar *uri;
      gchar *filename;
      GUnixFDList *fd_list = NULL;
      GVariantBuilder options_builder;
      gint fd;
      GError *error;
      gchar *loop_object_path = NULL;
      UDisksObject *object;
      UDisksFilesystem *filesystem;
      UDisksPartitionTable *partition_table;
      GFile *file;
      UDisksLoop *loop = NULL;
      guint num_mounts = 0;

      uri = l->data;
      file = g_file_new_for_commandline_arg (uri);
      filename = g_file_get_path (file);
      g_object_unref (file);

      if (filename == NULL)
        {
          show_error (_("Cannot open `%s' - maybe the volume isn't mounted?"), uri);
          goto done_with_image;
        }

      fd = open (filename, opt_writable ? O_RDWR : O_RDONLY);
      if (fd == -1)
        {
          show_error (_("Error opening `%s': %m"), filename);
          goto done_with_image;
        }

      g_variant_builder_init (&options_builder, G_VARIANT_TYPE_VARDICT);
      if (!opt_writable)
        g_variant_builder_add (&options_builder, "{sv}", "read-only", g_variant_new_boolean (TRUE));

      fd_list = g_unix_fd_list_new_from_array (&fd, 1); /* adopts the fd */

      /* first set up the disk image... */
      error = NULL;
      if (!udisks_manager_call_loop_setup_sync (udisks_client_get_manager (udisks_client),
                                                g_variant_new_handle (0),
                                                g_variant_builder_end (&options_builder),
                                                fd_list,
                                                &loop_object_path,
                                                NULL,              /* out_fd_list */
                                                NULL,              /* GCancellable */
                                                &error))
        {
          show_error (_("Error attaching disk image: %s (%s, %d)"),
                      error->message, g_quark_to_string (error->domain), error->code);
          g_error_free (error);
          goto done_with_image;
        }

      udisks_client_settle (udisks_client);

      /* ... and then mount whatever is inside it */
      object = udisks_client_peek_object (udisks_client, loop_object_path);
      g_assert (object != NULL);
      loop = udisks_object_peek_loop (object);
      g_assert (loop != NULL);
      filesystem = udisks_object_peek_filesystem (object);
      partition_table = udisks_object_peek_partition_table (object);
      if (partition_table != NULL)
        {
          GList *partitions, *l;
          partitions = udisks_client_get_partitions (udisks_client, partition_table);
          for (l = partitions; l != NULL; l = l->next)
            {
              UDisksPartition *partition = UDISKS_PARTITION (l->data);
              UDisksObject *partition_object;
              UDisksBlock *partition_block;
              UDisksFilesystem *partition_filesystem;

              partition_object = (UDisksObject *) g_dbus_interface_get_object (G_DBUS_INTERFACE (partition));
              if (partition_object == NULL)
                continue;

              partition_block = udisks_object_peek_block (partition_object);
              g_assert (partition_block != NULL);

              if (udisks_block_get_hint_ignore (partition_block))
                continue;

              partition_filesystem = udisks_object_peek_filesystem (partition_object);
              if (partition_filesystem == NULL)
                continue;

              error = NULL;
              if (!udisks_filesystem_call_mount_sync (partition_filesystem,
                                                      g_variant_new ("a{sv}", NULL), /* options */
                                                      NULL, /* out_mount_path */
                                                      NULL, /* cancellable */
                                                      &error))
                {
                  show_error (_("Error mounting filesystem on partition %d of disk image: %s (%s, %d)"),
                              udisks_partition_get_number (partition),
                              error->message, g_quark_to_string (error->domain), error->code);
                  g_error_free (error);
                  goto done_with_image;
                }
              num_mounts++;
            }
          g_list_free_full (partitions, g_object_unref);
        }
      else
        {
          /* not partitioned */
          if (filesystem == NULL)
            {
              show_error (_("The file `%s' does not appear to contain a mountable filesystem or partition table"), filename);
              goto done_with_image;
            }

          error = NULL;
          if (!udisks_filesystem_call_mount_sync (filesystem,
                                                  g_variant_new ("a{sv}", NULL), /* options */
                                                  NULL, /* out_mount_path */
                                                  NULL, /* cancellable */
                                                  &error))
            {
              show_error (_("Error mounting filesystem: %s (%s, %d)"),
                          error->message, g_quark_to_string (error->domain), error->code);
              g_error_free (error);
              goto done_with_image;
            }
          num_mounts++;
        }

    done_with_image:
      /* We arrive here both if it worked or if something failed.
       *
       * Now, if we did mount anything, set Autoclear to TRUE to
       * ensure that the loop device goes bye-bye when the last mount
       * is unmounted
       */
      if (num_mounts > 0)
        {
#ifdef UDISKS_CHECK_VERSION
# if UDISKS_CHECK_VERSION(1,97,0)
          error = NULL;
          if (!udisks_loop_call_set_autoclear_sync (loop,
                                                    TRUE,
                                                    g_variant_new ("a{sv}", NULL), /* options */
                                                    NULL, /* cancellable */
                                                    &error))
            {
              /* this is not fatal but can happen when using FUSE crap where uid 0 is
               * not permitted to view files on a FUSE mount
               */
              g_printerr (_("Non-fatal error: error setting autoclear to TRUE: %s (%s, %d)\n"),
                          error->message, g_quark_to_string (error->domain), error->code);
              g_error_free (error);
            }
# endif
#endif
          loop_device_objpaths = g_list_prepend (loop_device_objpaths, g_strdup (loop_object_path));
        }
      else if (loop != NULL)
        {
          /* otherwise, if we didn't mount anything, nuke the loop device */
          error = NULL;
          if (!udisks_loop_call_delete_sync (loop,
                                             g_variant_new ("a{sv}", NULL), /* options */
                                             NULL, /* cancellable */
                                             &error))
            {
              show_error (_("Error cleaning up loop device: %s (%s, %d)"),
                          error->message, g_quark_to_string (error->domain), error->code);
              g_error_free (error);
            }
        }

      g_clear_object (&fd_list);
      g_free (filename);
      g_free (loop_object_path);

    } /* for each image */

  if (opt_wait_until_clear)
    {
      g_signal_connect (udisks_client,
                        "changed",
                        G_CALLBACK (on_udisks_client_changed_check_loop_cleared),
                        loop_device_objpaths);
      g_main_loop_run (main_loop);
    }

  ret = 0;

 out:
  if (main_loop != NULL)
    g_main_loop_unref (main_loop);
  g_list_free_full (loop_device_objpaths, g_free);
  g_slist_free_full (uris, g_free);
  g_clear_object (&udisks_client);
  return ret;
}
