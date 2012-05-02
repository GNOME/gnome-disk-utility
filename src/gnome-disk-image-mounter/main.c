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

/* ---------------------------------------------------------------------------------------------------- */

static gboolean have_gtk = FALSE;
static UDisksClient *udisks_client = NULL;

/* ---------------------------------------------------------------------------------------------------- */

static void
usage (gint *argc, gchar **argv[], gboolean use_stdout)
{
  GOptionContext *o;
  gchar *s;
  gchar *program_name;

  o = g_option_context_new (_("COMMAND"));
  g_option_context_set_help_enabled (o, FALSE);
  /* Ignore parsing result */
  g_option_context_parse (o, argc, argv, NULL);
  program_name = g_path_get_basename ((*argv)[0]);
  s = g_strdup_printf (_("Commands:\n"
                         "  help         Shows this information\n"
                         "  attach       Attach and mount one or more disk image files\n"
                         "\n"
                         "Use \"%s COMMAND --help\" to get help on each command.\n"),
                       program_name);
  g_free (program_name);
  g_option_context_set_description (o, s);
  g_free (s);
  s = g_option_context_get_help (o, FALSE, NULL);
  if (use_stdout)
    g_print ("%s", s);
  else
    g_printerr ("%s", s);
  g_free (s);
  g_option_context_free (o);
}

/* ---------------------------------------------------------------------------------------------------- */

static void
remove_arg (gint num, gint *argc, gchar **argv[])
{
  gint n;

  g_assert (num <= (*argc));

  for (n = num; (*argv)[n] != NULL; n++)
    (*argv)[n] = (*argv)[n+1];
  (*argv)[n] = NULL;
  (*argc) = (*argc) - 1;
}

static void
modify_argv0_for_command (gint *argc, gchar **argv[], const gchar *command)
{
  gchar *s;
  gchar *program_name;

  /* TODO:
   *  1. get a g_set_prgname() ?; or
   *  2. save old argv[0] and restore later
   */

  g_assert (g_strcmp0 ((*argv)[1], command) == 0);
  remove_arg (1, argc, argv);

  program_name = g_path_get_basename ((*argv)[0]);
  s = g_strdup_printf ("%s %s", (*argv)[0], command);
  (*argv)[0] = s;
  g_free (program_name);
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

static gboolean opt_attach_writable = FALSE;

static const GOptionEntry attach_entries[] =
{
  { "writable", 'w', 0, G_OPTION_ARG_NONE, &opt_attach_writable, N_("Allow writing to the image"), NULL},
  { NULL }
};

static gint
handle_attach (gint *argc, gchar **argv[])
{
  guint n;
  gint ret = 1;
  gchar *s = NULL;
  GOptionContext *o = NULL;

  modify_argv0_for_command (argc, argv, "attach");

  o = g_option_context_new (NULL);
  g_option_context_set_help_enabled (o, FALSE);
  g_option_context_set_summary (o, _("Attach and mount one or more disk image files."));
  g_option_context_add_main_entries (o, attach_entries, GETTEXT_PACKAGE);

  if (!g_option_context_parse (o, argc, argv, NULL) || *argc <= 1)
    {
      s = g_option_context_get_help (o, FALSE, NULL);
      g_printerr ("%s", s);
      g_free (s);
      goto out;
    }

  /* Files to attach are positional arguments */
  for (n = 1; n < *argc; n++)
    {
      const gchar *filename = (*argv)[n];
      GUnixFDList *fd_list;
      GVariantBuilder options_builder;
      gint fd;
      GError *error;
      gchar *loop_object_path;
      UDisksObject *object;
      UDisksLoop *loop;
      UDisksFilesystem *filesystem;

      fd = open (filename, opt_attach_writable ? O_RDWR : O_RDONLY);
      if (fd == -1)
        {
          show_error (_("Error opening file `%s': %m"), filename);
          goto out;
        }

      g_variant_builder_init (&options_builder, G_VARIANT_TYPE_VARDICT);
      g_variant_builder_add (&options_builder, "{sv}", "no-part-scan", g_variant_new_boolean (TRUE));
      if (!opt_attach_writable)
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
          g_object_unref (fd_list);
          goto out;
        }
      g_object_unref (fd_list);

      udisks_client_settle (udisks_client);

      /* ... and then mount it */
      object = udisks_client_peek_object (udisks_client, loop_object_path);
      g_free (loop_object_path);
      g_assert (object != NULL);
      loop = udisks_object_peek_loop (object);
      g_assert (loop != NULL);
      filesystem = udisks_object_peek_filesystem (object);
      if (filesystem == NULL)
        {
          show_error (_("The file `%s' does not appear to contain a mountable filesystem"), filename);
          /* clean up */
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
          goto out;
        }

      g_variant_builder_init (&options_builder, G_VARIANT_TYPE_VARDICT);
      g_variant_builder_add (&options_builder, "{sv}", "loop.autoclear", g_variant_new_boolean (TRUE));
      error = NULL;
      if (!udisks_filesystem_call_mount_sync (filesystem,
                                              g_variant_builder_end (&options_builder),
                                              NULL, /* out_mount_path */
                                              NULL, /* cancellable */
                                              &error))
        {
          show_error (_("Error mounting filesystem: %s (%s, %d)"),
                      error->message, g_quark_to_string (error->domain), error->code);
          g_error_free (error);
          /* clean up */
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
          goto out;
        }
    }

  ret = 0;

 out:
  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

int
main (int argc, char *argv[])
{
  gint ret = 1;
  const gchar *command;
  GError *error = NULL;

  bindtextdomain (GETTEXT_PACKAGE, GNOMELOCALEDIR);
  bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
  textdomain (GETTEXT_PACKAGE);

  g_type_init ();
  have_gtk = gtk_init_check (&argc, &argv);

  udisks_client = udisks_client_new_sync (NULL, &error);
  if (udisks_client == NULL)
    {
      g_printerr (_("Error connecting to udisks daemon: %s (%s, %d)"),
                  error->message, g_quark_to_string (error->domain), error->code);
      g_error_free (error);
      goto out;
    }

  command = argv[1];
  if (g_strcmp0 (command, "help") == 0)
    {
      usage (&argc, &argv, TRUE);
      ret = 0;
    }
  else if (g_strcmp0 (command, "attach") == 0)
    {
      ret = handle_attach (&argc, &argv);
      goto out;
    }
  else
    {
      if (command != NULL)
        g_printerr (_("Unknown command `%s'\n\n"), command);
      usage (&argc, &argv, FALSE);
      goto out;
    }

 out:
  g_clear_object (&udisks_client);
  return ret;
}
