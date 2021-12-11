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
#include "gducreateformatdialog.h"
#include "gdurestorediskimagedialog.h"
#include "gdunewdiskimagedialog.h"
#include "gduwindow.h"
#include "gdulocaljob.h"

struct _GduApplication
{
  GtkApplication parent_instance;

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

  if (app->client != NULL)
    g_object_unref (app->client);

  G_OBJECT_CLASS (gdu_application_parent_class)->finalize (object);
}


gboolean
gdu_application_should_exit (GduApplication *app)
{
  GtkWidget *dialog;
  gint response;

  if (app->local_jobs != NULL && g_hash_table_size (app->local_jobs) != 0)
    {
      dialog = gtk_message_dialog_new (GTK_WINDOW (app->window),
                                       GTK_DIALOG_DESTROY_WITH_PARENT,
                                       GTK_MESSAGE_WARNING,
                                       GTK_BUTTONS_OK_CANCEL,
                                       _("Stop running jobs?"));
      gtk_message_dialog_format_secondary_text (GTK_MESSAGE_DIALOG (dialog),
                                                _("Closing now stops the running jobs and leads to a corrupt result."));

      response = gtk_dialog_run (GTK_DIALOG (dialog));
      gtk_widget_destroy (dialog);

      if (response != GTK_RESPONSE_OK)
        return FALSE;

    }

  return TRUE;
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

static GOptionEntry opt_entries[] = {
    {"block-device", 0, 0, G_OPTION_ARG_STRING, NULL, N_("Select device"), "DEVICE" },
    {"format-device", 0, 0, G_OPTION_ARG_NONE, NULL, N_("Format selected device"), NULL },
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
        gdu_create_format_show (app->client, GTK_WINDOW (app->window), object_to_select,
                                FALSE, 0, 0, NULL, NULL);
    }

  if (opt_restore_disk_image != NULL)
    {
      gdu_restore_disk_image_dialog_show (app->window, NULL, opt_restore_disk_image);
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

  app->window = gdu_window_new (app, app->client);
  gtk_application_add_window (GTK_APPLICATION (app),
                              GTK_WINDOW (app->window));
  gtk_widget_show (GTK_WIDGET (app->window));
}

/* ---------------------------------------------------------------------------------------------------- */

static void
new_disk_image_activated (GSimpleAction *action,
                          GVariant      *parameter,
                          gpointer       user_data)
{
  GduApplication *app = GDU_APPLICATION (user_data);
  gdu_new_disk_image_dialog_show (app->window);
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
shortcuts_activated (GSimpleAction *action,
                     GVariant      *parameter,
                     gpointer       user_data)
{
  GduApplication *app = GDU_APPLICATION (user_data);
  GtkWidget *dialog;

  dialog = GTK_WIDGET (gdu_application_new_widget (app,
                                                   "shortcuts.ui",
                                                   "shortcuts",
                                                   NULL));

  gtk_window_set_transient_for (GTK_WINDOW (dialog), GTK_WINDOW (app->window));
  gtk_widget_show_all (dialog);
}

static void
on_about_dialog_response (GtkDialog *dialog)
{
  gtk_widget_destroy (GTK_WIDGET (dialog));
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
  gtk_widget_show_all (dialog);
  g_signal_connect (dialog, "response", on_about_dialog_response, NULL);
  gtk_window_present (GTK_WINDOW (dialog));
}

static void
quit_activated (GSimpleAction *action,
                GVariant      *parameter,
                gpointer       user_data)
{
  GduApplication *app = GDU_APPLICATION (user_data);

  if (gdu_application_should_exit (app))
    gtk_widget_destroy (GTK_WIDGET (app->window));
}

static void
help_activated (GSimpleAction *action,
                GVariant      *parameter,
                gpointer       user_data)
{
  GduApplication *app = GDU_APPLICATION (user_data);
  gtk_show_uri_on_window (GTK_WINDOW (app->window),
                          "help:gnome-help/disk",
                          GDK_CURRENT_TIME,
                          NULL); /* GError */
}

static GActionEntry app_entries[] =
{
  { "new_disk_image", new_disk_image_activated, NULL, NULL, NULL },
  { "attach_disk_image", attach_disk_image_activated, NULL, NULL, NULL },
  { "shortcuts", shortcuts_activated, NULL, NULL, NULL },
  { "help", help_activated, NULL, NULL, NULL },
  { "about", about_activated, NULL, NULL, NULL },
  { "quit", quit_activated, NULL, NULL, NULL }
};

static void
gdu_application_startup (GApplication *_app)
{
  GduApplication *app = GDU_APPLICATION (_app);
  const gchar **it;
  const gchar *action_accels[] = {
    "win.go-back",               "Escape", NULL,
    "win.open-drive-menu",       "F10", NULL,
    "win.open-volume-menu",      "<Shift>F10", NULL,
    "win.open-app-menu",         "F9", NULL,

    "win.format-disk",           "<Primary>D", NULL,
    "win.restore-disk-image",    "<Primary>R", NULL,
    "win.view-smart",            "<Primary>S", NULL,
    "win.disk-settings",         "<Primary>E", NULL,

    "win.format-partition",      "<Primary>P", NULL,

    "app.new_disk_image",        "<Primary>N", NULL,
    "app.attach_disk_image",     "<Primary>A", NULL,

    "app.help",                  "F1", NULL,
    "app.quit",                  "<Primary>Q", NULL,
    "app.shortcuts",             "<Primary>question", NULL,

    NULL
  };

  if (G_APPLICATION_CLASS (gdu_application_parent_class)->startup != NULL)
    G_APPLICATION_CLASS (gdu_application_parent_class)->startup (_app);

  hdy_init ();
  hdy_style_manager_set_color_scheme (hdy_style_manager_get_default (),
                                      HDY_COLOR_SCHEME_PREFER_LIGHT);

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

GApplication *
gdu_application_new (void)
{
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

  path = g_strdup_printf ("/org/gnome/Disks/ui/%s", ui_file);

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

gboolean
gdu_application_has_running_job (GduApplication *application,
                                 UDisksObject   *object)
{
  UDisksClient *client;
  GList *l;
  GList *jobs = NULL;
  GList *objects_to_check = NULL;
  gboolean ret = FALSE;

  client = gdu_application_get_client (application);
  objects_to_check = gdu_utils_get_all_contained_objects (client, object);
  objects_to_check = g_list_prepend (objects_to_check, g_object_ref (object));

  for (l = objects_to_check; l != NULL; l = l->next)
    {
      UDisksObject *object_iter = UDISKS_OBJECT (l->data);
      UDisksEncrypted *encrypted_for_object;

      jobs = udisks_client_get_jobs_for_object (client, object_iter);
      if (jobs != NULL)
        {
          ret = TRUE;
          break;
        }

      jobs = gdu_application_get_local_jobs_for_object (application, object_iter);
      if (jobs != NULL)
        {
          ret = TRUE;
          break;
        }

      encrypted_for_object = udisks_object_peek_encrypted (object_iter);
      if (encrypted_for_object != NULL)
        {
          UDisksBlock *block_for_object;
          UDisksBlock *cleartext;

          block_for_object = udisks_object_peek_block (object_iter);
          cleartext = udisks_client_get_cleartext_block (client, block_for_object);
          if (cleartext != NULL)
            {
              UDisksObject *cleartext_object;

              cleartext_object = (UDisksObject *) g_dbus_interface_get_object (G_DBUS_INTERFACE (cleartext));
              g_object_unref (cleartext);

              ret = gdu_application_has_running_job (application, cleartext_object);
              if (ret)
                break;
            }
        }

    }

  g_list_foreach (jobs, (GFunc) g_object_unref, NULL);
  g_list_free (jobs);
  g_list_free_full (objects_to_check, g_object_unref);

  return ret;
}

