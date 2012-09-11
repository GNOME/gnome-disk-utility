/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*-
 *
 * Copyright (C) 2008-2012 Red Hat, Inc.
 *
 * Licensed under GPL version 2 or later.
 *
 * Author: David Zeuthen <zeuthen@gmail.com>
 */

#include "config.h"
#include <glib/gi18n.h>

#include "gduapplication.h"
#include "gduwindow.h"

struct _GduApplication
{
  GtkApplication parent_instance;

  gboolean running_from_source_tree;

  UDisksClient *client;
  GduWindow *window;
};

typedef struct
{
  GtkApplicationClass parent_class;
} GduApplicationClass;

G_DEFINE_TYPE (GduApplication, gdu_application, GTK_TYPE_APPLICATION);

static void
gdu_application_init (GduApplication *app)
{
}

static void
gdu_application_finalize (GObject *object)
{
  GduApplication *app = GDU_APPLICATION (object);

  if (app->client != NULL)
    g_object_unref (app->client);

  G_OBJECT_CLASS (gdu_application_parent_class)->finalize (object);
}

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

static void
gdu_application_activate (GApplication *_app)
{
  GduApplication *app = GDU_APPLICATION (_app);
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

  app->window = gdu_window_new (app, app->client);
  gtk_application_add_window (GTK_APPLICATION (app),
                              GTK_WINDOW (app->window));
  gtk_widget_show_all (GTK_WIDGET (app->window));

 out:
  ;
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

  dialog = GTK_WIDGET (gdu_application_new_widget (app,
                                                   "about-dialog.ui",
                                                   "about-dialog",
                                                   NULL));
  gtk_about_dialog_set_version (GTK_ABOUT_DIALOG (dialog), PACKAGE_VERSION);
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

static GActionEntry app_entries[] =
{
  { "attach_disk_image", attach_disk_image_activated, NULL, NULL, NULL },
  { "about", about_activated, NULL, NULL, NULL },
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
  application_class->activate           = gdu_application_activate;
  application_class->startup            = gdu_application_startup;
}

GApplication *
gdu_application_new (void)
{
  gtk_init (NULL, NULL);
  return G_APPLICATION (g_object_new (GDU_TYPE_APPLICATION,
                                      "application-id", "org.gnome.DiskUtility",
                                      "flags", G_APPLICATION_FLAGS_NONE,
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
