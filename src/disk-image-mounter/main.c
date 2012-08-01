/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*-
 *
 * Copyright (C) 2012 Red Hat, Inc.
 *
 * Licensed under GPL version 2 or later.
 *
 * Author: David Zeuthen <zeuthen@gmail.com>
 */

#include "config.h"
#include <glib/gi18n.h>

#include <glib-unix.h>
#include <gio/gunixfdlist.h>

#include <gtk/gtk.h>

#include <libgdu/libgdu.h>

static gboolean have_gtk = FALSE;
static UDisksClient *udisks_client = NULL;
static GMainLoop *main_loop = NULL;

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
      gtk_message_dialog_format_secondary_markup (GTK_MESSAGE_DIALOG (dialog), "%s", s);
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

static const GOptionEntry opt_entries[] =
{
  { "writable", 'w', 0, G_OPTION_ARG_NONE, &opt_writable, N_("Allow writing to the image"), NULL},
  { NULL }
};

/* ---------------------------------------------------------------------------------------------------- */

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
  gdu_utils_configure_file_chooser_for_disk_images (GTK_FILE_CHOOSER (dialog), TRUE);
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
  gint n;
  GSList *uris = NULL;
  GSList *l;

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
      gchar *loop_object_path = NULL;
      GFile *file;

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

      /* Set up the disk image... */
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
          g_clear_error (&error);
          goto done_with_image;
        }

      /* Note that the desktop automounter is responsible for mounting,
       * unlocking etc. partitions etc. inside the image...
       */

    done_with_image:

      g_clear_object (&fd_list);
      g_free (filename);
      g_free (loop_object_path);

    } /* for each image */

  ret = 0;

 out:
  if (main_loop != NULL)
    g_main_loop_unref (main_loop);
  g_slist_free_full (uris, g_free);
  g_clear_object (&udisks_client);
  return ret;
}
