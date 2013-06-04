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

#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include "gduapplication.h"
#include "gduformatvolumedialog.h"
#include "gdurestorediskimagedialog.h"
#include "gduwindow.h"
#include "gdulocaljob.h"

struct _GduApplication
{
  GtkApplication parent_instance;

  gboolean running_from_source_tree;

  UDisksClient *client;
  GduWindow *window;

  /* Maps from UDisksObject* -> GList<GduLocalJob*> */
  GHashTable *local_jobs;
};

typedef struct
{
  GtkApplicationClass parent_class;
} GduApplicationClass;

G_DEFINE_TYPE (GduApplication, gdu_application, GTK_TYPE_APPLICATION);

static void
gdu_application_init (GduApplication *app)
{
  app->local_jobs = g_hash_table_new (g_direct_hash, g_direct_equal);
}

static void
gdu_application_finalize (GObject *object)
{
  GduApplication *app = GDU_APPLICATION (object);

  if (app->local_jobs != NULL)
    {
      GHashTableIter iter;
      GList *local_jobs, *jobs_to_destroy = NULL, *l;

      g_hash_table_iter_init (&iter, app->local_jobs);
      while (g_hash_table_iter_next (&iter, NULL /* object*/, (gpointer) &local_jobs))
        jobs_to_destroy = g_list_concat (jobs_to_destroy, g_list_copy (local_jobs));
      for (l = jobs_to_destroy; l != NULL; l = l->next)
        gdu_application_destroy_local_job (app, GDU_LOCAL_JOB (l->data));
      g_list_free (jobs_to_destroy);
      g_hash_table_destroy (app->local_jobs);
    }

  if (app->client != NULL)
    g_object_unref (app->client);

  G_OBJECT_CLASS (gdu_application_parent_class)->finalize (object);
}

/* ---------------------------------------------------------------------------------------------------- */

/* called in local instance */
static gboolean
gdu_application_local_command_line (GApplication    *_app,
                                    gchar         ***arguments,
                                    int             *exit_status)
{
  GduApplication *app = GDU_APPLICATION (_app);

  /* figure out if running from source tree */
  if (g_strcmp0 ((*arguments)[0], "./gnome-disks") == 0)
    app->running_from_source_tree = TRUE;

  /* chain up */
  return G_APPLICATION_CLASS (gdu_application_parent_class)->local_command_line (_app,
                                                                                 arguments,
                                                                                 exit_status);
}

/* ---------------------------------------------------------------------------------------------------- */

static void
gdu_application_ensure_client (GduApplication *app)
{
  GError *error;

  if (app->client != NULL)
    goto out;

  error = NULL;
  app->client = udisks_client_new_sync (NULL, /* GCancellable* */
                                        &error);
  if (app->client == NULL)
    {
      g_error ("Error getting udisks client: %s", error->message);
      g_error_free (error);
    }
 out:
  ;
}

static UDisksObject *
gdu_application_object_from_block_device (GduApplication *app,
                                          const gchar *block_device,
                                          gchar **error_message)
{
  struct stat statbuf;
  const gchar *crypto_backing_device;
  UDisksObject *object, *crypto_backing_object;
  UDisksBlock *block;

  object = NULL;

  if (stat (block_device, &statbuf) != 0)
    {
      *error_message = g_strdup_printf (_("Error opening %s: %s"), block_device, g_strerror (errno));
      goto out;
    }

  block = udisks_client_get_block_for_dev (app->client, statbuf.st_rdev);
  if (block == NULL)
    {
      *error_message = g_strdup_printf (_("Error looking up block device for %s"), block_device);
      goto out;
    }

  object = UDISKS_OBJECT (g_dbus_interface_dup_object (G_DBUS_INTERFACE (block)));
  g_object_unref (block);

  crypto_backing_device = udisks_block_get_crypto_backing_device ((udisks_object_peek_block (object)));
  crypto_backing_object = udisks_client_get_object (app->client, crypto_backing_device);
  if (crypto_backing_object != NULL)
    {
      g_object_unref (object);
      object = crypto_backing_object;
    }

 out:
  return object;
}

/* ---------------------------------------------------------------------------------------------------- */

/* called in primary instance */
static gint
gdu_application_command_line (GApplication            *_app,
                              GApplicationCommandLine *command_line)
{
  GduApplication *app = GDU_APPLICATION (_app);
  UDisksObject *object_to_select = NULL;
  GOptionContext *context;
  gchar **argv = NULL;
  GError *error = NULL;
  gint argc;
  gint ret = 1;
  gchar *s;
  gchar *opt_block_device = NULL, *error_message = NULL;
  gboolean opt_help = FALSE;
  gboolean opt_format = FALSE;
  gchar *opt_restore_disk_image = NULL;
  gint opt_xid = -1;
  GOptionEntry opt_entries[] =
  {
    {"block-device", 0, 0, G_OPTION_ARG_STRING, &opt_block_device, N_("Select device"), NULL },
    {"format-device", 0, 0, G_OPTION_ARG_NONE, &opt_format, N_("Format selected device"), NULL },
    {"xid", 0, 0, G_OPTION_ARG_INT, &opt_xid, N_("Parent window XID for the format dialog"), NULL },
    {"restore-disk-image", 0, 0, G_OPTION_ARG_FILENAME, &opt_restore_disk_image, N_("Restore disk image"), NULL },
    {"help", '?', G_OPTION_FLAG_HIDDEN, G_OPTION_ARG_NONE, &opt_help, N_("Show help options"), NULL },
    {NULL}
  };

  argv = g_application_command_line_get_arguments (command_line, &argc);

  context = g_option_context_new (NULL);
  /* This is to avoid the primary instance calling exit() when encountering the "--help" option */
  g_option_context_set_help_enabled (context, FALSE);
  g_option_context_add_main_entries (context, opt_entries, GETTEXT_PACKAGE);

  if (!g_option_context_parse (context, &argc, &argv, &error))
    {
      g_application_command_line_printerr (command_line, "%s\n", error->message);
      g_clear_error (&error);
      goto out;
    }

  if (opt_help)
    {
      s = g_option_context_get_help (context, FALSE, NULL);
      g_application_command_line_print (command_line, "%s",  s);
      g_free (s);
      ret = 0;
      goto out;
    }

  if (opt_format && opt_block_device == NULL)
    {
      g_application_command_line_printerr (command_line, _("--format-device must be used together with --block-device\n"));
      goto out;
    }

  if (opt_xid != -1 && !opt_format)
    {
      g_application_command_line_printerr (command_line, _("--format-device must be specified when using --xid\n"));
      goto out;
    }

  gdu_application_ensure_client (app);

  if (opt_block_device != NULL)
    {
      object_to_select = gdu_application_object_from_block_device (app, opt_block_device, &error_message);
      if (object_to_select == NULL)
        {
          g_application_command_line_printerr (command_line, "%s\n", error_message);
          g_free (error_message);
          goto out;
        }
    }

  if (opt_restore_disk_image != NULL)
    {
      if (!g_file_test (opt_restore_disk_image, G_FILE_TEST_IS_REGULAR))
        {
          g_application_command_line_printerr (command_line,
                                               "%s does not appear to be a regular file\n",
                                               opt_restore_disk_image);
          goto out;
        }
    }

  if (opt_xid == -1)
    {
      if (app->window == NULL)
        {
          g_application_activate (G_APPLICATION (app));
        }
      else
        {
          /* TODO: startup notification stuff */
          gtk_window_present (GTK_WINDOW (app->window));
        }

      if (object_to_select != NULL)
        {
          gdu_window_select_object (app->window, object_to_select);
          if (opt_format)
            gdu_format_volume_dialog_show (app->window, object_to_select);
        }
    }
  else if (opt_format)
    {
      gdu_format_volume_dialog_show_for_xid (app->client, opt_xid, object_to_select);
    }

  if (opt_restore_disk_image != NULL)
    {
      gdu_restore_disk_image_dialog_show (app->window, NULL, opt_restore_disk_image);
    }

  ret = 0;

 out:
  g_option_context_free (context);
  g_clear_object (&object_to_select);
  g_free (opt_block_device);
  g_free (opt_restore_disk_image);
  g_strfreev (argv);
  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

static void
gdu_application_activate (GApplication *_app)
{
  GduApplication *app = GDU_APPLICATION (_app);

  gdu_application_ensure_client (app);

  app->window = gdu_window_new (app, app->client);
  gtk_application_add_window (GTK_APPLICATION (app),
                              GTK_WINDOW (app->window));
  gtk_widget_show (GTK_WIDGET (app->window));
}

/* ---------------------------------------------------------------------------------------------------- */

static void
attach_disk_image_activated (GSimpleAction *action,
                             GVariant      *parameter,
                             gpointer       user_data)
{
  GduApplication *app = GDU_APPLICATION (user_data);
  gdu_window_show_attach_disk_image (app->window);
}

static void
about_activated (GSimpleAction *action,
                 GVariant      *parameter,
                 gpointer       user_data)
{
  GduApplication *app = GDU_APPLICATION (user_data);
  GtkWidget *dialog;
  gchar *s;

  dialog = GTK_WIDGET (gdu_application_new_widget (app,
                                                   "about-dialog.ui",
                                                   "about-dialog",
                                                   NULL));
  /* Translators: Shown in the About dialog to convey version numbers.
   *              The first %s is the version of Disks (for example "3.6").
   *              The second %s is the version of the running udisks daemon (for example "2.0.90").
   *              The third, fourth and fifth %d are the major, minor and micro versions of libudisks2 that was used when compiling the Disks application (for example 2, 0 and 90).
   */
  s = g_strdup_printf (_("gnome-disk-utility %s\nUDisks %s (built against %d.%d.%d)"),
                       PACKAGE_VERSION,
                       udisks_manager_get_version (udisks_client_get_manager (app->client)),
                       UDISKS_MAJOR_VERSION, UDISKS_MINOR_VERSION, UDISKS_MICRO_VERSION);
  gtk_about_dialog_set_version (GTK_ABOUT_DIALOG (dialog), s);
  g_free (s);

  gtk_window_set_transient_for (GTK_WINDOW (dialog), GTK_WINDOW (app->window));
  gtk_dialog_set_default_response (GTK_DIALOG (dialog), GTK_RESPONSE_OK);
  gtk_widget_show_all (dialog);
  g_object_ref (dialog);
  gtk_dialog_run (GTK_DIALOG (dialog));
  gtk_widget_hide (dialog);
  gtk_widget_destroy (dialog);
  g_object_unref (dialog);
}

static void
quit_activated (GSimpleAction *action,
                GVariant      *parameter,
                gpointer       user_data)
{
  GduApplication *app = GDU_APPLICATION (user_data);
  gtk_widget_destroy (GTK_WIDGET (app->window));
}

static void
help_activated (GSimpleAction *action,
                GVariant      *parameter,
                gpointer       user_data)
{
  //GduApplication *app = GDU_APPLICATION (user_data);
  //gtk_widget_destroy (GTK_WIDGET (app->window));
  gtk_show_uri (NULL, /* GdkScreen */
                "help:gnome-help/disk",
                GDK_CURRENT_TIME,
                NULL); /* GError */
}

static GActionEntry app_entries[] =
{
  { "attach_disk_image", attach_disk_image_activated, NULL, NULL, NULL },
  { "about", about_activated, NULL, NULL, NULL },
  { "help", help_activated, NULL, NULL, NULL },
  { "quit", quit_activated, NULL, NULL, NULL }
};

static void
gdu_application_startup (GApplication *_app)
{
  GduApplication *app = GDU_APPLICATION (_app);
  GMenuModel *app_menu;
  GtkBuilder *builder;

  if (G_APPLICATION_CLASS (gdu_application_parent_class)->startup != NULL)
    G_APPLICATION_CLASS (gdu_application_parent_class)->startup (_app);

  g_action_map_add_action_entries (G_ACTION_MAP (app), app_entries, G_N_ELEMENTS (app_entries), app);

  app_menu = G_MENU_MODEL (gdu_application_new_widget (app,
                                                       "app-menu.ui",
                                                       "app-menu",
                                                       &builder));
  gtk_application_set_app_menu (GTK_APPLICATION (app), app_menu);
  g_object_unref (app_menu);
  g_clear_object (&builder);
}

/* ---------------------------------------------------------------------------------------------------- */

static void
gdu_application_class_init (GduApplicationClass *klass)
{
  GObjectClass *gobject_class;
  GApplicationClass *application_class;

  gobject_class = G_OBJECT_CLASS (klass);
  gobject_class->finalize = gdu_application_finalize;

  application_class = G_APPLICATION_CLASS (klass);
  application_class->local_command_line = gdu_application_local_command_line;
  application_class->command_line = gdu_application_command_line;
  application_class->activate           = gdu_application_activate;
  application_class->startup            = gdu_application_startup;
}

GApplication *
gdu_application_new (void)
{
  gtk_init (NULL, NULL);
  return G_APPLICATION (g_object_new (GDU_TYPE_APPLICATION,
                                      "application-id", "org.gnome.DiskUtility",
                                      "flags", G_APPLICATION_HANDLES_COMMAND_LINE,
                                      NULL));
}

UDisksClient *
gdu_application_get_client (GduApplication  *application)
{
  return application->client;
}


GObject *
gdu_application_new_widget (GduApplication  *application,
                            const gchar     *ui_file,
                            const gchar     *name,
                            GtkBuilder     **out_builder)
{
  GObject *ret = NULL;
  GtkBuilder *builder = NULL;
  gchar *path = NULL;
  GError *error;

  g_return_val_if_fail (GDU_IS_APPLICATION (application), NULL);
  g_return_val_if_fail (ui_file != NULL, NULL);

  builder = gtk_builder_new ();

  path = g_strdup_printf ("%s/%s",
                          application->running_from_source_tree ?
                            "../../data/ui" :
                            PACKAGE_DATA_DIR "/gnome-disk-utility",
                          ui_file);

  error = NULL;
  if (gtk_builder_add_from_file (builder, path, &error) == 0)
    {
      g_error ("Error loading UI file %s: %s", path, error->message);
      g_error_free (error);
      goto out;
    }

  if (name != NULL)
    ret = G_OBJECT (gtk_builder_get_object (builder, name));

 out:
  if (out_builder != NULL)
    {
      *out_builder = builder;
      builder = NULL;
    }
  if (builder != NULL)
    {
      g_object_unref (builder);
    }
  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

static void
on_local_job_notify (GObject    *object,
                     GParamSpec *pspec,
                     gpointer    user_data)
{
  GduApplication *app = GDU_APPLICATION (user_data);
  udisks_client_queue_changed (app->client);
}


GduLocalJob *
gdu_application_create_local_job  (GduApplication *application,
                                   UDisksObject   *object)
{
  GduLocalJob *job = NULL;
  GList *local_jobs;

  g_return_val_if_fail (GDU_IS_APPLICATION (application), NULL);
  g_return_val_if_fail (UDISKS_IS_OBJECT (object), NULL);

  job = gdu_local_job_new (object);

  local_jobs = g_hash_table_lookup (application->local_jobs, object);
  local_jobs = g_list_prepend (local_jobs, job);
  g_hash_table_insert (application->local_jobs, object, local_jobs);

  g_signal_connect (job, "notify", G_CALLBACK (on_local_job_notify), application);

  udisks_client_queue_changed (application->client);

  return job;
}


/* ---------------------------------------------------------------------------------------------------- */

void
gdu_application_destroy_local_job (GduApplication *application,
                                   GduLocalJob    *job)
{
  GList *local_jobs;
  UDisksObject *object;

  g_return_if_fail (GDU_IS_APPLICATION (application));
  g_return_if_fail (GDU_IS_LOCAL_JOB (job));

  object = gdu_local_job_get_object (job);

  local_jobs = g_hash_table_lookup (application->local_jobs, object);
  g_warn_if_fail (g_list_find (local_jobs, job) != NULL);
  local_jobs = g_list_remove (local_jobs, job);
  g_signal_handlers_disconnect_by_func (job, G_CALLBACK (on_local_job_notify), application);

  if (local_jobs != NULL)
    g_hash_table_insert (application->local_jobs, object, local_jobs);
  else
    g_hash_table_remove (application->local_jobs, object);

  g_object_unref (job);

  udisks_client_queue_changed (application->client);
}

/* ---------------------------------------------------------------------------------------------------- */

GList *
gdu_application_get_local_jobs_for_object (GduApplication *application,
                                           UDisksObject   *object)
{
  GList *ret;

  g_return_val_if_fail (GDU_IS_APPLICATION (application), NULL);
  g_return_val_if_fail (UDISKS_IS_OBJECT (object), NULL);

  ret = g_list_copy_deep (g_hash_table_lookup (application->local_jobs, object),
                          (GCopyFunc) g_object_ref,
                          NULL);
  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */
