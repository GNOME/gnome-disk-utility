/*
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

#include "gdu-manager.h"
#include "gdu-application.h"
#include "gdu-format-volume-dialog.h"
#include "gdu-new-disk-image-dialog.h"
#include "gdu-window.h"
#include "gdulocaljob.h"
#include "gdu-log.h"
#include "gdu-rust.h"

struct _GduApplication
{
  AdwApplication  parent_instance;

  GduManager     *disk_manager;
  UDisksClient   *client;
  GduWindow      *window;

  /* Maps from UDisksObject* -> GList<GduLocalJob*> */
  GHashTable     *local_jobs;
};

G_DEFINE_TYPE (GduApplication, gdu_application, ADW_TYPE_APPLICATION);

static void gdu_application_set_options (GduApplication *app);

static void
gdu_application_init (GduApplication *app)
{
  app->local_jobs = g_hash_table_new (g_direct_hash, g_direct_equal);

  gdu_application_set_options (app);
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

  if (gdu_rs_has_local_jobs())
      gdu_rs_local_jobs_clear();

  if (app->client != NULL)
    g_object_unref (app->client);

  G_OBJECT_CLASS (gdu_application_parent_class)->finalize (object);
}

static void
application_quit_response_cb (GObject           *source_object,
                              GAsyncResult      *response,
                              gpointer           user_data)
{
  GduApplication *self = GDU_APPLICATION (user_data);
  AdwAlertDialog *dialog = ADW_ALERT_DIALOG (source_object);

  if (g_strcmp0 (adw_alert_dialog_choose_finish(dialog, response), "cancel") == 0)
    return;

  gtk_window_close (GTK_WINDOW (self->window));
}

/* ---------------------------------------------------------------------------------------------------- */

static void
gdu_application_ensure_client (GduApplication *app)
{
  g_autoptr(GError) error = NULL;

  app->disk_manager = gdu_manager_get_default (&error);
  app->client = gdu_manager_get_client (app->disk_manager);
  if (error)
    g_error ("Error getting udisks client: %s", error->message);
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

static gboolean
cmd_verbose_cb (const char  *option_name,
                const char  *value,
                gpointer     data,
                GError     **error)
{
  gdu_log_increase_verbosity ();

  return TRUE;
}

static GOptionEntry opt_entries[] = {
    {"block-device", 0, 0, G_OPTION_ARG_STRING, NULL, N_("Select device"), "DEVICE" },
    {"format-device", 0, 0, G_OPTION_ARG_NONE, NULL, N_("Format selected device"), NULL },
    {"verbose", 'v', G_OPTION_FLAG_NO_ARG, G_OPTION_ARG_CALLBACK, cmd_verbose_cb, N_("Show verbose logs, specify up to four times to increase log level"), NULL },
    {"xid", 0, 0, G_OPTION_ARG_INT, NULL, N_("Ignored, kept for compatibility"), "ID" },
    {"restore-disk-image", 0, 0, G_OPTION_ARG_FILENAME, NULL, N_("Restore disk image"), "FILE" },
    {NULL}
};

static void
gdu_application_set_options (GduApplication *app)
{
  g_application_add_main_option_entries (G_APPLICATION (app), opt_entries);
}

/* called in primary instance */
static gint
gdu_application_command_line (GApplication            *_app,
                              GApplicationCommandLine *command_line)
{
  GduApplication *app = GDU_APPLICATION (_app);

  UDisksObject *object_to_select = NULL;
  gint ret = 1;
  const gchar *opt_block_device = NULL;
  gchar *error_message = NULL;
  gboolean opt_format = FALSE;
  const gchar *opt_restore_disk_image = NULL;
  GVariantDict *options;

  options = g_application_command_line_get_options_dict (command_line);

  g_variant_dict_lookup (options, "block-device", "&s", &opt_block_device);
  g_variant_dict_lookup (options, "format-device", "b", &opt_format);
  g_variant_dict_lookup (options, "restore-disk-image", "^&ay", &opt_restore_disk_image);

  if (opt_format && opt_block_device == NULL)
    {
      g_application_command_line_printerr (command_line, _("--format-device must be used together with --block-device\n"));
      goto out;
    }

  if (opt_format && opt_restore_disk_image != NULL)
    {
      g_application_command_line_printerr (command_line, _("--format-device must not be used together with --restore-disk-image"));
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

  g_application_activate (G_APPLICATION (app));

  /* gtk4 todo: after using GtkSelectionModel or so */
  /* if (object_to_select != NULL) */
  /*   { */
  /*     gdu_window_select_object (app->window, object_to_select); */
  /*     if (opt_format) */
  /*       gdu_create_format_show (app->client, GTK_WINDOW (app->window), object_to_select, */
  /*                               FALSE, 0, 0); */
  /*   } */

  if (opt_restore_disk_image != NULL)
    {
      gdu_rs_restore_disk_image_dialog_show (GTK_WINDOW (app->window), NULL, opt_restore_disk_image);
    }

  ret = 0;

 out:
  g_clear_object (&object_to_select);
  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

static void
gdu_application_activate (GApplication *_app)
{
  GduApplication *app = GDU_APPLICATION (_app);

  gdu_application_ensure_client (app);

  if (app->window == NULL)
    app->window = gdu_window_new (_app, app->disk_manager);

  gtk_window_present (GTK_WINDOW (app->window));
}

/* ---------------------------------------------------------------------------------------------------- */

static void
new_disk_image_activated (GSimpleAction *action,
                          GVariant      *parameter,
                          gpointer       user_data)
{
  GduApplication *app = GDU_APPLICATION (user_data);

  gdu_new_disk_image_dialog_show (app->client, GTK_WINDOW(app->window));
}

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

  adw_show_about_dialog (GTK_WIDGET (app->window),
                         "application-name", _("Disks"),
                         "application-icon", "org.gnome.DiskUtility",
                         "developer-name", _("The GNOME Project"),
                         "version", PACKAGE_VERSION,
                         "copyright", _("© 2009 The GNOME Project\n© 2008-2013 Red Hat, Inc.\n© 2008-2013 David Zeuthen"),
                         "website", "https://apps.gnome.org/DiskUtility/",
                         "issue-url", "https://gitlab.gnome.org/GNOME/gnome-disk-utility/-/issues/",
                         "license-type", GTK_LICENSE_GPL_2_0,
                         "translator-credits", _("translator-credits"),
                         NULL);
}

static void
gdu_application_quit (GSimpleAction *action,
                      GVariant      *parameter,
                      gpointer       user_data)
{
  GduApplication *app = GDU_APPLICATION (user_data);
  ConfirmationDialogData *data;

  if ((app->local_jobs == NULL || g_hash_table_size (app->local_jobs) == 0) && !gdu_rs_has_local_jobs())
    gtk_window_close (GTK_WINDOW (app->window));

  data = g_new0 (ConfirmationDialogData, 1);
  data->message = _("Stop running jobs?");
  data->description = _("Closing now stops the running jobs and leads to a corrupt result.");
  data->response_verb = _("Stop");
  data->response_appearance = ADW_RESPONSE_DESTRUCTIVE;
  data->callback = application_quit_response_cb;
  data->user_data = app;

  gdu_utils_show_confirmation (GTK_WIDGET (app->window),
                               data, NULL);
}

static void
help_activated (GSimpleAction *action,
                GVariant      *parameter,
                gpointer       user_data)
{
  GtkWindow *window;
  GtkApplication *application = user_data;
  GtkUriLauncher *launcher;

  window = gtk_application_get_active_window (application);
  launcher = gtk_uri_launcher_new ("help:gnome-help/disk");
  gtk_uri_launcher_launch (launcher, window, NULL, NULL, NULL);
}

static GActionEntry app_entries[] =
{
  { "new_disk_image", new_disk_image_activated, NULL, NULL, NULL },
  { "attach_disk_image", attach_disk_image_activated, NULL, NULL, NULL },
  { "help", help_activated, NULL, NULL, NULL },
  { "about", about_activated, NULL, NULL, NULL },
  { "quit", gdu_application_quit, NULL, NULL, NULL }
};

static void
gdu_application_startup (GApplication *_app)
{
  GduApplication *app = GDU_APPLICATION (_app);

  const gchar **it;
  const gchar *action_accels[] = {
    "win.go-back",               "Escape", NULL,
    "win.open-drive-menu",       "F9", NULL,
    "win.open-volume-menu",      "<Shift>F9", NULL,
    "win.open-app-menu",         "F10", NULL,

    "win.format-disk",           "<Primary>D", NULL,
    "win.restore-disk-image",    "<Primary>R", NULL,
    "win.view-smart",            "<Primary>S", NULL,
    "win.disk-settings",         "<Primary>E", NULL,

    "win.format-partition",      "<Primary>P", NULL,

    "app.new_disk_image",        "<Primary>N", NULL,
    "app.attach_disk_image",     "<Primary>A", NULL,

    "app.help",                  "F1", NULL,
    "app.quit",                  "<Primary>Q", NULL,

    NULL
  };

  if (G_APPLICATION_CLASS (gdu_application_parent_class)->startup != NULL)
    G_APPLICATION_CLASS (gdu_application_parent_class)->startup (_app);

  g_action_map_add_action_entries (G_ACTION_MAP (app), app_entries, G_N_ELEMENTS (app_entries), app);

  for (it = action_accels; it[0] != NULL; it += g_strv_length ((gchar **)it) + 1)
    gtk_application_set_accels_for_action (GTK_APPLICATION (app), it[0], &it[1]);
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

  application_class->command_line = gdu_application_command_line;
  application_class->activate     = gdu_application_activate;
  application_class->startup      = gdu_application_startup;
}

GtkApplication *
gdu_application_new (void)
{
  return g_object_new (GDU_TYPE_APPLICATION,
                       "application-id", "org.gnome.DiskUtility",
                       "flags", G_APPLICATION_HANDLES_COMMAND_LINE,
                       "resource-base-path", "/org/gnome/DiskUtility",
                       NULL);
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

  path = g_strdup_printf ("/org/gnome/DiskUtility/ui/%s", ui_file);

  error = NULL;
  if (gtk_builder_add_from_resource (builder, path, &error) == 0)
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
  g_free (path);
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
