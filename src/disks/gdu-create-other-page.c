/*
 * Copyright (C) 2017 Kai Lüke
 *
 * Licensed under GPL version 2 or later.
 *
 * Author: Kai Lüke <kailueke@riseup.net>
 */

#include "config.h"

#include <glib/gi18n.h>

#include "gdu-create-other-page.h"

enum
{
  PROP_0,
  PROP_COMPLETE,
  PROP_FS_TYPE,
  N_PROPS
};

static GParamSpec *properties[N_PROPS];

struct _GduCreateOtherPage
{
  AdwBin           parent_instance;

  GtkWidget       *fs_types_group;
  GtkWidget       *encrypt_switch;

  UDisksClient    *client;
  GduOtherFsType   fs_type;
};

G_DEFINE_TYPE (GduCreateOtherPage, gdu_create_other_page, ADW_TYPE_BIN);

G_DEFINE_ENUM_TYPE (GduOtherFsType, gdu_other_fs_type,
                    G_DEFINE_ENUM_VALUE (GDU_OTHER_FS_TYPE_XFS,   "xfs"),
                    G_DEFINE_ENUM_VALUE (GDU_OTHER_FS_TYPE_SWAP,  "swap"),
                    G_DEFINE_ENUM_VALUE (GDU_OTHER_FS_TYPE_BTRFS, "btrfs"),
                    G_DEFINE_ENUM_VALUE (GDU_OTHER_FS_TYPE_F2FS,  "f2fs"),
                    G_DEFINE_ENUM_VALUE (GDU_OTHER_FS_TYPE_EXFAT, "exfat"),
                    G_DEFINE_ENUM_VALUE (GDU_OTHER_FS_TYPE_UDF,   "udf"),
                    G_DEFINE_ENUM_VALUE (GDU_OTHER_FS_TYPE_EMPTY, "empty"));

static void
can_format_cb (UDisksManager *manager,
               GAsyncResult  *res,
               gpointer       user_data)
{
  GtkWidget *row = user_data;
  g_autoptr(GVariant) out_available = NULL;
  g_autofree char *util = NULL;
  gboolean available = FALSE;

  if (udisks_manager_call_can_format_finish (manager, &out_available, res, NULL))
    {
      g_variant_get (out_available, "(bs)", &available, &util);
    }

  if (!available)
    {
      g_autofree char *tooltip = NULL;

      gtk_widget_set_sensitive (row, FALSE);
      tooltip = g_strdup_printf (_("The utility %s is missing."), util);
      gtk_widget_set_tooltip_text (row, tooltip);
    }
}

const gchar *
gdu_create_other_page_get_fs (GduCreateOtherPage *self)
{
  GEnumClass *eclass;
  GEnumValue *value;

  eclass = G_ENUM_CLASS (g_type_class_peek (GDU_TYPE_OTHER_FS_TYPE));
  value = g_enum_get_value (eclass, self->fs_type);

  g_assert (value);

  return value->value_nick;
}

gboolean
gdu_create_other_page_is_encrypted (GduCreateOtherPage *self)
{
  return adw_switch_row_get_active (ADW_SWITCH_ROW (self->encrypt_switch));
}

static void
on_fs_type_changed (GduCreateOtherPage *self)
{
  g_object_notify (G_OBJECT (self), "complete");
}

static void
gdu_create_other_page_set_property (GObject      *object,
                                    guint         property_id,
                                    const GValue *value,
                                    GParamSpec   *pspec)
{
  GduCreateOtherPage *self = GDU_CREATE_OTHER_PAGE (object);

  switch (property_id)
    {
    case PROP_FS_TYPE:
      self->fs_type = g_value_get_enum (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    }
}

static void
gdu_create_other_page_get_property (GObject    *object,
                                    guint       property_id,
                                    GValue     *value,
                                    GParamSpec *pspec)
{
  GduCreateOtherPage *self = GDU_CREATE_OTHER_PAGE (object);

  switch (property_id)
    {
    case PROP_COMPLETE:
      g_value_set_boolean (value, TRUE);
      break;
    case PROP_FS_TYPE:
      g_value_set_enum (value, self->fs_type);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
    }
}

static void
gdu_create_other_page_class_init (GduCreateOtherPageClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->get_property = gdu_create_other_page_get_property;
  object_class->set_property = gdu_create_other_page_set_property;

  gtk_widget_class_set_template_from_resource (widget_class,
                                               "/org/gnome/DiskUtility/ui/"
                                               "gdu-create-other-page.ui");
  gtk_widget_class_bind_template_child (widget_class, GduCreateOtherPage, encrypt_switch);
  gtk_widget_class_bind_template_child (widget_class, GduCreateOtherPage, fs_types_group);

  gtk_widget_class_bind_template_callback (widget_class, on_fs_type_changed);
  properties[PROP_COMPLETE] =
    g_param_spec_boolean ("complete",
                          NULL, NULL, FALSE,
                          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  properties[PROP_FS_TYPE] =
    g_param_spec_enum ("fs-type",
                        NULL,
                        NULL,
                        GDU_TYPE_OTHER_FS_TYPE,
                        GDU_OTHER_FS_TYPE_EMPTY,
                        G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, N_PROPS, properties);
  gtk_widget_class_install_property_action (widget_class, "update_fs_type", "fs-type");
}

static void
gdu_create_other_page_init (GduCreateOtherPage *self)
{
  gtk_widget_init_template (GTK_WIDGET (self));
}

GduCreateOtherPage *
gdu_create_other_page_new (UDisksClient *client)
{
  GduCreateOtherPage *self;
  const char *other_fs_title_desc[N_OTHER_FS][2] = {
    { N_ ("XFS"), N_ ("Linux Filesystem") },
    { N_ ("Linux Swap Partition"), NULL },
    { N_ ("Btrfs"), N_ ("Copy-on-write Linux Filesystem, for snapshots") },
    { N_ ("F2FS"), N_ ("Flash Storage Linux Filesystem") },
    { N_ ("exFAT"), N_ ("Flash Storage Windows Filesystem, used on SDXC cards") },
    { N_ ("UDF"), N_ ("Universal Disk Format, for removable devices on many systems") },
    { N_ ("No Filesystem"), NULL }
  };

  self = g_object_new (GDU_TYPE_CREATE_OTHER_PAGE, NULL);
  self->client = client;

  for (guint i = 0; i < N_OTHER_FS; i++)
    {
      GtkWidget *row;
      GtkWidget *checkbox;
      GEnumValue *value;
      GEnumClass *eclass;

      eclass = G_ENUM_CLASS (g_type_class_peek (GDU_TYPE_OTHER_FS_TYPE));
      value = g_enum_get_value (eclass, (GduOtherFsType)i);

      row = adw_action_row_new ();
      checkbox = gtk_check_button_new ();

      gtk_actionable_set_action_name (GTK_ACTIONABLE (checkbox),
                                      "update_fs_type");
      gtk_actionable_set_action_target_value (GTK_ACTIONABLE (checkbox),
                                              g_variant_new_string (value->value_nick));
      adw_preferences_row_set_title (ADW_PREFERENCES_ROW (row), other_fs_title_desc[i][0]);
      if (other_fs_title_desc[i][1] != NULL)
        {
          adw_action_row_set_subtitle (ADW_ACTION_ROW (row), other_fs_title_desc[i][1]);
        }
      adw_action_row_add_prefix (ADW_ACTION_ROW (row), checkbox);
      adw_action_row_set_activatable_widget (ADW_ACTION_ROW (row), checkbox);

      adw_preferences_group_add (ADW_PREFERENCES_GROUP (self->fs_types_group), row);

      udisks_manager_call_can_format (udisks_client_get_manager (self->client),
                                      value->value_nick,
                                      NULL,
                                      (GAsyncReadyCallback)can_format_cb,
                                      row);
    }

  return self;
}
