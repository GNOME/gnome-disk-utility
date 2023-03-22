/*
 * Author: Mohammed Sadiq <www.sadiqpk.org>
 *
 * glue code to be used when migrating from GTK3 to GTK4.
 * should never be used for long, use it only as
 * a helper to migrate to GTK4, and drop this once
 * done
 *
 * Please note that these API may not do the right thing.
 * Some API also makes your application run slower.
 *
 * Consider this code only as a helper to avoid compiler
 * warnings.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later OR CC0-1.0
 */

#define G_LOG_DOMAIN "custom-gtk3-to-4"

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <adwaita.h>

#include "gtk3-to-4.h"

void
gtk_widget_destroy (GtkWidget *widget)
{
  if (GTK_IS_WINDOW (widget))
    gtk_window_destroy (GTK_WINDOW (widget));
  else
    g_assert_not_reached ();
}

static void
dialog_response_cb (GObject  *object,
                    int       response_id,
                    gpointer  user_data)
{
  GTask *task = user_data;

  g_assert (GTK_IS_DIALOG (object) || GTK_IS_NATIVE_DIALOG (object));
  g_assert (G_IS_TASK (task));

  g_task_return_int (task, response_id);
}

int
gtk_dialog_run (GtkDialog *dialog)
{
  g_autoptr(GTask) task = NULL;

  g_assert (GTK_IS_DIALOG (dialog));

  task = g_task_new (dialog, NULL, NULL, NULL);
  gtk_widget_show (GTK_WIDGET (dialog));

  g_signal_connect_object (dialog, "response",
                           G_CALLBACK (dialog_response_cb),
                           task, G_CONNECT_AFTER);

  /* Wait until the task is completed */
  while (!g_task_get_completed (task))
    g_main_context_iteration (NULL, TRUE);

  return g_task_propagate_int (task, NULL);
}

int
gtk_native_dialog_run (GtkNativeDialog *dialog)
{
  g_autoptr(GTask) task = NULL;

  g_assert (GTK_IS_NATIVE_DIALOG (dialog));

  task = g_task_new (dialog, NULL, NULL, NULL);
  gtk_native_dialog_show (dialog);

  g_signal_connect_object (dialog, "response",
                           G_CALLBACK (dialog_response_cb),
                           task, G_CONNECT_AFTER);

  /* Wait until the task is completed */
  while (!g_task_get_completed (task))
    g_main_context_iteration (NULL, TRUE);

  return g_task_propagate_int (task, NULL);
}

const char *
gtk_entry_get_text (gpointer entry)
{
  g_assert (GTK_IS_EDITABLE (entry));

  return gtk_editable_get_text (GTK_EDITABLE (entry));
}

void
gtk_entry_set_text (gpointer    entry,
                    const char *text)
{
  g_assert (GTK_IS_EDITABLE (entry));

  gtk_editable_set_text (GTK_EDITABLE (entry), text);
}

gboolean
gtk_window_has_toplevel_focus (GtkWindow *window)
{
  GdkSurface *surface;
  GdkToplevelState state;

  g_assert (GTK_IS_WINDOW (window));

  surface = gtk_native_get_surface (GTK_NATIVE (window));
  g_assert (GDK_IS_TOPLEVEL (surface));

  state = gdk_toplevel_get_state (GDK_TOPLEVEL (surface));

  return !!(state & GDK_TOPLEVEL_STATE_FOCUSED);
}

void
gtk_container_add (gpointer   container,
                   GtkWidget *widget)
{
  g_assert (GTK_IS_WIDGET (container));
  g_assert (GTK_IS_WIDGET (widget));

  if (GTK_IS_BOX (container))
    gtk_box_append (container, widget);
  else if (GTK_IS_STACK (container))
    gtk_stack_add_child (container, widget);
  else if (GTK_IS_SCROLLED_WINDOW (container))
    gtk_scrolled_window_set_child (container, widget);
  else if (ADW_IS_BIN (container))
    adw_bin_set_child (container, widget);
  else if (GTK_IS_LIST_BOX (container))
    gtk_list_box_append (container, widget);
  else if (ADW_IS_ACTION_ROW (container))
    adw_action_row_add_suffix (container, widget);
  else if (GTK_IS_FRAME (container))
    gtk_frame_set_child (container, widget);
  else if (GTK_IS_OVERLAY (container))
    gtk_overlay_add_overlay (container, widget);
  else
    {
      g_warn_if_reached ();
      gtk_widget_set_parent(widget, container);
    }
}

void
gtk_container_remove (gpointer   container,
                      GtkWidget *widget)
{
  g_assert (GTK_IS_WIDGET (container));
  g_assert (GTK_IS_WIDGET (widget));

  /* todo */
  /* g_assert_not_reached (); */
}

char *
gtk_file_chooser_get_filename (GtkFileChooser *chooser)
{
  g_autoptr(GFile) file = NULL;

  g_assert (GTK_IS_FILE_CHOOSER (chooser));

  file = gtk_file_chooser_get_file (chooser);

  return g_file_get_path (file);
}

gboolean
gtk_file_chooser_set_filename (GtkFileChooser *chooser,
                               const char     *path)
{
  g_autoptr(GFile) file = NULL;

  g_assert (GTK_IS_FILE_CHOOSER (chooser));

  file = g_file_new_for_path (path);

  return gtk_file_chooser_set_file (chooser, file, NULL);
}

static void
open_uri_cb (GObject      *object,
             GAsyncResult *result,
             gpointer      user_data)
{
  GTask *task = user_data;
  gboolean success;
  GError *error = NULL;

  g_assert (G_IS_TASK (task));

  success = gtk_show_uri_full_finish ((GtkWindow *)object, result, &error);

  if (error)
    g_task_return_error (task, error);
  else
    g_task_return_boolean (task, success);
}

gboolean
gtk_show_uri_on_window (GtkWindow   *parent,
                        const char  *uri,
                        guint32      timestamp,
                        GError     **error)
{
  g_autoptr(GTask) task = NULL;

  g_assert (!parent || GTK_IS_WINDOW (parent));

  task = g_task_new (parent, NULL, NULL, NULL);
  gtk_show_uri_full (parent, uri, timestamp, NULL,
                     open_uri_cb, task);

  /* Wait until the task is completed */
  while (!g_task_get_completed (task))
    g_main_context_iteration (NULL, TRUE);

  return g_task_propagate_boolean (task, error);
}

GList *
gtk_container_get_children (gpointer container)
{
  GtkWidget *child;
  GList *children = NULL;

  g_assert (GTK_IS_WIDGET (container));

  child = gtk_widget_get_last_child (container);

  while (child)
    {
      children = g_list_prepend (children, child);
      child = gtk_widget_get_prev_sibling (child);
    }

  return children;
}

void
gtk_widget_show_all (GtkWidget *widget)
{
  g_autoptr(GList) children = NULL;

  g_assert (GTK_IS_WIDGET (widget));

  children = gtk_container_get_children (widget);

  for (GList *child = children; child && child->data; child = child->next)
    gtk_widget_show (child->data);
}
