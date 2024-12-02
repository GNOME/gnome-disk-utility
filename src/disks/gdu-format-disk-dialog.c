/*
 * Copyright (C) 2008-2013 Red Hat, Inc.
 * Copyright (C) 2023 Purism SPC
 *
 * Licensed under GPL version 2 or later.
 *
 * Author(s):
 *   David Zeuthen <zeuthen@gmail.com>
 *   Mohammed Sadiq <sadiq@sadiqpk.org>
 */

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <glib/gi18n.h>

#include "gdu-application.h"
#include "gdu-format-disk-dialog.h"

/* ---------------------------------------------------------------------------------------------------- */

enum
{
  PROP_0,
  PROP_PARTITIONING_TYPE,
  N_PROPS
};

static GParamSpec *properties [N_PROPS];

struct _GduFormatDiskDialog
{
  AdwDialog              parent_instance;

  GtkWidget             *erase_switch;
  GtkWidget             *window_title;

  GtkWindow             *parent_window;
  UDisksClient          *udisks_client;
  UDisksObject          *udisks_object;
  UDisksBlock           *udisks_block;
  UDisksDrive           *udisks_drive;
  UDisksDriveAta        *udisks_drive_ata;
  GduPartitioningType    partitioning_type;
};

G_DEFINE_TYPE (GduFormatDiskDialog, gdu_format_disk_dialog, ADW_TYPE_DIALOG)

G_DEFINE_ENUM_TYPE (GduPartitioningType, gdu_partitioning_type,
                    G_DEFINE_ENUM_VALUE (GDU_PARTITIONING_TYPE_GPT, "gpt"),
                    G_DEFINE_ENUM_VALUE (GDU_PARTITIONING_TYPE_DOS, "dos"),
                    G_DEFINE_ENUM_VALUE (GDU_PARTITIONING_TYPE_EMPTY, "empty"));

static void
gdu_format_disk_dialog_set_title (GduFormatDiskDialog *self)
{
  g_autoptr(UDisksObjectInfo) info = NULL;

  info = udisks_client_get_object_info (self->udisks_client, self->udisks_object);
  adw_window_title_set_subtitle (ADW_WINDOW_TITLE (self->window_title), udisks_object_info_get_one_liner (info));
}

static gpointer
gdu_format_disk_dialog_get_window (GduFormatDiskDialog *self)
{
  return gtk_widget_get_ancestor (GTK_WIDGET (self), GTK_TYPE_WINDOW);
}

static const gchar *
gdu_format_disk_dialog_get_partitioning_type (GduFormatDiskDialog *self)
{
	GEnumClass *eclass;
  GEnumValue *value;

	eclass = G_ENUM_CLASS (g_type_class_peek (GDU_TYPE_PARTITIONING_TYPE));
	value = g_enum_get_value (eclass, self->partitioning_type);

	g_assert (value);

	return value->value_nick;
}

static void
format_cb (GObject      *source_object,
           GAsyncResult *res,
           gpointer      user_data)
{
  GduFormatDiskDialog *self = user_data;
  g_autoptr(GError) error = NULL;

  if (!udisks_block_call_format_finish (self->udisks_block, res, &error))
    {
      gdu_utils_show_error (gdu_format_disk_dialog_get_window (self),
                            _("Error formatting disk"),
                            error);
    }

  adw_dialog_close (ADW_DIALOG (self));
}

static void
ensure_unused_cb (GtkWindow    *parent_window,
                  GAsyncResult *res,
                  gpointer      user_data)
{
  GduFormatDiskDialog *self = user_data;
  const char *erase_type;
  GVariantBuilder options_builder;

  if (!gdu_utils_ensure_unused_finish (self->udisks_client, res, NULL))
    {
      adw_dialog_close (ADW_DIALOG (self));
      return;
    }

  /* gtk4 todo : Waiting for design update
  erase_type = gtk_combo_box_get_active_id (self->erase_combobox);
  */
  erase_type = "";

  g_variant_builder_init (&options_builder, G_VARIANT_TYPE_VARDICT);
  if (erase_type && *erase_type)
    g_variant_builder_add (&options_builder, "{sv}", "erase", g_variant_new_string (erase_type));
  udisks_block_call_format (self->udisks_block,
                            gdu_format_disk_dialog_get_partitioning_type (self),
                            g_variant_builder_end (&options_builder),
                            NULL, /* GCancellable */
                            format_cb,
                            self);

}

static void
on_confirmation_response_cb (GObject                     *object,
                             GAsyncResult                *response,
                             gpointer                     user_data)
{
  GduFormatDiskDialog *self = GDU_FORMAT_DISK_DIALOG (user_data);
  AdwAlertDialog *dialog = ADW_ALERT_DIALOG (object);

  if (g_strcmp0 (adw_alert_dialog_choose_finish (dialog, response), "cancel") == 0)
    return;
  /* ensure the volume is unused (e.g. unmounted) before formatting it... */
  gdu_utils_ensure_unused(self->udisks_client,
                          gdu_format_disk_dialog_get_window (self),
                          self->udisks_object,
                          (GAsyncReadyCallback) ensure_unused_cb,
                          NULL, /* GCancellable */
                          user_data);
}

static void
on_format_clicked_cb (GduFormatDiskDialog *self,
                      GtkButton           *button)
{
  gboolean erase_data;
  g_autoptr(GList) objects = NULL;
  g_autoptr(GString) str = NULL;
  ConfirmationDialogData *data;
  GtkWidget *affected_devices_widget;

  g_assert (GDU_IS_FORMAT_DISK_DIALOG (self));

  erase_data = gtk_switch_get_active (GTK_SWITCH (self->erase_switch));
  objects = g_list_append (NULL, self->udisks_object);

  affected_devices_widget = gdu_util_create_widget_from_objects (self->udisks_client,
                                                                 objects);

  if (erase_data)
    {
      /* Translators: warning used for quick format */
      str = g_string_new (_("All data on the disk will be lost but may still be recoverable by "
                            "data recovery services"));
      g_string_append (str, "\n\n");
      g_string_append (str, _("<b>Tip</b>: If you are planning to recycle, sell or give away your "
                              "old computer or disk, you should use a more thorough erase type to "
                              "keep your private information from falling into the wrong hands"));
    }
  else
    {
      /* Translators: warning used when overwriting data */
      str = g_string_new (_("All data on the disk will be overwritten and will likely not be "
                            "recoverable by data recovery services"));
    }

  /* gtk4 todo
  if (self->udisks_drive_ata &&
      (g_strcmp0 (erase_type, "ata-secure-erase") == 0 ||
       g_strcmp0 (erase_type, "ata-secure-erase-enhanced") == 0))
    {
      g_string_append (str, "\n\n");
      g_string_append (str, _("<b>WARNING</b>: The Secure Erase command may take a very long time "
                              "to complete, can’t be cancelled and may not work properly with some "
                              "hardware. In the worst case, your drive may be rendered unusable or "
                               "your system may crash or lock up. Before proceeding, please read the "
                               "article about <a href='https://ata.wiki.kernel.org/index.php/ATA_Secure_Erase'>ATA Secure Erase</a> "
                               "and make sure you understand the risks"));
    }
  */

  data = g_new0 (ConfirmationDialogData, 1);
  data->message = _("Format Disk?");
  data->description = str->str;
  data->response_verb = _("_Format");
  data->response_appearance = ADW_RESPONSE_DESTRUCTIVE;
  data->callback = on_confirmation_response_cb;
  data->user_data = self;

  gdu_utils_show_confirmation (gdu_format_disk_dialog_get_window (self),
                               data,
                               affected_devices_widget);
}

enum
{
  MODEL_COLUMN_ID,
  MODEL_COLUMN_MARKUP,
  MODEL_COLUMN_SEPARATOR,
  MODEL_COLUMN_SENSITIVE,
  MODEL_N_COLUMNS,
};

/* ---------------------------------------------------------------------------------------------------- */

static gchar *
get_erase_duration_string (gint minutes)
{
  char *s;

  if (minutes == 510)
    {
      g_autofree char *s2 = gdu_utils_format_duration_usec ((minutes - 2) * 60LL * G_USEC_PER_SEC,
                                                            GDU_FORMAT_DURATION_FLAGS_NONE);
      /* Translators: Used to convey that something takes at least
       * some specificed duration but may take longer. The %s is a
       * time duration e.g. "8 hours and 28 minutes"
       */
      s = g_strdup_printf (_("At least %s"), s2);
    }
  else
    {
      g_autofree char *s2 = gdu_utils_format_duration_usec (minutes * 60LL * G_USEC_PER_SEC,
                                                  GDU_FORMAT_DURATION_FLAGS_NONE);
      /* Translators: Used to convey that something takes
       * approximately some specificed duration. The %s is a time
       * duration e.g. "2 hours and 2 minutes"
       */
      s = g_strdup_printf (_("Approximately %s"), s2);
    }

  return s;
}

/* gtk4 todo */
// static void
// populate_erase_combobox (GduFormatDiskDialog *self)
// {

//   if (self->udisks_drive_ata != NULL)
//     {
//       gint erase_minutes, enhanced_erase_minutes;
//       gboolean frozen;

//       erase_minutes = udisks_drive_ata_get_security_erase_unit_minutes (self->udisks_drive_ata);
//       enhanced_erase_minutes = udisks_drive_ata_get_security_enhanced_erase_unit_minutes (self->udisks_drive_ata);
//       frozen = udisks_drive_ata_get_security_frozen (self->udisks_drive_ata);

//       if (erase_minutes > 0 || enhanced_erase_minutes > 0)
//         {
//           /* separator */
//           gtk_list_store_insert_with_values (model, NULL /* out_iter */, G_MAXINT, /* position */
//                                              MODEL_COLUMN_SEPARATOR, TRUE,
//                                              MODEL_COLUMN_SENSITIVE, TRUE,
//                                              -1);

//           /* if both normal and enhanced erase methods are available, only show the enhanced one */
//           if (erase_minutes > 0 && enhanced_erase_minutes == 0)
//             {
//               s2 = get_erase_duration_string (erase_minutes);
//               s = g_strdup_printf ("%s <span size=\"small\">(%s)</span>",
//                                    _("ATA Secure Erase"),
//                                    s2);
//               gtk_list_store_insert_with_values (model, NULL /* out_iter */, G_MAXINT, /* position */
//                                                  MODEL_COLUMN_ID, "ata-secure-erase",
//                                                  MODEL_COLUMN_MARKUP, s,
//                                                  MODEL_COLUMN_SENSITIVE, !frozen,
//                                                  -1);
//               g_free (s);
//               g_free (s2);
//             }

//           if (enhanced_erase_minutes > 0)
//             {
//               s2 = get_erase_duration_string (enhanced_erase_minutes);
//               s = g_strdup_printf ("%s <span size=\"small\">(%s)</span>",
//                                    _("ATA Enhanced Secure Erase"),
//                                    s2);
//               gtk_list_store_insert_with_values (model, NULL /* out_iter */, G_MAXINT, /* position */
//                                                  MODEL_COLUMN_ID, "ata-secure-erase-enhanced",
//                                                  MODEL_COLUMN_MARKUP, s,
//                                                  MODEL_COLUMN_SENSITIVE, !frozen,
//                                                  -1);
//               g_free (s);
//               g_free (s2);
//             }
//         }
//     }
// }

static void
gdu_format_disk_dialog_get_property (GObject    *object,
                                     guint       property_id,
                                     GValue     *value,
                                     GParamSpec *pspec)
{
  GduFormatDiskDialog *self = GDU_FORMAT_DISK_DIALOG (object);

  switch (property_id)
    {
    case PROP_PARTITIONING_TYPE:
      g_value_set_enum (value, self->partitioning_type);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
    }
}

static void
gdu_format_disk_dialog_set_property (GObject      *object,
                                     guint         property_id,
                                     const GValue *value,
                                     GParamSpec   *pspec)
{
  GduFormatDiskDialog *self = GDU_FORMAT_DISK_DIALOG (object);

  switch (property_id)
    {
    case PROP_PARTITIONING_TYPE:
      self->partitioning_type = g_value_get_enum (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    }
}

static void
gdu_format_disk_dialog_finalize (GObject *object)
{
  GduFormatDiskDialog *self = (GduFormatDiskDialog *)object;

  g_clear_object (&self->udisks_object);
  g_clear_object (&self->udisks_block);
  g_clear_object (&self->udisks_drive);
  g_clear_object (&self->udisks_drive_ata);

  G_OBJECT_CLASS (gdu_format_disk_dialog_parent_class)->finalize (object);
}

static void
gdu_format_disk_dialog_class_init (GduFormatDiskDialogClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->finalize = gdu_format_disk_dialog_finalize;
  object_class->get_property = gdu_format_disk_dialog_get_property;
  object_class->set_property = gdu_format_disk_dialog_set_property;

  gtk_widget_class_set_template_from_resource (widget_class,
                                               "/org/gnome/DiskUtility/ui/"
                                               "gdu-format-disk-dialog.ui");

  gtk_widget_class_bind_template_child (widget_class, GduFormatDiskDialog, erase_switch);
  gtk_widget_class_bind_template_child (widget_class, GduFormatDiskDialog, window_title);

  gtk_widget_class_bind_template_callback (widget_class, on_format_clicked_cb);

  properties[PROP_PARTITIONING_TYPE] =
    g_param_spec_enum ("part-type",
                        NULL,
                        NULL,
                        GDU_TYPE_PARTITIONING_TYPE,
                        GDU_PARTITIONING_TYPE_GPT,
                        G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, N_PROPS, properties);
  gtk_widget_class_install_property_action (widget_class, "format.update_part_type", "part-type");
}

static void
gdu_format_disk_dialog_init (GduFormatDiskDialog *self)
{
  gtk_widget_init_template (GTK_WIDGET (self));
}

void
gdu_format_disk_dialog_show (GtkWindow    *parent,
                             UDisksObject *object,
                             UDisksClient *client)
{
  GduFormatDiskDialog *self;

  self = g_object_new (GDU_TYPE_FORMAT_DISK_DIALOG,
                       NULL);

  self->udisks_client = client;
  self->udisks_object = g_object_ref (object);
  self->udisks_block = udisks_object_get_block (object);
  self->udisks_drive = udisks_client_get_drive_for_block (client, self->udisks_block);

  gdu_format_disk_dialog_set_title (self);

  if (self->udisks_drive)
    {
      GDBusObject *drive_object;

      drive_object = g_dbus_interface_get_object (G_DBUS_INTERFACE (self->udisks_drive));
      if (drive_object)
        self->udisks_drive_ata = udisks_object_get_drive_ata (UDISKS_OBJECT (drive_object));
    }

  /* Default to MBR for removable drives < 2TB */
  if (self->udisks_drive != NULL
      && udisks_drive_get_removable (self->udisks_drive)
      && udisks_drive_get_size (self->udisks_drive) < (guint64) (2000000000000ULL))
    {
      self->partitioning_type = GDU_PARTITIONING_TYPE_DOS;
    }

  adw_dialog_present (ADW_DIALOG (self), GTK_WIDGET (parent));
}
