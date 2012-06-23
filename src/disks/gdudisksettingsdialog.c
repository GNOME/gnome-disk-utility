/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*-
 *
 * Copyright (C) 2008-2011 Red Hat, Inc.
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

#include <math.h>

#include "gduapplication.h"
#include "gduwindow.h"
#include "gdudisksettingsdialog.h"
#include "gduutils.h"

/* ---------------------------------------------------------------------------------------------------- */

typedef struct
{
  volatile gint ref_count;

  UDisksObject *object;
  UDisksDrive *drive;
  UDisksDriveAta *ata;

  GCancellable *cancellable;

  GduWindow *window;
  GtkBuilder *builder;

  GtkWidget *dialog;

  GVariant *orig_drive_configuration;

  /* Power Management page */
  GtkWidget *pm_page;
  GtkWidget *pm_apply_settings_switch;
  GtkWidget *pm_settings_box;

  GtkWidget *standby_box;
  GtkAdjustment *standby_adjustment;
  GtkWidget *standby_scale;
  GtkWidget *standby_disable_checkbutton;

  GtkWidget *apm_box;
  GtkAdjustment *apm_adjustment;
  GtkWidget *apm_scale;
  GtkWidget *apm_disable_checkbutton;

  /* Acoustic page */
  GtkWidget *acoustic_page;
  GtkWidget *acoustic_apply_settings_switch;
  GtkWidget *acoustic_settings_box;

  GtkWidget *aam_box;
  GtkAdjustment *aam_adjustment;
  GtkWidget *aam_scale;
  GtkWidget *aam_disable_checkbutton;
} DialogData;

G_LOCK_DEFINE (bm_lock);

static const struct {
  goffset offset;
  const gchar *name;
} widget_mapping[] = {
  /* Power Management page */
  {G_STRUCT_OFFSET (DialogData, pm_page), "power-management-page"},
  {G_STRUCT_OFFSET (DialogData, pm_apply_settings_switch), "pm-apply-settings-switch"},
  {G_STRUCT_OFFSET (DialogData, pm_settings_box), "pm-settings-box"},
  {G_STRUCT_OFFSET (DialogData, standby_box), "standby-box"},
  {G_STRUCT_OFFSET (DialogData, standby_adjustment), "standby-adjustment"},
  {G_STRUCT_OFFSET (DialogData, standby_scale), "standby-scale"},
  {G_STRUCT_OFFSET (DialogData, standby_disable_checkbutton), "standby-disable-checkbutton"},
  {G_STRUCT_OFFSET (DialogData, apm_box), "apm-box"},
  {G_STRUCT_OFFSET (DialogData, apm_adjustment), "apm-adjustment"},
  {G_STRUCT_OFFSET (DialogData, apm_scale), "apm-scale"},
  {G_STRUCT_OFFSET (DialogData, apm_disable_checkbutton), "apm-disable-checkbutton"},

  /* Acoustic page */
  {G_STRUCT_OFFSET (DialogData, acoustic_page), "acoustic-page"},
  {G_STRUCT_OFFSET (DialogData, acoustic_apply_settings_switch), "acoustic-apply-settings-switch"},
  {G_STRUCT_OFFSET (DialogData, acoustic_settings_box), "acoustic-settings-box"},
  {G_STRUCT_OFFSET (DialogData, aam_box), "aam-box"},
  {G_STRUCT_OFFSET (DialogData, aam_adjustment), "aam-adjustment"},
  {G_STRUCT_OFFSET (DialogData, aam_scale), "aam-scale"},
  {G_STRUCT_OFFSET (DialogData, aam_disable_checkbutton), "aam-disable-checkbutton"},

  {0, NULL}
};

static void update_dialog (DialogData *data);

static void disable_unused_pages (DialogData *data);

/* ---------------------------------------------------------------------------------------------------- */

static DialogData *
dialog_data_ref (DialogData *data)
{
  g_atomic_int_inc (&data->ref_count);
  return data;
}

static void
dialog_data_unref (DialogData *data)
{
  if (g_atomic_int_dec_and_test (&data->ref_count))
    {
      if (data->dialog != NULL)
        {
          gtk_widget_hide (data->dialog);
          gtk_widget_destroy (data->dialog);
          data->dialog = NULL;
        }

      g_clear_object (&data->object);
      g_clear_object (&data->window);
      g_clear_object (&data->builder);

      if (data->orig_drive_configuration != NULL)
        g_variant_unref (data->orig_drive_configuration);

      g_free (data);
    }
}

static void
dialog_data_close (DialogData *data)
{
  gtk_dialog_response (GTK_DIALOG (data->dialog), GTK_RESPONSE_CANCEL);
}

/* ---------------------------------------------------------------------------------------------------- */

static GVariant *
compute_configuration (DialogData *data)
{
  GVariantBuilder builder;

  g_variant_builder_init (&builder, G_VARIANT_TYPE_VARDICT);
  if (data->orig_drive_configuration != NULL)
    {
      GVariantIter iter;
      const gchar *key;
      GVariant *value;
      g_variant_iter_init (&iter, data->orig_drive_configuration);
      while (g_variant_iter_next (&iter, "{&sv}", &key, &value))
        {
          if (g_strcmp0 (key, "ata-pm-standby") == 0 ||
              g_strcmp0 (key, "ata-apm-level") == 0 ||
              g_strcmp0 (key, "ata-aam-level") == 0)
            {
              /* handled by us, skip */
            }
          else
            {
              g_variant_builder_add (&builder, "{sv}", key, value);
            }
          g_variant_unref (value);
        }
    }

  /* Power Management page */
  if (gtk_switch_get_active (GTK_SWITCH (data->pm_apply_settings_switch)))
    {
      gint ata_pm_standby = 0;
      gint ata_apm_level = 255;

      if (!gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (data->standby_disable_checkbutton)))
        ata_pm_standby = (gint) gtk_adjustment_get_value (data->standby_adjustment);

      if (!gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (data->apm_disable_checkbutton)))
        ata_apm_level = (gint) gtk_adjustment_get_value (data->apm_adjustment);

      if (udisks_drive_ata_get_pm_supported (data->ata))
        g_variant_builder_add (&builder, "{sv}", "ata-pm-standby", g_variant_new_int32 (ata_pm_standby));
      if (udisks_drive_ata_get_apm_supported (data->ata))
        g_variant_builder_add (&builder, "{sv}", "ata-apm-level", g_variant_new_int32 (ata_apm_level));
    }

  /* Acoustic page */
  if (gtk_switch_get_active (GTK_SWITCH (data->acoustic_apply_settings_switch)))
    {
      gint ata_aam_level = 0;

      if (!gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (data->aam_disable_checkbutton)))
        ata_aam_level = (gint) gtk_adjustment_get_value (data->aam_adjustment);

      if (udisks_drive_ata_get_aam_supported (data->ata))
        g_variant_builder_add (&builder, "{sv}", "ata-aam-level", g_variant_new_int32 (ata_aam_level));
    }

  return g_variant_builder_end (&builder);
}

static gboolean
_g_variant_equal0 (GVariant *a, GVariant *b)
{
  gboolean ret = FALSE;
  if (a == NULL && b == NULL)
    {
      ret = TRUE;
      goto out;
    }
  if (a == NULL || b == NULL)
    goto out;
  ret = g_variant_equal (a, b);
out:
  return ret;
}

static void
update_dialog (DialogData *data)
{
  GVariant *new_drive_configuration;
  gboolean changed = FALSE;

  /* figure out if things has changed */
  new_drive_configuration = compute_configuration (data);
  if (!_g_variant_equal0 (new_drive_configuration, data->orig_drive_configuration))
    changed = TRUE;
  g_variant_unref (new_drive_configuration);
  gtk_dialog_set_response_sensitive (GTK_DIALOG (data->dialog), GTK_RESPONSE_OK, changed);
}

static void
on_property_changed (GObject     *object,
                     GParamSpec  *pspec,
                     gpointer     user_data)
{
  DialogData *data = user_data;
  update_dialog (data);
}

/* ---------------------------------------------------------------------------------------------------- */

static gchar *
on_apm_scale_format_value (GtkScale *scale,
                           gdouble   value,
                           gpointer  user_data)
{
  DialogData *data = user_data;
  gchar *ret = NULL;

  if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (data->apm_disable_checkbutton)))
    {
      ret = g_strdup (C_("apm-level", "Disabled"));
    }
  else if (value <= 127)
    {
      ret = g_strdup_printf (C_("apm-level", "%d (Spin-down permitted)"), (gint) value);
    }
  else
    {
      ret = g_strdup_printf (C_("apm-level", "%d (Spin-down not permitted)"), (gint) value);
    }

  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

static gchar *
on_aam_scale_format_value (GtkScale *scale,
                           gdouble   value,
                           gpointer  user_data)
{
  DialogData *data = user_data;
  gchar *ret = NULL;

  if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (data->aam_disable_checkbutton)))
    {
      ret = g_strdup (C_("aam-level", "Disabled"));
    }
  else
    {
      ret = g_strdup_printf ("%d", (gint) value);
    }

  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

static gchar *
on_standby_scale_format_value (GtkScale *scale,
                               gdouble   value,
                               gpointer  user_data)
{
  DialogData *data = user_data;
  gchar *ret = NULL;

  value = floor (value);

  if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (data->standby_disable_checkbutton)))
    {
      ret = g_strdup (C_("standby-value", "Disabled"));
    }
  else if (value < 241)
    {
      ret = gdu_utils_format_duration_msec (value * 5 * 1000);
    }
  else if (value < 252)
    {
      ret = gdu_utils_format_duration_msec ((value - 240) * 30 * 60 * 1000);
    }
  else if (value == 252)
    {
      ret = gdu_utils_format_duration_msec (21 * 60 * 1000);
    }
  else if (value == 253)
    {
      ret = g_strdup (C_("standby-value", "Vendor-defined"));
    }
  else if (value == 254)
    {
      ret = g_strdup (C_("standby-value", "Reserved"));
    }
  else if (value == 255)
    {
      ret = gdu_utils_format_duration_msec ((21 * 60 + 15) * 1000);
    }

  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

static void
on_set_configuration_cb (GObject      *source_object,
                         GAsyncResult *res,
                         gpointer      user_data)
{
  DialogData *data = user_data;
  GError *error = NULL;

  if (!udisks_drive_call_set_configuration_finish (UDISKS_DRIVE (source_object),
                                                   res,
                                                   &error))
    {
      gdu_window_show_error (data->window,
                             _("Error setting configuration"),
                             error);
      g_clear_error (&error);
      goto out;
    }
  else
    {
      dialog_data_close (data);
    }

 out:
  dialog_data_unref (data);
}

/* ---------------------------------------------------------------------------------------------------- */

typedef struct
{
  gdouble pos;
  const gchar *str;
} Mark;

void
gdu_disk_settings_dialog_show (GduWindow    *window,
                               UDisksObject *object)
{
  DialogData *data;
  guint n;
  Mark standby_marks[5] = {
    /* Translators: This is a mark on the Standby scale. The string should be as short as possible */
    { 12.0, N_("1 minute")},
    /* Translators: This is a mark on the Standby scale. The string should be as short as possible */
    { 60.0, N_("5 minutes")},
    /* Translators: This is a mark on the Standby scale. The string should be as short as possible */
    {120.0, N_("10 minutes")},
    /* Translators: This is a mark on the Standby scale. The string should be as short as possible */
    {180.0, N_("15 minutes")},
    /* Translators: This is a mark on the Standby scale. The string should be as short as possible */
    {246.0, N_("3 hours")},
  };
  Mark apm_marks[3] = {
    /* Translators: This is a mark on the APM scale. The string should be as short as possible */
    {  1.0, N_("Power Savings")},
    /* Translators: This is a mark on the APM scale. The string should be as short as possible. The left arrow ("←") is to signify that the left part of the scale offers spindown. In RTL locales, please use a right arrow ("→") instead. */
    {127.0, N_("← Spindown")},
    /* Translators: This is a mark on the APM scale. The string should be as short as possible */
    {254.0, N_("Performance")}
  };
  Mark aam_marks[2] = {
    /* Translators: This is a mark on the AAM scale. The string should be as short as possible */
    {128.0, N_("Quiet")},
    /* Translators: This is a mark on the AAM scale. The string should be as short as possible */
    {254.0, N_("Loud")}
  };

  data = g_new0 (DialogData, 1);
  data->ref_count = 1;
  data->object = g_object_ref (object);
  data->drive = udisks_object_peek_drive (data->object);
  data->ata = udisks_object_peek_drive_ata (data->object);
  data->window = g_object_ref (window);
  data->orig_drive_configuration = udisks_drive_dup_configuration (data->drive);

  data->dialog = GTK_WIDGET (gdu_application_new_widget (gdu_window_get_application (window),
                                                         "disk-settings-dialog.ui",
                                                         "dialog1",
                                                         &data->builder));
  for (n = 0; widget_mapping[n].name != NULL; n++)
    {
      gpointer *p = (gpointer *) ((char *) data + widget_mapping[n].offset);
      *p = gtk_builder_get_object (data->builder, widget_mapping[n].name);
    }

  /* add marks on Standby, APM and AAM scales */
  for (n = 0; n < G_N_ELEMENTS (standby_marks); n++)
    {
      Mark *mark = &standby_marks[n];
      gchar *s = g_strdup_printf ("<small>%s</small>", _(mark->str));
      gtk_scale_add_mark (GTK_SCALE (data->standby_scale), mark->pos, GTK_POS_BOTTOM, s);
      g_free (s);
    }
  for (n = 0; n < G_N_ELEMENTS (apm_marks); n++)
    {
      Mark *mark = &apm_marks[n];
      gchar *s = g_strdup_printf ("<small>%s</small>", _(mark->str));
      gtk_scale_add_mark (GTK_SCALE (data->apm_scale), mark->pos, GTK_POS_BOTTOM, s);
      g_free (s);
    }
  for (n = 0; n < G_N_ELEMENTS (aam_marks); n++)
    {
      Mark *mark = &aam_marks[n];
      gchar *s = g_strdup_printf ("<small>%s</small>", _(mark->str));
      gtk_scale_add_mark (GTK_SCALE (data->aam_scale), mark->pos, GTK_POS_BOTTOM, s);
      g_free (s);
    }

  g_signal_connect (data->standby_scale, "format-value", G_CALLBACK (on_standby_scale_format_value), data);
  g_signal_connect (data->apm_scale, "format-value", G_CALLBACK (on_apm_scale_format_value), data);
  g_signal_connect (data->aam_scale, "format-value", G_CALLBACK (on_aam_scale_format_value), data);

  gtk_window_set_transient_for (GTK_WINDOW (data->dialog), GTK_WINDOW (window));

  disable_unused_pages (data);

  /* initialize dialog with values from current configuration */
  if (data->orig_drive_configuration != NULL)
    {
      gint ata_pm_standby = -1;
      gint ata_apm_level = -1;
      gint ata_aam_level = -1;

      /* Power Management page */
      g_variant_lookup (data->orig_drive_configuration, "ata-pm-standby", "i", &ata_pm_standby);
      g_variant_lookup (data->orig_drive_configuration, "ata-apm-level", "i", &ata_apm_level);
      if (ata_pm_standby == -1 && ata_apm_level == -1)
        {
          /* No settings at all, set Switch to OFF and chose some good defaults */
          gtk_switch_set_active (GTK_SWITCH (data->pm_apply_settings_switch), FALSE);
          gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (data->standby_disable_checkbutton), FALSE);
          gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (data->apm_disable_checkbutton), FALSE);
          gtk_adjustment_set_value (data->standby_adjustment, 180);
          gtk_adjustment_set_value (data->apm_adjustment, 127);
        }
      else
        {
          /* Set "Disable" buttons as appropriate and set slider values to something reasonable */
          if (ata_pm_standby == 0 || ata_pm_standby == -1)
            {
              ata_pm_standby = 180;
              gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (data->standby_disable_checkbutton), TRUE);
            }
          if (ata_apm_level == 255 || ata_apm_level == -1)
            {
              ata_apm_level = 127;
              gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (data->apm_disable_checkbutton), TRUE);
            }
          gtk_adjustment_set_value (data->standby_adjustment, ata_pm_standby);
          gtk_adjustment_set_value (data->apm_adjustment, ata_apm_level);
          gtk_switch_set_active (GTK_SWITCH (data->pm_apply_settings_switch), TRUE);
        }

      /* Acoustic page */
      g_variant_lookup (data->orig_drive_configuration, "ata-aam-level", "i", &ata_aam_level);
      if (ata_aam_level == -1)
        {
          /* No settings at all, set Switch to OFF and chose some good defaults */
          gtk_switch_set_active (GTK_SWITCH (data->acoustic_apply_settings_switch), FALSE);
          gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (data->aam_disable_checkbutton), FALSE);
          gtk_adjustment_set_value (data->aam_adjustment, 128);
        }
      else
        {
          /* Set "Disable" buttons as appropriate and set slider values to something reasonable */
          if (ata_aam_level == 0 || ata_aam_level == -1)
            {
              ata_aam_level = 128;
              gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (data->aam_disable_checkbutton), TRUE);
            }
          gtk_adjustment_set_value (data->aam_adjustment, ata_aam_level);
          gtk_switch_set_active (GTK_SWITCH (data->acoustic_apply_settings_switch), TRUE);
        }
    }

  g_signal_connect (data->pm_apply_settings_switch,
                    "notify::active", G_CALLBACK (on_property_changed), data);
  g_signal_connect (data->acoustic_apply_settings_switch,
                    "notify::active", G_CALLBACK (on_property_changed), data);
  g_signal_connect (data->standby_adjustment,
                    "notify::value", G_CALLBACK (on_property_changed), data);
  g_signal_connect (data->standby_disable_checkbutton,
                    "notify::active", G_CALLBACK (on_property_changed), data);
  g_signal_connect (data->apm_adjustment,
                    "notify::value", G_CALLBACK (on_property_changed), data);
  g_signal_connect (data->apm_disable_checkbutton,
                    "notify::active", G_CALLBACK (on_property_changed), data);
  g_signal_connect (data->aam_adjustment,
                    "notify::value", G_CALLBACK (on_property_changed), data);
  g_signal_connect (data->aam_disable_checkbutton,
                    "notify::active", G_CALLBACK (on_property_changed), data);

  g_object_bind_property (data->pm_apply_settings_switch,
                          "active",
                          data->pm_settings_box,
                          "sensitive",
                          G_BINDING_SYNC_CREATE);

  g_object_bind_property (data->acoustic_apply_settings_switch,
                          "active",
                          data->acoustic_settings_box,
                          "sensitive",
                          G_BINDING_SYNC_CREATE);

  g_object_bind_property (data->standby_disable_checkbutton,
                          "active",
                          data->standby_scale,
                          "sensitive",
                          G_BINDING_SYNC_CREATE|G_BINDING_INVERT_BOOLEAN);

  g_object_bind_property (data->apm_disable_checkbutton,
                          "active",
                          data->apm_scale,
                          "sensitive",
                          G_BINDING_SYNC_CREATE|G_BINDING_INVERT_BOOLEAN);

  g_object_bind_property (data->aam_disable_checkbutton,
                          "active",
                          data->aam_scale,
                          "sensitive",
                          G_BINDING_SYNC_CREATE|G_BINDING_INVERT_BOOLEAN);

  update_dialog (data);

  while (TRUE)
    {
      gint response;
      response = gtk_dialog_run (GTK_DIALOG (data->dialog));
      /* Keep in sync with .ui file */
      switch (response)
        {
        case GTK_RESPONSE_OK: /* OK */
          udisks_drive_call_set_configuration (data->drive,
                                               compute_configuration (data),  /* consumes floating */
                                               g_variant_new ("a{sv}", NULL), /* options */
                                               NULL, /* cancellable */
                                               on_set_configuration_cb,
                                               dialog_data_ref (data));
          break;

        default:
          goto out;
        }
    }
 out:
  dialog_data_close (data);
  dialog_data_unref (data);
}

/* ---------------------------------------------------------------------------------------------------- */

static void
hide_forever (GtkWidget *widget)
{
  gtk_widget_set_no_show_all (widget, TRUE);
  gtk_widget_set_visible (widget, FALSE);
}

static void
disable_unused_pages (DialogData *data)
{
  /* Disable pages (and parts of pages) not relevant for a drive - see also gdu_disk_settings_dialog_should_show() */

  if (!(udisks_drive_ata_get_pm_supported (data->ata) || udisks_drive_ata_get_apm_supported (data->ata)))
    {
      hide_forever (data->pm_page);
    }
  else
    {
      if (!udisks_drive_ata_get_pm_supported (data->ata))
        hide_forever (data->standby_box);
      if (!udisks_drive_ata_get_apm_supported (data->ata))
        hide_forever (data->apm_box);
    }

  if (!udisks_drive_ata_get_aam_supported (data->ata))
    hide_forever (data->acoustic_page);
}

gboolean
gdu_disk_settings_dialog_should_show (UDisksObject *object)
{
  gboolean ret = FALSE;
  UDisksDrive *drive;
  UDisksDriveAta *ata;

  g_return_val_if_fail (UDISKS_IS_OBJECT (object), FALSE);

  /* see also disabled_unused_pages() above */

  drive = udisks_object_peek_drive (object);
  if (drive == NULL)
    goto out;

  ata = udisks_object_peek_drive_ata (object);
  if (ata == NULL)
    goto out;

  if (udisks_drive_ata_get_pm_supported (ata) ||
      udisks_drive_ata_get_apm_supported (ata) ||
      udisks_drive_ata_get_aam_supported (ata))
    {
      ret = TRUE;
    }

 out:
  return ret;
}

