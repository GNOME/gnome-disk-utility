/*
 * Copyright (C) 2008-2013 Red Hat, Inc.
 *
 * Licensed under GPL version 2 or later.
 *
 * Author: David Zeuthen <zeuthen@gmail.com>
 */

#include "adwaita.h"
#include "config.h"

#include <glib/gi18n.h>

#include <math.h>

#include "gdu-application.h"
#include "gdu-disk-settings-dialog.h"
#include "gdu-drive.h"
#include "gdu-manager.h"
#include "gdu-window.h"
#include "glib-object.h"
#include "glib.h"
#include "glibconfig.h"
#include "gtk/gtk.h"
#include "udisks/udisks-generated.h"

struct _GduDiskSettingsDialog {
  AdwDialog     parent_instance;

  GtkWidget    *window_title;
  GtkWidget    *done_button;

  /* Standby */
  GtkWidget    *standby_group;
  GtkWidget    *standby_value_row;
  GtkWidget    *override_standby_switch;
  GtkWidget    *standby_time_adjustment;

  /* APM */
  GtkWidget    *apm_group;
  GtkWidget    *apm_value_row;
  GtkWidget    *override_apm_switch;
  GtkWidget    *apm_adjustment;

  /* AAM */
  GtkWidget    *aam_group;
  GtkWidget    *aam_value_scale;
  GtkWidget    *override_aam_switch;
  GtkWidget    *aam_adjustment;

  /* Write Cache */
  GtkWidget    *write_cache_group;
  GtkWidget    *override_write_cache_switch;
  GtkWidget    *write_cache_combo_row;

  GduDrive     *drive;
};

G_DEFINE_TYPE (GduDiskSettingsDialog, gdu_disk_settings_dialog, ADW_TYPE_DIALOG)

static gpointer
gdu_disk_settings_dialog_get_window (GduDiskSettingsDialog *self)
{
  return gtk_widget_get_ancestor (GTK_WIDGET (self), GTK_TYPE_WINDOW);
}

static GVariant *
gdu_disk_settings_dialog_get_new_configuration (GduDiskSettingsDialog *self,
                                                GVariant              *orig_drive_configuration)
{
  GVariantBuilder builder;

  g_variant_builder_init (&builder, G_VARIANT_TYPE_VARDICT);
  if (orig_drive_configuration != NULL)
    {
      GVariantIter iter;
      const gchar *key;
      GVariant *value;
      g_variant_iter_init (&iter, orig_drive_configuration);
      while (g_variant_iter_next (&iter, "{&sv}", &key, &value))
        {
          if (g_strcmp0 (key, "ata-pm-standby") == 0 ||
              g_strcmp0 (key, "ata-apm-level") == 0 ||
              g_strcmp0 (key, "ata-aam-level") == 0 ||
              g_strcmp0 (key, "ata-write-cache-enabled") == 0)
            {
              /* handled by us, skip */
              g_variant_unref (value);
              continue;
            }

          g_variant_builder_add (&builder, "{sv}", key, value);
          g_variant_unref (value);
        }
    }

  /* Standby */
  if (adw_switch_row_get_active (ADW_SWITCH_ROW (self->override_standby_switch)))
    {
      gint value = (gint) gtk_adjustment_get_value (GTK_ADJUSTMENT (self->standby_time_adjustment));
      g_variant_builder_add (&builder, "{sv}", "ata-pm-standby", g_variant_new_int32 (value));
    }


  /* APM */
  if (adw_switch_row_get_active (ADW_SWITCH_ROW (self->override_apm_switch)))
    {
      gint value = (gint) gtk_adjustment_get_value (GTK_ADJUSTMENT (self->apm_adjustment));
      g_variant_builder_add (&builder, "{sv}", "ata-apm-level", g_variant_new_int32 (value));
    }

  /* AAM */
  if (adw_switch_row_get_active (ADW_SWITCH_ROW (self->override_aam_switch)))
    {
      gint value = (gint) gtk_adjustment_get_value (GTK_ADJUSTMENT (self->aam_adjustment));
      if (value < 128)
        value = 0;
      g_variant_builder_add (&builder, "{sv}", "ata-aam-level", g_variant_new_int32 (value));
    }

  /* Write Cache */
  if (adw_switch_row_get_active (ADW_SWITCH_ROW (self->override_write_cache_switch)))
    {
      gboolean enabled = (adw_combo_row_get_selected (ADW_COMBO_ROW (self->write_cache_combo_row)) == 0);
      g_variant_builder_add (&builder, "{sv}", "ata-write-cache-enabled", g_variant_new_boolean (enabled));
    }

  return g_variant_builder_end (&builder);
}

static void
gdu_disk_settings_dialog_update_standby_label (GduDiskSettingsDialog *self)
{
  gint64 value;
  g_autofree char *s = NULL;

  value = gtk_adjustment_get_value (GTK_ADJUSTMENT (self->standby_time_adjustment));

  if (value == 0)
    s = g_strdup (_("Never"));
  else if (value < 241)
    s = gdu_utils_format_duration_usec (value * 5 * G_USEC_PER_SEC,
                                        GDU_FORMAT_DURATION_FLAGS_NONE);
  else if (value < 252)
    s = gdu_utils_format_duration_usec ((value - 240) * 30 * 60 * G_USEC_PER_SEC,
                                        GDU_FORMAT_DURATION_FLAGS_NONE);
  else if (value == 252)
    s = gdu_utils_format_duration_usec (21 * 60 * G_USEC_PER_SEC,
                                        GDU_FORMAT_DURATION_FLAGS_NONE);
  else if (value == 253)
    s = g_strdup (_("Vendor-defined"));
  else if (value == 254)
    s = g_strdup (_("Reserved"));
  else if (value == 255)
    s = gdu_utils_format_duration_usec ((21 * 60 + 15) * G_USEC_PER_SEC,
                                          GDU_FORMAT_DURATION_FLAGS_NONE);
  adw_action_row_set_subtitle (ADW_ACTION_ROW (self->standby_value_row), s);
}

static void
gdu_disk_settings_dialog_update_apm_label (GduDiskSettingsDialog *self)
{
  gint value;
  g_autofree char *s = NULL;

  value = gtk_adjustment_get_value (GTK_ADJUSTMENT (self->apm_adjustment));
  if (value == 255)
    s = g_strdup (_("255 (Disabled)"));
  else if (value <= 127)
    s = g_strdup_printf (_("%d (Spin-down permitted)"), value);
  else
    s = g_strdup_printf (_("%d (Spin-down not permitted)"), value);
  adw_action_row_set_subtitle (ADW_ACTION_ROW (self->apm_value_row), s);
}

static void
on_property_changed (GduDiskSettingsDialog *self)
{
  GVariant *new_drive_configuration;
  GVariant *orig_drive_configuration;
  UDisksObject *object;
  gboolean changed;

  object = gdu_drive_get_object (self->drive);
  /* figure out if things has changed */
  orig_drive_configuration = udisks_drive_get_configuration(udisks_object_peek_drive(object));
  new_drive_configuration = gdu_disk_settings_dialog_get_new_configuration (self, orig_drive_configuration);
  changed = !g_variant_equal(new_drive_configuration, orig_drive_configuration);
  g_variant_unref (new_drive_configuration);

  gtk_widget_set_sensitive (self->done_button, changed);

  /* update labels */
  gdu_disk_settings_dialog_update_standby_label (self);
  gdu_disk_settings_dialog_update_apm_label (self);
}

static void
gdu_disk_settings_dialog_set_title (GduDiskSettingsDialog *self)
{
  g_autoptr(UDisksObjectInfo) info = NULL;
  GduManager *manager;

  manager = gdu_manager_get_default(NULL);

  info = udisks_client_get_object_info (gdu_manager_get_client(manager), gdu_drive_get_object(self->drive));
  adw_window_title_set_subtitle (ADW_WINDOW_TITLE (self->window_title), udisks_object_info_get_one_liner (info));
}

static void
on_set_configuration_cb (GObject      *source_object,
                         GAsyncResult *res,
                         gpointer      user_data)
{
  g_autoptr(GduDiskSettingsDialog) self = GDU_DISK_SETTINGS_DIALOG (user_data);
  g_autoptr(GError) error = NULL;

  if (!udisks_drive_call_set_configuration_finish (UDISKS_DRIVE (source_object),
                                                   res,
                                                   &error))
    {
      gdu_utils_show_error (gdu_disk_settings_dialog_get_window (self),
                            _("Error setting configuration"),
                            error);
    }

  adw_dialog_close (ADW_DIALOG (self));
}


static void
on_done_clicked (GduDiskSettingsDialog *self)
{
  UDisksObject *object;
  UDisksDrive *drive;

  object = gdu_drive_get_object(self->drive);
  drive = udisks_object_peek_drive (object);

  udisks_drive_call_set_configuration (drive,
                                       gdu_disk_settings_dialog_get_new_configuration (self, udisks_drive_get_configuration(drive)),  /* consumes floating */
                                       g_variant_new ("a{sv}", NULL), /* options */
                                       NULL, /* cancellable */
                                       on_set_configuration_cb,
                                       g_object_ref(self));
}

static void
gdu_disk_settings_dialog_disable_unused_widgets (GduDiskSettingsDialog *self)
{
  UDisksObject *object;
  UDisksDrive *drive;
  UDisksDriveAta *ata;
  gboolean is_ssd = FALSE;

  object = gdu_drive_get_object(self->drive);
  drive = udisks_object_peek_drive (object);
  ata = udisks_object_peek_drive_ata (object);

  if (ata == NULL)
    {
      gtk_widget_set_visible (self->standby_group, FALSE);
      gtk_widget_set_visible (self->apm_group, FALSE);
      gtk_widget_set_visible (self->aam_group, FALSE);
      gtk_widget_set_visible (self->write_cache_group, FALSE);
      return;
    }

  /* Disable widgets not relevant for a drive - see also gdu_disk_settings_dialog_should_show() */
  if (udisks_drive_get_rotation_rate (drive) == 0)
    is_ssd = TRUE;

  if (!udisks_drive_ata_get_pm_supported (ata) || is_ssd)
    gtk_widget_set_visible (self->standby_group, FALSE);

  if (!udisks_drive_ata_get_apm_supported (ata))
    gtk_widget_set_visible (self->apm_group, FALSE);

  if (!udisks_drive_ata_get_aam_supported (ata))
    gtk_widget_set_visible (self->aam_group, FALSE);

  if (!udisks_drive_ata_get_write_cache_supported (ata))
    gtk_widget_set_visible (self->write_cache_group, FALSE);
}

static void
gdu_disk_settings_dialog_dispose (GObject *object)
{
  GduDiskSettingsDialog *self = GDU_DISK_SETTINGS_DIALOG (object);

  g_clear_object (&self->drive);

  G_OBJECT_CLASS (gdu_disk_settings_dialog_parent_class)->dispose (object);
}

void
gdu_disk_settings_dialog_class_init (GduDiskSettingsDialogClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->dispose = gdu_disk_settings_dialog_dispose;

  gtk_widget_class_set_template_from_resource (widget_class,
                                               "/org/gnome/DiskUtility/ui/"
                                               "gdu-disk-settings-dialog.ui");

  gtk_widget_class_bind_template_child (widget_class, GduDiskSettingsDialog, window_title);
  gtk_widget_class_bind_template_child (widget_class, GduDiskSettingsDialog, done_button);

  gtk_widget_class_bind_template_child (widget_class, GduDiskSettingsDialog, standby_group);
  gtk_widget_class_bind_template_child (widget_class, GduDiskSettingsDialog, standby_value_row);
  gtk_widget_class_bind_template_child (widget_class, GduDiskSettingsDialog, override_standby_switch);
  gtk_widget_class_bind_template_child (widget_class, GduDiskSettingsDialog, standby_time_adjustment);

  gtk_widget_class_bind_template_child (widget_class, GduDiskSettingsDialog, apm_group);
  gtk_widget_class_bind_template_child (widget_class, GduDiskSettingsDialog, apm_value_row);
  gtk_widget_class_bind_template_child (widget_class, GduDiskSettingsDialog, override_apm_switch);
  gtk_widget_class_bind_template_child (widget_class, GduDiskSettingsDialog, apm_adjustment);

  gtk_widget_class_bind_template_child (widget_class, GduDiskSettingsDialog, aam_group);
  gtk_widget_class_bind_template_child (widget_class, GduDiskSettingsDialog, aam_value_scale);
  gtk_widget_class_bind_template_child (widget_class, GduDiskSettingsDialog, override_aam_switch);
  gtk_widget_class_bind_template_child (widget_class, GduDiskSettingsDialog, aam_adjustment);

  gtk_widget_class_bind_template_child (widget_class, GduDiskSettingsDialog, write_cache_group);
  gtk_widget_class_bind_template_child (widget_class, GduDiskSettingsDialog, override_write_cache_switch);
  gtk_widget_class_bind_template_child (widget_class, GduDiskSettingsDialog, write_cache_combo_row);

  gtk_widget_class_bind_template_callback (widget_class, on_done_clicked);
  gtk_widget_class_bind_template_callback (widget_class, on_property_changed);
}

void
gdu_disk_settings_dialog_init (GduDiskSettingsDialog *self)
{
  gtk_widget_init_template (GTK_WIDGET (self));
}

void
gdu_disk_settings_dialog_show (GtkWindow    *parent_window,

                               GduDrive     *drive)
{
  GduDiskSettingsDialog *self;
  UDisksObject *object;
  UDisksDriveAta *ata;
  GVariant *orig_drive_configuration;

  self = g_object_new (GDU_TYPE_DISK_SETTINGS_DIALOG, NULL);
  self->drive = g_object_ref (drive);
  object = gdu_drive_get_object (drive);
  ata = udisks_object_peek_drive_ata (object);
  orig_drive_configuration = udisks_drive_get_configuration(udisks_object_peek_drive(object));

  if (orig_drive_configuration != NULL)
    {
      gint standby_value = -1;
      gint apm_value = -1;
      gint aam_value = -1;
      gint vendor_recommended_value;
      gboolean write_cache_enabled = FALSE;
      gboolean write_cache_enabled_set = FALSE;

      /* Power Management page */
      g_variant_lookup (orig_drive_configuration, "ata-pm-standby", "i", &standby_value);
      g_variant_lookup (orig_drive_configuration, "ata-apm-level", "i", &apm_value);
      g_variant_lookup (orig_drive_configuration, "ata-aam-level", "i", &aam_value);
      if (g_variant_lookup (orig_drive_configuration, "ata-write-cache-enabled", "b", &write_cache_enabled))
        write_cache_enabled_set = TRUE;

      adw_switch_row_set_active (ADW_SWITCH_ROW (self->override_standby_switch), standby_value != -1 ? TRUE : FALSE);
      gtk_adjustment_set_value (GTK_ADJUSTMENT (self->standby_time_adjustment), standby_value != -1 ? standby_value : 120);

      adw_switch_row_set_active (ADW_SWITCH_ROW (self->override_apm_switch), apm_value != -1 ? TRUE : FALSE);
      gtk_adjustment_set_value (GTK_ADJUSTMENT (self->apm_adjustment), apm_value != -1 ? apm_value : 127);


      vendor_recommended_value = ata != NULL ? udisks_drive_ata_get_aam_vendor_recommended_value (ata) : 0;
      adw_switch_row_set_active (ADW_SWITCH_ROW (self->override_aam_switch), aam_value != -1 ? TRUE : FALSE);
      if (aam_value == -1)
        {
          gint default_aam_value = vendor_recommended_value;
          if (default_aam_value < 128 || default_aam_value > 254)
            default_aam_value = 254;
          gtk_adjustment_set_value (GTK_ADJUSTMENT (self->aam_adjustment), default_aam_value);
        }
      else
        {
          if (aam_value < 128)
            aam_value = 127;
          gtk_adjustment_set_value (GTK_ADJUSTMENT (self->aam_adjustment), aam_value);
        }

      if (vendor_recommended_value >= 128 && vendor_recommended_value <= 254)
        gtk_scale_add_mark (GTK_SCALE (self->aam_value_scale),
                            vendor_recommended_value,
                            GTK_POS_BOTTOM,
                            _("Vendor Recommended"));

      adw_switch_row_set_active (ADW_SWITCH_ROW (self->override_write_cache_switch), write_cache_enabled_set ? TRUE : FALSE);
      adw_combo_row_set_selected (ADW_COMBO_ROW (self->write_cache_combo_row), write_cache_enabled_set ? (write_cache_enabled ? 0 : 1) : -1);
    }

  gdu_disk_settings_dialog_set_title (self);
  gdu_disk_settings_dialog_disable_unused_widgets (self);
  on_property_changed (self);

  adw_dialog_present(ADW_DIALOG (self), GTK_WIDGET (parent_window));
}
