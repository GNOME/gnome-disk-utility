/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*-
 *
 * Copyright (C) 2008-2013 Red Hat, Inc.
 *
 * Licensed under GPL version 2 or later.
 *
 * Author: David Zeuthen <zeuthen@gmail.com>
 */

#include "config.h"
#include <string.h>
#include <stdlib.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>

#include <glib/gi18n.h>
#include <gio/gunixfdlist.h>

#include "gduapplication.h"
#include "gduwindow.h"
#include "gdudevicetreemodel.h"
#include "gduvolumegrid.h"
#include "gduatasmartdialog.h"
#include "gdubenchmarkdialog.h"
#include "gducrypttabdialog.h"
#include "gdufstabdialog.h"
#include "gdufilesystemdialog.h"
#include "gdupartitiondialog.h"
#include "gduunlockdialog.h"
#include "gduformatvolumedialog.h"
#include "gducreatepartitiondialog.h"
#include "gduformatdiskdialog.h"
#include "gducreatediskimagedialog.h"
#include "gdurestorediskimagedialog.h"
#include "gduchangepassphrasedialog.h"
#include "gdudisksettingsdialog.h"
#include "gduresizedialog.h"
#include "gdulocaljob.h"

#define JOB_SENSITIVITY_DELAY_MS 300

struct _GduWindow
{
  GtkApplicationWindow parent_instance;

  GduApplication *application;
  UDisksClient *client;

  const gchar *builder_path;
  GtkBuilder *builder;
  GduDeviceTreeModel *model;

  UDisksObject *current_object;
  gboolean has_drive_job;
  gboolean has_volume_job;
  guint delay_job_update_id;

  GtkWidget *volume_grid;

  GtkWidget *toolbutton_generic_menu;
  GtkWidget *toolbutton_partition_create;
  GtkWidget *toolbutton_partition_delete;
  GtkWidget *toolbutton_mount;
  GtkWidget *toolbutton_unmount;
  GtkWidget *toolbutton_unlock;
  GtkWidget *toolbutton_lock;
  GtkWidget *toolbutton_activate_swap;
  GtkWidget *toolbutton_deactivate_swap;

  GtkWidget *header;

  GtkWidget *main_box;
  GtkWidget *main_hpane;
  GtkWidget *details_notebook;
  GtkWidget *device_tree_scrolledwindow;
  GtkWidget *device_tree_treeview;

  GtkWidget *devtab_drive_loop_detach_button;
  GtkWidget *devtab_drive_eject_button;
  GtkWidget *devtab_drive_power_off_button;
  GtkWidget *devtab_drive_generic_button;
  GtkWidget *devtab_table;
  GtkWidget *devtab_drive_table;
  GtkWidget *devtab_grid_hbox;
  GtkWidget *devtab_volumes_label;
  GtkWidget *devtab_grid_toolbar;

  GtkWidget *generic_drive_menu;
  GtkWidget *generic_drive_menu_item_format_disk;
  GtkWidget *generic_drive_menu_item_create_disk_image;
  GtkWidget *generic_drive_menu_item_restore_disk_image;
  GtkWidget *generic_drive_menu_item_benchmark;
  /* Drive-specific items */
  GtkWidget *generic_drive_menu_item_drive_sep_1;
  GtkWidget *generic_drive_menu_item_view_smart;
  GtkWidget *generic_drive_menu_item_disk_settings;
  GtkWidget *generic_drive_menu_item_drive_sep_2;
  GtkWidget *generic_drive_menu_item_standby_now;
  GtkWidget *generic_drive_menu_item_resume_now;
  GtkWidget *generic_drive_menu_item_power_off;

  GtkWidget *generic_menu;
  GtkWidget *generic_menu_item_configure_fstab;
  GtkWidget *generic_menu_item_configure_crypttab;
  GtkWidget *generic_menu_item_change_passphrase;
  GtkWidget *generic_menu_item_resize;
  GtkWidget *generic_menu_item_repair;
  GtkWidget *generic_menu_item_check;
  GtkWidget *generic_menu_item_separator;
  GtkWidget *generic_menu_item_edit_label;
  GtkWidget *generic_menu_item_edit_partition;
  GtkWidget *generic_menu_item_format_volume;
  GtkWidget *generic_menu_item_create_volume_image;
  GtkWidget *generic_menu_item_restore_volume_image;
  GtkWidget *generic_menu_item_benchmark;

  GtkWidget *devtab_loop_autoclear_switch;

  GtkWidget *devtab_drive_job_label;
  GtkWidget *devtab_drive_job_grid;
  GtkWidget *devtab_drive_job_progressbar;
  GtkWidget *devtab_drive_job_remaining_label;
  GtkWidget *devtab_drive_job_no_progress_label;
  GtkWidget *devtab_drive_job_cancel_button;

  GtkWidget *devtab_job_label;
  GtkWidget *devtab_job_grid;
  GtkWidget *devtab_job_progressbar;
  GtkWidget *devtab_job_remaining_label;
  GtkWidget *devtab_job_no_progress_label;
  GtkWidget *devtab_job_cancel_button;

  /* GtkLabel instances we need to handle ::activate-link for */
  GtkWidget *devtab_volume_type_value_label;
};

static const struct {
  goffset offset;
  const gchar *name;
} widget_mapping[] = {
  {G_STRUCT_OFFSET (GduWindow, toolbutton_generic_menu), "toolbutton-generic-menu"},
  {G_STRUCT_OFFSET (GduWindow, toolbutton_partition_create), "toolbutton-partition-create"},
  {G_STRUCT_OFFSET (GduWindow, toolbutton_partition_delete), "toolbutton-partition-delete"},
  {G_STRUCT_OFFSET (GduWindow, toolbutton_mount), "toolbutton-mount"},
  {G_STRUCT_OFFSET (GduWindow, toolbutton_unmount), "toolbutton-unmount"},
  {G_STRUCT_OFFSET (GduWindow, toolbutton_unlock), "toolbutton-unlock"},
  {G_STRUCT_OFFSET (GduWindow, toolbutton_lock), "toolbutton-lock"},
  {G_STRUCT_OFFSET (GduWindow, toolbutton_activate_swap), "toolbutton-activate-swap"},
  {G_STRUCT_OFFSET (GduWindow, toolbutton_deactivate_swap), "toolbutton-deactivate-swap"},

  {G_STRUCT_OFFSET (GduWindow, main_box), "main-box"},
  {G_STRUCT_OFFSET (GduWindow, main_hpane), "main-hpane"},
  {G_STRUCT_OFFSET (GduWindow, device_tree_scrolledwindow), "device-tree-scrolledwindow"},

  {G_STRUCT_OFFSET (GduWindow, device_tree_treeview), "device-tree-treeview"},
  {G_STRUCT_OFFSET (GduWindow, details_notebook), "disks-notebook"},
  {G_STRUCT_OFFSET (GduWindow, devtab_drive_table), "devtab-drive-table"},
  {G_STRUCT_OFFSET (GduWindow, devtab_table), "devtab-table"},
  {G_STRUCT_OFFSET (GduWindow, devtab_grid_hbox), "devtab-grid-hbox"},
  {G_STRUCT_OFFSET (GduWindow, devtab_volumes_label), "devtab-volumes-label"},
  {G_STRUCT_OFFSET (GduWindow, devtab_grid_toolbar), "devtab-grid-toolbar"},

  {G_STRUCT_OFFSET (GduWindow, devtab_loop_autoclear_switch), "devtab-loop-autoclear-switch"},

  {G_STRUCT_OFFSET (GduWindow, generic_drive_menu), "generic-drive-menu"},
  {G_STRUCT_OFFSET (GduWindow, generic_drive_menu_item_format_disk), "generic-drive-menu-item-format-disk"},
  {G_STRUCT_OFFSET (GduWindow, generic_drive_menu_item_create_disk_image), "generic-drive-menu-item-create-disk-image"},
  {G_STRUCT_OFFSET (GduWindow, generic_drive_menu_item_restore_disk_image), "generic-drive-menu-item-restore-disk-image"},
  {G_STRUCT_OFFSET (GduWindow, generic_drive_menu_item_benchmark), "generic-drive-menu-item-benchmark"},
  /* Drive-specific items */
  {G_STRUCT_OFFSET (GduWindow, generic_drive_menu_item_drive_sep_1), "generic-drive-menu-item-drive-sep-1"},
  {G_STRUCT_OFFSET (GduWindow, generic_drive_menu_item_view_smart), "generic-drive-menu-item-view-smart"},
  {G_STRUCT_OFFSET (GduWindow, generic_drive_menu_item_disk_settings), "generic-drive-menu-item-disk-settings"},
  {G_STRUCT_OFFSET (GduWindow, generic_drive_menu_item_drive_sep_2), "generic-drive-menu-item-drive-sep-2"},
  {G_STRUCT_OFFSET (GduWindow, generic_drive_menu_item_standby_now), "generic-drive-menu-item-standby-now"},
  {G_STRUCT_OFFSET (GduWindow, generic_drive_menu_item_resume_now), "generic-drive-menu-item-resume-now"},
  {G_STRUCT_OFFSET (GduWindow, generic_drive_menu_item_power_off), "generic-drive-menu-item-power-off"},

  {G_STRUCT_OFFSET (GduWindow, generic_menu), "generic-menu"},
  {G_STRUCT_OFFSET (GduWindow, generic_menu_item_configure_fstab), "generic-menu-item-configure-fstab"},
  {G_STRUCT_OFFSET (GduWindow, generic_menu_item_configure_crypttab), "generic-menu-item-configure-crypttab"},
  {G_STRUCT_OFFSET (GduWindow, generic_menu_item_change_passphrase), "generic-menu-item-change-passphrase"},
  {G_STRUCT_OFFSET (GduWindow, generic_menu_item_resize), "generic-menu-item-resize"},
  {G_STRUCT_OFFSET (GduWindow, generic_menu_item_check), "generic-menu-item-check"},
  {G_STRUCT_OFFSET (GduWindow, generic_menu_item_repair), "generic-menu-item-repair"},
  {G_STRUCT_OFFSET (GduWindow, generic_menu_item_separator), "generic-menu-item-separator"},
  {G_STRUCT_OFFSET (GduWindow, generic_menu_item_edit_label), "generic-menu-item-edit-label"},
  {G_STRUCT_OFFSET (GduWindow, generic_menu_item_edit_partition), "generic-menu-item-edit-partition"},
  {G_STRUCT_OFFSET (GduWindow, generic_menu_item_format_volume), "generic-menu-item-format-volume"},
  {G_STRUCT_OFFSET (GduWindow, generic_menu_item_create_volume_image), "generic-menu-item-create-volume-image"},
  {G_STRUCT_OFFSET (GduWindow, generic_menu_item_restore_volume_image), "generic-menu-item-restore-volume-image"},
  {G_STRUCT_OFFSET (GduWindow, generic_menu_item_benchmark), "generic-menu-item-benchmark"},

  {G_STRUCT_OFFSET (GduWindow, devtab_drive_job_label), "devtab-drive-job-label"},
  {G_STRUCT_OFFSET (GduWindow, devtab_drive_job_grid), "devtab-drive-job-grid"},
  {G_STRUCT_OFFSET (GduWindow, devtab_drive_job_progressbar), "devtab-drive-job-progressbar"},
  {G_STRUCT_OFFSET (GduWindow, devtab_drive_job_remaining_label), "devtab-drive-job-remaining-label"},
  {G_STRUCT_OFFSET (GduWindow, devtab_drive_job_no_progress_label), "devtab-drive-job-no-progress-label"},
  {G_STRUCT_OFFSET (GduWindow, devtab_drive_job_cancel_button), "devtab-drive-job-cancel-button"},

  {G_STRUCT_OFFSET (GduWindow, devtab_job_label), "devtab-job-label"},
  {G_STRUCT_OFFSET (GduWindow, devtab_job_grid), "devtab-job-grid"},
  {G_STRUCT_OFFSET (GduWindow, devtab_job_progressbar), "devtab-job-progressbar"},
  {G_STRUCT_OFFSET (GduWindow, devtab_job_remaining_label), "devtab-job-remaining-label"},
  {G_STRUCT_OFFSET (GduWindow, devtab_job_no_progress_label), "devtab-job-no-progress-label"},
  {G_STRUCT_OFFSET (GduWindow, devtab_job_cancel_button), "devtab-job-cancel-button"},

  /* GtkLabel instances we need to handle ::activate-link for */
  {G_STRUCT_OFFSET (GduWindow, devtab_volume_type_value_label), "devtab-volume-type-value-label"},

  {0, NULL}
};

typedef struct
{
  GtkApplicationWindowClass parent_class;
} GduWindowClass;

enum
{
  PROP_0,
  PROP_APPLICATION,
  PROP_CLIENT
};

/* ---------------------------------------------------------------------------------------------------- */

typedef enum {
  SHOW_FLAGS_DRIVE_BUTTONS_EJECT            = (1<<0),
  SHOW_FLAGS_DRIVE_BUTTONS_POWER_OFF        = (1<<1),
  SHOW_FLAGS_DRIVE_BUTTONS_LOOP_DETACH      = (1<<2),
} ShowFlagsDriveButtons;

typedef enum
{
  SHOW_FLAGS_DRIVE_MENU_FORMAT_DISK           = (1<<0),
  SHOW_FLAGS_DRIVE_MENU_CREATE_DISK_IMAGE     = (1<<1),
  SHOW_FLAGS_DRIVE_MENU_RESTORE_DISK_IMAGE    = (1<<2),
  SHOW_FLAGS_DRIVE_MENU_BENCHMARK             = (1<<3),
  SHOW_FLAGS_DRIVE_MENU_VIEW_SMART            = (1<<4),
  SHOW_FLAGS_DRIVE_MENU_DISK_SETTINGS         = (1<<5),
  SHOW_FLAGS_DRIVE_MENU_STANDBY_NOW           = (1<<6),
  SHOW_FLAGS_DRIVE_MENU_RESUME_NOW            = (1<<7),
  SHOW_FLAGS_DRIVE_MENU_POWER_OFF             = (1<<8),
} ShowFlagsDriveMenu;

typedef enum {
  SHOW_FLAGS_VOLUME_BUTTONS_PARTITION_CREATE = (1<<0),
  SHOW_FLAGS_VOLUME_BUTTONS_PARTITION_DELETE = (1<<1),
  SHOW_FLAGS_VOLUME_BUTTONS_MOUNT            = (1<<2),
  SHOW_FLAGS_VOLUME_BUTTONS_UNMOUNT          = (1<<3),
  SHOW_FLAGS_VOLUME_BUTTONS_ACTIVATE_SWAP    = (1<<4),
  SHOW_FLAGS_VOLUME_BUTTONS_DEACTIVATE_SWAP  = (1<<5),
  SHOW_FLAGS_VOLUME_BUTTONS_ENCRYPTED_UNLOCK = (1<<6),
  SHOW_FLAGS_VOLUME_BUTTONS_ENCRYPTED_LOCK   = (1<<7),
} ShowFlagsVolumeButtons;

typedef enum
{
  SHOW_FLAGS_VOLUME_MENU_CONFIGURE_FSTAB       = (1<<0),
  SHOW_FLAGS_VOLUME_MENU_CONFIGURE_CRYPTTAB    = (1<<1),
  SHOW_FLAGS_VOLUME_MENU_CHANGE_PASSPHRASE     = (1<<2),
  SHOW_FLAGS_VOLUME_MENU_EDIT_LABEL            = (1<<3),
  SHOW_FLAGS_VOLUME_MENU_EDIT_PARTITION        = (1<<4),
  SHOW_FLAGS_VOLUME_MENU_FORMAT_VOLUME         = (1<<5),
  SHOW_FLAGS_VOLUME_MENU_CREATE_VOLUME_IMAGE   = (1<<6),
  SHOW_FLAGS_VOLUME_MENU_RESTORE_VOLUME_IMAGE  = (1<<7),
  SHOW_FLAGS_VOLUME_MENU_BENCHMARK             = (1<<8),
  SHOW_FLAGS_VOLUME_MENU_RESIZE                = (1<<9),
  SHOW_FLAGS_VOLUME_MENU_REPAIR                = (1<<10),
  SHOW_FLAGS_VOLUME_MENU_CHECK                 = (1<<11),
} ShowFlagsVolumeMenu;

typedef struct
{
  ShowFlagsDriveButtons      drive_buttons;
  ShowFlagsDriveMenu         drive_menu;
  ShowFlagsVolumeButtons     volume_buttons;
  ShowFlagsVolumeMenu        volume_menu;
} ShowFlags;

/* ---------------------------------------------------------------------------------------------------- */

static void update_all (GduWindow *window, gboolean is_delayed_job_update);

static void on_volume_grid_changed (GduVolumeGrid  *grid,
                                    gpointer        user_data);

static void on_generic_tool_button_clicked (GtkToolButton *button, gpointer user_data);
static void on_partition_create_tool_button_clicked (GtkToolButton *button, gpointer user_data);
static void on_partition_delete_tool_button_clicked (GtkToolButton *button, gpointer user_data);
static void on_mount_tool_button_clicked (GtkToolButton *button, gpointer user_data);
static void on_unmount_tool_button_clicked (GtkToolButton *button, gpointer user_data);
static void on_unlock_tool_button_clicked (GtkToolButton *button, gpointer user_data);
static void on_lock_tool_button_clicked (GtkToolButton *button, gpointer user_data);
static void on_activate_swap_tool_button_clicked (GtkToolButton *button, gpointer user_data);
static void on_deactivate_swap_tool_button_clicked (GtkToolButton *button, gpointer user_data);

static void on_devtab_drive_loop_detach_button_clicked (GtkButton *button, gpointer user_data);
static void on_devtab_drive_eject_button_clicked (GtkButton *button, gpointer user_data);
static void on_devtab_drive_power_off_button_clicked (GtkButton *button, gpointer user_data);

static void on_generic_drive_menu_item_view_smart (GtkMenuItem *menu_item,
                                             gpointer   user_data);
static void on_generic_drive_menu_item_disk_settings (GtkMenuItem *menu_item,
                                                      gpointer   user_data);
static void on_generic_drive_menu_item_standby_now (GtkMenuItem *menu_item,
                                                    gpointer   user_data);
static void on_generic_drive_menu_item_resume_now (GtkMenuItem *menu_item,
                                                   gpointer   user_data);
static void on_generic_drive_menu_item_power_off (GtkMenuItem *menu_item,
                                                  gpointer   user_data);
static void on_generic_drive_menu_item_format_disk (GtkMenuItem *menu_item,
                                              gpointer   user_data);
static void on_generic_drive_menu_item_create_disk_image (GtkMenuItem *menu_item,
                                                          gpointer   user_data);
static void on_generic_drive_menu_item_restore_disk_image (GtkMenuItem *menu_item,
                                                           gpointer   user_data);
static void on_generic_drive_menu_item_benchmark (GtkMenuItem *menu_item,
                                                  gpointer   user_data);

static void on_generic_menu_item_configure_fstab (GtkMenuItem *menu_item,
                                                  gpointer   user_data);
static void on_generic_menu_item_configure_crypttab (GtkMenuItem *menu_item,
                                                     gpointer   user_data);
static void on_generic_menu_item_change_passphrase (GtkMenuItem *menu_item,
                                                    gpointer   user_data);

#ifdef HAVE_UDISKS2_7_2
static void on_generic_menu_item_resize (GtkMenuItem *menu_item,
                                         gpointer     user_data);
static void on_generic_menu_item_repair (GtkMenuItem *menu_item,
                                         gpointer     user_data);
static void on_generic_menu_item_check (GtkMenuItem *menu_item,
                                        gpointer     user_data);
#endif

static void on_generic_menu_item_edit_label (GtkMenuItem *menu_item,
                                             gpointer   user_data);
static void on_generic_menu_item_edit_partition (GtkMenuItem *menu_item,
                                                 gpointer   user_data);
static void on_generic_menu_item_format_volume (GtkMenuItem *menu_item,
                                                gpointer   user_data);
static void on_generic_menu_item_create_volume_image (GtkMenuItem *menu_item,
                                                      gpointer   user_data);
static void on_generic_menu_item_restore_volume_image (GtkMenuItem *menu_item,
                                                       gpointer   user_data);
static void on_generic_menu_item_benchmark (GtkMenuItem *menu_item,
                                            gpointer   user_data);

static void on_devtab_loop_autoclear_switch_notify_active (GObject    *object,
                                                           GParamSpec *pspec,
                                                           gpointer    user_data);

static void on_drive_job_cancel_button_clicked (GtkButton *button,
                                                gpointer   user_data);

static void on_job_cancel_button_clicked (GtkButton     *button,
                                          gpointer       user_data);

static gboolean on_activate_link (GtkLabel    *label,
                                  const gchar *uri,
                                  gpointer     user_data);

G_DEFINE_TYPE (GduWindow, gdu_window, GTK_TYPE_APPLICATION_WINDOW);

static void
gdu_window_init (GduWindow *window)
{
}

static void on_client_changed (UDisksClient  *client,
                               gpointer       user_data);

static
gboolean
on_delete_event (GtkWidget *widget,
                 GdkEvent  *event,
                 gpointer user_data)
{
  return !gdu_application_should_exit (GDU_WINDOW (widget)->application);
}

static void
gdu_window_finalize (GObject *object)
{
  GduWindow *window = GDU_WINDOW (object);

  gtk_window_remove_mnemonic (GTK_WINDOW (window),
                              'd',
                              window->device_tree_treeview);

  g_signal_handlers_disconnect_by_func (window->client,
                                        G_CALLBACK (on_client_changed),
                                        window);

  if (window->current_object != NULL)
    g_object_unref (window->current_object);

  g_object_unref (window->builder);
  g_object_unref (window->model);
  g_object_unref (window->client);
  g_object_unref (window->application);

  G_OBJECT_CLASS (gdu_window_parent_class)->finalize (object);
}

static gboolean
dont_select_headings (GtkTreeSelection *selection,
                      GtkTreeModel     *model,
                      GtkTreePath      *path,
                      gboolean          selected,
                      gpointer          data)
{
  GtkTreeIter iter;
  gboolean is_heading;

  gtk_tree_model_get_iter (model,
                           &iter,
                           path);
  gtk_tree_model_get (model,
                      &iter,
                      GDU_DEVICE_TREE_MODEL_COLUMN_IS_HEADING,
                      &is_heading,
                      -1);

  return !is_heading;
}

static void
on_row_inserted (GtkTreeModel *tree_model,
                 GtkTreePath  *path,
                 GtkTreeIter  *iter,
                 gpointer      user_data)
{
  GduWindow *window = GDU_WINDOW (user_data);
  gtk_tree_view_expand_all (GTK_TREE_VIEW (window->device_tree_treeview));
}

static void
update_for_show_flags (GduWindow *window,
                       ShowFlags *show_flags)
{
  gtk_widget_set_visible (window->devtab_drive_loop_detach_button,
                          show_flags->drive_buttons & SHOW_FLAGS_DRIVE_BUTTONS_LOOP_DETACH);
  gtk_widget_set_visible (window->devtab_drive_eject_button,
                          show_flags->drive_buttons & SHOW_FLAGS_DRIVE_BUTTONS_EJECT);
  gtk_widget_set_visible (window->devtab_drive_power_off_button,
                          show_flags->drive_buttons & SHOW_FLAGS_DRIVE_BUTTONS_POWER_OFF);

  gtk_widget_set_visible (window->toolbutton_partition_create,
                          show_flags->volume_buttons & SHOW_FLAGS_VOLUME_BUTTONS_PARTITION_CREATE);
  gtk_widget_set_visible (window->toolbutton_partition_delete,
                          show_flags->volume_buttons & SHOW_FLAGS_VOLUME_BUTTONS_PARTITION_DELETE);
  gtk_widget_set_visible (window->toolbutton_unmount,
                          show_flags->volume_buttons & SHOW_FLAGS_VOLUME_BUTTONS_UNMOUNT);
  gtk_widget_set_visible (window->toolbutton_mount,
                          show_flags->volume_buttons & SHOW_FLAGS_VOLUME_BUTTONS_MOUNT);
  gtk_widget_set_visible (window->toolbutton_activate_swap,
                          show_flags->volume_buttons & SHOW_FLAGS_VOLUME_BUTTONS_ACTIVATE_SWAP);
  gtk_widget_set_visible (window->toolbutton_deactivate_swap,
                          show_flags->volume_buttons & SHOW_FLAGS_VOLUME_BUTTONS_DEACTIVATE_SWAP);
  gtk_widget_set_visible (window->toolbutton_unlock,
                          show_flags->volume_buttons & SHOW_FLAGS_VOLUME_BUTTONS_ENCRYPTED_UNLOCK);
  gtk_widget_set_visible (window->toolbutton_lock,
                          show_flags->volume_buttons & SHOW_FLAGS_VOLUME_BUTTONS_ENCRYPTED_LOCK);

  gtk_widget_set_sensitive (GTK_WIDGET (window->generic_drive_menu_item_format_disk),
                            show_flags->drive_menu & SHOW_FLAGS_DRIVE_MENU_FORMAT_DISK);
  gtk_widget_set_sensitive (GTK_WIDGET (window->generic_drive_menu_item_view_smart),
                            show_flags->drive_menu & SHOW_FLAGS_DRIVE_MENU_VIEW_SMART);
  gtk_widget_set_sensitive (GTK_WIDGET (window->generic_drive_menu_item_disk_settings),
                            show_flags->drive_menu & SHOW_FLAGS_DRIVE_MENU_DISK_SETTINGS);
  gtk_widget_set_sensitive (GTK_WIDGET (window->generic_drive_menu_item_create_disk_image),
                            show_flags->drive_menu & SHOW_FLAGS_DRIVE_MENU_CREATE_DISK_IMAGE);
  gtk_widget_set_sensitive (GTK_WIDGET (window->generic_drive_menu_item_restore_disk_image),
                            show_flags->drive_menu & SHOW_FLAGS_DRIVE_MENU_RESTORE_DISK_IMAGE);
  gtk_widget_set_sensitive (GTK_WIDGET (window->generic_drive_menu_item_benchmark),
                            show_flags->drive_menu & SHOW_FLAGS_DRIVE_MENU_BENCHMARK);

  gtk_widget_set_sensitive (GTK_WIDGET (window->generic_menu_item_configure_fstab),
                            show_flags->volume_menu & SHOW_FLAGS_VOLUME_MENU_CONFIGURE_FSTAB);
  gtk_widget_set_sensitive (GTK_WIDGET (window->generic_menu_item_configure_crypttab),
                            show_flags->volume_menu & SHOW_FLAGS_VOLUME_MENU_CONFIGURE_CRYPTTAB);
  gtk_widget_set_sensitive (GTK_WIDGET (window->generic_menu_item_change_passphrase),
                            show_flags->volume_menu & SHOW_FLAGS_VOLUME_MENU_CHANGE_PASSPHRASE);
  gtk_widget_set_sensitive (GTK_WIDGET (window->generic_menu_item_resize),
                            show_flags->volume_menu & SHOW_FLAGS_VOLUME_MENU_RESIZE);
  gtk_widget_set_sensitive (GTK_WIDGET (window->generic_menu_item_repair),
                            show_flags->volume_menu & SHOW_FLAGS_VOLUME_MENU_REPAIR);
  gtk_widget_set_sensitive (GTK_WIDGET (window->generic_menu_item_check),
                            show_flags->volume_menu & SHOW_FLAGS_VOLUME_MENU_CHECK);
  gtk_widget_set_sensitive (GTK_WIDGET (window->generic_menu_item_edit_label),
                            show_flags->volume_menu & SHOW_FLAGS_VOLUME_MENU_EDIT_LABEL);
  gtk_widget_set_sensitive (GTK_WIDGET (window->generic_menu_item_edit_partition),
                            show_flags->volume_menu & SHOW_FLAGS_VOLUME_MENU_EDIT_PARTITION);
  gtk_widget_set_sensitive (GTK_WIDGET (window->generic_menu_item_format_volume),
                            show_flags->volume_menu & SHOW_FLAGS_VOLUME_MENU_FORMAT_VOLUME);
  gtk_widget_set_sensitive (GTK_WIDGET (window->generic_menu_item_create_volume_image),
                            show_flags->volume_menu & SHOW_FLAGS_VOLUME_MENU_CREATE_VOLUME_IMAGE);
  gtk_widget_set_sensitive (GTK_WIDGET (window->generic_menu_item_restore_volume_image),
                            show_flags->volume_menu & SHOW_FLAGS_VOLUME_MENU_RESTORE_VOLUME_IMAGE);
  gtk_widget_set_sensitive (GTK_WIDGET (window->generic_menu_item_benchmark),
                            show_flags->volume_menu & SHOW_FLAGS_VOLUME_MENU_BENCHMARK);
  /* TODO: don't show the button bringing up the popup menu if it has no items */
}

static gboolean
select_object (GduWindow    *window,
               UDisksObject *object)
{
  gboolean is_delayed_job_update = FALSE;
  gboolean ret = FALSE;
  GtkTreeIter iter;

  if (object != NULL && gdu_device_tree_model_get_iter_for_object (window->model, object, &iter))
    {
      GtkTreePath *path;
      GtkTreeSelection *tree_selection;
      tree_selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (window->device_tree_treeview));
      gtk_tree_selection_select_iter (tree_selection, &iter);
      path = gtk_tree_model_get_path (GTK_TREE_MODEL (window->model), &iter);
      gtk_tree_view_set_cursor (GTK_TREE_VIEW (window->device_tree_treeview),
                                path,
                                NULL,
                                FALSE);
      gtk_tree_path_free (path);
      ret = TRUE;
    }
  else if (object != NULL)
    {
      g_warning ("Cannot display object with object path %s",
                 g_dbus_object_get_object_path (G_DBUS_OBJECT (object)));
      goto out;
    }

  if (window->current_object != object)
    {
      if (window->current_object != NULL)
        g_object_unref (window->current_object);
      window->current_object = object != NULL ? g_object_ref (object) : NULL;
      if (window->delay_job_update_id != 0)
        {
          g_source_remove (window->delay_job_update_id);
          window->delay_job_update_id = 0;
          is_delayed_job_update = TRUE;
        }
    }

  update_all (window, is_delayed_job_update);

 out:
  return ret;
}

static gboolean
ensure_something_selected_foreach_cb (GtkTreeModel  *model,
                                      GtkTreePath   *path,
                                      GtkTreeIter   *iter,
                                      gpointer       user_data)
{
  UDisksObject **object = user_data;
  gtk_tree_model_get (model, iter,
                      GDU_DEVICE_TREE_MODEL_COLUMN_OBJECT, object,
                      -1);
  if (*object != NULL)
    return TRUE;
  return FALSE;
}

static void
ensure_something_selected (GduWindow *window)
{
  UDisksObject *object = NULL;
  gtk_tree_model_foreach (GTK_TREE_MODEL (window->model),
                          ensure_something_selected_foreach_cb,
                          &object);
  if (object != NULL)
    {
      select_object (window, object);
      g_object_unref (object);
    }
}

static void
on_tree_selection_changed (GtkTreeSelection *tree_selection,
                           gpointer          user_data)
{
  GduWindow *window = GDU_WINDOW (user_data);
  GtkTreeIter iter;
  GtkTreeModel *model;

  if (gtk_tree_selection_get_selected (tree_selection, &model, &iter))
    {
      UDisksObject *object;
      gtk_tree_model_get (model,
                          &iter,
                          GDU_DEVICE_TREE_MODEL_COLUMN_OBJECT,
                          &object,
                          -1);
      select_object (window, object);
      g_object_unref (object);
    }
  else
    {
      select_object (window, NULL);
      ensure_something_selected (window);
    }
}

/* ---------------------------------------------------------------------------------------------------- */

static void
loop_delete_cb (UDisksLoop   *loop,
                GAsyncResult *res,
                gpointer      user_data)
{
  GduWindow *window = GDU_WINDOW (user_data);
  GError *error;

  error = NULL;
  if (!udisks_loop_call_delete_finish (loop, res, &error))
    {
      gdu_utils_show_error (GTK_WINDOW (window),
                            _("Error deleting loop device"),
                            error);
      g_error_free (error);
    }
  g_object_unref (window);
}

static void
loop_delete_ensure_unused_cb (GduWindow     *window,
                              GAsyncResult  *res,
                              gpointer       user_data)
{
  UDisksObject *object = UDISKS_OBJECT (user_data);
  if (gdu_window_ensure_unused_finish (window, res, NULL))
    {
      UDisksBlock *block;
      UDisksLoop *loop;

      udisks_client_settle (window->client);

      block = udisks_object_peek_block (object);
      loop = udisks_object_peek_loop (object);
      if (loop != NULL)
        {
          /* Could be that the loop device is using Auto-clear so
           * already detached because we just did ensure_unused() on
           * it
           */
          if (block != NULL && udisks_block_get_size (block) > 0)
            {
              GVariantBuilder options_builder;
              g_variant_builder_init (&options_builder, G_VARIANT_TYPE_VARDICT);
              udisks_loop_call_delete (loop,
                                       g_variant_builder_end (&options_builder),
                                       NULL, /* GCancellable */
                                       (GAsyncReadyCallback) loop_delete_cb,
                                       g_object_ref (window));
            }
        }
      else
        {
          g_warning ("no loop interface");
        }
    }
  g_object_unref (object);
}


static void
on_devtab_drive_loop_detach_button_clicked (GtkButton *button,
                                            gpointer   user_data)
{
  GduWindow *window = GDU_WINDOW (user_data);
  gdu_window_ensure_unused (window,
                            window->current_object,
                            (GAsyncReadyCallback) loop_delete_ensure_unused_cb,
                            NULL, /* GCancellable */
                            g_object_ref (window->current_object));
}

/* ---------------------------------------------------------------------------------------------------- */

typedef struct
{
  GduWindow *window;
  gchar *filename;
} LoopSetupData;

static LoopSetupData *
loop_setup_data_new (GduWindow   *window,
                     const gchar *filename)
{
  LoopSetupData *data;
  data = g_slice_new0 (LoopSetupData);
  data->window = g_object_ref (window);
  data->filename = g_strdup (filename);
  return data;
}

static void
loop_setup_data_free (LoopSetupData *data)
{
  g_object_unref (data->window);
  g_free (data->filename);
  g_slice_free (LoopSetupData, data);
}

static void
loop_setup_cb (UDisksManager  *manager,
               GAsyncResult   *res,
               gpointer        user_data)
{
  LoopSetupData *data = user_data;
  gchar *out_loop_device_object_path;
  GError *error;

  error = NULL;
  if (!udisks_manager_call_loop_setup_finish (manager, &out_loop_device_object_path, NULL, res, &error))
    {
      gdu_utils_show_error (GTK_WINDOW (data->window),
                            _("Error attaching disk image"),
                            error);
      g_error_free (error);
    }
  else
    {
      gchar *uri;
      UDisksObject *object;

      /* This is to make it appear in the file chooser's "Recently Used" list */
      uri = g_strdup_printf ("file://%s", data->filename);
      gtk_recent_manager_add_item (gtk_recent_manager_get_default (), uri);
      g_free (uri);

      udisks_client_settle (data->window->client);

      object = udisks_client_get_object (data->window->client, out_loop_device_object_path);
      select_object (data->window, object);
      g_object_unref (object);
      g_free (out_loop_device_object_path);
    }

  loop_setup_data_free (data);
}

/* ---------------------------------------------------------------------------------------------------- */

gboolean
gdu_window_attach_disk_image_helper (GduWindow *window, gchar *filename, gboolean readonly)
{
  gint fd = -1;
  GUnixFDList *fd_list;
  GVariantBuilder options_builder;
  fd = open (filename, O_RDWR);
  if (fd == -1)
    fd = open (filename, O_RDONLY);
  if (fd == -1)
    {
      GError *error;
      error = g_error_new (G_IO_ERROR,
                           g_io_error_from_errno (errno),
                           "%s", strerror (errno));
      gdu_utils_show_error (GTK_WINDOW (window),
                            _("Error attaching disk image"),
                            error);
      g_error_free (error);
      return FALSE;
    }
  g_variant_builder_init (&options_builder, G_VARIANT_TYPE_VARDICT);
  if (readonly)
    g_variant_builder_add (&options_builder, "{sv}", "read-only", g_variant_new_boolean (TRUE));
  fd_list = g_unix_fd_list_new_from_array (&fd, 1); /* adopts the fd */
  udisks_manager_call_loop_setup (udisks_client_get_manager (window->client),
                                  g_variant_new_handle (0),
                                  g_variant_builder_end (&options_builder),
                                  fd_list,
                                  NULL,                       /* GCancellable */
                                  (GAsyncReadyCallback) loop_setup_cb,
                                  loop_setup_data_new (window, filename));
  g_object_unref (fd_list);
  return TRUE;
}

/* ---------------------------------------------------------------------------------------------------- */

void
gdu_window_show_attach_disk_image (GduWindow *window)
{
  GtkWidget *dialog;
  GFile *folder = NULL;
  gchar *filename = NULL;
  GtkWidget *ro_checkbutton;

  dialog = gtk_file_chooser_dialog_new (_("Select Disk Image to Attach"),
                                        GTK_WINDOW (window),
                                        GTK_FILE_CHOOSER_ACTION_OPEN,
                                        _("_Cancel"), GTK_RESPONSE_CANCEL,
                                        _("_Attach"), GTK_RESPONSE_ACCEPT,
                                        NULL);
  gdu_utils_configure_file_chooser_for_disk_images (GTK_FILE_CHOOSER (dialog),
                                                    TRUE,   /* set file types */
                                                    FALSE); /* allow_compressed */

  /* Add a RO check button that defaults to RO */
  ro_checkbutton = gtk_check_button_new_with_mnemonic (_("Set up _read-only loop device"));
  gtk_widget_set_tooltip_markup (ro_checkbutton, _("If checked, the loop device will be read-only. This is useful if you don’t want the underlying file to be modified"));
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (ro_checkbutton), TRUE);
  gtk_file_chooser_set_extra_widget (GTK_FILE_CHOOSER (dialog), ro_checkbutton);

  //gtk_widget_show_all (dialog);
  if (gtk_dialog_run (GTK_DIALOG (dialog)) != GTK_RESPONSE_ACCEPT)
    goto out;

  filename = gtk_file_chooser_get_filename (GTK_FILE_CHOOSER (dialog));
  gtk_widget_hide (dialog);

  if (!gdu_window_attach_disk_image_helper (window, filename, gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (ro_checkbutton))))
    goto out;

  /* now that we know the user picked a folder, update file chooser settings */
  folder = gtk_file_chooser_get_current_folder_file (GTK_FILE_CHOOSER (dialog));
  gdu_utils_file_chooser_for_disk_images_set_default_folder (folder);

 out:
  gtk_widget_destroy (dialog);
  g_free (filename);
  g_clear_object (&folder);
}


/* ---------------------------------------------------------------------------------------------------- */


static void
init_css (GduWindow *window)
{
  GtkCssProvider *provider;
  GFile *file;
  GError *error;

  provider = gtk_css_provider_new ();
  file = g_file_new_for_uri ("resource:///org/gnome/Disks/ui/gdu.css");
  error = NULL;
  if (!gtk_css_provider_load_from_file (provider, file, &error))
    {
      g_warning ("Can’t parse custom CSS: %s\n", error->message);
      g_error_free (error);
      goto out;
    }

  gtk_style_context_add_provider_for_screen (gtk_widget_get_screen (GTK_WIDGET (window)),
                                             GTK_STYLE_PROVIDER (provider),
                                             GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);

  g_object_unref (file);
  g_object_unref (provider);

 out:
  ;
}


static gint
device_sort_function (GtkTreeModel *model,
                      GtkTreeIter *a,
                      GtkTreeIter *b,
                      gpointer user_data)
{
  gchar *sa, *sb;
  gint ret;

  gtk_tree_model_get (model, a,
                      GDU_DEVICE_TREE_MODEL_COLUMN_SORT_KEY, &sa,
                      -1);
  gtk_tree_model_get (model, b,
                      GDU_DEVICE_TREE_MODEL_COLUMN_SORT_KEY, &sb,
                      -1);
  ret = g_strcmp0 (sa, sb);
  g_free (sa);
  g_free (sb);
  return ret;
}

gboolean
gdu_window_select_object (GduWindow    *window,
                          UDisksObject *object)
{
  gboolean ret = FALSE;
  UDisksPartition *partition;
  UDisksPartitionTable *table = NULL;
  UDisksDrive *drive = NULL;

  drive = udisks_object_peek_drive (object);
  if (drive != NULL)
    {
      select_object (window, object);
      goto out;
    }

  partition = udisks_object_peek_partition (object);

  /* if it's a partition, first select the object with the partition table */
  if (partition != NULL)
    {
      UDisksObject *table_object = NULL;

      table = udisks_client_get_partition_table (window->client, partition);
      if (table == NULL)
        goto out;
      table_object = (UDisksObject *) g_dbus_interface_get_object (G_DBUS_INTERFACE (table));
      if (table_object == NULL)
        goto out;

      if (gdu_window_select_object (window, table_object))
        {
          /* then select the partition */
          if (!gdu_volume_grid_select_object (GDU_VOLUME_GRID (window->volume_grid), object))
            {
              g_warning ("Error selecting partition %s", g_dbus_object_get_object_path (G_DBUS_OBJECT (object)));
            }
          else
            {
              ret = TRUE;
            }
        }
    }
  else
    {
      UDisksBlock *block;

      block = udisks_object_peek_block (object);
      if (block != NULL)
        {
          /* OK, if not a partition, either select the drive (if available) or the block device itself */
          drive = udisks_client_get_drive_for_block (window->client, block);
          if (drive != NULL)
            {
              UDisksObject *drive_object = NULL;
              drive_object = (UDisksObject *) g_dbus_interface_get_object (G_DBUS_INTERFACE (drive));
              if (drive_object != NULL)
                {
                  if (!select_object (window, drive_object))
                    goto out;
                  ret = TRUE;
                }
            }
          else
            {
              if (!select_object (window, object))
                goto out;
              ret = TRUE;
            }
        }
    }

 out:
  g_clear_object (&drive);
  g_clear_object (&table);
  return ret;
}

static void
power_state_cell_func (GtkTreeViewColumn *column,
                       GtkCellRenderer   *renderer,
                       GtkTreeModel      *model,
                       GtkTreeIter       *iter,
                       gpointer           user_data)
{
  gboolean visible = FALSE;
  GduPowerStateFlags flags;
  GduWindow *window = GDU_WINDOW (user_data);

  gtk_tree_model_get (model,
                      iter,
                      GDU_DEVICE_TREE_MODEL_COLUMN_POWER_STATE_FLAGS, &flags,
                      -1);

  if (flags & GDU_POWER_STATE_FLAGS_STANDBY)
    visible = TRUE;

  gtk_cell_renderer_set_visible (renderer, visible);
  update_all (window, FALSE);
}

/* TODO: load from .ui file */
static GtkWidget *
create_header (GduWindow *window)
{
  GtkWidget *header;
  GtkWidget *button;
  GtkWidget *image;

  header = gtk_header_bar_new ();
  gtk_header_bar_set_show_close_button (GTK_HEADER_BAR (header), TRUE);

  button = window->devtab_drive_generic_button = gtk_menu_button_new ();
  gtk_menu_button_set_popup (GTK_MENU_BUTTON (button), window->generic_drive_menu);
  gtk_menu_button_set_direction (GTK_MENU_BUTTON (button), GTK_ARROW_NONE);
  gtk_header_bar_pack_end (GTK_HEADER_BAR (header), button);

  button = window->devtab_drive_power_off_button = gtk_button_new ();
  image = gtk_image_new_from_icon_name ("system-shutdown-symbolic", GTK_ICON_SIZE_MENU);
  gtk_button_set_image (GTK_BUTTON (button), image);
  gtk_widget_set_tooltip_text (button, _("Power off this disk"));
  gtk_header_bar_pack_end (GTK_HEADER_BAR (header), button);

  button = window->devtab_drive_eject_button = gtk_button_new ();
  image = gtk_image_new_from_icon_name ("media-eject-symbolic", GTK_ICON_SIZE_MENU);
  gtk_button_set_image (GTK_BUTTON (button), image);
  gtk_widget_set_tooltip_text (button, _("Eject this disk"));
  gtk_header_bar_pack_end (GTK_HEADER_BAR (header), button);

  button = window->devtab_drive_loop_detach_button = gtk_button_new ();
  image = gtk_image_new_from_icon_name ("list-remove-symbolic", GTK_ICON_SIZE_MENU);
  gtk_button_set_image (GTK_BUTTON (button), image);
  gtk_widget_set_tooltip_text (button, _("Detach this loop device"));
  gtk_header_bar_pack_end (GTK_HEADER_BAR (header), button);

  return header;
}

static gboolean
in_desktop (const gchar *name)
{
    const gchar *desktop_name_list;
    gchar **names;
    gboolean in_list = FALSE;
    gint i;

    desktop_name_list = g_getenv ("XDG_CURRENT_DESKTOP");
    if (!desktop_name_list)
        return FALSE;

    names = g_strsplit (desktop_name_list, ":", -1);
    for (i = 0; names[i] && !in_list; i++)
      {
        if (strcmp (names[i], name) == 0)
            in_list = TRUE;
      }
    g_strfreev (names);

    return in_list;
}

static void
gdu_window_constructed (GObject *object)
{
  GduWindow *window = GDU_WINDOW (object);
  guint key;
  GdkModifierType mod;
  GtkAccelGroup *accelgroup;
  GtkTreeViewColumn *column;
  GtkCellRenderer *renderer;
  GtkTreeSelection *selection;
  GtkStyleContext *context;
  GList *children, *l;
  guint n;

  init_css (window);

  /* chain up */
  if (G_OBJECT_CLASS (gdu_window_parent_class)->constructed != NULL)
    G_OBJECT_CLASS (gdu_window_parent_class)->constructed (object);

  gtk_window_set_icon_name (GTK_WINDOW (window), "gnome-disks");

  /* load UI file */
  gdu_application_new_widget (window->application, "disks.ui", NULL, &window->builder);

  /* set up widgets */
  for (n = 0; widget_mapping[n].name != NULL; n++)
    {
      gpointer *p = (gpointer *) ((char *) window + widget_mapping[n].offset);
      *p = G_OBJECT (gtk_builder_get_object (window->builder, widget_mapping[n].name));
      g_warn_if_fail (*p != NULL);
    }

  window->has_drive_job = FALSE;
  window->has_volume_job = FALSE;
  window->delay_job_update_id = 0;

  window->header = create_header (window);
  if (!in_desktop ("Unity"))
      gtk_window_set_titlebar (GTK_WINDOW (window), window->header);
  else
    {
      gtk_box_pack_start (GTK_BOX (window->main_box),
                          GTK_WIDGET (window->header),
                          FALSE, TRUE, 0);
      gtk_header_bar_set_show_close_button (GTK_HEADER_BAR (window->header),
                                            FALSE);
      context = gtk_widget_get_style_context (GTK_WIDGET (window->header));
      gtk_style_context_remove_class (context, "header-bar");
      gtk_style_context_add_class (context, "toolbar");
      gtk_style_context_add_class (context, "primary-toolbar");
    }

  gtk_widget_show_all (window->header);

  g_object_ref (window->main_box);
  gtk_container_remove (GTK_CONTAINER (gtk_widget_get_parent (window->main_box)), window->main_box);
  gtk_container_add (GTK_CONTAINER (window), window->main_box);
  g_object_unref (window->main_box);
  gtk_window_set_title (GTK_WINDOW (window), _("Disks"));
  gtk_window_set_default_size (GTK_WINDOW (window), 800, 700);
  gtk_window_set_position (GTK_WINDOW (window), GTK_WIN_POS_CENTER);

  /* set up mnemonic */
  gtk_window_add_mnemonic (GTK_WINDOW (window),
                           'd',
                           window->device_tree_treeview);

  /* hide all children in the devtab list, otherwise the dialog is going to be huge by default */
  children = gtk_container_get_children (GTK_CONTAINER (window->devtab_drive_table));
  for (l = children; l != NULL; l = l->next)
    {
      gtk_widget_hide (GTK_WIDGET (l->data));
      gtk_widget_set_no_show_all (GTK_WIDGET (l->data), TRUE);
    }
  g_list_free (children);
  children = gtk_container_get_children (GTK_CONTAINER (window->devtab_table));
  for (l = children; l != NULL; l = l->next)
    {
      gtk_widget_hide (GTK_WIDGET (l->data));
      gtk_widget_set_no_show_all (GTK_WIDGET (l->data), TRUE);
    }

  gtk_notebook_set_show_tabs (GTK_NOTEBOOK (window->details_notebook), FALSE);
  gtk_notebook_set_show_border (GTK_NOTEBOOK (window->details_notebook), FALSE);

  context = gtk_widget_get_style_context (window->device_tree_scrolledwindow);
  gtk_style_context_set_junction_sides (context, GTK_JUNCTION_TOP);

  window->model = gdu_device_tree_model_new (window->application,
                                             GDU_DEVICE_TREE_MODEL_FLAGS_UPDATE_POWER_STATE |
                                             GDU_DEVICE_TREE_MODEL_FLAGS_UPDATE_PULSE |
                                             GDU_DEVICE_TREE_MODEL_FLAGS_FLAT);

  gtk_tree_view_set_model (GTK_TREE_VIEW (window->device_tree_treeview), GTK_TREE_MODEL (window->model));
  gtk_tree_sortable_set_sort_column_id (GTK_TREE_SORTABLE (window->model),
                                        GDU_DEVICE_TREE_MODEL_COLUMN_SORT_KEY,
                                        GTK_SORT_ASCENDING);
  /* Force g_strcmp0() as the sort function otherwise ___aa won't come before ____b ... */
  gtk_tree_sortable_set_sort_func (GTK_TREE_SORTABLE (window->model),
                                   GDU_DEVICE_TREE_MODEL_COLUMN_SORT_KEY,
                                   device_sort_function,
                                   NULL, /* user_data */
                                   NULL); /* GDestroyNotify */

  selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (window->device_tree_treeview));
  gtk_tree_selection_set_select_function (selection, dont_select_headings, NULL, NULL);
  g_signal_connect (selection,
                    "changed",
                    G_CALLBACK (on_tree_selection_changed),
                    window);

  /* -------------------- */

  column = gtk_tree_view_column_new ();
  gtk_tree_view_column_set_expand (column, TRUE);
  gtk_tree_view_append_column (GTK_TREE_VIEW (window->device_tree_treeview), column);

  renderer = gtk_cell_renderer_text_new ();
  gtk_tree_view_column_pack_start (column, renderer, FALSE);
  gtk_tree_view_column_set_attributes (column,
                                       renderer,
                                       "markup", GDU_DEVICE_TREE_MODEL_COLUMN_HEADING_TEXT,
                                       "visible", GDU_DEVICE_TREE_MODEL_COLUMN_IS_HEADING,
                                       NULL);

  renderer = gtk_cell_renderer_pixbuf_new ();
  g_object_set (G_OBJECT (renderer),
                "stock-size", GTK_ICON_SIZE_DND,
                NULL);
  gtk_tree_view_column_pack_start (column, renderer, FALSE);
  gtk_tree_view_column_set_attributes (column,
                                       renderer,
                                       "gicon", GDU_DEVICE_TREE_MODEL_COLUMN_ICON,
                                       NULL);
  renderer = gtk_cell_renderer_text_new ();
  g_object_set (G_OBJECT (renderer),
                "ellipsize", PANGO_ELLIPSIZE_MIDDLE,
                NULL);
  gtk_tree_view_column_pack_start (column, renderer, FALSE);
  gtk_tree_view_column_set_attributes (column,
                                       renderer,
                                       "markup", GDU_DEVICE_TREE_MODEL_COLUMN_NAME,
                                       NULL);
  renderer = gtk_cell_renderer_spinner_new ();
  g_object_set (G_OBJECT (renderer),
                "xalign", 1.0,
                NULL);
  gtk_tree_view_column_pack_end (column, renderer, FALSE);
  gtk_tree_view_column_set_attributes (column,
                                       renderer,
                                       "visible", GDU_DEVICE_TREE_MODEL_COLUMN_JOBS_RUNNING,
                                       "active", GDU_DEVICE_TREE_MODEL_COLUMN_JOBS_RUNNING,
                                       "pulse", GDU_DEVICE_TREE_MODEL_COLUMN_PULSE,
                                       NULL);
  renderer = gtk_cell_renderer_pixbuf_new ();
  g_object_set (G_OBJECT (renderer),
                "xalign", 1.0,
                "stock-size", GTK_ICON_SIZE_MENU,
                "icon-name", "gnome-disks-state-standby-symbolic",
                NULL);
  gtk_tree_view_column_pack_end (column, renderer, FALSE);

  gtk_tree_view_column_set_cell_data_func (column,
                                           renderer,
                                           power_state_cell_func,
                                           window,
                                           NULL); /* user_data GDestroyNotify */

  /* -------------------- */

  /* expand on insertion - hmm, I wonder if there's an easier way to do this */
  g_signal_connect (window->model,
                    "row-inserted",
                    G_CALLBACK (on_row_inserted),
                    window);
  gtk_tree_view_expand_all (GTK_TREE_VIEW (window->device_tree_treeview));

  g_signal_connect (window->client,
                    "changed",
                    G_CALLBACK (on_client_changed),
                    window);

  /* set up non-standard widgets that isn't in the .ui file */

  window->volume_grid = gdu_volume_grid_new (window->application);
  gtk_widget_show (window->volume_grid);
  gtk_box_pack_start (GTK_BOX (window->devtab_grid_hbox),
                      window->volume_grid,
                      TRUE, TRUE, 0);
  gtk_label_set_mnemonic_widget (GTK_LABEL (window->devtab_volumes_label),
                                 window->volume_grid);
  g_signal_connect (window->volume_grid,
                    "changed",
                    G_CALLBACK (on_volume_grid_changed),
                    window);

  context = gtk_widget_get_style_context (window->devtab_grid_toolbar);
  gtk_widget_set_name (window->devtab_grid_toolbar, "devtab-grid-toolbar");
  gtk_style_context_set_junction_sides (context, GTK_JUNCTION_TOP);

  /* toolbar buttons */
  g_signal_connect (window->toolbutton_generic_menu,
                    "clicked",
                    G_CALLBACK (on_generic_tool_button_clicked),
                    window);
  g_signal_connect (window->toolbutton_partition_create,
                    "clicked",
                    G_CALLBACK (on_partition_create_tool_button_clicked),
                    window);
  g_signal_connect (window->toolbutton_partition_delete,
                    "clicked",
                    G_CALLBACK (on_partition_delete_tool_button_clicked),
                    window);
  g_signal_connect (window->toolbutton_mount,
                    "clicked",
                    G_CALLBACK (on_mount_tool_button_clicked),
                    window);
  g_signal_connect (window->toolbutton_unmount,
                    "clicked",
                    G_CALLBACK (on_unmount_tool_button_clicked),
                    window);
  g_signal_connect (window->toolbutton_unlock,
                    "clicked",
                    G_CALLBACK (on_unlock_tool_button_clicked),
                    window);
  g_signal_connect (window->toolbutton_lock,
                    "clicked",
                    G_CALLBACK (on_lock_tool_button_clicked),
                    window);
  g_signal_connect (window->toolbutton_activate_swap,
                    "clicked",
                    G_CALLBACK (on_activate_swap_tool_button_clicked),
                    window);
  g_signal_connect (window->toolbutton_deactivate_swap,
                    "clicked",
                    G_CALLBACK (on_deactivate_swap_tool_button_clicked),
                    window);

  /* drive buttons */
  g_signal_connect (window->devtab_drive_loop_detach_button,
                    "clicked",
                    G_CALLBACK (on_devtab_drive_loop_detach_button_clicked),
                    window);
  g_signal_connect (window->devtab_drive_eject_button,
                    "clicked",
                    G_CALLBACK (on_devtab_drive_eject_button_clicked),
                    window);
  g_signal_connect (window->devtab_drive_power_off_button,
                    "clicked",
                    G_CALLBACK (on_devtab_drive_power_off_button_clicked),
                    window);

  /* drive menu */
  g_signal_connect (window->generic_drive_menu_item_view_smart,
                    "activate",
                    G_CALLBACK (on_generic_drive_menu_item_view_smart),
                    window);
  g_signal_connect (window->generic_drive_menu_item_disk_settings,
                    "activate",
                    G_CALLBACK (on_generic_drive_menu_item_disk_settings),
                    window);
  g_signal_connect (window->generic_drive_menu_item_standby_now,
                    "activate",
                    G_CALLBACK (on_generic_drive_menu_item_standby_now),
                    window);
  g_signal_connect (window->generic_drive_menu_item_resume_now,
                    "activate",
                    G_CALLBACK (on_generic_drive_menu_item_resume_now),
                    window);
  g_signal_connect (window->generic_drive_menu_item_power_off,
                    "activate",
                    G_CALLBACK (on_generic_drive_menu_item_power_off),
                    window);
  g_signal_connect (window->generic_drive_menu_item_format_disk,
                    "activate",
                    G_CALLBACK (on_generic_drive_menu_item_format_disk),
                    window);
  g_signal_connect (window->generic_drive_menu_item_create_disk_image,
                    "activate",
                    G_CALLBACK (on_generic_drive_menu_item_create_disk_image),
                    window);
  g_signal_connect (window->generic_drive_menu_item_restore_disk_image,
                    "activate",
                    G_CALLBACK (on_generic_drive_menu_item_restore_disk_image),
                    window);
  g_signal_connect (window->generic_drive_menu_item_benchmark,
                    "activate",
                    G_CALLBACK (on_generic_drive_menu_item_benchmark),
                    window);

  /* volume menu */
  g_signal_connect (window->generic_menu_item_configure_fstab,
                    "activate",
                    G_CALLBACK (on_generic_menu_item_configure_fstab),
                    window);
  g_signal_connect (window->generic_menu_item_configure_crypttab,
                    "activate",
                    G_CALLBACK (on_generic_menu_item_configure_crypttab),
                    window);
  g_signal_connect (window->generic_menu_item_change_passphrase,
                    "activate",
                    G_CALLBACK (on_generic_menu_item_change_passphrase),
                    window);

#ifdef HAVE_UDISKS2_7_2
  g_signal_connect (window->generic_menu_item_resize,
                    "activate",
                    G_CALLBACK (on_generic_menu_item_resize),
                    window);
  g_signal_connect (window->generic_menu_item_repair,
                    "activate",
                    G_CALLBACK (on_generic_menu_item_repair),
                    window);
  g_signal_connect (window->generic_menu_item_check,
                    "activate",
                    G_CALLBACK (on_generic_menu_item_check),
                    window);
#else
  gtk_widget_hide (window->generic_menu_item_resize);
  gtk_widget_hide (window->generic_menu_item_repair);
  gtk_widget_hide (window->generic_menu_item_check);
  gtk_widget_hide (window->generic_menu_item_separator);
#endif

  g_signal_connect (window->generic_menu_item_edit_label,
                    "activate",
                    G_CALLBACK (on_generic_menu_item_edit_label),
                    window);
  g_signal_connect (window->generic_menu_item_edit_partition,
                    "activate",
                    G_CALLBACK (on_generic_menu_item_edit_partition),
                    window);
  g_signal_connect (window->generic_menu_item_format_volume,
                    "activate",
                    G_CALLBACK (on_generic_menu_item_format_volume),
                    window);
  g_signal_connect (window->generic_menu_item_create_volume_image,
                    "activate",
                    G_CALLBACK (on_generic_menu_item_create_volume_image),
                    window);
  g_signal_connect (window->generic_menu_item_restore_volume_image,
                    "activate",
                    G_CALLBACK (on_generic_menu_item_restore_volume_image),
                    window);
  g_signal_connect (window->generic_menu_item_benchmark,
                    "activate",
                    G_CALLBACK (on_generic_menu_item_benchmark),
                    window);

  /* loop's auto-clear switch */
  g_signal_connect (window->devtab_loop_autoclear_switch,
                    "notify::active",
                    G_CALLBACK (on_devtab_loop_autoclear_switch_notify_active),
                    window);

  /* cancel-button for drive job */
  g_signal_connect (window->devtab_drive_job_cancel_button,
                    "clicked",
                    G_CALLBACK (on_drive_job_cancel_button_clicked),
                    window);

  /* cancel-button for job */
  g_signal_connect (window->devtab_job_cancel_button,
                    "clicked",
                    G_CALLBACK (on_job_cancel_button_clicked),
                    window);

  /* GtkLabel instances we need to handle ::activate-link for */
  g_signal_connect (window->devtab_volume_type_value_label,
                    "activate-link",
                    G_CALLBACK (on_activate_link),
                    window);

  g_signal_connect (window,
                    "delete-event",
                    G_CALLBACK (on_delete_event),
                    NULL);

  ensure_something_selected (window);
  gtk_widget_grab_focus (window->device_tree_treeview);
  update_all (window, FALSE);

  /* attach the generic menu to the toplevel window for correct placement */
  gtk_menu_attach_to_widget (GTK_MENU (window->generic_menu),
                             GTK_WIDGET (window),
                             NULL);

  /* TODO: would be better to have all this in the .ui file - no idea
   * why it doesn't work - accelerator support in GTK+ seems extremely
   * confusing and flaky :-(
   */
  accelgroup = gtk_accel_group_new ();
  gtk_window_add_accel_group (GTK_WINDOW (window), accelgroup);

  /* Translators: This is the short-cut to open the disks/drive gear menu */
  gtk_accelerator_parse (C_("accelerator", "F10"), &key, &mod);
  gtk_accel_map_add_entry ("<Disks>/DriveMenu", key, mod);
  gtk_widget_set_accel_path (window->devtab_drive_generic_button, "<Disks>/DriveMenu", accelgroup);

  /* Translators: This is the short-cut to format a disk.
   *              The Ctrl modifier must not be translated or parsing will fail.
   *              You can however change to another English modifier (e.g. <Shift>).
   */
  gtk_accelerator_parse (C_("accelerator", "<Ctrl>F"), &key, &mod);
  gtk_accel_map_add_entry ("<Disks>/DriveMenu/Format", key, mod);
  gtk_widget_set_accel_path (window->generic_drive_menu_item_format_disk, "<Disks>/DriveMenu/Format", accelgroup);

  /* Translators: This is the short-cut to view SMART data for a disk.
   *              The Ctrl modifier must not be translated or parsing will fail.
   *              You can however change to another English modifier (e.g. <Shift>).
   */
  gtk_accelerator_parse (C_("accelerator", "<Ctrl>S"), &key, &mod);
  gtk_accel_map_add_entry ("<Disks>/DriveMenu/ViewSmart", key, mod);
  gtk_widget_set_accel_path (window->generic_drive_menu_item_view_smart, "<Disks>/DriveMenu/ViewSmart", accelgroup);

  /* Translators: This is the short-cut to view the "Drive Settings" dialog for a hard disk.
   *              The Ctrl modifier must not be translated or parsing will fail.
   *              You can however change to another English modifier (e.g. <Shift>).
   */
  gtk_accelerator_parse (C_("accelerator", "<Ctrl>E"), &key, &mod);
  gtk_accel_map_add_entry ("<Disks>/DriveMenu/Settings", key, mod);
  gtk_widget_set_accel_path (window->generic_drive_menu_item_disk_settings, "<Disks>/DriveMenu/Settings", accelgroup);

  /* Translators: This is the short-cut to open the volume gear menu.
   *              The Shift modifier must not be translated or parsing will fail.
   *              You can however change to another English modifier (e.g. <Ctrl>).
   */
  /* TODO: This results in
   *
   * Gtk-CRITICAL **: gtk_widget_set_accel_path: assertion 'GTK_WIDGET_GET_CLASS (widget)->activate_signal != 0' failed
   *
   * so comment it out for now.
   */
#if 0
  gtk_accelerator_parse (C_("accelerator", "<Shift>F10"), &key, &mod);
  gtk_accel_map_add_entry ("<Disks>/VolumeMenu", key, mod);
  gtk_widget_set_accel_path (window->toolbutton_generic_menu, "<Disks>/VolumeMenu", accelgroup);
#endif


  /* Translators: This is the short-cut to format a volume.
   *              The Shift and Ctrl modifiers must not be translated or parsing will fail.
   *              You can however change to other English modifiers.
   */
  gtk_accelerator_parse (C_("accelerator", "<Shift><Ctrl>F"), &key, &mod);
  gtk_accel_map_add_entry ("<Disks>/VolumeMenu/Format", key, mod);
  gtk_widget_set_accel_path (window->generic_menu_item_format_volume, "<Disks>/VolumeMenu/Format", accelgroup);
}

static void
gdu_window_get_property (GObject    *object,
                         guint       prop_id,
                         GValue     *value,
                         GParamSpec *pspec)
{
  GduWindow *window = GDU_WINDOW (object);

  switch (prop_id)
    {
    case PROP_APPLICATION:
      g_value_set_object (value, gdu_window_get_application (window));
      break;

    case PROP_CLIENT:
      g_value_set_object (value, gdu_window_get_client (window));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
gdu_window_set_property (GObject      *object,
                         guint         prop_id,
                         const GValue *value,
                         GParamSpec   *pspec)
{
  GduWindow *window = GDU_WINDOW (object);

  switch (prop_id)
    {
    case PROP_APPLICATION:
      window->application = g_value_dup_object (value);
      break;

    case PROP_CLIENT:
      window->client = g_value_dup_object (value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
gdu_window_class_init (GduWindowClass *klass)
{
  GObjectClass *gobject_class;

  gobject_class = G_OBJECT_CLASS (klass);
  gobject_class->constructed  = gdu_window_constructed;
  gobject_class->finalize     = gdu_window_finalize;
  gobject_class->get_property = gdu_window_get_property;
  gobject_class->set_property = gdu_window_set_property;

  /**
   * GduWindow:application:
   *
   * The #GduApplication for the #GduWindow.
   */
  g_object_class_install_property (gobject_class,
                                   PROP_APPLICATION,
                                   g_param_spec_object ("application",
                                                        "Application",
                                                        "The application for the window",
                                                        GDU_TYPE_APPLICATION,
                                                        G_PARAM_READABLE |
                                                        G_PARAM_WRITABLE |
                                                        G_PARAM_CONSTRUCT_ONLY |
                                                        G_PARAM_STATIC_STRINGS));

  /**
   * GduWindow:client:
   *
   * The #UDisksClient used by the #GduWindow.
   */
  g_object_class_install_property (gobject_class,
                                   PROP_CLIENT,
                                   g_param_spec_object ("client",
                                                        "Client",
                                                        "The client used by the window",
                                                        UDISKS_TYPE_CLIENT,
                                                        G_PARAM_READABLE |
                                                        G_PARAM_WRITABLE |
                                                        G_PARAM_CONSTRUCT_ONLY |
                                                        G_PARAM_STATIC_STRINGS));
}

GduWindow *
gdu_window_new (GduApplication *application,
                UDisksClient   *client)
{
  return GDU_WINDOW (g_object_new (GDU_TYPE_WINDOW,
                                   "application", application,
                                   "client", client,
                                   NULL));
}

GduApplication *
gdu_window_get_application   (GduWindow *window)
{
  g_return_val_if_fail (GDU_IS_WINDOW (window), NULL);
  return window->application;
}

UDisksClient *
gdu_window_get_client (GduWindow *window)
{
  g_return_val_if_fail (GDU_IS_WINDOW (window), NULL);
  return window->client;
}

typedef enum
{
  SET_MARKUP_FLAGS_NONE = 0,
  SET_MARKUP_FLAGS_HYPHEN_IF_EMPTY = (1<<0)
} SetMarkupFlags;

static void
set_markup (GduWindow      *window,
            const gchar    *key_label_id,
            const gchar    *label_id,
            const gchar    *markup,
            SetMarkupFlags  flags)
{
  GtkWidget *key_label;
  GtkWidget *label;

  if (markup == NULL || strlen (markup) == 0)
    {
      if (flags & SET_MARKUP_FLAGS_HYPHEN_IF_EMPTY)
        markup = "—";
      else
        goto out;
    }

  key_label = GTK_WIDGET (gtk_builder_get_object (window->builder, key_label_id));
  label = GTK_WIDGET (gtk_builder_get_object (window->builder, label_id));

  /* TODO: utf-8 validate */

  gtk_label_set_markup (GTK_LABEL (label), markup);
  gtk_widget_show (key_label);
  gtk_widget_show (label);

 out:
  ;
}

static void
set_size (GduWindow      *window,
          const gchar    *key_label_id,
          const gchar    *label_id,
          guint64         size,
          SetMarkupFlags  flags)
{
  gchar *s;
  s = udisks_client_get_size_for_display (window->client, size, FALSE, TRUE);
  set_markup (window, key_label_id, label_id, s, size);
  g_free (s);
}

static void
set_switch (GduWindow      *window,
            const gchar    *key_label_id,
            const gchar    *switch_box_id,
            const gchar    *switch_id,
            gboolean        active,
            gboolean        sensitive)
{
  GtkWidget *key_label;
  GtkWidget *switch_box;
  GtkWidget *switch_;

  key_label = GTK_WIDGET (gtk_builder_get_object (window->builder, key_label_id));
  switch_box = GTK_WIDGET (gtk_builder_get_object (window->builder, switch_box_id));
  switch_ = GTK_WIDGET (gtk_builder_get_object (window->builder, switch_id));

  gtk_switch_set_active (GTK_SWITCH (switch_), active);
  gtk_widget_set_sensitive (switch_, sensitive);
  gtk_widget_show (key_label);
  gtk_widget_show (switch_box);
  gtk_widget_show (switch_);
}

static GList *
get_top_level_blocks_for_drive (GduWindow   *window,
                                const gchar *drive_object_path)
{
  GList *ret;
  GList *l;
  GList *object_proxies;
  GDBusObjectManager *object_manager;

  object_manager = udisks_client_get_object_manager (window->client);
  object_proxies = g_dbus_object_manager_get_objects (object_manager);

  ret = NULL;
  for (l = object_proxies; l != NULL; l = l->next)
    {
      UDisksObject *object = UDISKS_OBJECT (l->data);
      UDisksBlock *block;

      block = udisks_object_peek_block (object);
      if (block == NULL)
        continue;

      if (g_strcmp0 (udisks_block_get_drive (block), drive_object_path) != 0)
        continue;

      if (udisks_object_peek_partition (object) != NULL)
        continue;

      ret = g_list_append (ret, g_object_ref (object));
    }
  g_list_foreach (object_proxies, (GFunc) g_object_unref, NULL);
  g_list_free (object_proxies);
  return ret;
}

static gint
block_compare_on_preferred (UDisksObject *a,
                            UDisksObject *b)
{
  return g_strcmp0 (udisks_block_get_preferred_device (udisks_object_peek_block (a)),
                    udisks_block_get_preferred_device (udisks_object_peek_block (b)));
}

/* ---------------------------------------------------------------------------------------------------- */

static void update_device_page (GduWindow *window, ShowFlags *show_flags, gboolean is_delayed_job_update);

/* Keep in sync with tabs in disks.ui file */
typedef enum
{
  DETAILS_PAGE_NOT_SELECTED,
  DETAILS_PAGE_NOT_IMPLEMENTED,
  DETAILS_PAGE_DEVICE,
} DetailsPage;

static void
update_all (GduWindow *window, gboolean is_delayed_job_update)
{
  ShowFlags show_flags = {0};
  DetailsPage page = DETAILS_PAGE_NOT_IMPLEMENTED;

  /* figure out page to display */
  if (window->current_object != NULL)
    {
      if (udisks_object_peek_drive (window->current_object) != NULL ||
          udisks_object_peek_block (window->current_object) != NULL)
        {
          page = DETAILS_PAGE_DEVICE;
        }
    }
  else
    {
      page = DETAILS_PAGE_NOT_SELECTED;
    }

  if (page == DETAILS_PAGE_NOT_IMPLEMENTED)
    {
      g_warning ("no page for object %s", g_dbus_object_get_object_path (G_DBUS_OBJECT (window->current_object)));
    }
  gtk_notebook_set_current_page (GTK_NOTEBOOK (window->details_notebook), page);

  switch (page)
    {
    case DETAILS_PAGE_NOT_SELECTED:
      /* Nothing to update */
      break;

    case DETAILS_PAGE_NOT_IMPLEMENTED:
      /* Nothing to update */
      break;

    case DETAILS_PAGE_DEVICE:
      update_device_page (window, &show_flags, is_delayed_job_update);
      break;

    default:
      g_assert_not_reached ();
    }
  update_for_show_flags (window, &show_flags);
}

static void
on_client_changed (UDisksClient   *client,
                   gpointer        user_data)
{
  GduWindow *window = GDU_WINDOW (user_data);
  //g_debug ("on_client_changed");
  update_all (window, FALSE);
}

static void
on_volume_grid_changed (GduVolumeGrid  *grid,
                        gpointer        user_data)
{
  GduWindow *window = GDU_WINDOW (user_data);
  gboolean is_delayed_job_update = FALSE;
  //g_debug ("on_volume_grid_changed");
  if (window->delay_job_update_id != 0)
    {
      g_source_remove (window->delay_job_update_id);
      window->delay_job_update_id = 0;
      is_delayed_job_update = TRUE;
    }

  update_all (window, is_delayed_job_update);
}

/* ---------------------------------------------------------------------------------------------------- */

static gchar *
get_device_file_for_display (UDisksBlock *block)
{
  gchar *ret;
  if (udisks_block_get_read_only (block))
    {
      /* Translators: Shown for a read-only device. The %s is the device file, e.g. /dev/sdb1 */
      ret = g_strdup_printf (_("%s (Read-Only)"),
                             udisks_block_get_preferred_device (block));
    }
  else
    {
      ret = g_strdup (udisks_block_get_preferred_device (block));
    }
  return ret;
}

static gchar *
get_job_progress_text (GduWindow *window,
                       UDisksJob *job)
{
  gchar *s = NULL, *tmp;
  gint64 expected_end_time_usec;
  guint64 rate;
  guint64 bytes;

  expected_end_time_usec = udisks_job_get_expected_end_time (job);
  rate = udisks_job_get_rate (job);
  bytes = udisks_job_get_bytes (job);
  if (expected_end_time_usec > 0)
    {
      gint64 usec_left;
      gchar *s2, *s3;

      usec_left = expected_end_time_usec - g_get_real_time ();
      if (usec_left < 1)
        usec_left = 1;
      s2 = gdu_utils_format_duration_usec (usec_left, GDU_FORMAT_DURATION_FLAGS_NO_SECONDS);
      if (rate > 0)
        {
          s3 = g_format_size (rate);
          /* Translators: Used for job progress.
           *              The first %s is the estimated amount of time remaining (ex. "1 minute" or "5 minutes").
           *              The second %s is the average amount of bytes transfered per second (ex. "8.9 MB").
           */
          s = g_strdup_printf (C_("job-remaining-with-rate", "%s remaining (%s/sec)"), s2, s3);
          g_free (s3);
        }
      else
        {
          /* Translators: Used for job progress.
           *              The first %s is the estimated amount of time remaining (ex. "1 minute" or "5 minutes").
           */
          s = g_strdup_printf (C_("job-remaining", "%s remaining"), s2);
        }
      g_free (s2);

      if (bytes > 0 && udisks_job_get_progress_valid (job))
        {
          guint64 bytes_done = bytes * udisks_job_get_progress (job);
          s2 = g_format_size (bytes_done);
          s3 = g_format_size (bytes);
          tmp = s;
          /* Translators: Used to convey job progress where the amount of bytes to process is known.
           *              The first %s is the amount of bytes processed (ex. "650 MB").
           *              The second %s is the total amount of bytes to process (ex. "8.5 GB").
           *              The third %s is the estimated amount of time remaining including speed (if known) (ex. "1 minute remaining", "5 minutes remaining (42.3 MB/s)", "Less than a minute remaining").
           */
          s = g_strdup_printf (_("%s of %s — %s"), s2, s3, s);
          g_free (tmp);
          g_free (s3);
          g_free (s2);
        }
    }

  if (GDU_IS_LOCAL_JOB (job))
    {
      const gchar *extra_markup = gdu_local_job_get_extra_markup (GDU_LOCAL_JOB (job));
      if (extra_markup != NULL)
        {
          if (s != NULL)
            {
              tmp = s;
              s = g_strdup_printf ("%s\n%s", s, extra_markup);
              g_free (tmp);
            }
          else
            {
              s = g_strdup (extra_markup);
            }
        }
    }

  if (s != NULL)
    {
      tmp = s;
      s = g_strdup_printf ("<small>%s</small>", s);
      g_free (tmp);
    }

  return s;
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
delayed_job_update (gpointer user_data)
{
  GduWindow *window = GDU_WINDOW (user_data);

  window->delay_job_update_id = 0;
  update_all (window, TRUE);

  return G_SOURCE_REMOVE;
}

static void
update_jobs (GduWindow *window,
             GList     *jobs,
             gboolean   is_volume,
             gboolean   is_delayed_job_update)
{
  GtkWidget *label = window->devtab_drive_job_label;
  GtkWidget *grid = window->devtab_drive_job_grid;
  GtkWidget *progressbar = window->devtab_drive_job_progressbar;
  GtkWidget *remaining_label = window->devtab_drive_job_remaining_label;
  GtkWidget *no_progress_label = window->devtab_drive_job_no_progress_label;
  GtkWidget *cancel_button = window->devtab_drive_job_cancel_button;
  gboolean drive_sensitivity;
  gboolean selected_volume_sensitivity;
  gboolean gets_sensitive;

  if (is_volume)
    {
      label = window->devtab_job_label;
      grid = window->devtab_job_grid;
      progressbar = window->devtab_job_progressbar;
      remaining_label = window->devtab_job_remaining_label;
      no_progress_label = window->devtab_job_no_progress_label;
      cancel_button = window->devtab_job_cancel_button;
    }

  drive_sensitivity = !gdu_application_has_running_job (window->application, window->current_object);
  selected_volume_sensitivity = (!window->has_volume_job && !window->has_drive_job);
  gets_sensitive = (drive_sensitivity && !gtk_widget_get_sensitive (window->devtab_drive_generic_button))
                   || (selected_volume_sensitivity && !gtk_widget_get_sensitive (window->devtab_grid_toolbar));

  /* delay for some milliseconds if change to sensitive or while a delay is pending */
  if (window->delay_job_update_id > 0 || (gets_sensitive && !is_delayed_job_update))
    {
      if (window->delay_job_update_id != 0)
        g_source_remove (window->delay_job_update_id);

      window->delay_job_update_id = g_timeout_add (JOB_SENSITIVITY_DELAY_MS, delayed_job_update, window);
    }
  else
    {
      gtk_widget_set_sensitive (window->devtab_drive_generic_button, drive_sensitivity);
      gtk_widget_set_sensitive (window->devtab_drive_eject_button, drive_sensitivity);
      gtk_widget_set_sensitive (window->devtab_drive_power_off_button, drive_sensitivity);
      gtk_widget_set_sensitive (window->devtab_drive_loop_detach_button, drive_sensitivity);

      gtk_widget_set_sensitive (window->devtab_grid_toolbar, selected_volume_sensitivity);
    }

  if (jobs == NULL)
    {
      gtk_widget_hide (label);
      gtk_widget_hide (grid);
    }
  else
    {
      UDisksJob *job = UDISKS_JOB (jobs->data);
      gchar *s, *s2;

      gtk_widget_show (label);
      gtk_widget_show (grid);
      if (udisks_job_get_progress_valid (job))
        {
          gdouble progress = udisks_job_get_progress (job);
          gtk_widget_show (progressbar);
          gtk_widget_hide (no_progress_label);

          gtk_progress_bar_set_fraction (GTK_PROGRESS_BAR (progressbar), progress);

          if (GDU_IS_LOCAL_JOB (job))
            s2 = g_strdup (gdu_local_job_get_description (GDU_LOCAL_JOB (job)));
          else
            s2 = udisks_client_get_job_description (window->client, job);
          /* Translators: Used in job progress bar.
           *              The %s is the job description (e.g. "Erasing Device").
           *              The %f is the completion percentage (between 0.0 and 100.0).
           */
          s = g_strdup_printf (_("%s: %2.1f%%"),
                                s2,
                                100.0 * progress);
          g_free (s2);
          gtk_progress_bar_set_show_text (GTK_PROGRESS_BAR (progressbar), TRUE);
          gtk_progress_bar_set_text (GTK_PROGRESS_BAR (progressbar), s);
          g_free (s);

          s = get_job_progress_text (window, job);
          if (s != NULL)
            {
              gtk_widget_show (remaining_label);
              gtk_label_set_markup (GTK_LABEL (remaining_label), s);
              g_free (s);
            }
          else
            {
              gtk_widget_hide (remaining_label);
            }
        }
      else
        {
          gtk_widget_hide (progressbar);
          gtk_widget_hide (remaining_label);
          gtk_widget_show (no_progress_label);
          if (GDU_IS_LOCAL_JOB (job))
            s = g_strdup (gdu_local_job_get_description (GDU_LOCAL_JOB (job)));
          else
            s = udisks_client_get_job_description (window->client, job);
          gtk_label_set_text (GTK_LABEL (no_progress_label), s);
          g_free (s);
        }
      if (udisks_job_get_cancelable (job))
        gtk_widget_show (cancel_button);
      else
        gtk_widget_hide (cancel_button);
    }
}

static void
update_drive_jobs (GduWindow *window,
                   GList     *jobs,
                   gboolean   is_delayed_job_update)
{
  window->has_volume_job = FALSE; /* comes before update_volume_jobs */
  window->has_drive_job = (jobs != NULL);
  update_jobs (window, jobs, FALSE, is_delayed_job_update);
}

static void
update_volume_jobs (GduWindow    *window,
                    GList        *jobs,
                    UDisksObject *origin_object,
                    gboolean      is_delayed_job_update)
{
  /* in contrast to variable 'jobs' this call includes jobs on contained objects */
  window->has_volume_job = gdu_application_has_running_job (window->application, origin_object);
  update_jobs (window, jobs, TRUE, is_delayed_job_update);
}

/* ---------------------------------------------------------------------------------------------------- */

static void
update_generic_drive_bits (GduWindow      *window,
                           UDisksBlock    *block,          /* should be the whole disk */
                           GList          *jobs,           /* jobs not specific to @block */
                           ShowFlags      *show_flags,
                           gboolean        is_delayed_job_update)
{
  if (block != NULL)
    {
      gchar *s = NULL;
      guint64 size = 0;
      UDisksObject *object = NULL;
      UDisksPartitionTable *partition_table = NULL;

      object = (UDisksObject *) g_dbus_interface_get_object (G_DBUS_INTERFACE (block));
      partition_table = udisks_object_get_partition_table (object);

      gdu_volume_grid_set_no_media_string (GDU_VOLUME_GRID (window->volume_grid),
                                           _("Block device is empty"));

      size = udisks_block_get_size (block);

      /* -------------------------------------------------- */
      /* 'Size' field */

      set_size (window,
                "devtab-drive-size-label",
                "devtab-drive-size-value-label",
                size, SET_MARKUP_FLAGS_HYPHEN_IF_EMPTY);

      /* -------------------------------------------------- */
      /* 'Partitioning' field - only show if actually partitioned */

      s = NULL;
      if (partition_table != NULL)
        {
          const gchar *table_type = udisks_partition_table_get_type_ (partition_table);
          s = g_strdup (udisks_client_get_partition_table_type_for_display (window->client, table_type));
          if (s == NULL)
            {
              /* Translators: Shown for unknown partitioning type. The first %s is the low-level type. */
              s = g_strdup_printf (C_("partitioning", "Unknown (%s)"), table_type);
            }
        }
      set_markup (window,
                  "devtab-drive-partitioning-label",
                  "devtab-drive-partitioning-value-label",
                  s, SET_MARKUP_FLAGS_NONE);
      g_free (s);

      g_clear_object (&partition_table);
    }

  /* -------------------------------------------------- */
  /* 'Job' field - only shown if a job is running */

  /* if there are no given jobs, look at the block object */
  if (jobs == NULL && block != NULL)
    {
      UDisksObject *block_object = (UDisksObject *) g_dbus_interface_get_object (G_DBUS_INTERFACE (block));
      jobs = udisks_client_get_jobs_for_object (window->client, block_object);
      jobs = g_list_concat (jobs, gdu_application_get_local_jobs_for_object (window->application, block_object));
      update_drive_jobs (window, jobs, is_delayed_job_update);
      g_list_free_full (jobs, g_object_unref);
    }
  else
    {
      update_drive_jobs (window, jobs, is_delayed_job_update);
    }
}

/* ---------------------------------------------------------------------------------------------------- */

static void
update_device_page_for_drive (GduWindow      *window,
                              UDisksObject   *object,
                              UDisksDrive    *drive,
                              ShowFlags      *show_flags,
                              gboolean        is_delayed_job_update)
{
  gchar *s;
  GList *blocks;
  GList *l;
  GString *str;
  const gchar *drive_vendor;
  const gchar *drive_model;
  const gchar *drive_revision;
  UDisksBlock *block = NULL; /* first block */
  UDisksObjectInfo *info = NULL;
  guint64 size;
  UDisksDriveAta *ata;
  const gchar *our_seat;
  const gchar *serial;
  GList *jobs;
  gchar *title = NULL;

  //g_debug ("In update_device_page_for_drive() - selected=%s",
  //         object != NULL ? g_dbus_object_get_object_path (object) : "<nothing>");

  /* TODO: for multipath, ensure e.g. mpathk is before sda, sdb */
  blocks = get_top_level_blocks_for_drive (window, g_dbus_object_get_object_path (G_DBUS_OBJECT (object)));
  blocks = g_list_sort (blocks, (GCompareFunc) block_compare_on_preferred);
  if (blocks != NULL)
    block = udisks_object_peek_block (UDISKS_OBJECT (blocks->data));

  jobs = udisks_client_get_jobs_for_object (window->client, object);
  jobs = g_list_concat (jobs, gdu_application_get_local_jobs_for_object (window->application, object));
  update_generic_drive_bits (window, block, jobs, show_flags, is_delayed_job_update);

  gdu_volume_grid_set_no_media_string (GDU_VOLUME_GRID (window->volume_grid),
                                       _("No Media"));

  ata = udisks_object_peek_drive_ata (object);

  info = udisks_client_get_object_info (window->client, object);

  drive_vendor = udisks_drive_get_vendor (drive);
  drive_model = udisks_drive_get_model (drive);
  drive_revision = udisks_drive_get_revision (drive);

  str = g_string_new (NULL);
  for (l = blocks; l != NULL; l = l->next)
    {
      UDisksObject *block_object = UDISKS_OBJECT (l->data);
      if (str->len > 0)
        g_string_append_c (str, ' ');
      s = get_device_file_for_display (udisks_object_peek_block (block_object));
      g_string_append (str, s);
      g_free (s);
    }

  if (!in_desktop ("Unity"))
    {
      gtk_header_bar_set_title (GTK_HEADER_BAR (window->header), udisks_object_info_get_description (info));
      gtk_header_bar_set_subtitle (GTK_HEADER_BAR (window->header), str->str);
    }
  else
    {
      title = g_strdup_printf ("%s — %s", udisks_object_info_get_description (info), str->str);
      gtk_window_set_title (GTK_WINDOW (window), title);

      g_free (title);
    }

  g_string_free (str, TRUE);

  gtk_widget_show (window->devtab_drive_generic_button);

  str = g_string_new (NULL);
  if (strlen (drive_vendor) == 0)
    g_string_append (str, drive_model);
  else if (strlen (drive_model) == 0)
    g_string_append (str, drive_vendor);
  else
    g_string_append_printf (str, "%s %s", drive_vendor, drive_model);
  if (strlen (drive_revision) > 0)
    g_string_append_printf (str, " (%s)", drive_revision);
  set_markup (window,
              "devtab-drive-model-label",
              "devtab-drive-model-value-label",
              str->str, SET_MARKUP_FLAGS_HYPHEN_IF_EMPTY);
  g_string_free (str, TRUE);

  serial = udisks_drive_get_serial (drive);
  set_markup (window,
              "devtab-drive-serial-number-label",
              "devtab-drive-serial-number-value-label",
              serial, SET_MARKUP_FLAGS_NONE);
  if (serial == NULL || strlen (serial) == 0)
    {
      set_markup (window,
                  "devtab-drive-wwn-label",
                  "devtab-drive-wwn-value-label",
                  udisks_drive_get_wwn (drive), SET_MARKUP_FLAGS_NONE);
    }

  /* Figure out Location ...
   *
   * TODO: should also show things like "USB port 3" (if connected
   * to the main chassis) or "Bay 11 of Promise VTRAK 630" (if in a
   * disk enclosure)...
   */
  s = NULL;
  /* First see if connected to another seat than ours */
  our_seat = gdu_utils_get_seat ();
  if (our_seat != NULL)
    {
      const gchar *drive_seat = NULL;
      gboolean consider;
      /* If the device is not tagged, assume that udisks does not have
       * working seat-support... so just assume it's available at our
       * seat.
       */
      drive_seat = udisks_drive_get_seat (drive);
      if (drive_seat != NULL)
        {
          /* If device is attached to seat0, only consider it to be another seat if
           * it's removable...
           */
          consider = TRUE;
          if (g_strcmp0 (drive_seat, "seat0") == 0)
            {
              consider = FALSE;
              if (udisks_drive_get_removable (drive))
                consider = TRUE;
            }
          if (consider && g_strcmp0 (our_seat, drive_seat) != 0)
            {
              /* Translators: Shown in "Location" when drive is connected to another seat than where
               * our application is running.
               */
              s = g_strdup (_("Connected to another seat"));
            }
        }
    }
  if (s != NULL)
    {
      set_markup (window,
                  "devtab-drive-location-label",
                  "devtab-drive-location-value-label",
                  s, SET_MARKUP_FLAGS_NONE);
      g_free (s);
    }


  if (ata != NULL && !udisks_drive_get_media_removable (drive))
    {
      gboolean smart_is_supported;
      s = gdu_ata_smart_get_one_liner_assessment (ata, &smart_is_supported, NULL /* out_warning */);
      set_markup (window,
                  "devtab-drive-smart-label",
                  "devtab-drive-smart-value-label",
                  s, SET_MARKUP_FLAGS_NONE);
      if (smart_is_supported)
        show_flags->drive_menu |= SHOW_FLAGS_DRIVE_MENU_VIEW_SMART;
      g_free (s);
    }

  if (gdu_disk_settings_dialog_should_show (object))
    show_flags->drive_menu |= SHOW_FLAGS_DRIVE_MENU_DISK_SETTINGS;

  if (ata != NULL)
    {
      gboolean is_ssd = FALSE;
      if (udisks_drive_get_rotation_rate (drive) == 0)
        is_ssd = TRUE;
      if (udisks_drive_ata_get_pm_supported (ata) && !is_ssd)
        {
          GtkTreeIter iter;
          GduPowerStateFlags power_state_flags = GDU_POWER_STATE_FLAGS_NONE;
          if (gdu_device_tree_model_get_iter_for_object (window->model, object, &iter))
            {
              gtk_tree_model_get (GTK_TREE_MODEL (window->model), &iter,
                                  GDU_DEVICE_TREE_MODEL_COLUMN_POWER_STATE_FLAGS, &power_state_flags,
                                  -1);
            }
          if (power_state_flags & GDU_POWER_STATE_FLAGS_STANDBY)
            show_flags->drive_menu |= SHOW_FLAGS_DRIVE_MENU_RESUME_NOW;
          else
            show_flags->drive_menu |= SHOW_FLAGS_DRIVE_MENU_STANDBY_NOW;
        }
    }


  if (udisks_drive_get_can_power_off (drive))
    {
      show_flags->drive_menu |= SHOW_FLAGS_DRIVE_MENU_POWER_OFF;
      show_flags->drive_buttons |= SHOW_FLAGS_DRIVE_BUTTONS_POWER_OFF;
    }

  size = udisks_drive_get_size (drive);
  if (size > 0)
    {
      s = udisks_client_get_size_for_display (window->client, size, FALSE, TRUE);
      set_markup (window,
                  "devtab-drive-size-label",
                  "devtab-drive-size-value-label",
                  s, SET_MARKUP_FLAGS_HYPHEN_IF_EMPTY);
      g_free (s);
    }
  else
    {
      set_markup (window,
                  "devtab-drive-size-label",
                  "devtab-drive-size-value-label",
                  "",
                  SET_MARKUP_FLAGS_HYPHEN_IF_EMPTY);
    }

  if (udisks_drive_get_media_available (drive))
    {
      set_markup (window,
                  "devtab-drive-media-label",
                  "devtab-drive-media-value-label",
                  udisks_object_info_get_media_description (info), SET_MARKUP_FLAGS_NONE);
    }
  else
    {
      set_markup (window,
                  "devtab-drive-media-label",
                  "devtab-drive-media-value-label",
                  "",
                  SET_MARKUP_FLAGS_HYPHEN_IF_EMPTY);
    }

  if (udisks_drive_get_ejectable (drive) && udisks_drive_get_media_removable (drive))
    {
      show_flags->drive_buttons |= SHOW_FLAGS_DRIVE_BUTTONS_EJECT;
    }

  /* Show Drive-specific items */
  gtk_widget_show (GTK_WIDGET (window->generic_drive_menu_item_drive_sep_1));
  gtk_widget_show (GTK_WIDGET (window->generic_drive_menu_item_view_smart));
  gtk_widget_show (GTK_WIDGET (window->generic_drive_menu_item_disk_settings));
  gtk_widget_show (GTK_WIDGET (window->generic_drive_menu_item_drive_sep_2));
  if (!(show_flags->drive_menu & (SHOW_FLAGS_DRIVE_MENU_STANDBY_NOW|SHOW_FLAGS_DRIVE_MENU_RESUME_NOW)))
    {
      /* no PM / safely-remove capabilities... only show "standby" greyed out */
      gtk_widget_show (GTK_WIDGET (window->generic_drive_menu_item_standby_now));
      gtk_widget_set_sensitive (GTK_WIDGET (window->generic_drive_menu_item_standby_now), FALSE);
    }
  else
    {
      /* Only show one of Standby and Resume (they are mutually exclusive) */
      gtk_widget_set_sensitive (GTK_WIDGET (window->generic_drive_menu_item_standby_now), TRUE);
      gtk_widget_set_sensitive (GTK_WIDGET (window->generic_drive_menu_item_resume_now), TRUE);
      if (show_flags->drive_menu & SHOW_FLAGS_DRIVE_MENU_STANDBY_NOW)
        gtk_widget_show (GTK_WIDGET (window->generic_drive_menu_item_standby_now));
      else
        gtk_widget_show (GTK_WIDGET (window->generic_drive_menu_item_resume_now));
    }
  if (show_flags->drive_menu & SHOW_FLAGS_DRIVE_MENU_POWER_OFF)
    gtk_widget_show (GTK_WIDGET (window->generic_drive_menu_item_power_off));

  g_list_foreach (blocks, (GFunc) g_object_unref, NULL);
  g_list_free (blocks);
  g_clear_object (&info);

  g_list_foreach (jobs, (GFunc) g_object_unref, NULL);
  g_list_free (jobs);
}

/* ---------------------------------------------------------------------------------------------------- */

static void
update_device_page_for_loop (GduWindow      *window,
                             UDisksObject   *object,
                             UDisksBlock    *block,
                             UDisksLoop     *loop,
                             ShowFlags      *show_flags,
                             gboolean        is_delayed_job_update)
{
  UDisksObjectInfo *info = NULL;
  gchar *s = NULL;
  gchar *device_desc = NULL;
  gchar *title = NULL;

  gdu_volume_grid_set_no_media_string (GDU_VOLUME_GRID (window->volume_grid),
                                       _("Loop device is empty"));

  info = udisks_client_get_object_info (window->client, object);
  device_desc = get_device_file_for_display (block);

  if (!in_desktop ("Unity"))
    {
      gtk_header_bar_set_title (GTK_HEADER_BAR (window->header), udisks_object_info_get_description (info));
      gtk_header_bar_set_subtitle (GTK_HEADER_BAR (window->header), device_desc);
    }
  else
    {
      title = g_strdup_printf ("%s — %s", udisks_object_info_get_description (info), device_desc);
      gtk_window_set_title (GTK_WINDOW (window), title);
    }

  gtk_widget_show (window->devtab_drive_generic_button);

  update_generic_drive_bits (window, block, NULL, show_flags, is_delayed_job_update);

  /* -------------------------------------------------- */
  /* 'Auto-clear' and 'Backing File' fields */

  if (loop != NULL)
    {
      s = gdu_utils_unfuse_path (udisks_loop_get_backing_file (loop));
      set_markup (window,
                  "devtab-backing-file-label",
                  "devtab-backing-file-value-label",
                  s, SET_MARKUP_FLAGS_NONE);
      g_free (s);

      set_switch (window,
                  "devtab-loop-autoclear-label",
                  "devtab-loop-autoclear-switch-box",
                  "devtab-loop-autoclear-switch",
                  udisks_loop_get_autoclear (loop),
                  gdu_utils_is_in_use (window->client, object));
    }

  /* cleanup */
  g_clear_object (&info);
  g_free (device_desc);
  g_free (title);
}

/* ---------------------------------------------------------------------------------------------------- */

/* this is for setting the drive stuff for things any random block
 * device that we don't have explicit support for... (typically things
 * like LVM)
 */
static void
update_device_page_for_fake_block (GduWindow      *window,
                                   UDisksObject   *object,
                                   UDisksBlock    *block,
                                   ShowFlags      *show_flags,
                                   gboolean        is_delayed_job_update)
{
  UDisksObjectInfo *info = NULL;
  gchar *device_desc = NULL;
  gchar *title = NULL;

  gdu_volume_grid_set_no_media_string (GDU_VOLUME_GRID (window->volume_grid),
                                       _("Block device is empty"));

  info = udisks_client_get_object_info (window->client, object);
  device_desc = get_device_file_for_display (block);

  if (!in_desktop ("Unity"))
    {
      gtk_header_bar_set_title (GTK_HEADER_BAR (window->header), udisks_object_info_get_description (info));
      gtk_header_bar_set_subtitle (GTK_HEADER_BAR (window->header), device_desc);
    }
  else
    {
      title = g_strdup_printf ("%s — %s", udisks_object_info_get_description (info), device_desc);
      gtk_window_set_title (GTK_WINDOW (window), title);
    }

  gtk_widget_show (window->devtab_drive_generic_button);

  update_generic_drive_bits (window, block, NULL, show_flags, is_delayed_job_update);

  /* cleanup */
  g_clear_object (&info);
  g_free (device_desc);
  g_free (title);
}

/* ---------------------------------------------------------------------------------------------------- */

static UDisksObject *
lookup_cleartext_device_for_crypto_device (UDisksClient  *client,
                                           const gchar   *object_path)
{
  GDBusObjectManager *object_manager;
  UDisksObject *ret;
  GList *objects;
  GList *l;

  ret = NULL;

  object_manager = udisks_client_get_object_manager (client);
  objects = g_dbus_object_manager_get_objects (object_manager);
  for (l = objects; l != NULL; l = l->next)
    {
      UDisksObject *object = UDISKS_OBJECT (l->data);
      UDisksBlock *block;

      block = udisks_object_peek_block (object);
      if (block == NULL)
        continue;

      if (g_strcmp0 (udisks_block_get_crypto_backing_device (block),
                     object_path) == 0)
        {
          ret = g_object_ref (object);
          goto out;
        }
    }

 out:
  g_list_foreach (objects, (GFunc) g_object_unref, NULL);
  g_list_free (objects);
  return ret;
}

static void
update_device_page_for_block (GduWindow          *window,
                              UDisksObject       *object,
                              UDisksBlock        *block,
                              guint64             size,
                              ShowFlags          *show_flags,
                              gboolean            is_delayed_job_update)
{
  const gchar *usage;
  const gchar *type;
  const gchar *version;
  UDisksFilesystem *filesystem;
  UDisksPartition *partition;
  UDisksPartitionTable *partition_table = NULL;
  gboolean read_only;
  gchar *s, *s2, *s3;
  gchar *in_use_markup = NULL;
  UDisksObject *drive_object;
  UDisksDrive *drive = NULL;
  GList *jobs = NULL;
  gint64 unused_space = -1;

  read_only = udisks_block_get_read_only (block);
  partition = udisks_object_peek_partition (object);
  filesystem = udisks_object_peek_filesystem (object);

  partition_table = udisks_object_get_partition_table (object);
  if (partition_table == NULL && partition != NULL)
    partition_table = udisks_client_get_partition_table (window->client, partition);

  drive_object = (UDisksObject *) g_dbus_object_manager_get_object (udisks_client_get_object_manager (window->client),
                                                                        udisks_block_get_drive (block));
  if (drive_object != NULL)
    {
      drive = udisks_object_peek_drive (drive_object);
      g_object_unref (drive_object);
    }

  /* TODO: don't show on CD-ROM drives etc. */
  if (udisks_block_get_size (block) > 0 || (drive != NULL && !udisks_drive_get_media_change_detected (drive)))
    {
      show_flags->volume_menu |= SHOW_FLAGS_VOLUME_MENU_CREATE_VOLUME_IMAGE;
      show_flags->volume_menu |= SHOW_FLAGS_VOLUME_MENU_BENCHMARK;
      show_flags->drive_menu |= SHOW_FLAGS_DRIVE_MENU_BENCHMARK;
      show_flags->drive_menu |= SHOW_FLAGS_DRIVE_MENU_CREATE_DISK_IMAGE;
      if (!read_only)
        show_flags->drive_menu |= SHOW_FLAGS_DRIVE_MENU_RESTORE_DISK_IMAGE;
      if (!read_only)
        {
          show_flags->volume_menu |= SHOW_FLAGS_VOLUME_MENU_RESTORE_VOLUME_IMAGE;
          if (udisks_block_get_hint_partitionable (block))
            show_flags->drive_menu |= SHOW_FLAGS_DRIVE_MENU_FORMAT_DISK;
          show_flags->volume_menu |= SHOW_FLAGS_VOLUME_MENU_FORMAT_VOLUME;
        }
    }

  unused_space = gdu_utils_get_unused_for_block (window->client, block);

  if (partition != NULL && !read_only)
    show_flags->volume_buttons |= SHOW_FLAGS_VOLUME_BUTTONS_PARTITION_DELETE;

  /* Since /etc/fstab, /etc/crypttab and so on can reference
   * any device regardless of its content ... we want to show
   * the relevant menu option (to get to the configuration dialog)
   * if the device matches the configuration....
   */
  if (gdu_utils_has_configuration (block, "fstab", NULL))
    show_flags->volume_menu |= SHOW_FLAGS_VOLUME_MENU_CONFIGURE_FSTAB;
  if (gdu_utils_has_configuration (block, "crypttab", NULL))
    show_flags->volume_menu |= SHOW_FLAGS_VOLUME_MENU_CONFIGURE_CRYPTTAB;

  /* if the device has no media and there is no existing configuration, then
   * show CONFIGURE_FSTAB since the user might want to add an entry for e.g.
   * /media/cdrom
   */
  if (udisks_block_get_size (block) == 0 &&
      !(show_flags->volume_menu & (SHOW_FLAGS_VOLUME_MENU_CONFIGURE_FSTAB |
                                   SHOW_FLAGS_VOLUME_MENU_CONFIGURE_CRYPTTAB)))
    {
      show_flags->volume_menu |= SHOW_FLAGS_VOLUME_MENU_CONFIGURE_FSTAB;
    }

  //g_debug ("In update_device_page_for_block() - size=%" G_GUINT64_FORMAT " selected=%s",
  //         size,
  //         object != NULL ? g_dbus_object_get_object_path (object) : "<nothing>");

  s = get_device_file_for_display (block);
  set_markup (window,
              "devtab-device-label",
              "devtab-device-value-label",
              s, SET_MARKUP_FLAGS_NONE);
  g_free (s);
  if (size > 0)
    {
      if (unused_space > 0)
        {
          s2 = udisks_client_get_size_for_display (window->client, unused_space, FALSE, FALSE);
          s3 = udisks_client_get_size_for_display (window->client, size, FALSE, FALSE);
          /* Translators: Shown in 'Size' field for a filesystem where we know the amount of unused
           *              space.
           *              The first %s is a short string with the size (e.g. '69 GB (68,719,476,736 bytes)').
           *              The second %s is a short string with the space free (e.g. '43 GB').
           *              The %f is the percentage in use (e.g. 62.2).
           */
          s = g_strdup_printf (_("%s — %s free (%.1f%% full)"), s3, s2,
                               100.0 * (size - unused_space) / size);
          g_free (s3);
          g_free (s2);
          set_markup (window,
                      "devtab-size-label",
                      "devtab-size-value-label",
                      s, SET_MARKUP_FLAGS_NONE);
          g_free (s);
        }
      else
        {
          set_size (window,
                    "devtab-size-label",
                    "devtab-size-value-label",
                    size, SET_MARKUP_FLAGS_HYPHEN_IF_EMPTY);
        }
    }
  else
    {
      set_markup (window,
                  "devtab-size-label",
                  "devtab-size-value-label",
                  NULL, SET_MARKUP_FLAGS_HYPHEN_IF_EMPTY);
    }

  if (partition != NULL)
    {
      s = udisks_client_get_partition_info (window->client, partition);
      if (s == NULL)
        s = g_strdup (C_("partition type", "Unknown"));
      set_markup (window,
                  "devtab-partition-label",
                  "devtab-partition-value-label",
                  s, SET_MARKUP_FLAGS_NONE);
      g_free (s);
    }

  /* ------------------------------ */
  /* 'UUID' field */

  s = udisks_block_dup_id_uuid (block);
  set_markup (window,
              "devtab-uuid-label",
              "devtab-uuid-value-label",
              s, SET_MARKUP_FLAGS_NONE);
  g_free (s);

  /* ------------------------------ */
  /* 'Contents' field */

  usage = udisks_block_get_id_usage (block);
  type = udisks_block_get_id_type (block);
  version = udisks_block_get_id_version (block);

  /* Figure out in use */
  in_use_markup = NULL;
  if (filesystem != NULL)
    {
      const gchar *const *mount_points;
      mount_points = udisks_filesystem_get_mount_points (filesystem);
      if (g_strv_length ((gchar **) mount_points) > 0)
        {
          /* TODO: right now we only display the first mount point */
          if (g_strcmp0 (mount_points[0], "/") == 0)
            {
              /* Translators: Use for mount point '/' simply because '/' is too small to hit as a hyperlink
               */
              s = g_strdup_printf ("<a href=\"file:///\">%s</a>", C_("volume-content-fs", "Filesystem Root"));
            }
          else
            {
              s = g_strdup_printf ("<a href=\"file://%s\">%s</a>",
                                   mount_points[0], mount_points[0]);
            }
          /* Translators: Shown as in-use part of 'Contents'. The first %s is the mount point, e.g. /media/foobar */
          in_use_markup = g_strdup_printf (C_("volume-content-fs", "Mounted at %s"), s);
          g_free (s);
        }
      else
        {
          /* Translators: Shown when the device is not mounted next to the "In Use" label */
          in_use_markup = g_strdup (C_("volume-content-fs", "Not Mounted"));
        }

      if (g_strv_length ((gchar **) mount_points) > 0)
        show_flags->volume_buttons |= SHOW_FLAGS_VOLUME_BUTTONS_UNMOUNT;
      else
        show_flags->volume_buttons |= SHOW_FLAGS_VOLUME_BUTTONS_MOUNT;

      show_flags->volume_menu |= SHOW_FLAGS_VOLUME_MENU_CONFIGURE_FSTAB;
      if (!read_only)
        show_flags->volume_menu |= SHOW_FLAGS_VOLUME_MENU_EDIT_LABEL;
    }
  else if (g_strcmp0 (udisks_block_get_id_usage (block), "other") == 0 &&
           g_strcmp0 (udisks_block_get_id_type (block), "swap") == 0)
    {
      UDisksSwapspace *swapspace;
      swapspace = udisks_object_peek_swapspace (object);
      if (swapspace != NULL)
        {
          if (udisks_swapspace_get_active (swapspace))
            {
              show_flags->volume_buttons |= SHOW_FLAGS_VOLUME_BUTTONS_DEACTIVATE_SWAP;
              /* Translators: Shown as in-use part of 'Contents' if the swap device is in use */
              in_use_markup = g_strdup (C_("volume-content-swap", "Active"));
            }
          else
            {
              show_flags->volume_buttons |= SHOW_FLAGS_VOLUME_BUTTONS_ACTIVATE_SWAP;
              /* Translators: Shown as in-use part of 'Contents' if the swap device is not in use */
              in_use_markup = g_strdup (C_("volume-content-swap", "Not Active"));
            }
        }
    }
  else if (g_strcmp0 (udisks_block_get_id_usage (block), "crypto") == 0)
    {
      UDisksObject *cleartext_device;
      cleartext_device = lookup_cleartext_device_for_crypto_device (window->client,
                                                                    g_dbus_object_get_object_path (G_DBUS_OBJECT (object)));
      if (cleartext_device != NULL)
        {
          show_flags->volume_buttons |= SHOW_FLAGS_VOLUME_BUTTONS_ENCRYPTED_LOCK;
          /* Translators: Shown as in-use part of 'Contents' if the encrypted device is unlocked */
          in_use_markup = g_strdup (C_("volume-content-luks", "Unlocked"));
        }
      else
        {
          show_flags->volume_buttons |= SHOW_FLAGS_VOLUME_BUTTONS_ENCRYPTED_UNLOCK;
          /* Translators: Shown as in-use part of 'Contents' if the encrypted device is unlocked */
          in_use_markup = g_strdup (C_("volume-content-luks", "Locked"));
        }
      show_flags->volume_menu |= SHOW_FLAGS_VOLUME_MENU_CONFIGURE_CRYPTTAB;
      show_flags->volume_menu |= SHOW_FLAGS_VOLUME_MENU_CHANGE_PASSPHRASE;
    }

  if (size > 0)
    {
      if (partition != NULL && udisks_partition_get_is_container (partition))
        {
          s = g_strdup (C_("volume-contents-msdos-ext", "Extended Partition"));
        }
      else
        {
          s = udisks_client_get_id_for_display (window->client, usage, type, version, TRUE);
        }
    }
  else
    {
      s = NULL;
    }

  if (in_use_markup != NULL)
    {
      if (s != NULL)
        {
          /* Translators: Shown in 'Contents' field for a member that can be "mounted" (e.g. filesystem or swap area).
           *              The first %s is the usual contents string e.g. "Swapspace" or "Ext4 (version 1.0)".
           *              The second %s is either "Mounted at /path/to/fs", "Not Mounted, "Active", "Not Active", "Unlocked" or "Locked".
           */
          s2 = g_strdup_printf (C_("volume-contents-combiner", "%s — %s"), s, in_use_markup);
          g_free (s); s = s2;
        }
      else
        {
          s = in_use_markup; in_use_markup = NULL;
        }
    }
  set_markup (window,
              "devtab-volume-type-label",
              "devtab-volume-type-value-label",
              s, SET_MARKUP_FLAGS_HYPHEN_IF_EMPTY);
  g_free (s);

  if (partition != NULL)
    {
      if (!read_only)
        show_flags->volume_menu |= SHOW_FLAGS_VOLUME_MENU_EDIT_PARTITION;
    }
  else
    {
      if (drive != NULL && udisks_drive_get_ejectable (drive) && udisks_drive_get_media_removable (drive))
        show_flags->drive_buttons |= SHOW_FLAGS_DRIVE_BUTTONS_EJECT;
    }

#ifdef HAVE_UDISKS2_7_2

  if (partition != NULL && g_strcmp0 (usage, "") == 0 && !read_only)
    {

      /* allow partition resize if no known structured data was found on the device */
      show_flags->volume_menu |= SHOW_FLAGS_VOLUME_MENU_RESIZE;
    }
  else if (filesystem != NULL)
    {
      /* for now the filesystem resize on just any block device is not shown, see resize_dialog_show */
      if (!read_only && partition != NULL && gdu_utils_can_resize (window->client, type, FALSE, NULL, NULL))
        show_flags->volume_menu |= SHOW_FLAGS_VOLUME_MENU_RESIZE;

      if (!read_only && gdu_utils_can_repair (window->client, type, FALSE, NULL))
        show_flags->volume_menu |= SHOW_FLAGS_VOLUME_MENU_REPAIR;

      if (gdu_utils_can_check (window->client, type, FALSE, NULL))
        show_flags->volume_menu |= SHOW_FLAGS_VOLUME_MENU_CHECK;
    }

#endif

  /* Only show jobs if the volume is a partition (if it's not, we're already showing
   * the jobs in the drive section)
   */
  if (partition != NULL)
    {
      jobs = udisks_client_get_jobs_for_object (window->client, object);
      jobs = g_list_concat (jobs, gdu_application_get_local_jobs_for_object (window->application, object));
    }
  update_volume_jobs (window, jobs, object, is_delayed_job_update);
  g_list_foreach (jobs, (GFunc) g_object_unref, NULL);
  g_list_free (jobs);
  g_clear_object (&partition_table);
  g_free (in_use_markup);
}

static void
update_device_page_for_no_media (GduWindow          *window,
                                 UDisksObject       *object,
                                 UDisksBlock        *block,
                                 ShowFlags          *show_flags)
{
  //g_debug ("In update_device_page_for_no_media() - selected=%s",
  //         object != NULL ? g_dbus_object_get_object_path (object) : "<nothing>");
}

static void
update_device_page_for_free_space (GduWindow          *window,
                                   UDisksObject       *object,
                                   UDisksBlock        *block,
                                   guint64             size,
                                   ShowFlags          *show_flags)
{
  gchar *s;
  UDisksLoop *loop;
  gboolean read_only;

  //g_debug ("In update_device_page_for_free_space() - size=%" G_GUINT64_FORMAT " selected=%s",
  //         size,
  //         object != NULL ? g_dbus_object_get_object_path (object) : "<nothing>");

  read_only = udisks_block_get_read_only (block);
  loop = udisks_object_peek_loop (window->current_object);

  show_flags->drive_menu |= SHOW_FLAGS_DRIVE_MENU_BENCHMARK;
  if (!read_only)
    {
      show_flags->drive_menu |= SHOW_FLAGS_DRIVE_MENU_FORMAT_DISK;
      show_flags->drive_menu |= SHOW_FLAGS_DRIVE_MENU_CREATE_DISK_IMAGE;
      show_flags->drive_menu |= SHOW_FLAGS_DRIVE_MENU_RESTORE_DISK_IMAGE;
    }

  if (loop != NULL)
    {
      s = gdu_utils_unfuse_path (udisks_loop_get_backing_file (loop));
      set_markup (window,
                  "devtab-backing-file-label",
                  "devtab-backing-file-value-label",
                  s, SET_MARKUP_FLAGS_NONE);
      g_free (s);

      set_switch (window,
                  "devtab-loop-autoclear-label",
                  "devtab-loop-autoclear-switch-box",
                  "devtab-loop-autoclear-switch",
                  udisks_loop_get_autoclear (loop),
                  gdu_utils_is_in_use (window->client, object));
    }

  set_size (window,
            "devtab-size-label",
            "devtab-size-value-label",
            size, SET_MARKUP_FLAGS_HYPHEN_IF_EMPTY);
  set_markup (window,
              "devtab-volume-type-label",
              "devtab-volume-type-value-label",
              /* Translators: used to convey free space for partitions */
              _("Unallocated Space"),
              SET_MARKUP_FLAGS_NONE);
  if (!read_only)
    show_flags->volume_buttons |= SHOW_FLAGS_VOLUME_BUTTONS_PARTITION_CREATE;

  s = get_device_file_for_display (block);
  set_markup (window,
              "devtab-device-label",
              "devtab-device-value-label",
              s, SET_MARKUP_FLAGS_NONE);
  g_free (s);
}

/* ---------------------------------------------------------------------------------------------------- */

static void
device_page_ensure_grid (GduWindow *window)
{
  UDisksDrive *drive;
  UDisksBlock *block;

  drive = udisks_object_peek_drive (window->current_object);
  block = udisks_object_peek_block (window->current_object);

  if (drive != NULL)
    {
      GList *blocks;

      /* TODO: for multipath, ensure e.g. mpathk is before sda, sdb */
      blocks = get_top_level_blocks_for_drive (window, g_dbus_object_get_object_path (G_DBUS_OBJECT (window->current_object)));
      blocks = g_list_sort (blocks, (GCompareFunc) block_compare_on_preferred);

      if (blocks != NULL)
        gdu_volume_grid_set_block_object (GDU_VOLUME_GRID (window->volume_grid), blocks->data);
      else
        gdu_volume_grid_set_block_object (GDU_VOLUME_GRID (window->volume_grid), NULL);

      g_list_foreach (blocks, (GFunc) g_object_unref, NULL);
      g_list_free (blocks);
    }
  else if (block != NULL)
    {
      gdu_volume_grid_set_block_object (GDU_VOLUME_GRID (window->volume_grid), window->current_object);
    }
  else
    {
      g_assert_not_reached ();
    }
}

static void
maybe_hide (GtkWidget *widget,
            gpointer   user_data)
{
  GduWindow *window = GDU_WINDOW (user_data);

  /* Don't hide grid containing job widgets. The visibility of its
   * children (e.g. buttons) are controlled in update_jobs() - hiding
   * it here only to show it later may cause focus problems so the
   * buttons can't be clicked if the window is continously updated.
   */
  if (widget == window->devtab_drive_job_grid ||
      widget == window->devtab_job_grid)
    {
      /* do nothing */
    }
  else
    {
      gtk_widget_hide (widget);
    }
}

static void
update_device_page (GduWindow      *window,
                    ShowFlags      *show_flags,
                    gboolean        is_delayed_job_update)
{
  UDisksObject *object;
  GduVolumeGridElementType type;
  UDisksBlock *block;
  UDisksDrive *drive;
  UDisksLoop *loop = NULL;
  guint64 size;

  /* First hide everything
   *
   * (TODO: this is wrong as hiding a widget only to show it again, is
   * a cause for bad focus problems in GTK+)
   */
  gtk_container_foreach (GTK_CONTAINER (window->devtab_drive_table), maybe_hide, window);
  gtk_container_foreach (GTK_CONTAINER (window->devtab_table), maybe_hide, window);

  /* Hide all Drive-specific menu items - will be turned on again in update_device_page_for_drive() */
  gtk_widget_hide (GTK_WIDGET (window->generic_drive_menu_item_drive_sep_1));
  gtk_widget_hide (GTK_WIDGET (window->generic_drive_menu_item_view_smart));
  gtk_widget_hide (GTK_WIDGET (window->generic_drive_menu_item_disk_settings));
  gtk_widget_hide (GTK_WIDGET (window->generic_drive_menu_item_drive_sep_2));
  gtk_widget_hide (GTK_WIDGET (window->generic_drive_menu_item_standby_now));
  gtk_widget_hide (GTK_WIDGET (window->generic_drive_menu_item_resume_now));
  gtk_widget_hide (GTK_WIDGET (window->generic_drive_menu_item_power_off));

  /* ensure grid is set to the right volumes */
  device_page_ensure_grid (window);

  object = window->current_object;
  block = udisks_object_peek_block (window->current_object);
  drive = udisks_object_peek_drive (window->current_object);
  if (block != NULL)
    loop = udisks_client_get_loop_for_block (window->client, block);

  if (udisks_object_peek_loop (object) != NULL)
    show_flags->drive_buttons |= SHOW_FLAGS_DRIVE_BUTTONS_LOOP_DETACH;

  if (drive != NULL)
    update_device_page_for_drive (window, object, drive, show_flags, is_delayed_job_update);
  else if (loop != NULL)
    update_device_page_for_loop (window, object, block, loop, show_flags, is_delayed_job_update);
  else
    update_device_page_for_fake_block (window, object, block, show_flags, is_delayed_job_update);

  type = gdu_volume_grid_get_selected_type (GDU_VOLUME_GRID (window->volume_grid));
  size = gdu_volume_grid_get_selected_size (GDU_VOLUME_GRID (window->volume_grid));

  if (type == GDU_VOLUME_GRID_ELEMENT_TYPE_CONTAINER)
    {
      if (block != NULL)
        update_device_page_for_block (window, object, block, size, show_flags, is_delayed_job_update);
    }
  else
    {
      object = gdu_volume_grid_get_selected_device (GDU_VOLUME_GRID (window->volume_grid));
      if (object == NULL)
        object = gdu_volume_grid_get_block_object (GDU_VOLUME_GRID (window->volume_grid));
      if (object != NULL)
        {
          block = udisks_object_peek_block (object);
          switch (type)
            {
            case GDU_VOLUME_GRID_ELEMENT_TYPE_CONTAINER:
              g_assert_not_reached (); /* already handled above */
              break;

            case GDU_VOLUME_GRID_ELEMENT_TYPE_DEVICE:
              update_device_page_for_block (window, object, block, size, show_flags, is_delayed_job_update);
              break;

            case GDU_VOLUME_GRID_ELEMENT_TYPE_NO_MEDIA:
              update_device_page_for_block (window, object, block, size, show_flags, is_delayed_job_update);
              update_device_page_for_no_media (window, object, block, show_flags);
              break;

            case GDU_VOLUME_GRID_ELEMENT_TYPE_FREE_SPACE:
              update_device_page_for_free_space (window, object, block, size, show_flags);
              break;

            default:
              g_assert_not_reached ();
            }
        }
    }

  g_clear_object (&loop);
}

/* ---------------------------------------------------------------------------------------------------- */

#ifdef HAVE_UDISKS2_7_2

static void
on_generic_menu_item_resize (GtkMenuItem *menu_item,
                             gpointer     user_data)
{
  GduWindow *window = GDU_WINDOW (user_data);
  UDisksObject *object;

  object = gdu_volume_grid_get_selected_device (GDU_VOLUME_GRID (window->volume_grid));
  g_assert (object != NULL);
  gdu_resize_dialog_show (window, object);
}

/* ---------------------------------------------------------------------------------------------------- */

static void
fs_repair_cb (UDisksFilesystem *filesystem,
              GAsyncResult     *res,
              GduWindow        *window)
{
  gboolean success;
  GError *error = NULL;

  if (!udisks_filesystem_call_repair_finish (filesystem, &success, res, &error))
    {
      gdu_utils_show_error (GTK_WINDOW (window),
                            _("Error while repairing filesystem"),
                            error);
      g_error_free (error);
    }
  else
    {
      GtkWidget *message_dialog;
      UDisksObjectInfo *info;
      UDisksBlock *block;
      UDisksObject *object;
      const gchar *name;
      gchar *s;

      object = UDISKS_OBJECT (g_dbus_interface_get_object (G_DBUS_INTERFACE (filesystem)));
      block = udisks_object_peek_block (object);
      g_assert (block != NULL);
      info = udisks_client_get_object_info (window->client, object);
      name = udisks_block_get_id_label (block);

      if (name == NULL || strlen (name) == 0)
        name = udisks_block_get_id_type (block);

      message_dialog = gtk_message_dialog_new_with_markup  (GTK_WINDOW (window),
                                                            GTK_DIALOG_MODAL,
                                                            GTK_MESSAGE_INFO,
                                                            GTK_BUTTONS_CLOSE,
                                                            "<big><b>%s</b></big>",
                                                            success ? _("Repair successful") : _("Repair failed"));
      if (success)
        {
          s = g_strdup_printf (_("Filesystem %s on %s has been repaired."),
                               name, udisks_object_info_get_name (info));
        }
      else
        {
          /* show as result and not error message, because it's not a malfunction of GDU */
          s = g_strdup_printf (_("Filesystem %s on %s could not be repaired."),
                               name, udisks_object_info_get_name (info));
        }

      gtk_message_dialog_format_secondary_markup (GTK_MESSAGE_DIALOG (message_dialog), "%s", s);
      gtk_dialog_run (GTK_DIALOG (message_dialog));

      gtk_widget_destroy (message_dialog);
      g_free (s);
    }

}

static void
fs_repair_unmount_cb (GduWindow        *window,
                      GAsyncResult     *res,
                      gpointer          user_data)
{
  UDisksObject *object = UDISKS_OBJECT (user_data);
  GError *error = NULL;

  if (!gdu_window_ensure_unused_finish (window,
                                        res,
                                        &error))
    {
      gdu_utils_show_error (GTK_WINDOW (window),
                            _("Error unmounting filesystem"),
                            error);
      g_error_free (error);
    }
  else
    {
      UDisksFilesystem *filesystem;

      filesystem = udisks_object_peek_filesystem (object);
      g_assert (filesystem != NULL);
      udisks_filesystem_call_repair (filesystem,
                                     g_variant_new ("a{sv}", NULL),
                                     NULL,
                                     (GAsyncReadyCallback) fs_repair_cb,
                                     window);
    }

  g_object_unref (object);
}

static void
on_generic_menu_item_repair (GtkMenuItem *menu_item,
                             gpointer     user_data)
{
  GduWindow *window = GDU_WINDOW (user_data);
  GtkWidget *message_dialog, *ok_button;
  UDisksObject *object;

  object = gdu_volume_grid_get_selected_device (GDU_VOLUME_GRID (window->volume_grid));
  g_assert (object != NULL);

  message_dialog = gtk_message_dialog_new_with_markup  (GTK_WINDOW (window),
                                                        GTK_DIALOG_MODAL,
                                                        GTK_MESSAGE_WARNING,
                                                        GTK_BUTTONS_OK_CANCEL,
                                                        "<big><b>%s</b></big>",
                                                        _("Confirm Repair"));

  gtk_message_dialog_format_secondary_markup (GTK_MESSAGE_DIALOG (message_dialog), "%s",
                                              _("A filesystem repair is not always possible and can cause data loss. "
                                                "Consider backing it up first in order to use forensic recovery tools "
                                                "that retrieve lost files. "
                                                "Depending on the amount of data this operation takes longer time."));

  ok_button = gtk_dialog_get_widget_for_response (GTK_DIALOG (message_dialog), GTK_RESPONSE_OK);
  gtk_style_context_add_class (gtk_widget_get_style_context (ok_button), "destructive-action");

  if (gtk_dialog_run (GTK_DIALOG (message_dialog)) == GTK_RESPONSE_OK)
    gdu_window_ensure_unused (window, object, (GAsyncReadyCallback) fs_repair_unmount_cb,
                              NULL, g_object_ref (object));

  gtk_widget_destroy (message_dialog);
}

/* ---------------------------------------------------------------------------------------------------- */

static void
fs_check_cb (UDisksFilesystem *filesystem,
             GAsyncResult     *res,
             GduWindow        *window)
{
  gboolean consistent;
  GError *error = NULL;

  if (!udisks_filesystem_call_check_finish (filesystem, &consistent, res, &error))
    {
      gdu_utils_show_error (GTK_WINDOW (window),
                            _("Error while checking filesystem"),
                            error);
      g_error_free (error);
    }
  else
    {
      GtkWidget *message_dialog;
      UDisksObjectInfo *info;
      UDisksBlock *block;
      UDisksObject *object;
      const gchar *name;
      gchar *s;

      object = UDISKS_OBJECT (g_dbus_interface_get_object (G_DBUS_INTERFACE (filesystem)));
      block = udisks_object_peek_block (object);
      g_assert (block != NULL);
      info = udisks_client_get_object_info (window->client, object);
      name = udisks_block_get_id_label (block);

      if (name == NULL || strlen (name) == 0)
        name = udisks_block_get_id_type (block);

      message_dialog = gtk_message_dialog_new_with_markup  (GTK_WINDOW (window),
                                                            GTK_DIALOG_MODAL,
                                                            GTK_MESSAGE_INFO,
                                                            GTK_BUTTONS_CLOSE,
                                                            "<big><b>%s</b></big>",
                                                            consistent ? _("Filesystem intact") : _("Filesystem damaged"));
      if (consistent)
        {
          s = g_strdup_printf (_("Filesystem %s on %s is undamaged."),
                               name, udisks_object_info_get_name (info));
        }
      else
        {
          /* show as result and not error message, because it's not a malfunction of GDU */
          s = g_strdup_printf (_("Filesystem %s on %s needs repairing."),
                               name, udisks_object_info_get_name (info));
        }

      gtk_message_dialog_format_secondary_markup (GTK_MESSAGE_DIALOG (message_dialog), "%s", s);
      gtk_dialog_run (GTK_DIALOG (message_dialog));

      gtk_widget_destroy (message_dialog);
      g_free (s);
    }
}

static void
fs_check_unmount_cb (GduWindow        *window,
                     GAsyncResult     *res,
                     gpointer          user_data)
{
  UDisksObject *object = UDISKS_OBJECT (user_data);
  GError *error = NULL;

  if (!gdu_window_ensure_unused_finish (window,
                                        res,
                                        &error))
    {
      gdu_utils_show_error (GTK_WINDOW (window),
                            _("Error unmounting filesystem"),
                            error);
      g_error_free (error);
    }
  else
    {
      UDisksFilesystem *filesystem;

      filesystem = udisks_object_peek_filesystem (object);
      udisks_filesystem_call_check (filesystem,
                                    g_variant_new ("a{sv}", NULL),
                                    NULL,
                                    (GAsyncReadyCallback) fs_check_cb,
                                    window);
    }

  g_object_unref (object);
}

static void
on_generic_menu_item_check (GtkMenuItem *menu_item,
                            gpointer     user_data)
{
  GduWindow *window = GDU_WINDOW (user_data);
  UDisksObject *object;
  GtkWidget *message_dialog, *ok_button;

  object = gdu_volume_grid_get_selected_device (GDU_VOLUME_GRID (window->volume_grid));
  g_assert (object != NULL);

  message_dialog = gtk_message_dialog_new_with_markup  (GTK_WINDOW (window),
                                                        GTK_DIALOG_MODAL,
                                                        GTK_MESSAGE_WARNING,
                                                        GTK_BUTTONS_OK_CANCEL,
                                                        "<big><b>%s</b></big>",
                                                        _("Confirm Check"));

  gtk_message_dialog_format_secondary_markup (GTK_MESSAGE_DIALOG (message_dialog), "%s",
                                              _("Depending on the amount of data the filesystem check takes longer time."));

  ok_button = gtk_dialog_get_widget_for_response (GTK_DIALOG (message_dialog), GTK_RESPONSE_OK);
  gtk_style_context_add_class (gtk_widget_get_style_context (ok_button), "suggested-action");

  if (gtk_dialog_run (GTK_DIALOG (message_dialog)) == GTK_RESPONSE_OK)
    gdu_window_ensure_unused (window, object, (GAsyncReadyCallback) fs_check_unmount_cb,
                              NULL, g_object_ref (object));

  gtk_widget_destroy (message_dialog);
}

#endif

/* ---------------------------------------------------------------------------------------------------- */

static void
on_generic_menu_item_edit_label (GtkMenuItem *menu_item,
                                 gpointer   user_data)
{
  GduWindow *window = GDU_WINDOW (user_data);
  UDisksObject *object;

  object = gdu_volume_grid_get_selected_device (GDU_VOLUME_GRID (window->volume_grid));
  g_assert (object != NULL);
  gdu_filesystem_dialog_show (window, object);
}

/* ---------------------------------------------------------------------------------------------------- */

static void
on_generic_menu_item_edit_partition (GtkMenuItem *menu_item,
                                     gpointer   user_data)
{
  GduWindow *window = GDU_WINDOW (user_data);
  UDisksObject *object;

  object = gdu_volume_grid_get_selected_device (GDU_VOLUME_GRID (window->volume_grid));
  g_assert (object != NULL);
  gdu_partition_dialog_show (window, object);
}

/* ---------------------------------------------------------------------------------------------------- */

static void
on_generic_menu_item_format_volume (GtkMenuItem *menu_item,
                                    gpointer   user_data)
{
  GduWindow *window = GDU_WINDOW (user_data);
  UDisksObject *object;

  object = gdu_volume_grid_get_selected_device (GDU_VOLUME_GRID (window->volume_grid));
  g_assert (object != NULL);
  gdu_format_volume_dialog_show (window, object);
}

/* ---------------------------------------------------------------------------------------------------- */

static void
on_generic_drive_menu_item_create_disk_image (GtkMenuItem *menu_item,
                                              gpointer   user_data)
{
  GduWindow *window = GDU_WINDOW (user_data);
  UDisksObject *object;

  object = gdu_volume_grid_get_block_object (GDU_VOLUME_GRID (window->volume_grid));
  g_assert (object != NULL);
  gdu_create_disk_image_dialog_show (window, object);
}

/* ---------------------------------------------------------------------------------------------------- */

static void
on_generic_drive_menu_item_restore_disk_image (GtkMenuItem *menu_item,
                                               gpointer   user_data)
{
  GduWindow *window = GDU_WINDOW (user_data);
  UDisksObject *object;

  object = gdu_volume_grid_get_block_object (GDU_VOLUME_GRID (window->volume_grid));
  g_assert (object != NULL);
  gdu_restore_disk_image_dialog_show (window, object, NULL);
}

/* ---------------------------------------------------------------------------------------------------- */

static void
on_generic_drive_menu_item_benchmark (GtkMenuItem *menu_item,
                                      gpointer   user_data)
{
  GduWindow *window = GDU_WINDOW (user_data);
  UDisksObject *object;

  object = gdu_volume_grid_get_block_object (GDU_VOLUME_GRID (window->volume_grid));
  g_assert (object != NULL);
  gdu_benchmark_dialog_show (window, object);
}

/* ---------------------------------------------------------------------------------------------------- */

static void
on_generic_menu_item_create_volume_image (GtkMenuItem *menu_item,
                                          gpointer   user_data)
{
  GduWindow *window = GDU_WINDOW (user_data);
  UDisksObject *object;

  object = gdu_volume_grid_get_selected_device (GDU_VOLUME_GRID (window->volume_grid));
  g_assert (object != NULL);
  gdu_create_disk_image_dialog_show (window, object);
}

/* ---------------------------------------------------------------------------------------------------- */

static void
on_generic_menu_item_restore_volume_image (GtkMenuItem *menu_item,
                                           gpointer   user_data)
{
  GduWindow *window = GDU_WINDOW (user_data);
  UDisksObject *object;

  object = gdu_volume_grid_get_selected_device (GDU_VOLUME_GRID (window->volume_grid));
  g_assert (object != NULL);
  gdu_restore_disk_image_dialog_show (window, object, NULL);
}

/* ---------------------------------------------------------------------------------------------------- */

static void
on_generic_menu_item_benchmark (GtkMenuItem *menu_item,
                                gpointer   user_data)
{
  GduWindow *window = GDU_WINDOW (user_data);
  UDisksObject *object;

  object = gdu_volume_grid_get_selected_device (GDU_VOLUME_GRID (window->volume_grid));
  g_assert (object != NULL);
  gdu_benchmark_dialog_show (window, object);
}

/* ---------------------------------------------------------------------------------------------------- */

static void
on_generic_drive_menu_item_format_disk (GtkMenuItem *menu_item,
                                        gpointer   user_data)
{
  GduWindow *window = GDU_WINDOW (user_data);
  UDisksObject *object;

  object = gdu_volume_grid_get_block_object (GDU_VOLUME_GRID (window->volume_grid));
  g_assert (object != NULL);
  gdu_format_disk_dialog_show (window, object);
}

/* ---------------------------------------------------------------------------------------------------- */

static void
on_generic_menu_item_configure_fstab (GtkMenuItem *menu_item,
                                      gpointer   user_data)
{
  GduWindow *window = GDU_WINDOW (user_data);
  UDisksObject *object;
  object = gdu_volume_grid_get_selected_device (GDU_VOLUME_GRID (window->volume_grid));
  if (object == NULL)
    object = gdu_volume_grid_get_block_object (GDU_VOLUME_GRID (window->volume_grid));
  gdu_fstab_dialog_show (window, object);
}

static void
on_generic_drive_menu_item_view_smart (GtkMenuItem *menu_item,
                                       gpointer     user_data)
{
  GduWindow *window = GDU_WINDOW (user_data);
  gdu_ata_smart_dialog_show (window, window->current_object);
}

static void
on_generic_drive_menu_item_disk_settings (GtkMenuItem *menu_item,
                                          gpointer     user_data)
{
  GduWindow *window = GDU_WINDOW (user_data);
  gdu_disk_settings_dialog_show (window, window->current_object);
}

/* ---------------------------------------------------------------------------------------------------- */

static void
ata_pm_standby_cb (GObject      *source_object,
                   GAsyncResult *res,
                   gpointer      user_data)
{
  GduWindow *window = GDU_WINDOW (user_data);
  GError *error = NULL;

  error = NULL;
  if (!udisks_drive_ata_call_pm_standby_finish (UDISKS_DRIVE_ATA (source_object),
                                                res,
                                                &error))
    {
      gdu_utils_show_error (GTK_WINDOW (window),
                            _("An error occurred when trying to put the drive into standby mode"),
                            error);
      g_clear_error (&error);
    }

  g_object_unref (window);
}

static void
on_generic_drive_menu_item_standby_now (GtkMenuItem *menu_item,
                                        gpointer     user_data)
{
  GduWindow *window = GDU_WINDOW (user_data);
  UDisksDriveAta *ata;

  ata = udisks_object_peek_drive_ata (window->current_object);
  if (ata != NULL)
    {
      udisks_drive_ata_call_pm_standby (ata,
                                        g_variant_new ("a{sv}", NULL), /* options */
                                        NULL, /* GCancellable */
                                        (GAsyncReadyCallback) ata_pm_standby_cb,
                                        g_object_ref (window));
    }
  else
    {
      g_warning ("object is not an ATA drive");
    }
}

/* ---------------------------------------------------------------------------------------------------- */

static void
ata_pm_wakeup_cb (GObject      *source_object,
                  GAsyncResult *res,
                  gpointer      user_data)
{
  GduWindow *window = GDU_WINDOW (user_data);
  GError *error = NULL;

  error = NULL;
  if (!udisks_drive_ata_call_pm_wakeup_finish (UDISKS_DRIVE_ATA (source_object),
                                               res,
                                               &error))
    {
      gdu_utils_show_error (GTK_WINDOW (window),
                            _("An error occurred when trying to wake up the drive from standby mode"),
                            error);
      g_clear_error (&error);
    }

  g_object_unref (window);
}

static void
on_generic_drive_menu_item_resume_now (GtkMenuItem *menu_item,
                                       gpointer     user_data)
{
  GduWindow *window = GDU_WINDOW (user_data);
  UDisksDriveAta *ata;

  ata = udisks_object_peek_drive_ata (window->current_object);
  if (ata != NULL)
    {
      udisks_drive_ata_call_pm_wakeup (ata,
                                       g_variant_new ("a{sv}", NULL), /* options */
                                       NULL, /* GCancellable */
                                       (GAsyncReadyCallback) ata_pm_wakeup_cb,
                                       g_object_ref (window));
    }
  else
    {
      g_warning ("object is not an ATA drive");
    }
}

/* ---------------------------------------------------------------------------------------------------- */

static void
power_off_cb (GObject      *source_object,
              GAsyncResult *res,
              gpointer      user_data)
{
  GduWindow *window = GDU_WINDOW (user_data);
  GError *error = NULL;

  if (!udisks_drive_call_power_off_finish (UDISKS_DRIVE (source_object),
                                           res,
                                           &error))
    {
      gdu_utils_show_error (GTK_WINDOW (window),
                            _("Error powering off drive"),
                            error);
      g_clear_error (&error);
    }
  g_object_unref (window);
}

static void
power_off_ensure_unused_cb (GduWindow     *window,
                            GAsyncResult  *res,
                            gpointer       user_data)
{
  UDisksObject *object = UDISKS_OBJECT (user_data);
  if (gdu_window_ensure_unused_list_finish (window, res, NULL))
    {
      UDisksDrive *drive = udisks_object_peek_drive (object);
      udisks_drive_call_power_off (drive,
                                   g_variant_new ("a{sv}", NULL), /* options */
                                   NULL, /* GCancellable */
                                   (GAsyncReadyCallback) power_off_cb,
                                   g_object_ref (window));
    }
  g_object_unref (object);
}

static void
do_power_off (GduWindow *window)
{
  UDisksObject *object;
  GList *objects = NULL;
  GList *siblings, *l;
  UDisksDrive *drive;
  const gchar *heading;
  const gchar *message;

  object = window->current_object;
  drive = udisks_object_peek_drive (object);
  objects = g_list_append (NULL, object);

  /* include other drives this will affect */
  siblings = udisks_client_get_drive_siblings (window->client, drive);
  for (l = siblings; l != NULL; l = l->next)
    {
      UDisksDrive *sibling = UDISKS_DRIVE (l->data);
      UDisksObject *sibling_object = (UDisksObject *) g_dbus_interface_get_object (G_DBUS_INTERFACE (sibling));
      if (sibling_object != NULL)
        objects = g_list_append (objects, sibling_object);
    }

  if (siblings != NULL)
    {
      /* Translators: Heading for powering off a device with multiple drives */
      heading = _("Are you sure you want to power off the drives?");
      /* Translators: Message for powering off a device with multiple drives */
      message = _("This operation will prepare the system for the following drives to be powered down and removed.");
      if (!gdu_utils_show_confirmation (GTK_WINDOW (window),
                                        heading,
                                        message,
                                        _("_Power Off"),
                                        NULL, NULL,
                                        window->client, objects))
        goto out;
    }

  gdu_window_ensure_unused_list (window,
                                 objects,
                                 (GAsyncReadyCallback) power_off_ensure_unused_cb,
                                 NULL, /* GCancellable */
                                 g_object_ref (object));
 out:
  g_list_free_full (siblings, g_object_unref);
  g_list_free (objects);
}

static void
on_generic_drive_menu_item_power_off (GtkMenuItem *menu_item,
                                      gpointer     user_data)
{
  GduWindow *window = GDU_WINDOW (user_data);
  do_power_off (window);
}

static void
on_devtab_drive_power_off_button_clicked (GtkButton *button,
                                          gpointer   user_data)
{
  GduWindow *window = GDU_WINDOW (user_data);
  do_power_off (window);
}

/* ---------------------------------------------------------------------------------------------------- */

static void
on_generic_menu_item_configure_crypttab (GtkMenuItem *menu_item,
                                         gpointer   user_data)
{
  GduWindow *window = GDU_WINDOW (user_data);
  UDisksObject *object;

  object = gdu_volume_grid_get_selected_device (GDU_VOLUME_GRID (window->volume_grid));
  if (object == NULL)
    object = gdu_volume_grid_get_block_object (GDU_VOLUME_GRID (window->volume_grid));
  gdu_crypttab_dialog_show (window, object);
}

static void
on_generic_menu_item_change_passphrase (GtkMenuItem *menu_item,
                                        gpointer   user_data)
{
  GduWindow *window = GDU_WINDOW (user_data);
  UDisksObject *object;

  object = gdu_volume_grid_get_selected_device (GDU_VOLUME_GRID (window->volume_grid));
  if (object == NULL)
    object = gdu_volume_grid_get_block_object (GDU_VOLUME_GRID (window->volume_grid));
  gdu_change_passphrase_dialog_show (window, object);
}

/* ---------------------------------------------------------------------------------------------------- */

static void
mount_cb (UDisksFilesystem *filesystem,
          GAsyncResult     *res,
          gpointer          user_data)
{
  GduWindow *window = GDU_WINDOW (user_data);
  GError *error;

  error = NULL;
  if (!udisks_filesystem_call_mount_finish (filesystem,
                                            NULL, /* out_mount_path */
                                            res,
                                            &error))
    {
      gdu_utils_show_error (GTK_WINDOW (window),
                            _("Error mounting filesystem"),
                            error);
      g_error_free (error);
    }
  g_object_unref (window);
}

static void
on_mount_tool_button_clicked (GtkToolButton *button, gpointer user_data)
{
  GduWindow *window = GDU_WINDOW (user_data);
  UDisksObject *object;
  UDisksFilesystem *filesystem;

  object = gdu_volume_grid_get_selected_device (GDU_VOLUME_GRID (window->volume_grid));
  filesystem = udisks_object_peek_filesystem (object);
  udisks_filesystem_call_mount (filesystem,
                                g_variant_new ("a{sv}", NULL), /* options */
                                NULL, /* cancellable */
                                (GAsyncReadyCallback) mount_cb,
                                g_object_ref (window));
}

/* ---------------------------------------------------------------------------------------------------- */

static void
unmount_cb (GduWindow        *window,
            GAsyncResult     *res,
            gpointer          user_data)
{
  GError *error = NULL;

  if (!gdu_window_ensure_unused_finish (window,
                                        res,
                                        &error))
    {
      gdu_utils_show_error (GTK_WINDOW (window),
                            _("Error unmounting filesystem"),
                            error);
      g_error_free (error);
    }
}

static void
on_unmount_tool_button_clicked (GtkToolButton *button, gpointer user_data)
{
  GduWindow *window = GDU_WINDOW (user_data);
  UDisksObject *object;

  object = gdu_volume_grid_get_selected_device (GDU_VOLUME_GRID (window->volume_grid));
  gdu_window_ensure_unused (window,
                            object,
                            (GAsyncReadyCallback) unmount_cb,
                            NULL,
                            NULL);
}

/* ---------------------------------------------------------------------------------------------------- */

static void
on_generic_tool_button_clicked (GtkToolButton *button, gpointer user_data)
{
  GduWindow *window = GDU_WINDOW (user_data);

  update_all (window, FALSE);

  gtk_menu_popup_at_widget (GTK_MENU (window->generic_menu),
                            window->toolbutton_generic_menu,
                            GDK_GRAVITY_SOUTH_WEST,
                            GDK_GRAVITY_NORTH_WEST,
                            NULL);
}

/* ---------------------------------------------------------------------------------------------------- */

static void
on_partition_create_tool_button_clicked (GtkToolButton *button, gpointer user_data)
{
  GduWindow *window = GDU_WINDOW (user_data);
  UDisksObject *object;

  object = gdu_volume_grid_get_block_object (GDU_VOLUME_GRID (window->volume_grid));
  g_assert (object != NULL);
  gdu_create_partition_dialog_show (window,
                                    object,
                                    gdu_volume_grid_get_selected_offset (GDU_VOLUME_GRID (window->volume_grid)),
                                    gdu_volume_grid_get_selected_size (GDU_VOLUME_GRID (window->volume_grid)));
}

/* ---------------------------------------------------------------------------------------------------- */

static void
partition_delete_cb (UDisksPartition *partition,
                     GAsyncResult    *res,
                     gpointer         user_data)
{
  GduWindow *window = GDU_WINDOW (user_data);
  GError *error;

  error = NULL;
  if (!udisks_partition_call_delete_finish (partition,
                                            res,
                                            &error))
    {
      gdu_utils_show_error (GTK_WINDOW (window),
                            _("Error deleting partition"),
                            error);
      g_error_free (error);
    }
  g_object_unref (window);
}

static void
partition_delete_ensure_unused_cb (GduWindow     *window,
                                   GAsyncResult  *res,
                                   gpointer       user_data)
{
  UDisksObject *object = UDISKS_OBJECT (user_data);
  if (gdu_window_ensure_unused_finish (window, res, NULL))
    {
      UDisksPartition *partition;
      partition = udisks_object_peek_partition (object);
      udisks_partition_call_delete (partition,
                                    g_variant_new ("a{sv}", NULL), /* options */
                                    NULL, /* cancellable */
                                    (GAsyncReadyCallback) partition_delete_cb,
                                    g_object_ref (window));

    }
  g_object_unref (object);
}

static void
on_partition_delete_tool_button_clicked (GtkToolButton *button, gpointer user_data)
{
  GduWindow *window = GDU_WINDOW (user_data);
  UDisksObject *object;
  GList *objects = NULL;

  object = gdu_volume_grid_get_selected_device (GDU_VOLUME_GRID (window->volume_grid));
  objects = g_list_append (NULL, object);
  if (!gdu_utils_show_confirmation (GTK_WINDOW (window),
                                    _("Are you sure you want to delete the partition?"),
                                    _("All data on the partition will be lost"),
                                    _("_Delete"),
                                    NULL, NULL,
                                    window->client, objects))
    goto out;

  gdu_window_ensure_unused (window,
                            object,
                            (GAsyncReadyCallback) partition_delete_ensure_unused_cb,
                            NULL, /* GCancellable */
                            g_object_ref (object));

 out:
  g_list_free (objects);
}

/* ---------------------------------------------------------------------------------------------------- */

static void
eject_cb (UDisksDrive  *drive,
          GAsyncResult *res,
          gpointer      user_data)
{
  GduWindow *window = GDU_WINDOW (user_data);
  GError *error;

  error = NULL;
  if (!udisks_drive_call_eject_finish (drive,
                                       res,
                                       &error))
    {
      gdu_utils_show_error (GTK_WINDOW (window),
                            _("Error ejecting media"),
                            error);
      g_error_free (error);
    }
  g_object_unref (window);
}


static void
eject_ensure_unused_cb (GduWindow     *window,
                        GAsyncResult  *res,
                        gpointer       user_data)
{
  UDisksObject *object = UDISKS_OBJECT (user_data);
  if (gdu_window_ensure_unused_finish (window, res, NULL))
    {
      UDisksDrive *drive = udisks_object_peek_drive (object);
      udisks_drive_call_eject (drive,
                               g_variant_new ("a{sv}", NULL), /* options */
                               NULL, /* cancellable */
                               (GAsyncReadyCallback) eject_cb,
                               g_object_ref (window));
    }
  g_object_unref (object);
}

static void
on_devtab_drive_eject_button_clicked (GtkButton *button,
                                      gpointer   user_data)
{
  GduWindow *window = GDU_WINDOW (user_data);
  gdu_window_ensure_unused (window,
                            window->current_object,
                            (GAsyncReadyCallback) eject_ensure_unused_cb,
                            NULL, /* GCancellable */
                            g_object_ref (window->current_object));
}

/* ---------------------------------------------------------------------------------------------------- */

static void
on_unlock_tool_button_clicked (GtkToolButton *button, gpointer user_data)
{
  GduWindow *window = GDU_WINDOW (user_data);
  UDisksObject *object;

  object = gdu_volume_grid_get_selected_device (GDU_VOLUME_GRID (window->volume_grid));
  g_assert (object != NULL);
  gdu_unlock_dialog_show (window, object);
}

/* ---------------------------------------------------------------------------------------------------- */

static void
lock_cb (GduWindow       *window,
         GAsyncResult    *res,
         gpointer         user_data)
{
  GError *error = NULL;

  if (!gdu_window_ensure_unused_finish (window,
                                        res,
                                        &error))
    {
      gdu_utils_show_error (GTK_WINDOW (window),
                            _("Error locking encrypted device"),
                            error);
      g_error_free (error);
    }
}

static void
on_lock_tool_button_clicked (GtkToolButton *button, gpointer user_data)
{
  GduWindow *window = GDU_WINDOW (user_data);
  UDisksObject *object;

  object = gdu_volume_grid_get_selected_device (GDU_VOLUME_GRID (window->volume_grid));
  gdu_window_ensure_unused (window,
                            object,
                            (GAsyncReadyCallback) lock_cb,
                            NULL, /* GCancellable */
                            NULL);
}

/* ---------------------------------------------------------------------------------------------------- */

static void
swapspace_start_cb (UDisksSwapspace  *swapspace,
                    GAsyncResult     *res,
                    gpointer          user_data)
{
  GduWindow *window = GDU_WINDOW (user_data);
  GError *error;

  error = NULL;
  if (!udisks_swapspace_call_start_finish (swapspace,
                                           res,
                                           &error))
    {
      gdu_utils_show_error (GTK_WINDOW (window),
                            _("Error starting swap"),
                            error);
      g_error_free (error);
    }
  g_object_unref (window);
}

static void
on_activate_swap_tool_button_clicked (GtkToolButton *button, gpointer user_data)
{
  GduWindow *window = GDU_WINDOW (user_data);
  UDisksObject *object;
  UDisksSwapspace *swapspace;

  object = gdu_volume_grid_get_selected_device (GDU_VOLUME_GRID (window->volume_grid));
  swapspace = udisks_object_peek_swapspace (object);
  udisks_swapspace_call_start (swapspace,
                               g_variant_new ("a{sv}", NULL), /* options */
                               NULL, /* cancellable */
                               (GAsyncReadyCallback) swapspace_start_cb,
                               g_object_ref (window));
}

static void
swapspace_stop_cb (UDisksSwapspace  *swapspace,
                   GAsyncResult     *res,
                   gpointer          user_data)
{
  GduWindow *window = GDU_WINDOW (user_data);
  GError *error;

  error = NULL;
  if (!udisks_swapspace_call_stop_finish (swapspace,
                                          res,
                                          &error))
    {
      gdu_utils_show_error (GTK_WINDOW (window),
                            _("Error stopping swap"),
                            error);
      g_error_free (error);
    }
  g_object_unref (window);
}

static void
on_deactivate_swap_tool_button_clicked (GtkToolButton *button, gpointer user_data)
{
  GduWindow *window = GDU_WINDOW (user_data);
  UDisksObject *object;
  UDisksSwapspace *swapspace;

  object = gdu_volume_grid_get_selected_device (GDU_VOLUME_GRID (window->volume_grid));
  swapspace = udisks_object_peek_swapspace (object);
  udisks_swapspace_call_stop (swapspace,
                              g_variant_new ("a{sv}", NULL), /* options */
                              NULL, /* cancellable */
                              (GAsyncReadyCallback) swapspace_stop_cb,
                              g_object_ref (window));
}

/* ---------------------------------------------------------------------------------------------------- */

static void
loop_set_autoclear_cb (UDisksLoop      *loop,
                       GAsyncResult    *res,
                       gpointer         user_data)
{
  GduWindow *window = GDU_WINDOW (user_data);
  GError *error;

  /* in case of error, make sure the GtkSwitch:active corresponds to UDisksLoop:autoclear */
  update_all (window, FALSE);

  error = NULL;
  if (!udisks_loop_call_set_autoclear_finish (loop,
                                              res,
                                              &error))
    {
      gdu_utils_show_error (GTK_WINDOW (window),
                            _("Error setting autoclear flag"),
                            error);
      g_error_free (error);
    }
  g_object_unref (window);
}

static void
on_devtab_loop_autoclear_switch_notify_active (GObject    *gobject,
                                               GParamSpec *pspec,
                                               gpointer    user_data)
{
  GduWindow *window = GDU_WINDOW (user_data);
  UDisksLoop *loop;
  gboolean sw_value;

  g_return_if_fail (window->current_object != NULL);

  loop = udisks_object_peek_loop (window->current_object);
  if (loop == NULL)
    {
      g_warning ("current object is not a loop object");
      goto out;
    }

  sw_value = !! gtk_switch_get_active (GTK_SWITCH (gobject));
  if (sw_value != (!!udisks_loop_get_autoclear (loop)))
    {
      udisks_loop_call_set_autoclear (loop,
                                      sw_value,
                                      g_variant_new ("a{sv}", NULL), /* options */
                                      NULL, /* cancellable */
                                      (GAsyncReadyCallback) loop_set_autoclear_cb,
                                      g_object_ref (window));
    }

 out:
  ;
}

/* ---------------------------------------------------------------------------------------------------- */

static void
drive_job_cancel_cb (UDisksJob       *job,
                     GAsyncResult    *res,
                     gpointer         user_data)
{
  GduWindow *window = GDU_WINDOW (user_data);
  GError *error = NULL;

  if (!udisks_job_call_cancel_finish (job, res, &error))
    {
      gdu_utils_show_error (GTK_WINDOW (window),
                            _("Error canceling job"),
                            error);
      g_error_free (error);
    }
  g_object_unref (window);
}

static void
on_drive_job_cancel_button_clicked (GtkButton   *button,
                                    gpointer     user_data)
{
  GduWindow *window = GDU_WINDOW (user_data);
  GList *jobs;

  jobs = udisks_client_get_jobs_for_object (window->client, window->current_object);
  jobs = g_list_concat (jobs, gdu_application_get_local_jobs_for_object (window->application, window->current_object));
  /* if there are no jobs on the drive, look at the first block object */
  if (jobs == NULL)
    {
      GList *blocks;
      blocks = get_top_level_blocks_for_drive (window, g_dbus_object_get_object_path (G_DBUS_OBJECT (window->current_object)));
      blocks = g_list_sort (blocks, (GCompareFunc) block_compare_on_preferred);
      if (blocks != NULL)
        {
          UDisksObject *block_object = UDISKS_OBJECT (blocks->data);
          jobs = udisks_client_get_jobs_for_object (window->client, block_object);
          jobs = g_list_concat (jobs, gdu_application_get_local_jobs_for_object (window->application, block_object));
        }
      g_list_foreach (blocks, (GFunc) g_object_unref, NULL);
      g_list_free (blocks);
    }
  if (jobs != NULL)
    {
      UDisksJob *job = UDISKS_JOB (jobs->data);
      if (GDU_IS_LOCAL_JOB (job))
        {
          gdu_local_job_canceled (GDU_LOCAL_JOB (job));
        }
      else
        {
          udisks_job_call_cancel (job,
                                  g_variant_new ("a{sv}", NULL), /* options */
                                  NULL, /* cancellable */
                                  (GAsyncReadyCallback) drive_job_cancel_cb,
                                  g_object_ref (window));
        }
    }
  g_list_foreach (jobs, (GFunc) g_object_unref, NULL);
  g_list_free (jobs);
}

/* ---------------------------------------------------------------------------------------------------- */

static void
job_cancel_cb (UDisksJob       *job,
               GAsyncResult    *res,
               gpointer         user_data)
{
  GduWindow *window = GDU_WINDOW (user_data);
  GError *error = NULL;

  if (!udisks_job_call_cancel_finish (job, res, &error))
    {
      gdu_utils_show_error (GTK_WINDOW (window),
                            _("Error canceling job"),
                            error);
      g_error_free (error);
    }
  g_object_unref (window);
}

static void
on_job_cancel_button_clicked (GtkButton    *button,
                              gpointer      user_data)
{
  GduWindow *window = GDU_WINDOW (user_data);
  GList *jobs;
  UDisksObject *object;

  object = gdu_volume_grid_get_selected_device (GDU_VOLUME_GRID (window->volume_grid));
  g_assert (object != NULL);

  jobs = udisks_client_get_jobs_for_object (window->client, object);
  jobs = g_list_concat (jobs, gdu_application_get_local_jobs_for_object (window->application, object));
  if (jobs != NULL)
    {
      UDisksJob *job = UDISKS_JOB (jobs->data);
      if (GDU_IS_LOCAL_JOB (job))
        {
          gdu_local_job_canceled (GDU_LOCAL_JOB (job));
        }
      else
        {
          udisks_job_call_cancel (job,
                                  g_variant_new ("a{sv}", NULL), /* options */
                                  NULL, /* cancellable */
                                  (GAsyncReadyCallback) job_cancel_cb,
                                  g_object_ref (window));
        }
    }
  g_list_foreach (jobs, (GFunc) g_object_unref, NULL);
  g_list_free (jobs);
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
on_activate_link (GtkLabel    *label,
                  const gchar *uri,
                  gpointer     user_data)
{
  GduWindow *window = GDU_WINDOW (user_data);
  gboolean handled = FALSE;

  if (g_str_has_prefix (uri, "x-udisks://"))
    {
      UDisksObject *object = udisks_client_peek_object (window->client, uri + strlen ("x-udisks://"));
      if (object != NULL)
        {
          select_object (window, object);
          handled = TRUE;
        }
    }

  return handled;
}

/* ---------------------------------------------------------------------------------------------------- */

typedef struct {
  GTask *task;
  GduWindow *window;
} EnsureListData;

static void
ensure_list_data_free (EnsureListData *data)
{
  g_object_unref (data->window);
  g_object_unref (data->task);
  g_slice_free (EnsureListData, data);
}

static void
ensure_list_cb (GObject      *source_object,
                GAsyncResult *res,
                gpointer      user_data)
{
  EnsureListData *data = user_data;
  g_task_set_task_data (data->task, g_object_ref (res), g_object_unref);
  g_task_return_pointer (data->task, NULL, NULL);
  ensure_list_data_free (data);
}

void
gdu_window_ensure_unused_list (GduWindow            *window,
                               GList                *objects,
                               GAsyncReadyCallback   callback,
                               GCancellable         *cancellable,
                               gpointer              user_data)
{
  EnsureListData *data = g_slice_new0 (EnsureListData);
  data->window = g_object_ref (window);
  data->task = g_task_new (G_OBJECT (window),
                           cancellable,
                           callback,
                           user_data);
  gdu_utils_ensure_unused_list (window->client,
                                GTK_WINDOW (window),
                                objects,
                                ensure_list_cb,
                                cancellable,
                                data);
}

gboolean
gdu_window_ensure_unused_list_finish (GduWindow     *window,
                                      GAsyncResult  *res,
                                      GError       **error)
{
  GTask *task = G_TASK (res);

  g_return_val_if_fail (G_IS_TASK (res), FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  return gdu_utils_ensure_unused_list_finish (window->client,
                                              G_ASYNC_RESULT (g_task_get_task_data (task)),
                                              error);
}

/* ---------------------------------------------------------------------------------------------------- */

void
gdu_window_ensure_unused (GduWindow            *window,
                          UDisksObject         *object,
                          GAsyncReadyCallback   callback,
                          GCancellable         *cancellable,
                          gpointer              user_data)
{
  GList *list = g_list_append (NULL, object);
  gdu_window_ensure_unused_list (window, list, callback, cancellable, user_data);
  g_list_free (list);
}

gboolean
gdu_window_ensure_unused_finish (GduWindow     *window,
                                 GAsyncResult  *res,
                                 GError       **error)
{
  return gdu_window_ensure_unused_list_finish (window, res, error);
}

/* ---------------------------------------------------------------------------------------------------- */
