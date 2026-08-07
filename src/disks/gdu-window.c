/* gdu-window.c
 *
 * Copyright (C) 2008-2013 Red Hat, Inc.
 * Copyright 2023 Mohammed Sadiq <sadiq@sadiqpk.org>
 *
 * Licensed under GPL version 2 or later.
 *
 * Author(s):
 *   David Zeuthen <zeuthen@gmail.com>
 *   Mohammed Sadiq <sadiq@sadiqpk.org>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#define G_LOG_DOMAIN "gdu-window"

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gdu-window.h"
#include "gdu-application.h"
#include "gdu-drive-row.h"
#include "gdu-drive-view.h"
#include "gdu-job-manager.h"
#include "gdu-job-row.h"
#include "gdu-new-disk-image-dialog.h"
#include "gduutils.h"

struct _GduWindow {
    AdwApplicationWindow parent_instance;

    AdwOverlaySplitView *split_view;
    GtkStack *main_stack;
    GtkListBox *drives_listbox;
    GduDriveView *drive_view;

    GtkWidget *job_progress_button;
    GtkListBox *jobs_listbox;

    GduManager *manager;
    GduJobManager *job_manager;
};

G_DEFINE_FINAL_TYPE (GduWindow, gdu_window, ADW_TYPE_APPLICATION_WINDOW)

GSettings *gdu_window_state;

static GtkWidget *
gdu_window_create_job_row_cb (gpointer item, gpointer user_data)
{
    return GTK_WIDGET (gdu_job_row_new (GDU_LOCAL_JOB (item), GDU_JOB_MANAGER (user_data)));
}

static void
gdu_window_set_job_manager (GduWindow *self, GduJobManager *job_manager)
{
    g_assert (GDU_IS_WINDOW (self));
    g_assert (GDU_IS_JOB_MANAGER (job_manager));

    self->job_manager = g_object_ref (job_manager);

    gtk_list_box_bind_model (self->jobs_listbox, gdu_job_manager_get_jobs (job_manager), gdu_window_create_job_row_cb,
                             g_object_ref (job_manager), g_object_unref);

    g_object_bind_property (self->job_manager, "n-jobs", self->job_progress_button, "visible", G_BINDING_SYNC_CREATE);
}

static void
gdu_window_unset_job_manager (GduWindow *self)
{
    if (self->job_manager == NULL)
        return;

    if (self->jobs_listbox != NULL)
        gtk_list_box_bind_model (self->jobs_listbox, NULL, NULL, NULL, NULL);

    g_clear_object (&self->job_manager);
}

static void
drive_list_row_selection_changed_cb (GduWindow *self)
{
    GduDriveRow *row;

    g_assert (GDU_IS_WINDOW (self));

    row = (gpointer) gtk_list_box_get_selected_row (self->drives_listbox);
    if (!row)
        gtk_stack_set_visible_child_name (self->main_stack, "empty_page");
    else
        gtk_stack_set_visible_child_name (self->main_stack, "drive_page");

    if (row)
        gdu_drive_view_set_drive (self->drive_view, gdu_drive_row_get_drive (row));
}

static void
gdu_window_unmap (GtkWidget *widget)
{
    GtkWindow *window = GTK_WINDOW (widget);
    GVariant *initial_state;
    gint width;
    gint height;
    gboolean is_maximized;

    is_maximized = gtk_window_is_maximized (window);

    gtk_window_get_default_size (window, &width, &height);
    initial_state = g_variant_new_parsed ("(%i, %i, %b)", width, height, is_maximized);

    g_settings_set_value (gdu_window_state, GDU_WINDOW_INITIAL_STATE, initial_state);

    GTK_WIDGET_CLASS (gdu_window_parent_class)->unmap (widget);
}

static void
gdu_window_load_state (GduWindow *self)
{
    g_autoptr (GVariant) default_size = NULL;
    gboolean maximized = FALSE;
    gint current_width = -1;
    gint current_height = -1;

    default_size = g_settings_get_value (gdu_window_state, GDU_WINDOW_INITIAL_STATE);

    g_variant_get (default_size, "(iib)", &current_width, &current_height, &maximized);

    if (current_width == -1)
        current_width = 980;

    if (current_height == -1)
        current_height = 640;

    gtk_window_set_default_size (GTK_WINDOW (self), current_width, current_height);

    if (maximized)
        gtk_window_maximize (GTK_WINDOW (self));
}

static void
gdu_window_finalize (GObject *object)
{
    GduWindow *self = GDU_WINDOW (object);

    gdu_window_unset_job_manager (self);
    g_clear_object (&self->manager);

    G_OBJECT_CLASS (gdu_window_parent_class)->finalize (object);
}

static void
gdu_window_class_init (GduWindowClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

    object_class->finalize = gdu_window_finalize;
    widget_class->unmap = gdu_window_unmap;

    gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/DiskUtility/ui/"
                                                               "gdu-window.ui");

    gtk_widget_class_bind_template_child (widget_class, GduWindow, split_view);
    gtk_widget_class_bind_template_child (widget_class, GduWindow, main_stack);
    gtk_widget_class_bind_template_child (widget_class, GduWindow, drives_listbox);
    gtk_widget_class_bind_template_child (widget_class, GduWindow, drive_view);
    gtk_widget_class_bind_template_child (widget_class, GduWindow, job_progress_button);
    gtk_widget_class_bind_template_child (widget_class, GduWindow, jobs_listbox);

    gtk_widget_class_bind_template_callback (widget_class, drive_list_row_selection_changed_cb);
}

static void
gdu_window_init (GduWindow *self)
{
    gdu_window_state = g_settings_new ("org.gnome.Disks.window-state");

    gdu_window_load_state (self);
    gtk_widget_init_template (GTK_WIDGET (self));
}

GduWindow *
gdu_window_new (GApplication *application, GduManager *manager)
{
    GduWindow *self;
    GListModel *drives;

    g_return_val_if_fail (GDU_IS_APPLICATION (application), NULL);
    g_return_val_if_fail (GDU_IS_MANAGER (manager), NULL);

    self = g_object_new (GDU_TYPE_WINDOW, "application", application, NULL);

    self->manager = g_object_ref (manager);
    gdu_window_set_job_manager (self, gdu_application_get_job_manager ());

    drives = gdu_manager_get_drives (manager);

    gtk_list_box_bind_model (self->drives_listbox, drives, (GtkListBoxCreateWidgetFunc) gdu_drive_row_new, NULL, NULL);

    return self;
}
