/*
 * Copyright (C) 2008-2013 Red Hat, Inc.
 *
 * Licensed under GPL version 2 or later.
 *
 * Author: David Zeuthen <zeuthen@gmail.com>
 */

#include "config.h"

#include <glib/gi18n.h>

#include <math.h>

#include "gdu-application.h"
#include "gdu-disk-settings-dialog.h"
#include "gdu-window.h"

/* ---------------------------------------------------------------------------------------------------- */

typedef struct
{
  volatile gint ref_count;

  UDisksObject *object;
  UDisksClient *client;
  UDisksDrive *drive;
  UDisksDriveAta *ata;

  GCancellable *cancellable;

  GtkWindow  *window;
  GtkWidget  *window_title;
  GtkBuilder *builder;

  GtkWidget *dialog;

  GtkWidget *ok_button;

  GVariant *orig_drive_configuration;

  /* Standby */
  GtkWidget *standby_box;
  GtkWidget *standby_switch;
  GtkWidget *standby_widgets_box;
  GtkWidget *standby_value_label;
  GtkWidget *standby_scale;
  GtkAdjustment *standby_adjustment;

  /* APM */
  GtkWidget *apm_box;
  GtkWidget *apm_switch;
  GtkWidget *apm_widgets_box;
  GtkWidget *apm_value_label;
  GtkWidget *apm_scale;
  GtkAdjustment *apm_adjustment;

  /* AAM */
  GtkWidget *aam_box;
  GtkWidget *aam_switch;
  GtkWidget *aam_widgets_box;
  GtkWidget *aam_vendor_recommended_value_label;
  GtkWidget *aam_value_label;
  GtkWidget *aam_scale;
  GtkAdjustment *aam_adjustment;

  /* Write Cache */
  GtkWidget *write_cache_box;
  GtkWidget *write_cache_switch;
  GtkWidget *write_cache_widgets_box;
  GtkWidget *write_cache_comboboxtext;

} DialogData;

static const struct {
  goffset offset;
  const gchar *name;
} widget_mapping[] = {
  /* Standby */
  {G_STRUCT_OFFSET (DialogData, standby_box), "standby-box"},
  {G_STRUCT_OFFSET (DialogData, standby_switch), "standby-switch"},
  {G_STRUCT_OFFSET (DialogData, standby_widgets_box), "standby-widgets-box"},
  {G_STRUCT_OFFSET (DialogData, standby_value_label), "standby-value-label"},
  {G_STRUCT_OFFSET (DialogData, standby_scale), "standby-scale"},
  {G_STRUCT_OFFSET (DialogData, standby_adjustment), "standby-adjustment"},

  /* APM */
  {G_STRUCT_OFFSET (DialogData, apm_box), "apm-box"},
  {G_STRUCT_OFFSET (DialogData, apm_switch), "apm-switch"},
  {G_STRUCT_OFFSET (DialogData, apm_widgets_box), "apm-widgets-box"},
  {G_STRUCT_OFFSET (DialogData, apm_value_label), "apm-value-label"},
  {G_STRUCT_OFFSET (DialogData, apm_scale), "apm-scale"},
  {G_STRUCT_OFFSET (DialogData, apm_adjustment), "apm-adjustment"},

  /* AAM */
  {G_STRUCT_OFFSET (DialogData, aam_box), "aam-box"},
  {G_STRUCT_OFFSET (DialogData, aam_switch), "aam-switch"},
  {G_STRUCT_OFFSET (DialogData, aam_widgets_box), "aam-widgets-box"},
  {G_STRUCT_OFFSET (DialogData, aam_value_label), "aam-value-label"},
  {G_STRUCT_OFFSET (DialogData, aam_vendor_recommended_value_label), "aam-vendor-recommended-value-label"},
  {G_STRUCT_OFFSET (DialogData, aam_scale), "aam-scale"},
  {G_STRUCT_OFFSET (DialogData, aam_adjustment), "aam-adjustment"},

  /* Write Cache */
  {G_STRUCT_OFFSET (DialogData, write_cache_box), "write-cache-box"},
  {G_STRUCT_OFFSET (DialogData, write_cache_switch), "write-cache-switch"},
  {G_STRUCT_OFFSET (DialogData, write_cache_widgets_box), "write-cache-widgets-box"},
  {G_STRUCT_OFFSET (DialogData, write_cache_comboboxtext), "write-cache-comboboxtext"},

  {G_STRUCT_OFFSET (DialogData, ok_button), "ok-button"},
  {G_STRUCT_OFFSET (DialogData, window_title), "window_title"},

  {0, NULL}
};

static void update_dialog (DialogData *data);
static void update_standby_label (DialogData *data);
static void update_apm_label (DialogData *data);
static void update_aam_label (DialogData *data);

static void disable_unused_widgets (DialogData *data);

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
          adw_dialog_close (ADW_DIALOG (data->dialog));
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
  adw_dialog_close (ADW_DIALOG (data->dialog));
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
              g_strcmp0 (key, "ata-aam-level") == 0 ||
              g_strcmp0 (key, "ata-write-cache-enabled") == 0)
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

  /* Standby */
  if (gtk_switch_get_active (GTK_SWITCH (data->standby_switch)))
    {
      if (udisks_drive_ata_get_pm_supported (data->ata))
        {
          gint value = (gint) gtk_adjustment_get_value (data->standby_adjustment);
          g_variant_builder_add (&builder, "{sv}", "ata-pm-standby", g_variant_new_int32 (value));
        }
    }

  /* APM */
  if (gtk_switch_get_active (GTK_SWITCH (data->apm_switch)))
    {
      if (udisks_drive_ata_get_pm_supported (data->ata))
        {
          gint value = (gint) gtk_adjustment_get_value (data->apm_adjustment);
          g_variant_builder_add (&builder, "{sv}", "ata-apm-level", g_variant_new_int32 (value));
        }
    }

  /* AAM */
  if (gtk_switch_get_active (GTK_SWITCH (data->aam_switch)))
    {
      if (udisks_drive_ata_get_pm_supported (data->ata))
        {
          gint value = (gint) gtk_adjustment_get_value (data->aam_adjustment);
          if (value < 128)
            value = 0;
          g_variant_builder_add (&builder, "{sv}", "ata-aam-level", g_variant_new_int32 (value));
        }
    }

  /* AAM */
  if (gtk_switch_get_active (GTK_SWITCH (data->write_cache_switch)))
    {
      if (udisks_drive_ata_get_write_cache_supported (data->ata))
        {
          gboolean enabled = FALSE;
          if (gtk_combo_box_get_active (GTK_COMBO_BOX (data->write_cache_comboboxtext)) == 0)
            enabled = TRUE;
          g_variant_builder_add (&builder, "{sv}", "ata-write-cache-enabled", g_variant_new_boolean (enabled));
        }
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
  gtk_widget_set_sensitive (data->ok_button, changed);

  /* update labels */
  update_standby_label (data);
  update_apm_label (data);
  update_aam_label (data);
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

static void
update_standby_label (DialogData *data)
{
  gint64 value;
  gchar *s = NULL;

  value = gtk_adjustment_get_value (data->standby_adjustment);
  if (value == 0)
    {
      s = g_strdup (C_("standby-value", "Never"));
    }
  else if (value < 241)
    {
      s = gdu_utils_format_duration_usec (value * 5 * G_USEC_PER_SEC,
                                          GDU_FORMAT_DURATION_FLAGS_NONE);
    }
  else if (value < 252)
    {
      s = gdu_utils_format_duration_usec ((value - 240) * 30 * 60 * G_USEC_PER_SEC,
                                          GDU_FORMAT_DURATION_FLAGS_NONE);
    }
  else if (value == 252)
    {
      s = gdu_utils_format_duration_usec (21 * 60 * G_USEC_PER_SEC,
                                          GDU_FORMAT_DURATION_FLAGS_NONE);
    }
  else if (value == 253)
    {
      s = g_strdup (C_("standby-value", "Vendor-defined"));
    }
  else if (value == 254)
    {
      s = g_strdup (C_("standby-value", "Reserved"));
    }
  else if (value == 255)
    {
      s = gdu_utils_format_duration_usec ((21 * 60 + 15) * G_USEC_PER_SEC,
                                          GDU_FORMAT_DURATION_FLAGS_NONE);
    }
  gtk_label_set_text (GTK_LABEL (data->standby_value_label), s);
  g_free (s);
}


static void
update_apm_label (DialogData *data)
{
  gint value;
  gchar *s = NULL;

  value = gtk_adjustment_get_value (data->apm_adjustment);
  if (value == 255)
    {
      s = g_strdup (C_("apm-level", "255 (Disabled)"));
    }
  else if (value <= 127)
    {
      s = g_strdup_printf (C_("apm-level", "%d (Spin-down permitted)"), value);
    }
  else
    {
      s = g_strdup_printf (C_("apm-level", "%d (Spin-down not permitted)"), value);
    }
  gtk_label_set_text (GTK_LABEL (data->apm_value_label), s);
  g_free (s);
}

static void
update_aam_label (DialogData *data)
{
  gint value;
  gchar *s = NULL;

  value = gtk_adjustment_get_value (data->aam_adjustment);
  if (value == 127)
    {
      s = g_strdup (C_("aam-level", "0 (Disabled)"));
    }
  else
    {
      s = g_strdup_printf ("%d", value);
    }
  gtk_label_set_text (GTK_LABEL (data->aam_value_label), s);
  g_free (s);

  s = g_strdup_printf ("%d", udisks_drive_ata_get_aam_vendor_recommended_value (data->ata));
  gtk_label_set_text (GTK_LABEL (data->aam_vendor_recommended_value_label), s);
  g_free (s);
}

static void
gdu_disk_settings_dialog_set_title (DialogData *data)
{
  g_autoptr(UDisksObjectInfo) info = NULL;

  info = udisks_client_get_object_info (data->client, data->object);
  adw_window_title_set_subtitle (ADW_WINDOW_TITLE (data->window_title), udisks_object_info_get_one_liner (info));
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
      gdu_utils_show_error (data->window,
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

static void
on_ok_clicked (AdwAlertDialog *dialog,
               gpointer user_data)
{
  DialogData *data = user_data;

  udisks_drive_call_set_configuration (data->drive,
                                       compute_configuration (data),  /* consumes floating */
                                       g_variant_new ("a{sv}", NULL), /* options */
                                       NULL, /* cancellable */
                                       on_set_configuration_cb,
                                       dialog_data_ref (data));
}

static void
on_dialog_closed (AdwDialog *dialog,
                  gpointer user_data)
{
  DialogData *data = user_data;
  dialog_data_unref (data);
}

void
gdu_disk_settings_dialog_show (GtkWindow    *window,
                               UDisksObject *object,
                               UDisksClient *client)
{
  DialogData *data;
  guint n;
  Mark standby_marks[5] = {
    /* Translators: This is a mark on the Standby scale. The string should be as short as possible */
    { 0.0, N_("Never")},
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
    {  1.0, N_("Save Power")},
    /* Translators: This is a mark on the APM scale. The string should be as short as possible. The left arrow ("←") is to signify that the left part of the scale offers spindown. In RTL locales, please use a right arrow ("→") instead. */
    {127.0, N_("← Spindown")},
    /* Translators: This is a mark on the APM scale. The string should be as short as possible */
    {254.0, N_("Perform Better")},
  };
  Mark aam_marks[2] = {
    /* Translators: This is a mark on the AAM scale. The string should be as short as possible */
    {128.0, N_("Quiet (Slow)")},
    /* Translators: This is a mark on the AAM scale. The string should be as short as possible */
    {254.0, N_("Loud (Fast)")}
  };

  data = g_new0 (DialogData, 1);
  data->ref_count = 1;
  data->object = g_object_ref (object);
  data->client = client;
  data->drive = udisks_object_peek_drive (data->object);
  data->ata = udisks_object_peek_drive_ata (data->object);
  data->window = g_object_ref (window);
  data->orig_drive_configuration = udisks_drive_dup_configuration (data->drive);

  data->dialog = GTK_WIDGET (gdu_application_new_widget ((gpointer)g_application_get_default (),
                                                         "gdu-disk-settings-dialog.ui",
                                                         "disk-settings-dialog",
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

#if 0
  g_signal_connect (data->standby_scale, "format-value", G_CALLBACK (on_standby_scale_format_value), data);
  g_signal_connect (data->apm_scale, "format-value", G_CALLBACK (on_apm_scale_format_value), data);
  g_signal_connect (data->aam_scale, "format-value", G_CALLBACK (on_aam_scale_format_value), data);
#endif

  disable_unused_widgets (data);

  /* initialize dialog with values from current configuration */
  if (data->orig_drive_configuration != NULL)
    {
      gint standby_value = -1;
      gint apm_value = -1;
      gint aam_value = -1;
      gboolean write_cache_enabled = FALSE;
      gboolean write_cache_enabled_set = FALSE;

      /* Power Management page */
      g_variant_lookup (data->orig_drive_configuration, "ata-pm-standby", "i", &standby_value);
      g_variant_lookup (data->orig_drive_configuration, "ata-apm-level", "i", &apm_value);
      g_variant_lookup (data->orig_drive_configuration, "ata-aam-level", "i", &aam_value);
      if (g_variant_lookup (data->orig_drive_configuration, "ata-write-cache-enabled", "b", &write_cache_enabled))
        write_cache_enabled_set = TRUE;

      /* Standby (default to 10 minutes -> 120) */
      if (standby_value == -1)
        {
          gtk_switch_set_active (GTK_SWITCH (data->standby_switch), FALSE);
          gtk_adjustment_set_value (data->standby_adjustment, 120);
        }
      else
        {
          gtk_adjustment_set_value (data->standby_adjustment, standby_value);
          gtk_switch_set_active (GTK_SWITCH (data->standby_switch), TRUE);
        }

      /* APM (default to 127) */
      if (apm_value == -1)
        {
          gtk_switch_set_active (GTK_SWITCH (data->apm_switch), FALSE);
          gtk_adjustment_set_value (data->apm_adjustment, 127);
        }
      else
        {
          gtk_adjustment_set_value (data->apm_adjustment, apm_value);
          gtk_switch_set_active (GTK_SWITCH (data->apm_switch), TRUE);
        }

      /* AAM (default to vendor recommended value, if available, otherwise default to 254) */
      if (aam_value == -1)
        {
          gint default_value = udisks_drive_ata_get_aam_vendor_recommended_value (data->ata);
          if (default_value < 128 || default_value > 254)
            default_value = 254;
          gtk_switch_set_active (GTK_SWITCH (data->aam_switch), FALSE);
          gtk_adjustment_set_value (data->aam_adjustment, default_value);
        }
      else
        {
          if (aam_value < 128)
            aam_value = 127;
          gtk_adjustment_set_value (data->aam_adjustment, aam_value);
          gtk_switch_set_active (GTK_SWITCH (data->aam_switch), TRUE);
        }

      /* Write Cache (default to "Enabled") */
      if (!write_cache_enabled_set)
        {
          gtk_combo_box_set_active (GTK_COMBO_BOX (data->write_cache_comboboxtext), 0);
          gtk_switch_set_active (GTK_SWITCH (data->write_cache_switch), FALSE);
        }
      else
        {
          gtk_combo_box_set_active (GTK_COMBO_BOX (data->write_cache_comboboxtext),
                                    write_cache_enabled ? 0 : 1);
          gtk_switch_set_active (GTK_SWITCH (data->write_cache_switch), TRUE);
        }
    }

  g_signal_connect (data->standby_switch,
                    "notify::active", G_CALLBACK (on_property_changed), data);
  g_signal_connect (data->apm_switch,
                    "notify::active", G_CALLBACK (on_property_changed), data);
  g_signal_connect (data->aam_switch,
                    "notify::active", G_CALLBACK (on_property_changed), data);
  g_signal_connect (data->write_cache_switch,
                    "notify::active", G_CALLBACK (on_property_changed), data);

  g_signal_connect (data->standby_adjustment,
                    "notify::value", G_CALLBACK (on_property_changed), data);
  g_signal_connect (data->apm_adjustment,
                    "notify::value", G_CALLBACK (on_property_changed), data);
  g_signal_connect (data->aam_adjustment,
                    "notify::value", G_CALLBACK (on_property_changed), data);
  g_signal_connect (data->write_cache_comboboxtext,
                    "notify::active", G_CALLBACK (on_property_changed), data);

  g_object_bind_property (data->standby_switch,
                          "active",
                          data->standby_widgets_box,
                          "sensitive",
                          G_BINDING_SYNC_CREATE);

  g_object_bind_property (data->apm_switch,
                          "active",
                          data->apm_widgets_box,
                          "sensitive",
                          G_BINDING_SYNC_CREATE);

  g_object_bind_property (data->aam_switch,
                          "active",
                          data->aam_widgets_box,
                          "sensitive",
                          G_BINDING_SYNC_CREATE);

  g_object_bind_property (data->write_cache_switch,
                          "active",
                          data->write_cache_widgets_box,
                          "sensitive",
                          G_BINDING_SYNC_CREATE);

  update_dialog (data);
  gdu_disk_settings_dialog_set_title (data);


  g_signal_connect (data->dialog,
                    "closed",
                    G_CALLBACK (on_dialog_closed),
                    data);
  g_signal_connect (data->ok_button,
                    "clicked",
                    G_CALLBACK (on_ok_clicked),
                    data);

  adw_dialog_present (ADW_DIALOG (data->dialog), GTK_WIDGET (window));
}

/* ---------------------------------------------------------------------------------------------------- */

static void
hide_forever (GtkWidget *widget)
{
  gtk_widget_set_visible (widget, FALSE);
}

static void
disable_unused_widgets (DialogData *data)
{
  gboolean is_ssd = FALSE;

  /* Disable widgets not relevant for a drive - see also gdu_disk_settings_dialog_should_show() */

  if (udisks_drive_get_rotation_rate (data->drive) == 0)
    is_ssd = TRUE;

  if (!udisks_drive_ata_get_pm_supported (data->ata) || is_ssd)
    hide_forever (data->standby_box);

  if (!udisks_drive_ata_get_apm_supported (data->ata))
    hide_forever (data->apm_box);

  if (!udisks_drive_ata_get_aam_supported (data->ata))
    hide_forever (data->aam_box);

  if (!udisks_drive_ata_get_write_cache_supported (data->ata))
    hide_forever (data->write_cache_box);
}

gboolean
gdu_disk_settings_dialog_should_show (UDisksObject *object)
{
  gboolean ret = FALSE;
  UDisksDrive *drive;
  UDisksDriveAta *ata;
  gboolean is_ssd = FALSE;

  g_return_val_if_fail (UDISKS_IS_OBJECT (object), FALSE);

  /* see also disabled_unused_widgets() above */

  drive = udisks_object_peek_drive (object);
  if (drive == NULL)
    goto out;

  ata = udisks_object_peek_drive_ata (object);
  if (ata == NULL)
    goto out;

  if (udisks_drive_get_rotation_rate (drive) == 0)
    is_ssd = TRUE;

  if ((udisks_drive_ata_get_pm_supported (ata) && !is_ssd) ||
      udisks_drive_ata_get_apm_supported (ata) ||
      udisks_drive_ata_get_aam_supported (ata) ||
      udisks_drive_ata_get_write_cache_supported (ata))
    {
      ret = TRUE;
    }

 out:
  return ret;
}

