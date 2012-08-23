/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*-
 *
 * Copyright (C) 2008-2012 Red Hat, Inc.
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

/* Keep in sync with tabs in disks.ui file */
typedef enum
{
  DETAILS_PAGE_NOT_SELECTED,
  DETAILS_PAGE_NOT_IMPLEMENTED,
  DETAILS_PAGE_DEVICE,
} DetailsPage;

struct _GduWindow
{
  GtkApplicationWindow parent_instance;

  GduApplication *application;
  UDisksClient *client;

  const gchar *builder_path;
  GtkBuilder *builder;
  GduDeviceTreeModel *model;

  DetailsPage current_page;
  UDisksObject *current_object;

  GtkWidget *volume_grid;

  GtkWidget *main_hpane;
  GtkWidget *details_notebook;
  GtkWidget *device_scrolledwindow;
  GtkWidget *device_treeview;
  GtkWidget *device_toolbar;
  GtkWidget *device_toolbar_attach_disk_image_button;
  GtkWidget *device_toolbar_detach_disk_image_button;
  GtkWidget *devtab_drive_box;
  GtkWidget *devtab_drive_vbox;
  GtkWidget *devtab_drive_buttonbox;
  GtkWidget *devtab_drive_eject_button;
  GtkWidget *devtab_drive_generic_button;
  GtkWidget *devtab_drive_name_label;
  GtkWidget *devtab_drive_devices_label;
  GtkWidget *devtab_drive_image;
  GtkWidget *devtab_table;
  GtkWidget *devtab_drive_table;
  GtkWidget *devtab_grid_hbox;
  GtkWidget *devtab_volumes_label;
  GtkWidget *devtab_grid_toolbar;
  GtkWidget *devtab_action_generic;
  GtkWidget *devtab_action_partition_create;
  GtkWidget *devtab_action_partition_delete;
  GtkWidget *devtab_action_mount;
  GtkWidget *devtab_action_unmount;
  GtkWidget *devtab_action_eject;
  GtkWidget *devtab_action_unlock;
  GtkWidget *devtab_action_lock;
  GtkWidget *devtab_action_activate_swap;
  GtkWidget *devtab_action_deactivate_swap;
  GtkWidget *devtab_action_generic_drive;

  GtkWidget *generic_drive_menu;
  GtkWidget *generic_drive_menu_item_view_smart;
  GtkWidget *generic_drive_menu_item_disk_settings;
  GtkWidget *generic_drive_menu_item_standby_now;
  GtkWidget *generic_drive_menu_item_resume_now;
  GtkWidget *generic_drive_menu_item_format_disk;
  GtkWidget *generic_drive_menu_item_create_disk_image;
  GtkWidget *generic_drive_menu_item_restore_disk_image;
  GtkWidget *generic_drive_menu_item_benchmark;

  GtkWidget *generic_menu;
  GtkWidget *generic_menu_item_configure_fstab;
  GtkWidget *generic_menu_item_configure_crypttab;
  GtkWidget *generic_menu_item_change_passphrase;
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
};

static const struct {
  goffset offset;
  const gchar *name;
} widget_mapping[] = {
  {G_STRUCT_OFFSET (GduWindow, main_hpane), "main-hpane"},
  {G_STRUCT_OFFSET (GduWindow, device_scrolledwindow), "device-tree-scrolledwindow"},
  {G_STRUCT_OFFSET (GduWindow, device_toolbar), "device-tree-add-remove-toolbar"},
  {G_STRUCT_OFFSET (GduWindow, device_toolbar_attach_disk_image_button), "device-tree-attach-disk-image-button"},
  {G_STRUCT_OFFSET (GduWindow, device_toolbar_detach_disk_image_button), "device-tree-detach-disk-image-button"},
  {G_STRUCT_OFFSET (GduWindow, device_treeview), "device-tree-treeview"},
  {G_STRUCT_OFFSET (GduWindow, details_notebook), "disks-notebook"},
  {G_STRUCT_OFFSET (GduWindow, devtab_drive_table), "devtab-drive-table"},
  {G_STRUCT_OFFSET (GduWindow, devtab_drive_box), "devtab-drive-box"},
  {G_STRUCT_OFFSET (GduWindow, devtab_drive_vbox), "devtab-drive-vbox"},
  {G_STRUCT_OFFSET (GduWindow, devtab_drive_buttonbox), "devtab-drive-buttonbox"},
  {G_STRUCT_OFFSET (GduWindow, devtab_drive_eject_button), "devtab-drive-eject-button"},
  {G_STRUCT_OFFSET (GduWindow, devtab_drive_generic_button), "devtab-drive-generic-button"},
  {G_STRUCT_OFFSET (GduWindow, devtab_drive_name_label), "devtab-drive-name-label"},
  {G_STRUCT_OFFSET (GduWindow, devtab_drive_devices_label), "devtab-drive-devices-label"},
  {G_STRUCT_OFFSET (GduWindow, devtab_drive_image), "devtab-drive-image"},
  {G_STRUCT_OFFSET (GduWindow, devtab_table), "devtab-table"},
  {G_STRUCT_OFFSET (GduWindow, devtab_grid_hbox), "devtab-grid-hbox"},
  {G_STRUCT_OFFSET (GduWindow, devtab_volumes_label), "devtab-volumes-label"},
  {G_STRUCT_OFFSET (GduWindow, devtab_grid_toolbar), "devtab-grid-toolbar"},
  {G_STRUCT_OFFSET (GduWindow, devtab_action_generic), "devtab-action-generic"},
  {G_STRUCT_OFFSET (GduWindow, devtab_action_partition_create), "devtab-action-partition-create"},
  {G_STRUCT_OFFSET (GduWindow, devtab_action_partition_delete), "devtab-action-partition-delete"},
  {G_STRUCT_OFFSET (GduWindow, devtab_action_mount), "devtab-action-mount"},
  {G_STRUCT_OFFSET (GduWindow, devtab_action_unmount), "devtab-action-unmount"},
  {G_STRUCT_OFFSET (GduWindow, devtab_action_eject), "devtab-action-eject"},
  {G_STRUCT_OFFSET (GduWindow, devtab_action_unlock), "devtab-action-unlock"},
  {G_STRUCT_OFFSET (GduWindow, devtab_action_lock), "devtab-action-lock"},
  {G_STRUCT_OFFSET (GduWindow, devtab_action_activate_swap), "devtab-action-activate-swap"},
  {G_STRUCT_OFFSET (GduWindow, devtab_action_deactivate_swap), "devtab-action-deactivate-swap"},
  {G_STRUCT_OFFSET (GduWindow, devtab_action_generic_drive), "devtab-action-generic-drive"},

  {G_STRUCT_OFFSET (GduWindow, devtab_loop_autoclear_switch), "devtab-loop-autoclear-switch"},

  {G_STRUCT_OFFSET (GduWindow, generic_drive_menu), "generic-drive-menu"},
  {G_STRUCT_OFFSET (GduWindow, generic_drive_menu_item_format_disk), "generic-drive-menu-item-format-disk"},
  {G_STRUCT_OFFSET (GduWindow, generic_drive_menu_item_create_disk_image), "generic-drive-menu-item-create-disk-image"},
  {G_STRUCT_OFFSET (GduWindow, generic_drive_menu_item_restore_disk_image), "generic-drive-menu-item-restore-disk-image"},
  {G_STRUCT_OFFSET (GduWindow, generic_drive_menu_item_benchmark), "generic-drive-menu-item-benchmark"},
  {G_STRUCT_OFFSET (GduWindow, generic_drive_menu_item_view_smart), "generic-drive-menu-item-view-smart"},
  {G_STRUCT_OFFSET (GduWindow, generic_drive_menu_item_disk_settings), "generic-drive-menu-item-disk-settings"},
  {G_STRUCT_OFFSET (GduWindow, generic_drive_menu_item_standby_now), "generic-drive-menu-item-standby-now"},
  {G_STRUCT_OFFSET (GduWindow, generic_drive_menu_item_resume_now), "generic-drive-menu-item-resume-now"},

  {G_STRUCT_OFFSET (GduWindow, generic_menu), "generic-menu"},
  {G_STRUCT_OFFSET (GduWindow, generic_menu_item_configure_fstab), "generic-menu-item-configure-fstab"},
  {G_STRUCT_OFFSET (GduWindow, generic_menu_item_configure_crypttab), "generic-menu-item-configure-crypttab"},
  {G_STRUCT_OFFSET (GduWindow, generic_menu_item_change_passphrase), "generic-menu-item-change-passphrase"},
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

typedef enum
{
  SHOW_FLAGS_NONE                    = 0,

  /* device toolbar */
  SHOW_FLAGS_DETACH_DISK_IMAGE       = (1<<0),

  /* drive buttonbox */
  SHOW_FLAGS_EJECT_BUTTON            = (1<<1),

  /* volume toolbar */
  SHOW_FLAGS_PARTITION_CREATE_BUTTON = (1<<2),
  SHOW_FLAGS_PARTITION_DELETE_BUTTON = (1<<3),
  SHOW_FLAGS_MOUNT_BUTTON            = (1<<4),
  SHOW_FLAGS_UNMOUNT_BUTTON          = (1<<5),
  SHOW_FLAGS_ACTIVATE_SWAP_BUTTON    = (1<<6),
  SHOW_FLAGS_DEACTIVATE_SWAP_BUTTON  = (1<<7),
  SHOW_FLAGS_ENCRYPTED_UNLOCK_BUTTON = (1<<8),
  SHOW_FLAGS_ENCRYPTED_LOCK_BUTTON   = (1<<9),

  /* generic drive menu */
  SHOW_FLAGS_DISK_POPUP_MENU_FORMAT_DISK           = (1<<10),
  SHOW_FLAGS_DISK_POPUP_MENU_CREATE_DISK_IMAGE     = (1<<11),
  SHOW_FLAGS_DISK_POPUP_MENU_RESTORE_DISK_IMAGE    = (1<<12),
  SHOW_FLAGS_DISK_POPUP_MENU_BENCHMARK             = (1<<13),
  SHOW_FLAGS_DISK_POPUP_MENU_VIEW_SMART            = (1<<14),
  SHOW_FLAGS_DISK_POPUP_MENU_DISK_SETTINGS         = (1<<15),
  SHOW_FLAGS_DISK_POPUP_MENU_STANDBY_NOW           = (1<<16),
  SHOW_FLAGS_DISK_POPUP_MENU_RESUME_NOW            = (1<<17),

  /* generic volume menu */
  SHOW_FLAGS_POPUP_MENU_CONFIGURE_FSTAB       = (1<<20),
  SHOW_FLAGS_POPUP_MENU_CONFIGURE_CRYPTTAB    = (1<<21),
  SHOW_FLAGS_POPUP_MENU_CHANGE_PASSPHRASE     = (1<<22),
  SHOW_FLAGS_POPUP_MENU_EDIT_LABEL            = (1<<23),
  SHOW_FLAGS_POPUP_MENU_EDIT_PARTITION        = (1<<24),
  SHOW_FLAGS_POPUP_MENU_FORMAT_VOLUME         = (1<<25),
  SHOW_FLAGS_POPUP_MENU_CREATE_VOLUME_IMAGE   = (1<<26),
  SHOW_FLAGS_POPUP_MENU_RESTORE_VOLUME_IMAGE  = (1<<27),
  SHOW_FLAGS_POPUP_MENU_BENCHMARK             = (1<<28),
} ShowFlags;


static void setup_device_page (GduWindow *window, UDisksObject *object);
static void update_device_page (GduWindow *window, ShowFlags *show_flags);
static void teardown_device_page (GduWindow *window);

static void on_volume_grid_changed (GduVolumeGrid  *grid,
                                    gpointer        user_data);

static void on_devtab_action_generic_activated (GtkAction *action, gpointer user_data);
static void on_devtab_action_partition_create_activated (GtkAction *action, gpointer user_data);
static void on_devtab_action_partition_delete_activated (GtkAction *action, gpointer user_data);
static void on_devtab_action_mount_activated (GtkAction *action, gpointer user_data);
static void on_devtab_action_unmount_activated (GtkAction *action, gpointer user_data);
static void on_devtab_action_eject_activated (GtkAction *action, gpointer user_data);
static void on_devtab_action_unlock_activated (GtkAction *action, gpointer user_data);
static void on_devtab_action_lock_activated (GtkAction *action, gpointer user_data);
static void on_devtab_action_activate_swap_activated (GtkAction *action, gpointer user_data);
static void on_devtab_action_deactivate_swap_activated (GtkAction *action, gpointer user_data);
static void on_devtab_action_generic_drive_activated (GtkAction *action, gpointer user_data);

static void on_generic_drive_menu_item_view_smart (GtkMenuItem *menu_item,
                                             gpointer   user_data);
static void on_generic_drive_menu_item_disk_settings (GtkMenuItem *menu_item,
                                                      gpointer   user_data);
static void on_generic_drive_menu_item_standby_now (GtkMenuItem *menu_item,
                                                    gpointer   user_data);
static void on_generic_drive_menu_item_resume_now (GtkMenuItem *menu_item,
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

G_DEFINE_TYPE (GduWindow, gdu_window, GTK_TYPE_APPLICATION_WINDOW);

static void
gdu_window_init (GduWindow *window)
{
}

static void on_client_changed (UDisksClient  *client,
                               gpointer       user_data);

static void
gdu_window_finalize (GObject *object)
{
  GduWindow *window = GDU_WINDOW (object);

  gtk_window_remove_mnemonic (GTK_WINDOW (window),
                              'd',
                              window->device_treeview);

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
  gtk_tree_view_expand_all (GTK_TREE_VIEW (window->device_treeview));
}

static void select_details_page (GduWindow       *window,
                                 UDisksObject    *object,
                                 DetailsPage      page,
                                 ShowFlags       *show_flags);

static void
update_for_show_flags (GduWindow *window,
                       ShowFlags  show_flags)
{
  gtk_widget_set_visible (GTK_WIDGET (window->device_toolbar_detach_disk_image_button),
                          show_flags & SHOW_FLAGS_DETACH_DISK_IMAGE);

  gtk_action_set_sensitive (GTK_ACTION (window->devtab_action_eject),
                            show_flags & SHOW_FLAGS_EJECT_BUTTON);
  gtk_action_set_visible (GTK_ACTION (window->devtab_action_eject), TRUE);
  gtk_widget_set_visible (window->devtab_drive_eject_button,
                          show_flags & SHOW_FLAGS_EJECT_BUTTON);

  gtk_action_set_visible (GTK_ACTION (window->devtab_action_partition_create),
                          show_flags & SHOW_FLAGS_PARTITION_CREATE_BUTTON);
  gtk_action_set_visible (GTK_ACTION (window->devtab_action_partition_delete),
                          show_flags & SHOW_FLAGS_PARTITION_DELETE_BUTTON);
  gtk_action_set_visible (GTK_ACTION (window->devtab_action_unmount),
                          show_flags & SHOW_FLAGS_UNMOUNT_BUTTON);
  gtk_action_set_visible (GTK_ACTION (window->devtab_action_mount),
                          show_flags & SHOW_FLAGS_MOUNT_BUTTON);
  gtk_action_set_visible (GTK_ACTION (window->devtab_action_activate_swap),
                          show_flags & SHOW_FLAGS_ACTIVATE_SWAP_BUTTON);
  gtk_action_set_visible (GTK_ACTION (window->devtab_action_deactivate_swap),
                          show_flags & SHOW_FLAGS_DEACTIVATE_SWAP_BUTTON);
  gtk_action_set_visible (GTK_ACTION (window->devtab_action_unlock),
                          show_flags & SHOW_FLAGS_ENCRYPTED_UNLOCK_BUTTON);
  gtk_action_set_visible (GTK_ACTION (window->devtab_action_lock),
                          show_flags & SHOW_FLAGS_ENCRYPTED_LOCK_BUTTON);

  gtk_widget_set_sensitive (GTK_WIDGET (window->generic_drive_menu_item_format_disk),
                            show_flags & SHOW_FLAGS_DISK_POPUP_MENU_FORMAT_DISK);
  gtk_widget_set_sensitive (GTK_WIDGET (window->generic_drive_menu_item_view_smart),
                            show_flags & SHOW_FLAGS_DISK_POPUP_MENU_VIEW_SMART);
  gtk_widget_set_sensitive (GTK_WIDGET (window->generic_drive_menu_item_disk_settings),
                            show_flags & SHOW_FLAGS_DISK_POPUP_MENU_DISK_SETTINGS);
  gtk_widget_set_sensitive (GTK_WIDGET (window->generic_drive_menu_item_create_disk_image),
                            show_flags & SHOW_FLAGS_DISK_POPUP_MENU_CREATE_DISK_IMAGE);
  gtk_widget_set_sensitive (GTK_WIDGET (window->generic_drive_menu_item_restore_disk_image),
                            show_flags & SHOW_FLAGS_DISK_POPUP_MENU_RESTORE_DISK_IMAGE);
  gtk_widget_set_sensitive (GTK_WIDGET (window->generic_drive_menu_item_benchmark),
                            show_flags & SHOW_FLAGS_DISK_POPUP_MENU_BENCHMARK);

  if (!(show_flags & (SHOW_FLAGS_DISK_POPUP_MENU_STANDBY_NOW|SHOW_FLAGS_DISK_POPUP_MENU_RESUME_NOW)))
    {
      /* no PM capabilities... only show "standby" greyed out */
      gtk_widget_show (GTK_WIDGET (window->generic_drive_menu_item_standby_now));
      gtk_widget_hide (GTK_WIDGET (window->generic_drive_menu_item_resume_now));
      gtk_widget_set_sensitive (GTK_WIDGET (window->generic_drive_menu_item_standby_now), FALSE);
    }
  else
    {
      /* Only show one of Standby and Resume (they are mutually exclusive) */
      gtk_widget_set_sensitive (GTK_WIDGET (window->generic_drive_menu_item_standby_now), TRUE);
      gtk_widget_set_sensitive (GTK_WIDGET (window->generic_drive_menu_item_resume_now), TRUE);
      if (show_flags & SHOW_FLAGS_DISK_POPUP_MENU_STANDBY_NOW)
        {
          gtk_widget_show (GTK_WIDGET (window->generic_drive_menu_item_standby_now));
          gtk_widget_hide (GTK_WIDGET (window->generic_drive_menu_item_resume_now));
        }
      else
        {
          gtk_widget_hide (GTK_WIDGET (window->generic_drive_menu_item_standby_now));
          gtk_widget_show (GTK_WIDGET (window->generic_drive_menu_item_resume_now));
        }
    }

  gtk_widget_set_sensitive (GTK_WIDGET (window->generic_menu_item_configure_fstab),
                            show_flags & SHOW_FLAGS_POPUP_MENU_CONFIGURE_FSTAB);
  gtk_widget_set_sensitive (GTK_WIDGET (window->generic_menu_item_configure_crypttab),
                            show_flags & SHOW_FLAGS_POPUP_MENU_CONFIGURE_CRYPTTAB);
  gtk_widget_set_sensitive (GTK_WIDGET (window->generic_menu_item_change_passphrase),
                            show_flags & SHOW_FLAGS_POPUP_MENU_CHANGE_PASSPHRASE);
  gtk_widget_set_sensitive (GTK_WIDGET (window->generic_menu_item_edit_label),
                            show_flags & SHOW_FLAGS_POPUP_MENU_EDIT_LABEL);
  gtk_widget_set_sensitive (GTK_WIDGET (window->generic_menu_item_edit_partition),
                            show_flags & SHOW_FLAGS_POPUP_MENU_EDIT_PARTITION);
  gtk_widget_set_sensitive (GTK_WIDGET (window->generic_menu_item_format_volume),
                            show_flags & SHOW_FLAGS_POPUP_MENU_FORMAT_VOLUME);
  gtk_widget_set_sensitive (GTK_WIDGET (window->generic_menu_item_create_volume_image),
                            show_flags & SHOW_FLAGS_POPUP_MENU_CREATE_VOLUME_IMAGE);
  gtk_widget_set_sensitive (GTK_WIDGET (window->generic_menu_item_restore_volume_image),
                            show_flags & SHOW_FLAGS_POPUP_MENU_RESTORE_VOLUME_IMAGE);
  gtk_widget_set_sensitive (GTK_WIDGET (window->generic_menu_item_benchmark),
                            show_flags & SHOW_FLAGS_POPUP_MENU_BENCHMARK);
  /* TODO: don't show the button bringing up the popup menu if it has no items */
}

static gboolean
set_selected_object (GduWindow    *window,
                     UDisksObject *object)
{
  gboolean ret = FALSE;
  ShowFlags show_flags;
  GtkTreeIter iter;

  if (gdu_device_tree_model_get_iter_for_object (window->model, object, &iter))
    {
      GtkTreePath *path;
      GtkTreeSelection *tree_selection;
      tree_selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (window->device_treeview));
      gtk_tree_selection_select_iter (tree_selection, &iter);
      path = gtk_tree_model_get_path (GTK_TREE_MODEL (window->model), &iter);
      gtk_tree_view_set_cursor (GTK_TREE_VIEW (window->device_treeview),
                                path,
                                NULL,
                                FALSE);
      gtk_tree_path_free (path);
      ret = TRUE;
    }
  else
    {
      if (object != NULL)
        g_warning ("Cannot display object with object path %s",
                   g_dbus_object_get_object_path (G_DBUS_OBJECT (object)));
      goto out;
    }

  show_flags = SHOW_FLAGS_NONE;
  if (object != NULL)
    {
      if (udisks_object_peek_drive (object) != NULL ||
          udisks_object_peek_block (object) != NULL)
        {
          select_details_page (window, object, DETAILS_PAGE_DEVICE, &show_flags);
        }
      else
        {
          g_warning ("no page for object %s", g_dbus_object_get_object_path (G_DBUS_OBJECT (object)));
          select_details_page (window, NULL, DETAILS_PAGE_NOT_IMPLEMENTED, &show_flags);
        }
    }
  else
    {
      select_details_page (window, NULL, DETAILS_PAGE_NOT_SELECTED, &show_flags);
    }
  update_for_show_flags (window, show_flags);
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
      set_selected_object (window, object);
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
      set_selected_object (window, object);
      g_object_unref (object);
    }
  else
    {
      set_selected_object (window, NULL);
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
on_device_tree_detach_disk_image_button_clicked (GtkToolButton *button,
                                                 gpointer       user_data)
{
  GduWindow *window = GDU_WINDOW (user_data);
  UDisksLoop *loop;

  loop = udisks_object_peek_loop (window->current_object);
  if (loop != NULL)
    {
      GVariantBuilder options_builder;
      g_variant_builder_init (&options_builder, G_VARIANT_TYPE_VARDICT);
      udisks_loop_call_delete (loop,
                               g_variant_builder_end (&options_builder),
                               NULL, /* GCancellable */
                               (GAsyncReadyCallback) loop_delete_cb,
                               g_object_ref (window));
    }
  else
    {
      g_warning ("remove action not implemented for object");
    }
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
      set_selected_object (data->window, object);
      g_object_unref (object);
      g_free (out_loop_device_object_path);
    }

  loop_setup_data_free (data);
}

/* ---------------------------------------------------------------------------------------------------- */

void
gdu_window_show_attach_disk_image (GduWindow *window)
{
  GtkWidget *dialog;
  gchar *filename;
  gint fd;
  GUnixFDList *fd_list;
  GVariantBuilder options_builder;
  GtkWidget *ro_checkbutton;

  filename = NULL;
  fd = -1;

  dialog = gtk_file_chooser_dialog_new (_("Select Disk Image to Attach"),
                                        GTK_WINDOW (window),
                                        GTK_FILE_CHOOSER_ACTION_OPEN,
                                        GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
                                        _("_Attach"), GTK_RESPONSE_ACCEPT,
                                        NULL);
  gdu_utils_configure_file_chooser_for_disk_images (GTK_FILE_CHOOSER (dialog), TRUE);

  /* Add a RO check button that defaults to RO */
  ro_checkbutton = gtk_check_button_new_with_mnemonic (_("Set up _read-only loop device"));
  gtk_widget_set_tooltip_markup (ro_checkbutton, _("If checked, the loop device will be read-only. This is useful if you don't want the underlying file to be modified"));
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (ro_checkbutton), TRUE);
  gtk_file_chooser_set_extra_widget (GTK_FILE_CHOOSER (dialog), ro_checkbutton);

  //gtk_widget_show_all (dialog);
  if (gtk_dialog_run (GTK_DIALOG (dialog)) != GTK_RESPONSE_ACCEPT)
    goto out;

  filename = gtk_file_chooser_get_filename (GTK_FILE_CHOOSER (dialog));
  gtk_widget_hide (dialog);

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
      goto out;
    }

  /* now that we know the user picked a folder, update file chooser settings */
  gdu_utils_file_chooser_for_disk_images_update_settings (GTK_FILE_CHOOSER (dialog));

  g_variant_builder_init (&options_builder, G_VARIANT_TYPE_VARDICT);
  if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (ro_checkbutton)))
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

 out:
  gtk_widget_destroy (dialog);
  g_free (filename);
}

static void
on_device_tree_attach_disk_image_button_clicked (GtkToolButton *button,
                                                 gpointer       user_data)
{
  GduWindow *window = GDU_WINDOW (user_data);
  gdu_window_show_attach_disk_image (window);
}

/* ---------------------------------------------------------------------------------------------------- */

gboolean
gdu_window_select_object (GduWindow    *window,
                          UDisksObject *object)
{
  gboolean ret = FALSE;
  UDisksPartition *partition;
  UDisksPartitionTable *table = NULL;
  UDisksDrive *drive = NULL;

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
                  if (!set_selected_object (window, drive_object))
                    goto out;
                  ret = TRUE;
                }
            }
          else
            {
              if (!set_selected_object (window, object))
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

/* ---------------------------------------------------------------------------------------------------- */


static void
init_css (GduWindow *window)
{
  GtkCssProvider *provider;
  GError *error;
  const gchar *css =
"#devtab-grid-toolbar.toolbar {\n"
"    border-width: 1px;\n"
"    border-radius: 3px;\n"
"    border-style: solid;\n"
"    background-color: @theme_base_color;\n"
"}\n"
"\n"
".gnome-disk-utility-grid {\n"
"  border-radius: 3px;\n"
"}\n"
"\n"
".gnome-disk-utility-grid:selected {\n"
"  background-image: -gtk-gradient(radial,\n"
"                                  center center, 0,\n"
"                                  center center, 1,\n"
"                                  from(@theme_selected_bg_color),\n"
"                                  to(shade (@theme_selected_bg_color, 0.80)));\n"
"  -adwaita-focus-border-color: mix(@theme_selected_fg_color, @theme_selected_bg_color, 0.30);\n"
"}\n"
"\n"
".gnome-disk-utility-grid:selected:backdrop {\n"
"  background-image: -gtk-gradient(radial,\n"
"                                  center center, 0,\n"
"                                  center center, 1,\n"
"                                  from(@theme_unfocused_selected_bg_color),\n"
"                                  to(shade (@theme_unfocused_selected_bg_color, 0.80)));\n"
"  -adwaita-focus-border-color: mix(@theme_unfocused_selected_fg_color, @theme_unfocused_selected_bg_color, 0.30);\n"
"}\n"
;

  provider = gtk_css_provider_new ();
  error = NULL;
  if (!gtk_css_provider_load_from_data (provider,
                                        css,
                                        -1,
                                        &error))
    {
      g_warning ("Can't parse custom CSS: %s\n", error->message);
      g_error_free (error);
      goto out;
    }

  gtk_style_context_add_provider_for_screen (gtk_widget_get_screen (GTK_WIDGET (window)),
                                             GTK_STYLE_PROVIDER (provider),
                                             GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
  g_object_unref (provider);

 out:
  ;
}


static gboolean
on_constructed_in_idle (gpointer user_data)
{
  GduWindow *window = GDU_WINDOW (user_data);
  /* select something sensible */
  ensure_something_selected (window);
  return FALSE; /* remove source */
}

static gint
device_sort_function (GtkTreeModel *model,
                      GtkTreeIter *a,
                      GtkTreeIter *b,
                      gpointer user_data)
{
  gchar *sa, *sb;

  gtk_tree_model_get (model, a,
                      GDU_DEVICE_TREE_MODEL_COLUMN_SORT_KEY, &sa,
                      -1);
  gtk_tree_model_get (model, b,
                      GDU_DEVICE_TREE_MODEL_COLUMN_SORT_KEY, &sb,
                      -1);

  return g_strcmp0 (sa, sb);
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

  gtk_tree_model_get (model,
                      iter,
                      GDU_DEVICE_TREE_MODEL_COLUMN_POWER_STATE_FLAGS, &flags,
                      -1);

  if (flags & GDU_POWER_STATE_FLAGS_STANDBY)
    visible = TRUE;

  gtk_cell_renderer_set_visible (renderer, visible);
}

static void
gdu_window_constructed (GObject *object)
{
  GduWindow *window = GDU_WINDOW (object);
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

  /* load UI file */
  gdu_application_new_widget (window->application, "disks.ui", NULL, &window->builder);

  /* set up widgets */
  for (n = 0; widget_mapping[n].name != NULL; n++)
    {
      gpointer *p = (gpointer *) ((char *) window + widget_mapping[n].offset);
      *p = G_OBJECT (gtk_builder_get_object (window->builder, widget_mapping[n].name));
      g_warn_if_fail (*p != NULL);
    }

  gtk_widget_reparent (window->main_hpane, GTK_WIDGET (window));
  gtk_window_set_title (GTK_WINDOW (window), _("Disks"));
  gtk_window_set_default_size (GTK_WINDOW (window), 800, 600);
  gtk_container_set_border_width (GTK_CONTAINER (window), 12);

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

  context = gtk_widget_get_style_context (window->device_scrolledwindow);
  gtk_style_context_set_junction_sides (context, GTK_JUNCTION_BOTTOM);
  context = gtk_widget_get_style_context (window->device_toolbar);
  gtk_style_context_add_class (context, GTK_STYLE_CLASS_INLINE_TOOLBAR);
  gtk_style_context_set_junction_sides (context, GTK_JUNCTION_TOP);

  window->model = gdu_device_tree_model_new (window->client);

  /* set up mnemonic */
  gtk_window_add_mnemonic (GTK_WINDOW (window),
                           'd',
                           window->device_treeview);

  gtk_tree_view_set_model (GTK_TREE_VIEW (window->device_treeview), GTK_TREE_MODEL (window->model));
  gtk_tree_sortable_set_sort_column_id (GTK_TREE_SORTABLE (window->model),
                                        GDU_DEVICE_TREE_MODEL_COLUMN_SORT_KEY,
                                        GTK_SORT_ASCENDING);
  /* Force g_strcmp0() as the sort function otherwise ___aa won't come before ____b ... */
  gtk_tree_sortable_set_sort_func (GTK_TREE_SORTABLE (window->model),
                                   GDU_DEVICE_TREE_MODEL_COLUMN_SORT_KEY,
                                   device_sort_function,
                                   NULL, /* user_data */
                                   NULL); /* GDestroyNotify */

  selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (window->device_treeview));
  gtk_tree_selection_set_select_function (selection, dont_select_headings, NULL, NULL);
  g_signal_connect (selection,
                    "changed",
                    G_CALLBACK (on_tree_selection_changed),
                    window);

  column = gtk_tree_view_column_new ();
  gtk_tree_view_append_column (GTK_TREE_VIEW (window->device_treeview), column);

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
                                           NULL,  /* user_data */
                                           NULL); /* user_data GDestroyNotify */

  /* expand on insertion - hmm, I wonder if there's an easier way to do this */
  g_signal_connect (window->model,
                    "row-inserted",
                    G_CALLBACK (on_row_inserted),
                    window);
  gtk_tree_view_expand_all (GTK_TREE_VIEW (window->device_treeview));

  g_signal_connect (window->client,
                    "changed",
                    G_CALLBACK (on_client_changed),
                    window);

  /* set up non-standard widgets that isn't in the .ui file */

  window->volume_grid = gdu_volume_grid_new (window->client);
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

  /* main toolbar */
  g_signal_connect (window->device_toolbar_attach_disk_image_button,
                    "clicked",
                    G_CALLBACK (on_device_tree_attach_disk_image_button_clicked),
                    window);
  g_signal_connect (window->device_toolbar_detach_disk_image_button,
                    "clicked",
                    G_CALLBACK (on_device_tree_detach_disk_image_button_clicked),
                    window);

  /* actions */
  g_signal_connect (window->devtab_action_generic,
                    "activate",
                    G_CALLBACK (on_devtab_action_generic_activated),
                    window);
  g_signal_connect (window->devtab_action_partition_create,
                    "activate",
                    G_CALLBACK (on_devtab_action_partition_create_activated),
                    window);
  g_signal_connect (window->devtab_action_partition_delete,
                    "activate",
                    G_CALLBACK (on_devtab_action_partition_delete_activated),
                    window);
  g_signal_connect (window->devtab_action_mount,
                    "activate",
                    G_CALLBACK (on_devtab_action_mount_activated),
                    window);
  g_signal_connect (window->devtab_action_unmount,
                    "activate",
                    G_CALLBACK (on_devtab_action_unmount_activated),
                    window);
  g_signal_connect (window->devtab_action_eject,
                    "activate",
                    G_CALLBACK (on_devtab_action_eject_activated),
                    window);
  g_signal_connect (window->devtab_action_unlock,
                    "activate",
                    G_CALLBACK (on_devtab_action_unlock_activated),
                    window);
  g_signal_connect (window->devtab_action_lock,
                    "activate",
                    G_CALLBACK (on_devtab_action_lock_activated),
                    window);
  g_signal_connect (window->devtab_action_activate_swap,
                    "activate",
                    G_CALLBACK (on_devtab_action_activate_swap_activated),
                    window);
  g_signal_connect (window->devtab_action_deactivate_swap,
                    "activate",
                    G_CALLBACK (on_devtab_action_deactivate_swap_activated),
                    window);
  g_signal_connect (window->devtab_action_generic_drive,
                    "activate",
                    G_CALLBACK (on_devtab_action_generic_drive_activated),
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

  g_idle_add (on_constructed_in_idle, g_object_ref (window));
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

static void
teardown_details_page (GduWindow    *window,
                       UDisksObject *object,
                       DetailsPage   page)
{
  //g_debug ("teardown for %s, page %d",
  //       object != NULL ? g_dbus_object_get_object_path (object) : "<none>",
  //         page);
  switch (page)
    {
    case DETAILS_PAGE_NOT_SELECTED:
      break;

    case DETAILS_PAGE_NOT_IMPLEMENTED:
      break;

    case DETAILS_PAGE_DEVICE:
      teardown_device_page (window);
      break;
    }
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
            gboolean        active)
{
  GtkWidget *key_label;
  GtkWidget *switch_box;
  GtkWidget *switch_;

  key_label = GTK_WIDGET (gtk_builder_get_object (window->builder, key_label_id));
  switch_box = GTK_WIDGET (gtk_builder_get_object (window->builder, switch_box_id));
  switch_ = GTK_WIDGET (gtk_builder_get_object (window->builder, switch_id));

  gtk_switch_set_active (GTK_SWITCH (switch_), active);
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

static void
setup_details_page (GduWindow     *window,
                    UDisksObject  *object,
                    DetailsPage    page)
{
  //g_debug ("setup for %s, page %d",
  //         object != NULL ? g_dbus_object_get_object_path (object) : "<none>",
  //         page);

  switch (page)
    {
    case DETAILS_PAGE_NOT_SELECTED:
      break;

    case DETAILS_PAGE_NOT_IMPLEMENTED:
      break;

    case DETAILS_PAGE_DEVICE:
      setup_device_page (window, object);
      break;
    }
}

/* ---------------------------------------------------------------------------------------------------- */

static void
update_details_page (GduWindow      *window,
                     DetailsPage     page,
                     ShowFlags      *show_flags)
{
  ;
  //g_debug ("update for %s, page %d",
  //         object != NULL ? g_dbus_object_get_object_path (object) : "<none>",
  //         page);

  switch (page)
    {
    case DETAILS_PAGE_NOT_SELECTED:
      break;

    case DETAILS_PAGE_NOT_IMPLEMENTED:
      break;

    case DETAILS_PAGE_DEVICE:
      update_device_page (window, show_flags);
      break;
    }
}

static void
select_details_page (GduWindow      *window,
                     UDisksObject   *object,
                     DetailsPage     page,
                     ShowFlags      *show_flags)
{
  teardown_details_page (window,
                         window->current_object,
                         window->current_page);

  window->current_page = page;
  if (window->current_object != NULL)
    g_object_unref (window->current_object);
  window->current_object = object != NULL ? g_object_ref (object) : NULL;

  gtk_notebook_set_current_page (GTK_NOTEBOOK (window->details_notebook), page);

  setup_details_page (window,
                      window->current_object,
                      window->current_page);

  update_details_page (window, window->current_page, show_flags);
}

static void
update_all (GduWindow     *window)
{
  ShowFlags show_flags;

  switch (window->current_page)
    {
    case DETAILS_PAGE_NOT_SELECTED:
      /* Nothing to update */
      break;

    case DETAILS_PAGE_NOT_IMPLEMENTED:
      /* Nothing to update */
      break;

    case DETAILS_PAGE_DEVICE:
      show_flags = SHOW_FLAGS_NONE;
      update_details_page (window, window->current_page, &show_flags);
      update_for_show_flags (window, show_flags);
      break;
    }
}

static void
on_client_changed (UDisksClient   *client,
                   gpointer        user_data)
{
  GduWindow *window = GDU_WINDOW (user_data);
  //g_debug ("on_client_changed");
  update_all (window);
}

static void
on_volume_grid_changed (GduVolumeGrid  *grid,
                        gpointer        user_data)
{
  GduWindow *window = GDU_WINDOW (user_data);
  //g_debug ("on_volume_grid_changed");
  update_all (window);
}

/* ---------------------------------------------------------------------------------------------------- */

static void
setup_device_page (GduWindow     *window,
                   UDisksObject  *object)
{
  UDisksDrive *drive;
  UDisksBlock *block;

  drive = udisks_object_peek_drive (object);
  block = udisks_object_peek_block (object);

  gdu_volume_grid_set_container_visible (GDU_VOLUME_GRID (window->volume_grid), FALSE);
  if (drive != NULL)
    {
      GList *blocks;

      /* TODO: for multipath, ensure e.g. mpathk is before sda, sdb */
      blocks = get_top_level_blocks_for_drive (window, g_dbus_object_get_object_path (G_DBUS_OBJECT (object)));
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
      gdu_volume_grid_set_block_object (GDU_VOLUME_GRID (window->volume_grid), object);
    }
  else
    {
      g_assert_not_reached ();
    }
}

static gchar *
get_device_file_for_display (UDisksBlock *block)
{
  gchar *ret;
  if (udisks_block_get_read_only (block))
    {
      /* Translators: Shown for a read-only device. The %s is the device file, e.g. /dev/sdb1 */
      ret = g_strdup_printf (_("%s <span size=\"smaller\">(Read-Only)</span>"),
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
  gchar *s;
  gchar *desc;
  gint64 expected_end_time_usec;

  desc = udisks_client_get_job_description (window->client, job);

  expected_end_time_usec = udisks_job_get_expected_end_time (job);
  if (expected_end_time_usec > 0)
    {
      gint64 usec_left;
      gchar *s2, *s3;

      usec_left = expected_end_time_usec - g_get_real_time ();
      if (usec_left < 0)
        {
          /* Translators: Shown instead of e.g. "10 seconds remaining" when we've passed
           * the expected end time...
           */
          s3 = g_strdup_printf (C_("job-remaining-exceeded", "Almost done..."));
        }
      else
        {
          s2 = gdu_utils_format_duration_usec (usec_left, GDU_FORMAT_DURATION_FLAGS_NONE);
          s3 = g_strdup_printf (C_("job-remaining", "%s remaining"), s2);
          g_free (s2);
        }
      s = g_strdup_printf ("<small>%s — %s</small>", desc, s3);
      g_free (s3);
    }
  else
    {
      s = g_strdup_printf ("<small>%s</small>", desc);
    }

  g_free (desc);

  return s;
}


static void
update_device_page_for_drive (GduWindow      *window,
                              UDisksObject   *object,
                              UDisksDrive    *drive,
                              ShowFlags      *show_flags)
{
  gchar *s;
  GList *blocks;
  GList *l;
  GString *str;
  const gchar *drive_vendor;
  const gchar *drive_model;
  const gchar *drive_revision;
  gchar *name;
  gchar *description;
  gchar *media_description;
  GIcon *drive_icon;
  GIcon *media_icon;
  guint64 size;
  UDisksDriveAta *ata;
  const gchar *our_seat;
  const gchar *serial;
  GList *jobs;

  //g_debug ("In update_device_page_for_drive() - selected=%s",
  //         object != NULL ? g_dbus_object_get_object_path (object) : "<nothing>");

  /* TODO: for multipath, ensure e.g. mpathk is before sda, sdb */
  blocks = get_top_level_blocks_for_drive (window, g_dbus_object_get_object_path (G_DBUS_OBJECT (object)));
  blocks = g_list_sort (blocks, (GCompareFunc) block_compare_on_preferred);

  ata = udisks_object_peek_drive_ata (object);

  udisks_client_get_drive_info (window->client,
                                drive,
                                &name,
                                &description,
                                &drive_icon,
                                &media_description,
                                &media_icon);

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
  s = g_strdup_printf ("<big><b>%s</b></big>",
                       description);
  gtk_label_set_markup (GTK_LABEL (window->devtab_drive_name_label), s);
  gtk_widget_show (window->devtab_drive_name_label);
  g_free (s);
  s = g_strdup_printf ("<small>%s</small>", str->str);
  gtk_label_set_markup (GTK_LABEL (window->devtab_drive_devices_label), s);
  gtk_widget_show (window->devtab_drive_devices_label);
  g_free (s);
  g_string_free (str, TRUE);
  gtk_widget_show (window->devtab_drive_box);
  gtk_widget_show (window->devtab_drive_vbox);
  gtk_widget_show (window->devtab_drive_buttonbox);
  gtk_widget_show (window->devtab_drive_eject_button);
  gtk_widget_show (window->devtab_drive_generic_button);

  if (media_icon != NULL)
    gtk_image_set_from_gicon (GTK_IMAGE (window->devtab_drive_image), media_icon, GTK_ICON_SIZE_DIALOG);
  else
    gtk_image_set_from_gicon (GTK_IMAGE (window->devtab_drive_image), drive_icon, GTK_ICON_SIZE_DIALOG);
  gtk_widget_show (window->devtab_drive_image);

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
              "devtab-model-label",
              "devtab-model-value-label", str->str, SET_MARKUP_FLAGS_HYPHEN_IF_EMPTY);
  g_string_free (str, TRUE);

  serial = udisks_drive_get_serial (drive);
  set_markup (window,
              "devtab-serial-number-label",
              "devtab-serial-number-value-label",
              serial, SET_MARKUP_FLAGS_NONE);
  if (serial == NULL || strlen (serial) == 0)
    {
      set_markup (window,
                  "devtab-wwn-label",
                  "devtab-wwn-value-label",
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
                  "devtab-location-label",
                  "devtab-location-value-label",
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
        *show_flags |= SHOW_FLAGS_DISK_POPUP_MENU_VIEW_SMART;
      g_free (s);
    }

  if (gdu_disk_settings_dialog_should_show (object))
    *show_flags |= SHOW_FLAGS_DISK_POPUP_MENU_DISK_SETTINGS;

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
            *show_flags |= SHOW_FLAGS_DISK_POPUP_MENU_RESUME_NOW;
          else
            *show_flags |= SHOW_FLAGS_DISK_POPUP_MENU_STANDBY_NOW;
        }
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
                  "devtab-media-label",
                  "devtab-media-value-label",
                  media_description, SET_MARKUP_FLAGS_NONE);
    }
  else
    {
      set_markup (window,
                  "devtab-media-label",
                  "devtab-media-value-label",
                  "",
                  SET_MARKUP_FLAGS_HYPHEN_IF_EMPTY);
    }

  jobs = udisks_client_get_jobs_for_object (window->client, object);
  /* if there are no jobs on the drive, look at the first block object if it's partitioned
   * (because: if it's not partitioned, we'll see the job in Volumes below so no need to show it here)
   */
  if (jobs == NULL && blocks != NULL)
    {
      UDisksObject *block_object = UDISKS_OBJECT (blocks->data);
      if (udisks_object_peek_partition_table (block_object) != NULL)
        {
          jobs = udisks_client_get_jobs_for_object (window->client, block_object);
        }
    }
  if (jobs == NULL)
    {
      gtk_widget_hide (window->devtab_drive_job_label);
      gtk_widget_hide (window->devtab_drive_job_grid);
    }
  else
    {
      UDisksJob *job = UDISKS_JOB (jobs->data);

      gtk_widget_show (window->devtab_drive_job_label);
      gtk_widget_show (window->devtab_drive_job_grid);
      if (udisks_job_get_progress_valid (job))
        {
          gdouble progress = udisks_job_get_progress (job);
          gtk_widget_show (window->devtab_drive_job_progressbar);
          gtk_widget_show (window->devtab_drive_job_remaining_label);
          gtk_widget_hide (window->devtab_drive_job_no_progress_label);

          gtk_progress_bar_set_fraction (GTK_PROGRESS_BAR (window->devtab_drive_job_progressbar), progress);

          s = g_strdup_printf ("%2.1f%%", 100.0 * progress);
          gtk_progress_bar_set_show_text (GTK_PROGRESS_BAR (window->devtab_drive_job_progressbar), TRUE);
          gtk_progress_bar_set_text (GTK_PROGRESS_BAR (window->devtab_drive_job_progressbar), s);
          g_free (s);

          s = get_job_progress_text (window, job);
          gtk_label_set_markup (GTK_LABEL (window->devtab_drive_job_remaining_label), s);
          g_free (s);
        }
      else
        {
          gtk_widget_hide (window->devtab_drive_job_progressbar);
          gtk_widget_hide (window->devtab_drive_job_remaining_label);
          gtk_widget_show (window->devtab_drive_job_no_progress_label);
          s = udisks_client_get_job_description (window->client, job);
          gtk_label_set_text (GTK_LABEL (window->devtab_drive_job_no_progress_label), s);
          g_free (s);
        }
      if (udisks_job_get_cancelable (job))
        gtk_widget_show (window->devtab_drive_job_cancel_button);
      else
        gtk_widget_hide (window->devtab_drive_job_cancel_button);
    }
  g_list_foreach (jobs, (GFunc) g_object_unref, NULL);
  g_list_free (jobs);

  if (udisks_drive_get_ejectable (drive))
    {
      *show_flags |= SHOW_FLAGS_EJECT_BUTTON;
    }

  g_list_foreach (blocks, (GFunc) g_object_unref, NULL);
  g_list_free (blocks);
  if (media_icon != NULL)
    g_object_unref (media_icon);
  g_object_unref (drive_icon);
  g_free (description);
  g_free (media_description);
  g_free (name);
}

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
                              ShowFlags          *show_flags)
{
  const gchar *usage;
  const gchar *type;
  const gchar *version;
  UDisksFilesystem *filesystem;
  UDisksPartition *partition;
  UDisksLoop *loop;
  gboolean read_only;
  gchar *s;
  UDisksObject *drive_object;
  UDisksDrive *drive = NULL;
  GList *jobs;

  read_only = udisks_block_get_read_only (block);
  partition = udisks_object_peek_partition (object);
  filesystem = udisks_object_peek_filesystem (object);

  /* loop device of main block device (not partition) */
  loop = udisks_object_peek_loop (window->current_object);

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
      *show_flags |= SHOW_FLAGS_POPUP_MENU_CREATE_VOLUME_IMAGE;
      *show_flags |= SHOW_FLAGS_POPUP_MENU_BENCHMARK;
      *show_flags |= SHOW_FLAGS_DISK_POPUP_MENU_BENCHMARK;
      *show_flags |= SHOW_FLAGS_DISK_POPUP_MENU_CREATE_DISK_IMAGE;
      if (!read_only)
        *show_flags |= SHOW_FLAGS_DISK_POPUP_MENU_RESTORE_DISK_IMAGE;
      if (!read_only)
        {
          *show_flags |= SHOW_FLAGS_POPUP_MENU_RESTORE_VOLUME_IMAGE;
          if (udisks_block_get_hint_partitionable (block))
            *show_flags |= SHOW_FLAGS_DISK_POPUP_MENU_FORMAT_DISK;
          *show_flags |= SHOW_FLAGS_POPUP_MENU_FORMAT_VOLUME;
        }
    }

  if (partition != NULL && !read_only)
    *show_flags |= SHOW_FLAGS_PARTITION_DELETE_BUTTON;

  /* Since /etc/fstab, /etc/crypttab and so on can reference
   * any device regardless of its content ... we want to show
   * the relevant menu option (to get to the configuration dialog)
   * if the device matches the configuration....
   */
  if (gdu_utils_has_configuration (block, "fstab", NULL))
    *show_flags |= SHOW_FLAGS_POPUP_MENU_CONFIGURE_FSTAB;
  if (gdu_utils_has_configuration (block, "crypttab", NULL))
    *show_flags |= SHOW_FLAGS_POPUP_MENU_CONFIGURE_CRYPTTAB;

  /* if the device has no media and there is no existing configuration, then
   * show CONFIGURE_FSTAB since the user might want to add an entry for e.g.
   * /media/cdrom
   */
  if (udisks_block_get_size (block) == 0 &&
      !(*show_flags & (SHOW_FLAGS_POPUP_MENU_CONFIGURE_FSTAB |
                       SHOW_FLAGS_POPUP_MENU_CONFIGURE_CRYPTTAB)))
    {
      *show_flags |= SHOW_FLAGS_POPUP_MENU_CONFIGURE_FSTAB;
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
      set_size (window,
                "devtab-size-label",
                "devtab-size-value-label",
                size, SET_MARKUP_FLAGS_HYPHEN_IF_EMPTY);
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
                  udisks_loop_get_autoclear (loop));
    }

  usage = udisks_block_get_id_usage (block);
  type = udisks_block_get_id_type (block);
  version = udisks_block_get_id_version (block);

  if (size > 0)
    {
      if (partition != NULL && udisks_partition_get_is_container (partition))
        {
          s = g_strdup (_("Extended Partition"));
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
  set_markup (window,
              "devtab-volume-type-label",
              "devtab-volume-type-value-label",
              s, SET_MARKUP_FLAGS_HYPHEN_IF_EMPTY);
  g_free (s);

  if (partition != NULL)
    {
      if (!read_only)
        *show_flags |= SHOW_FLAGS_POPUP_MENU_EDIT_PARTITION;
    }
  else
    {
      if (drive != NULL && udisks_drive_get_ejectable (drive))
        *show_flags |= SHOW_FLAGS_EJECT_BUTTON;
    }

  if (filesystem != NULL)
    {
      const gchar *const *mount_points;
      gchar *mount_point;

      mount_points = udisks_filesystem_get_mount_points (filesystem);
      if (g_strv_length ((gchar **) mount_points) > 0)
        {
          /* TODO: right now we only display the first mount point */
          if (g_strcmp0 (mount_points[0], "/") == 0)
            {
              /* Translators: Use for mount point '/' simply because '/' is too small to hit as a hyperlink
               */
              s = g_strdup_printf ("<a href=\"file:///\">%s</a>", _("Filesystem Root"));
            }
          else
            {
              s = g_strdup_printf ("<a href=\"file://%s\">%s</a>",
                                   mount_points[0], mount_points[0]);
            }
          /* Translators: Shown next to "In Use". The first %s is the mount point, e.g. /media/foobar */
          mount_point = g_strdup_printf (_("Yes, mounted at %s"), s);
          g_free (s);
        }
      else
        {
          /* Translators: Shown when the device is not mounted next to the "In Use" label */
          mount_point = g_strdup (_("No"));
        }
      set_markup (window,
                  "devtab-volume-in-use-label",
                  "devtab-volume-in-use-value-label",
                  mount_point,
                  SET_MARKUP_FLAGS_NONE);
      g_free (mount_point);

      if (g_strv_length ((gchar **) mount_points) > 0)
        *show_flags |= SHOW_FLAGS_UNMOUNT_BUTTON;
      else
        *show_flags |= SHOW_FLAGS_MOUNT_BUTTON;

      *show_flags |= SHOW_FLAGS_POPUP_MENU_CONFIGURE_FSTAB;
      if (!read_only)
        *show_flags |= SHOW_FLAGS_POPUP_MENU_EDIT_LABEL;
    }
  else if (g_strcmp0 (udisks_block_get_id_usage (block), "other") == 0 &&
           g_strcmp0 (udisks_block_get_id_type (block), "swap") == 0)
    {
      UDisksSwapspace *swapspace;
      const gchar *str;
      swapspace = udisks_object_peek_swapspace (object);
      if (swapspace != NULL)
        {
          if (udisks_swapspace_get_active (swapspace))
            {
              *show_flags |= SHOW_FLAGS_DEACTIVATE_SWAP_BUTTON;
              /* Translators: Shown if the swap device is in use next to the "In Use" label */
              str = _("Yes");
            }
          else
            {
              *show_flags |= SHOW_FLAGS_ACTIVATE_SWAP_BUTTON;
              /* Translators: Shown if the swap device is not in use next to the "In Use" label */
              str = _("No");
            }
          set_markup (window,
                      "devtab-volume-in-use-label",
                      "devtab-volume-in-use-value-label",
                      str,
                      SET_MARKUP_FLAGS_NONE);
        }
    }
  else if (g_strcmp0 (udisks_block_get_id_usage (block), "crypto") == 0)
    {
      UDisksObject *cleartext_device;
      const gchar *str;

      cleartext_device = lookup_cleartext_device_for_crypto_device (window->client,
                                                                    g_dbus_object_get_object_path (G_DBUS_OBJECT (object)));
      if (cleartext_device != NULL)
        {
          *show_flags |= SHOW_FLAGS_ENCRYPTED_LOCK_BUTTON;
          /* Translators: Shown if the encrypted device is unlocked next to the "In Use" label */
          str = _("Yes");
        }
      else
        {
          *show_flags |= SHOW_FLAGS_ENCRYPTED_UNLOCK_BUTTON;
          /* Translators: Shown if the encrypted device is not unlocked next to the "In Use" label */
          str = _("No");
        }
      set_markup (window,
                  "devtab-volume-in-use-label",
                  "devtab-volume-in-use-value-label",
                  str,
                  SET_MARKUP_FLAGS_NONE);

      *show_flags |= SHOW_FLAGS_POPUP_MENU_CONFIGURE_CRYPTTAB;
      *show_flags |= SHOW_FLAGS_POPUP_MENU_CHANGE_PASSPHRASE;
    }


  jobs = udisks_client_get_jobs_for_object (window->client, object);
  if (jobs == NULL)
    {
      gtk_widget_hide (window->devtab_job_label);
      gtk_widget_hide (window->devtab_job_grid);
    }
  else
    {
      UDisksJob *job = UDISKS_JOB (jobs->data);

      gtk_widget_show (window->devtab_job_label);
      gtk_widget_show (window->devtab_job_grid);
      if (udisks_job_get_progress_valid (job))
        {
          gdouble progress = udisks_job_get_progress (job);
          gtk_widget_show (window->devtab_job_progressbar);
          gtk_widget_show (window->devtab_job_remaining_label);
          gtk_widget_hide (window->devtab_job_no_progress_label);

          gtk_progress_bar_set_fraction (GTK_PROGRESS_BAR (window->devtab_job_progressbar), progress);

          s = g_strdup_printf ("%2.1f%%", 100.0 * progress);
          gtk_progress_bar_set_show_text (GTK_PROGRESS_BAR (window->devtab_job_progressbar), TRUE);
          gtk_progress_bar_set_text (GTK_PROGRESS_BAR (window->devtab_job_progressbar), s);
          g_free (s);

          s = get_job_progress_text (window, job);
          gtk_label_set_markup (GTK_LABEL (window->devtab_job_remaining_label), s);
          g_free (s);
        }
      else
        {
          gtk_widget_hide (window->devtab_job_progressbar);
          gtk_widget_hide (window->devtab_job_remaining_label);
          gtk_widget_show (window->devtab_job_no_progress_label);
          s = udisks_client_get_job_description (window->client, job);
          gtk_label_set_text (GTK_LABEL (window->devtab_job_no_progress_label), s);
          g_free (s);
        }
      if (udisks_job_get_cancelable (job))
        gtk_widget_show (window->devtab_job_cancel_button);
      else
        gtk_widget_hide (window->devtab_job_cancel_button);
    }
  g_list_foreach (jobs, (GFunc) g_object_unref, NULL);
  g_list_free (jobs);
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
  UDisksPartitionTable *table;
  const gchar *table_type = NULL;
  gboolean read_only;

  //g_debug ("In update_device_page_for_free_space() - size=%" G_GUINT64_FORMAT " selected=%s",
  //         size,
  //         object != NULL ? g_dbus_object_get_object_path (object) : "<nothing>");

  read_only = udisks_block_get_read_only (block);
  loop = udisks_object_peek_loop (window->current_object);

  *show_flags |= SHOW_FLAGS_DISK_POPUP_MENU_BENCHMARK;
  if (!read_only)
    {
      *show_flags |= SHOW_FLAGS_DISK_POPUP_MENU_FORMAT_DISK;
      *show_flags |= SHOW_FLAGS_DISK_POPUP_MENU_CREATE_DISK_IMAGE;
      *show_flags |= SHOW_FLAGS_DISK_POPUP_MENU_RESTORE_DISK_IMAGE;
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
                  udisks_loop_get_autoclear (loop));
    }

  table = udisks_object_peek_partition_table (object);
  if (table != NULL)
    table_type = udisks_partition_table_get_type_ (table);

  if (table_type != NULL)
    {
      const gchar *table_type_for_display;
      table_type_for_display = udisks_client_get_partition_table_type_for_display (window->client, table_type);
      if (table_type_for_display == NULL)
        table_type_for_display = table_type;
      /* Translators: used to convey free space for partitions - the %s is the
       * partition table format e.g. "Master Boot Record" or "GUID Partition Table"
       */
      s = g_strdup_printf (_("Unallocated Space (%s)"), table_type_for_display);
    }
  else
    {
      /* Translators: used to convey free space for partitions (partition table format not known) */
      s = g_strdup (_("Unallocated Space"));
    }

  set_size (window,
            "devtab-size-label",
            "devtab-size-value-label",
            size, SET_MARKUP_FLAGS_HYPHEN_IF_EMPTY);
  set_markup (window,
              "devtab-volume-type-label",
              "devtab-volume-type-value-label",
              s,
              SET_MARKUP_FLAGS_NONE);
  if (!read_only)
    *show_flags |= SHOW_FLAGS_PARTITION_CREATE_BUTTON;
  g_free (s);

  s = get_device_file_for_display (block);
  set_markup (window,
              "devtab-device-label",
              "devtab-device-value-label",
              s, SET_MARKUP_FLAGS_NONE);
  g_free (s);
}

static void
update_device_page (GduWindow      *window,
                    ShowFlags      *show_flags)
{
  UDisksObject *object;
  GduVolumeGridElementType type;
  UDisksBlock *block;
  UDisksDrive *drive;
  guint64 size;
  GList *children;
  GList *l;

  /* first hide everything */
  gtk_container_foreach (GTK_CONTAINER (window->devtab_drive_table), (GtkCallback) gtk_widget_hide, NULL);
  gtk_container_foreach (GTK_CONTAINER (window->devtab_table), (GtkCallback) gtk_widget_hide, NULL);
  children = gtk_action_group_list_actions (GTK_ACTION_GROUP (gtk_builder_get_object (window->builder, "devtab-actions")));
  for (l = children; l != NULL; l = l->next)
    gtk_action_set_visible (GTK_ACTION (l->data), FALSE);
  g_list_free (children);

  /* always show the generic toolbar item */
  gtk_action_set_visible (GTK_ACTION (gtk_builder_get_object (window->builder,
                                                              "devtab-action-generic")), TRUE);


  object = window->current_object;
  drive = udisks_object_peek_drive (window->current_object);
  block = udisks_object_peek_block (window->current_object);
  type = gdu_volume_grid_get_selected_type (GDU_VOLUME_GRID (window->volume_grid));
  size = gdu_volume_grid_get_selected_size (GDU_VOLUME_GRID (window->volume_grid));

  if (udisks_object_peek_loop (object) != NULL)
    *show_flags |= SHOW_FLAGS_DETACH_DISK_IMAGE;

  if (drive != NULL)
    update_device_page_for_drive (window, object, drive, show_flags);

  if (type == GDU_VOLUME_GRID_ELEMENT_TYPE_CONTAINER)
    {
      if (block != NULL)
        update_device_page_for_block (window, object, block, size, show_flags);
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
              update_device_page_for_block (window, object, block, size, show_flags);
              break;

            case GDU_VOLUME_GRID_ELEMENT_TYPE_NO_MEDIA:
              update_device_page_for_block (window, object, block, size, show_flags);
              update_device_page_for_no_media (window, object, block, show_flags);
              break;

            case GDU_VOLUME_GRID_ELEMENT_TYPE_FREE_SPACE:
              update_device_page_for_free_space (window, object, block, size, show_flags);
              break;
            }
        }
    }
}

static void
teardown_device_page (GduWindow *window)
{
  gdu_volume_grid_set_block_object (GDU_VOLUME_GRID (window->volume_grid), NULL);
}

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
  gdu_restore_disk_image_dialog_show (window, object);
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
  gdu_restore_disk_image_dialog_show (window, object);
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
on_devtab_action_mount_activated (GtkAction *action,
                                  gpointer   user_data)
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
unmount_cb (UDisksFilesystem *filesystem,
            GAsyncResult     *res,
            gpointer          user_data)
{
  GduWindow *window = GDU_WINDOW (user_data);
  GError *error;

  error = NULL;
  if (!udisks_filesystem_call_unmount_finish (filesystem,
                                              res,
                                              &error))
    {
      gdu_utils_show_error (GTK_WINDOW (window),
                            _("Error unmounting filesystem"),
                            error);
      g_error_free (error);
    }
  g_object_unref (window);
}

static void
on_devtab_action_unmount_activated (GtkAction *action,
                                    gpointer   user_data)
{
  GduWindow *window = GDU_WINDOW (user_data);
  UDisksObject *object;
  UDisksFilesystem *filesystem;

  object = gdu_volume_grid_get_selected_device (GDU_VOLUME_GRID (window->volume_grid));
  filesystem = udisks_object_peek_filesystem (object);
  udisks_filesystem_call_unmount (filesystem,
                                  g_variant_new ("a{sv}", NULL), /* options */
                                  NULL, /* cancellable */
                                  (GAsyncReadyCallback) unmount_cb,
                                  g_object_ref (window));
}

/* ---------------------------------------------------------------------------------------------------- */

static void
on_devtab_action_generic_activated (GtkAction *action,
                                    gpointer   user_data)
{
  GduWindow *window = GDU_WINDOW (user_data);
  update_all (window);
  gtk_menu_popup (GTK_MENU (window->generic_menu),
                  NULL, NULL, NULL, NULL, 1, gtk_get_current_event_time ());
}

/* ---------------------------------------------------------------------------------------------------- */

static void
on_devtab_action_generic_drive_activated (GtkAction *action,
                                          gpointer   user_data)
{
  GduWindow *window = GDU_WINDOW (user_data);
  update_all (window);
  gtk_menu_popup (GTK_MENU (window->generic_drive_menu),
                  NULL, NULL, NULL, NULL, 1, gtk_get_current_event_time ());
}

/* ---------------------------------------------------------------------------------------------------- */

static void
on_devtab_action_partition_create_activated (GtkAction *action,
                                             gpointer   user_data)
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
on_devtab_action_partition_delete_activated (GtkAction *action,
                                             gpointer   user_data)
{
  GduWindow *window = GDU_WINDOW (user_data);
  UDisksObject *object;
  UDisksPartition *partition;

  if (!gdu_utils_show_confirmation (GTK_WINDOW (window),
                                    _("Are you sure you want to delete the partition?"),
                                    _("All data on the partition will be lost"),
                                    _("_Delete")))
    goto out;

  object = gdu_volume_grid_get_selected_device (GDU_VOLUME_GRID (window->volume_grid));
  partition = udisks_object_peek_partition (object);
  udisks_partition_call_delete (partition,
                                g_variant_new ("a{sv}", NULL), /* options */
                                NULL, /* cancellable */
                                (GAsyncReadyCallback) partition_delete_cb,
                                g_object_ref (window));

 out:
  ;
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
on_devtab_action_eject_activated (GtkAction *action,
                                  gpointer   user_data)
{
  GduWindow *window = GDU_WINDOW (user_data);
  UDisksDrive *drive;

  drive = udisks_object_peek_drive (window->current_object);
  udisks_drive_call_eject (drive,
                           g_variant_new ("a{sv}", NULL), /* options */
                           NULL, /* cancellable */
                           (GAsyncReadyCallback) eject_cb,
                           g_object_ref (window));
}

/* ---------------------------------------------------------------------------------------------------- */

static void
on_devtab_action_unlock_activated (GtkAction *action,
                                   gpointer   user_data)
{
  GduWindow *window = GDU_WINDOW (user_data);
  UDisksObject *object;

  object = gdu_volume_grid_get_selected_device (GDU_VOLUME_GRID (window->volume_grid));
  g_assert (object != NULL);
  gdu_unlock_dialog_show (window, object);
}

/* ---------------------------------------------------------------------------------------------------- */

static void
lock_cb (UDisksEncrypted *encrypted,
         GAsyncResult    *res,
         gpointer         user_data)
{
  GduWindow *window = GDU_WINDOW (user_data);
  GError *error;

  error = NULL;
  if (!udisks_encrypted_call_lock_finish (encrypted,
                                          res,
                                          &error))
    {
      gdu_utils_show_error (GTK_WINDOW (window),
                            _("Error locking encrypted device"),
                            error);
      g_error_free (error);
    }
  g_object_unref (window);
}

static void
on_devtab_action_lock_activated (GtkAction *action,
                                 gpointer   user_data)
{
  GduWindow *window = GDU_WINDOW (user_data);
  UDisksObject *object;
  UDisksEncrypted *encrypted;

  object = gdu_volume_grid_get_selected_device (GDU_VOLUME_GRID (window->volume_grid));
  encrypted = udisks_object_peek_encrypted (object);

  udisks_encrypted_call_lock (encrypted,
                              g_variant_new ("a{sv}", NULL), /* options */
                              NULL, /* cancellable */
                              (GAsyncReadyCallback) lock_cb,
                              g_object_ref (window));
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
on_devtab_action_activate_swap_activated (GtkAction *action, gpointer user_data)
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
on_devtab_action_deactivate_swap_activated (GtkAction *action, gpointer user_data)
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
  update_all (window);

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
        }
      g_list_foreach (blocks, (GFunc) g_object_unref, NULL);
      g_list_free (blocks);
    }
  if (jobs != NULL)
    {
      UDisksJob *job = UDISKS_JOB (jobs->data);
      udisks_job_call_cancel (job,
                              g_variant_new ("a{sv}", NULL), /* options */
                              NULL, /* cancellable */
                              (GAsyncReadyCallback) drive_job_cancel_cb,
                              g_object_ref (window));
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
  if (jobs != NULL)
    {
      UDisksJob *job = UDISKS_JOB (jobs->data);
      udisks_job_call_cancel (job,
                              g_variant_new ("a{sv}", NULL), /* options */
                              NULL, /* cancellable */
                              (GAsyncReadyCallback) job_cancel_cb,
                              g_object_ref (window));
    }
  g_list_foreach (jobs, (GFunc) g_object_unref, NULL);
  g_list_free (jobs);
}
