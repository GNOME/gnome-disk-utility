/*
 * Copyright (C) 2008-2013 Red Hat, Inc.
 *
 * Licensed under GPL version 2 or later.
 *
 * Author: David Zeuthen <zeuthen@gmail.com>
 */
/* NOTE: Keep this file in sync with gduutils.rs */
#include "config.h"
#include <glib/gi18n.h>
#include <math.h>
#include <sys/statvfs.h>
#include <adwaita.h>

#include "gduutils.h"

/* For __GNUC_PREREQ usage below */
#ifdef __GNUC__
# include <features.h>
#endif

#if defined(HAVE_LOGIND)
#include <systemd/sd-login.h>
#endif

gboolean
gdu_utils_has_configuration (UDisksBlock  *block,
                             const gchar  *type,
                             gboolean     *out_has_passphrase)
{
  GVariantIter iter;
  const gchar *config_type;
  GVariant *config_details;
  gboolean ret;
  gboolean has_passphrase;

  ret = FALSE;
  has_passphrase = FALSE;

  g_variant_iter_init (&iter, udisks_block_get_configuration (block));
  while (g_variant_iter_next (&iter, "(&s@a{sv})", &config_type, &config_details))
    {
      if (g_strcmp0 (config_type, type) == 0)
        {
          if (g_strcmp0 (type, "crypttab") == 0)
            {
              const gchar *passphrase_path;
              if (g_variant_lookup (config_details, "passphrase-path", "^&ay", &passphrase_path) &&
                  strlen (passphrase_path) > 0 &&
                  !g_str_has_prefix (passphrase_path, "/dev"))
                has_passphrase = TRUE;
            }
          ret = TRUE;
          g_variant_unref (config_details);
          goto out;
        }
      g_variant_unref (config_details);
    }

 out:
  if (out_has_passphrase != NULL)
    *out_has_passphrase = has_passphrase;
  return ret;
}

gboolean
gdu_utils_has_userspace_mount_option (UDisksBlock *block,
                                      const gchar *option)
{
  const gchar *const *options;
  gboolean ret;

  ret = FALSE;

  options = udisks_block_get_userspace_mount_options (block);
  if (options != NULL)
    ret = g_strv_contains (options, option);

  return ret;
}

void
gdu_utils_configure_file_dialog_for_disk_images (GtkFileDialog   *file_dialog,
                                                 gboolean         set_file_types,
                                                 gboolean         allow_compressed)
{
  GtkFileFilter *filter;
  GListStore *filters;
  g_autoptr(GFile) folder = NULL;

  folder = g_file_new_for_path (g_get_user_special_dir (G_USER_DIRECTORY_DOCUMENTS));
  gtk_file_dialog_set_initial_folder (file_dialog, folder);

  if (set_file_types)
    {
      filters = g_list_store_new (GTK_TYPE_FILE_FILTER);
      filter = gtk_file_filter_new ();
      gtk_file_filter_set_name (filter, _("All Files"));
      gtk_file_filter_add_pattern (filter, "*");
      g_list_store_append(filters, filter);
      filter = gtk_file_filter_new ();
      if (allow_compressed)
        gtk_file_filter_set_name (filter, _("Disk Images (*.img, *.img.xz, *.iso)"));
      else
        gtk_file_filter_set_name (filter, _("Disk Images (*.img, *.iso)"));
      gtk_file_filter_add_mime_type (filter, "application/x-raw-disk-image");
      if (allow_compressed)
        {
          gtk_file_filter_add_mime_type (filter, "application/x-raw-disk-image-xz-compressed");
        }
      gtk_file_filter_add_mime_type (filter, "application/x-cd-image");
      g_list_store_append(filters, filter);
      gtk_file_dialog_set_filters (file_dialog, G_LIST_MODEL (filters));

      gtk_file_dialog_set_default_filter (file_dialog, filter);
    }
}

/* should be called when user chooses file/dir from @file_chooser */
void
gdu_utils_file_chooser_for_disk_images_set_default_folder (GFile *folder)
{
  gchar *folder_uri;
  GSettings *settings;

  folder_uri = g_file_get_uri (folder);
  settings = g_settings_new ("org.gnome.Disks");
  g_settings_set_string (settings, "image-dir-uri", folder_uri);
  g_clear_object (&settings);
  g_free (folder_uri);
}

/* ---------------------------------------------------------------------------------------------------- */

gchar *
gdu_utils_unfuse_path (const gchar *path)
{
  gchar *ret;
  GFile *file;
  gchar *uri;
  const gchar *home;

  /* Map GVfs FUSE paths to GVfs URIs */
  file = g_file_new_for_path (path);
  uri = g_file_get_uri (file);
  if (g_str_has_prefix (uri, "file:"))
    {
      ret = g_strdup (path);
    }
  else
    {
      ret = g_uri_unescape_string (uri, NULL);
    }
  g_object_unref (file);
  g_free (uri);

  /* Replace $HOME with ~ */
  home = g_get_home_dir ();
  if (g_str_has_prefix (ret, home))
    {
      size_t home_len = strlen (home);
      if (home_len > 2)
        {
          if (home[home_len - 1] == '/')
            home_len--;
          if (ret[home_len] == '/')
            {
              gchar *tmp = ret;
              ret = g_strdup_printf ("~/%s", ret + home_len + 1);
              g_free (tmp);
            }
        }
    }

  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
has_option (GtkWidget       *options_entry,
            const gchar     *option,
            gboolean         check_prefix,
            gchar          **out_value)
{
  guint n;
  gboolean ret = FALSE;
  gchar **options;

  options = g_strsplit (gtk_editable_get_text (GTK_EDITABLE (options_entry)), ",", 0);
  for (n = 0; options != NULL && options[n] != NULL; n++)
    {
      if (check_prefix)
        {
          if (g_str_has_prefix (options[n], option))
            {
              if (out_value != NULL)
                *out_value = g_strdup (options[n] + strlen (option));
              ret = TRUE;
              goto out;
            }
        }
      else
        {
          if (g_strcmp0 (options[n], option) == 0)
            {
              ret = TRUE;
              goto out;
            }
        }
    }
 out:
  g_strfreev (options);
  return ret;
}

static void
add_option (GtkWidget       *options_entry,
            const gchar     *prefix,
            const gchar     *option,
            gboolean         add_to_front)
{
  gchar *s;
  const gchar *text;
  text = gtk_editable_get_text (GTK_EDITABLE (options_entry));
  s = g_strdup_printf ("%s%s%s%s",
                       add_to_front ? option : text,
                       strlen (text) > 0 ? "," : "",
                       prefix,
                       add_to_front ? text : option);
  gtk_editable_set_text (GTK_EDITABLE (options_entry), s);
  g_free (s);
}

static void
remove_option (GtkWidget       *options_entry,
               const gchar     *option,
               gboolean         check_prefix)
{
  GString *str;
  guint n;
  gchar **options;

  str = g_string_new (NULL);
  options = g_strsplit (gtk_editable_get_text (GTK_EDITABLE (options_entry)), ",", 0);
  for (n = 0; options != NULL && options[n] != NULL; n++)
    {
      if (check_prefix)
        {
          if (g_str_has_prefix (options[n], option))
            continue;
        }
      else
        {
          if (g_strcmp0 (options[n], option) == 0)
            continue;
        }
      if (str->len > 0)
        g_string_append_c (str, ',');
      g_string_append (str, options[n]);
    }
  gtk_editable_set_text (GTK_EDITABLE (options_entry), str->str);
  g_string_free (str, TRUE);
}

void
gdu_options_update_check_option (GtkWidget       *options_entry,
                                 const gchar     *option,
                                 GtkWidget       *widget,
                                 GtkWidget       *check_button,
                                 gboolean         negate,
                                 gboolean         add_to_front)
{
  gboolean opts, ui;
  opts = !! has_option (options_entry, option, FALSE, NULL);
  ui = adw_switch_row_get_active (ADW_SWITCH_ROW (check_button));
  if ((!negate && (opts != ui)) || (negate && (opts == ui)))
    {
      if (widget == check_button)
        {
          if ((!negate && ui) || (negate && !ui))
            add_option (options_entry, "", option, add_to_front);
          else
            remove_option (options_entry, option, FALSE);
        }
      else
        {
          if (negate)
            adw_switch_row_set_active (ADW_SWITCH_ROW (check_button), !opts);
          else
            adw_switch_row_set_active (ADW_SWITCH_ROW (check_button), opts);
        }
    }
}

void
gdu_options_update_entry_option (GtkWidget       *options_entry,
                                 const gchar     *option,
                                 GtkWidget       *widget,
                                 GtkWidget       *entry)
{
  gchar *opts = NULL;
  const gchar *ui;
  gchar *ui_escaped;
  has_option (options_entry, option, TRUE, &opts);
  if (opts == NULL)
    opts = g_strdup ("");
  ui = gtk_editable_get_text (GTK_EDITABLE (entry));
  ui_escaped = g_uri_escape_string (ui, NULL, TRUE);
  // g_print ("opts=`%s', ui=`%s', widget=%p, entry=%p\n", opts, ui, widget, entry);
  if (g_strcmp0 (opts, ui_escaped) != 0)
    {
      if (widget == entry)
        {
          if (strlen (ui_escaped) > 0)
            {
              remove_option (options_entry, option, TRUE);
              add_option (options_entry, option, ui_escaped, FALSE);
            }
          else
            {
              remove_option (options_entry, option, TRUE);
            }
        }
      else
        {
          gchar *opts_unescaped = g_uri_unescape_string (opts, NULL);
          gtk_editable_set_text (GTK_EDITABLE (entry), opts_unescaped);
          g_free (opts_unescaped);
        }
    }
  g_free (ui_escaped);
  g_free (opts);
}

#if defined(HAVE_LOGIND)

const gchar *
gdu_utils_get_seat (void)
{
  static gsize once = 0;
  static char *seat = NULL;

  if (g_once_init_enter (&once))
    {
      char *session = NULL;
      if (sd_pid_get_session (getpid (), &session) == 0)
        {
          sd_session_get_seat (session, &seat);
          /* we intentionally leak seat here... */
        }
      g_once_init_leave (&once, (gsize) 1);
    }
  return seat;
}

#else

const gchar *
gdu_utils_get_seat (void)
{
  return NULL;
}

#endif

static gchar *
years_to_string (gint value)
{
  /* Translators: Used for number of years */
  return g_strdup_printf (g_dngettext (GETTEXT_PACKAGE, "%d year", "%d years", value), value);
}

static gchar *
months_to_string (gint value)
{
  /* Translators: Used for number of months */
  return g_strdup_printf (g_dngettext (GETTEXT_PACKAGE, "%d month", "%d months", value), value);
}

static gchar *
days_to_string (gint value)
{
  /* Translators: Used for number of days */
  return g_strdup_printf (g_dngettext (GETTEXT_PACKAGE, "%d day", "%d days", value), value);
}

static gchar *
hours_to_string (gint value)
{
  /* Translators: Used for number of hours */
  return g_strdup_printf (g_dngettext (GETTEXT_PACKAGE, "%d hour", "%d hours", value), value);
}

static gchar *
minutes_to_string (gint value)
{
  /* Translators: Used for number of minutes */
  return g_strdup_printf (g_dngettext (GETTEXT_PACKAGE, "%d minute", "%d minutes", value), value);
}

static gchar *
seconds_to_string (gint value)
{
  /* Translators: Used for number of seconds */
  return g_strdup_printf (g_dngettext (GETTEXT_PACKAGE, "%d second", "%d seconds", value), value);
}

static gchar *
milliseconds_to_string (gint value)
{
  /* Translators: Used for number of milli-seconds */
  return g_strdup_printf (g_dngettext (GETTEXT_PACKAGE, "%d milli-second", "%d milli-seconds", value), value);
}

#define USEC_PER_YEAR   (G_USEC_PER_SEC * 60LL * 60 * 24 * 365.25)
#define USEC_PER_MONTH  (G_USEC_PER_SEC * 60LL * 60 * 24 * 365.25 / 12.0)
#define USEC_PER_DAY    (G_USEC_PER_SEC * 60LL * 60 * 24)
#define USEC_PER_HOUR   (G_USEC_PER_SEC * 60LL * 60)
#define USEC_PER_MINUTE (G_USEC_PER_SEC * 60LL)
#define MSEC_PER_USEC   (1000LL)

gchar *
gdu_utils_format_duration_usec (guint64                usec,
                                GduFormatDurationFlags flags)
{
  gchar *ret;
  guint64 years = 0;
  guint64 months = 0;
  guint64 days = 0;
  guint64 hours = 0;
  guint64 minutes = 0;
  guint64 seconds = 0;
  guint64 milliseconds = 0;
  gchar *years_str = NULL;
  gchar *months_str = NULL;
  gchar *days_str = NULL;
  gchar *hours_str = NULL;
  gchar *minutes_str = NULL;
  gchar *seconds_str = NULL;
  gchar *milliseconds_str = NULL;
  guint64 t;

  t = usec;

  years  = floor (t / USEC_PER_YEAR);
  t -= years * USEC_PER_YEAR;

  months = floor (t / USEC_PER_MONTH);
  t -= months * USEC_PER_MONTH;

  days = floor (t / USEC_PER_DAY);
  t -= days * USEC_PER_DAY;

  hours = floor (t / USEC_PER_HOUR);
  t -= hours * USEC_PER_HOUR;

  minutes = floor (t / USEC_PER_MINUTE);
  t -= minutes * USEC_PER_MINUTE;

  seconds = floor (t / G_USEC_PER_SEC);
  t -= seconds * G_USEC_PER_SEC;

  milliseconds = floor (t / MSEC_PER_USEC);

  years_str = years_to_string (years);
  months_str = months_to_string (months);
  days_str = days_to_string (days);
  hours_str = hours_to_string (hours);
  minutes_str = minutes_to_string (minutes);
  seconds_str = seconds_to_string (seconds);
  milliseconds_str = milliseconds_to_string (milliseconds);

  if (years > 0)
    {
      /* Translators: Used for duration greater than one year. First %s is number of years, second %s is months, third %s is days */
      ret = g_strdup_printf (C_("duration-year-to-inf", "%s, %s and %s"), years_str, months_str, days_str);
    }
  else if (months > 0)
    {
      /* Translators: Used for durations less than one year but greater than one month. First %s is number of months, second %s is days */
      ret = g_strdup_printf (C_("duration-months-to-year", "%s and %s"), months_str, days_str);
    }
  else if (days > 0)
    {
      /* Translators: Used for durations less than one month but greater than one day. First %s is number of days, second %s is hours */
      ret = g_strdup_printf (C_("duration-day-to-month", "%s and %s"), days_str, hours_str);
    }
  else if (hours > 0)
    {
      /* Translators: Used for durations less than one day but greater than one hour. First %s is number of hours, second %s is minutes */
      ret = g_strdup_printf (C_("duration-hour-to-day", "%s and %s"), hours_str, minutes_str);
    }
  else if (minutes > 0)
    {
      if (flags & GDU_FORMAT_DURATION_FLAGS_NO_SECONDS)
        {
          ret = g_strdup (minutes_str);
        }
      else
        {
          /* Translators: Used for durations less than one hour but greater than one minute. First %s is number of minutes, second %s is seconds */
          ret = g_strdup_printf (C_("duration-minute-to-hour", "%s and %s"), minutes_str, seconds_str);
        }
    }
  else if (seconds > 0 ||
           !(flags & GDU_FORMAT_DURATION_FLAGS_SUBSECOND_PRECISION) ||
           (flags & GDU_FORMAT_DURATION_FLAGS_NO_SECONDS))
    {
      if (flags & GDU_FORMAT_DURATION_FLAGS_NO_SECONDS)
        {
          ret = g_strdup (C_("duration", "Less than a minute"));
        }
      else
        {
          /* Translators: Used for durations less than one minute byte greater than one second. First %s is number of seconds */
          ret = g_strdup_printf (C_("duration-second-to-minute", "%s"), seconds_str);
        }
    }
  else
    {
      /* Translators: Used for durations less than one second. First %s is number of milli-seconds */
      ret = g_strdup_printf (C_("duration-zero-to-second", "%s"), milliseconds_str);
    }

  g_free (years_str);
  g_free (months_str);
  g_free (days_str);
  g_free (hours_str);
  g_free (minutes_str);
  g_free (seconds_str);
  g_free (milliseconds_str);

  return ret;
}

gboolean
gdu_utils_is_flash (UDisksDrive *drive)
{
  gboolean ret = FALSE;
  guint n;
  const gchar *const *media_compat;

  media_compat = udisks_drive_get_media_compatibility (drive);
  for (n = 0; media_compat != NULL && media_compat[n] != NULL; n++)
    {
      if (g_str_has_prefix (media_compat[n], "flash"))
        {
          ret = TRUE;
          break;
        }
    }

  return ret;
}

guint
gdu_utils_count_primary_dos_partitions (UDisksClient         *client,
                                        UDisksPartitionTable *table)
{
  GList *partitions, *l;
  guint ret = 0;

  partitions = udisks_client_get_partitions (client, table);
  for (l = partitions; l != NULL; l = l->next)
    {
      UDisksPartition *partition = UDISKS_PARTITION (l->data);
      if (!udisks_partition_get_is_contained (partition))
        ret += 1;
    }

  g_list_foreach (partitions, (GFunc) g_object_unref, NULL);
  g_list_free (partitions);

  return ret;
}

gboolean
gdu_utils_have_dos_extended (UDisksClient         *client,
                             UDisksPartitionTable *table)
{
  GList *partitions, *l;
  gboolean ret = FALSE;

  partitions = udisks_client_get_partitions (client, table);
  for (l = partitions; l != NULL; l = l->next)
    {
      UDisksPartition *partition = UDISKS_PARTITION (l->data);
      if (udisks_partition_get_is_container (partition))
        {
          ret = TRUE;
          break;
        }
    }

  g_list_foreach (partitions, (GFunc) g_object_unref, NULL);
  g_list_free (partitions);

  return ret;
}

gboolean
gdu_utils_is_inside_dos_extended (UDisksClient         *client,
                                  UDisksPartitionTable *table,
                                  guint64               offset)
{
  GList *partitions, *l;
  gboolean ret = FALSE;

  partitions = udisks_client_get_partitions (client, table);
  for (l = partitions; l != NULL; l = l->next)
    {
      UDisksPartition *partition = UDISKS_PARTITION (l->data);
      if (udisks_partition_get_is_container (partition))
        {
          if (offset >= udisks_partition_get_offset (partition) &&
              offset < udisks_partition_get_offset (partition) + udisks_partition_get_size (partition))
            {
              ret = TRUE;
              break;
            }
        }
    }

  g_list_foreach (partitions, (GFunc) g_object_unref, NULL);
  g_list_free (partitions);

  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

static void
response_cb (AdwMessageDialog *dialog,
             gchar            *response,
             gpointer         *user_data)
{
  gtk_window_close (GTK_WINDOW (dialog));
}

void
gdu_utils_show_message (const char *title,
                        const char *message,
                        GtkWidget  *parent_window)
{

  AdwDialog *dialog;

  dialog = adw_alert_dialog_new (title, message);

  adw_alert_dialog_add_responses (ADW_ALERT_DIALOG (dialog),
                                  "close",  _("_Close"),
                                  NULL);

  adw_alert_dialog_set_close_response (ADW_ALERT_DIALOG (dialog),
                                       "close");

  adw_alert_dialog_set_default_response (ADW_ALERT_DIALOG (dialog),
                                         "close");

  adw_dialog_present (dialog, parent_window);
}

void
gdu_utils_show_error (GtkWindow   *parent_window,
                      const gchar *title,
                      GError      *error)
{
  g_autoptr(GError) fixed_up_error = NULL;
  const char *message;
  /* Never show an error if it's because the user dismissed the
   * authentication dialog himself
   *
   * ... or if the user cancelled the operation
   */
  if ((error->domain == UDISKS_ERROR && error->code == UDISKS_ERROR_NOT_AUTHORIZED_DISMISSED) ||
      (error->domain == UDISKS_ERROR && error->code == UDISKS_ERROR_CANCELLED))
      return;

  fixed_up_error = g_error_copy (error);
  if (g_dbus_error_is_remote_error (fixed_up_error))
    g_dbus_error_strip_remote_error (fixed_up_error);

  message = g_strdup_printf("%s (%s, %d)",
                            fixed_up_error->message,
                            g_quark_to_string (error->domain),
                            error->code);
  /* TODO: probably provide the error-domain / error-code / D-Bus error name
   * in a GtkExpander.
   */
  gdu_utils_show_message (title, message, GTK_WIDGET (parent_window));
}

static GtkWidget *
widget_for_object (UDisksClient *client,
                   UDisksObject *object)
{
  GtkWidget *row;
  UDisksObjectInfo *info;

  row = adw_action_row_new ();
  info = udisks_client_get_object_info (client, object);

  adw_preferences_row_set_title(ADW_PREFERENCES_ROW (row), udisks_object_info_get_description (info));
  adw_action_row_set_subtitle (ADW_ACTION_ROW (row), udisks_object_info_get_name (info));

  return GTK_WIDGET (row);
}

GtkWidget *
gdu_util_create_widget_from_objects (UDisksClient *client,
                                     GList        *objects)
{
  GtkWidget *group;

  group = adw_preferences_group_new ();

  for (GList *l = objects; l != NULL; l = l->next)
    {
      GtkWidget *widget = widget_for_object (client, UDISKS_OBJECT (l->data));
      adw_preferences_group_add (ADW_PREFERENCES_GROUP (group), widget);
    }

  return group;
}

void
gdu_utils_show_confirmation (GtkWidget                *parent_window,
                             ConfirmationDialogData   *data,
                             GtkWidget                *extra_child)
{
  AdwAlertDialog *dialog;

  dialog = ADW_ALERT_DIALOG (adw_alert_dialog_new (data->message,
                                                   data->description));

  adw_alert_dialog_add_responses (dialog,
                                  "cancel",  _("_Cancel"),
                                  "confirm", data->response_verb,
                                  NULL);

  adw_alert_dialog_set_close_response (dialog, "cancel");
  adw_alert_dialog_set_response_appearance (dialog, "confirm", data->response_appearance);

  adw_alert_dialog_set_default_response (dialog,
                                         data->response_appearance == ADW_RESPONSE_SUGGESTED ?
                                         "confirm" : "cancel");

  if (extra_child)
    adw_alert_dialog_set_extra_child (dialog, extra_child);

  adw_alert_dialog_choose (dialog,
                           parent_window,
                           NULL, /* GCancellable */
                           data->callback,
                           data->user_data);
}

typedef struct
{
  gboolean available;
  gchar *missing_util;
  ResizeFlags mode;
} UtilCacheEntry;

static void
util_cache_entry_free (UtilCacheEntry *data)
{
  g_free (data->missing_util);
  g_free (data);
}

G_LOCK_DEFINE (can_resize_lock);

/* Uses an internal cache, set flush to rebuild it first */
gboolean
gdu_utils_can_resize (UDisksClient *client,
                      const gchar  *fstype,
                      gboolean      flush,
                      ResizeFlags  *mode_out,
                      gchar       **missing_util_out)
{
  static GHashTable *cache = NULL;
  const gchar *const *supported_fs;
  UtilCacheEntry *result;

  G_LOCK (can_resize_lock);
  if (flush)
    g_clear_pointer (&cache, g_hash_table_destroy);

  if (cache == NULL)
    {
      cache = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, (GDestroyNotify) util_cache_entry_free);
      supported_fs = udisks_manager_get_supported_filesystems (udisks_client_get_manager (client));
      for (gsize i = 0; supported_fs[i] != NULL; i++)
        {
          GVariant *out_available;

          if (udisks_manager_call_can_resize_sync (udisks_client_get_manager (client),
                                                   supported_fs[i], &out_available, NULL, NULL))
            {
              UtilCacheEntry *entry;
              guint64 m = 0;

              entry = g_new0 (UtilCacheEntry, 1);
              g_variant_get (out_available, "(bts)", &entry->available, &m, &entry->missing_util);
              g_variant_unref (out_available);
              entry->mode = (ResizeFlags) m;
              g_hash_table_insert (cache, g_strdup (supported_fs[i]), entry);
            }
        }
    }
  G_UNLOCK (can_resize_lock);

  result = g_hash_table_lookup (cache, fstype);
  if (mode_out != NULL)
    *mode_out = result ? result->mode : 0;

  if (missing_util_out != NULL)
    *missing_util_out = result ? g_strdup (result->missing_util) : NULL;

  return result ? result->available : FALSE;
}

G_LOCK_DEFINE (can_repair_lock);

gboolean
gdu_utils_can_repair (UDisksClient *client,
                      const gchar  *fstype,
                      gboolean      flush,
                      gchar       **missing_util_out)
{
  static GHashTable *cache = NULL;
  const gchar *const *supported_fs;
  UtilCacheEntry *result;

  G_LOCK (can_repair_lock);
  if (flush)
    g_clear_pointer (&cache, g_hash_table_destroy);

  if (cache == NULL)
    {
      cache = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, (GDestroyNotify) util_cache_entry_free);
      supported_fs = udisks_manager_get_supported_filesystems (udisks_client_get_manager (client));
      for (gsize i = 0; supported_fs[i] != NULL; i++)
        {
          GVariant *out_available;

          if (udisks_manager_call_can_repair_sync (udisks_client_get_manager (client),
                                                   supported_fs[i], &out_available, NULL, NULL))
            {
              UtilCacheEntry *entry;

              entry = g_new0 (UtilCacheEntry, 1);
              g_variant_get (out_available, "(bs)", &entry->available, &entry->missing_util);
              g_variant_unref (out_available);
              g_hash_table_insert (cache, g_strdup (supported_fs[i]), entry);
            }
        }
    }
  G_UNLOCK (can_repair_lock);

  result = g_hash_table_lookup (cache, fstype);
  if (missing_util_out != NULL)
    *missing_util_out = result ? g_strdup (result->missing_util) : NULL;

  return result ? result->available : FALSE;
}

G_LOCK_DEFINE (can_format_lock);

gboolean
gdu_utils_can_format (UDisksClient *client,
                      const gchar  *fstype,
                      gboolean      flush,
                      gchar       **missing_util_out)
{
  static GHashTable *cache = NULL;
  const gchar *const *supported_fs;
  UtilCacheEntry *result;

  G_LOCK (can_format_lock);
  if (flush)
    g_clear_pointer (&cache, g_hash_table_destroy);

  if (cache == NULL)
  {
    cache = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, (GDestroyNotify) util_cache_entry_free);
    supported_fs = udisks_manager_get_supported_filesystems (udisks_client_get_manager (client));
    for (gsize i = 0; supported_fs[i] != NULL; i++)
    {
      GVariant *out_available;

      if (udisks_manager_call_can_format_sync (udisks_client_get_manager (client),
                                               supported_fs[i], &out_available, NULL, NULL))
      {
        UtilCacheEntry *entry;

        entry = g_new0 (UtilCacheEntry, 1);
        g_variant_get (out_available, "(bs)", &entry->available, &entry->missing_util);
        g_variant_unref (out_available);
        g_hash_table_insert (cache, g_strdup (supported_fs[i]), entry);
      }
    }
  }
  G_UNLOCK (can_format_lock);

  result = g_hash_table_lookup (cache, fstype);
  if (missing_util_out != NULL)
    *missing_util_out = result ? g_strdup (result->missing_util) : NULL;

  return result ? result->available : FALSE;
}

gboolean
gdu_utils_can_take_ownership (const gchar *fstype)
{
  if (g_strcmp0 (fstype, "ntfs") == 0 ||
      g_strcmp0 (fstype, "vfat") == 0 ||
      g_strcmp0 (fstype, "exfat") == 0)
    {
      return FALSE;
    }
  return TRUE;
}

G_LOCK_DEFINE (can_check_lock);

gboolean
gdu_utils_can_check (UDisksClient *client,
                     const gchar  *fstype,
                     gboolean      flush,
                     gchar       **missing_util_out)
{
  static GHashTable *cache = NULL;
  const gchar *const *supported_fs;
  UtilCacheEntry *result;

  G_LOCK (can_check_lock);
  if (flush && cache != NULL)
    g_clear_pointer (&cache, g_hash_table_destroy);

  if (cache == NULL)
    {
      cache = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, (GDestroyNotify) util_cache_entry_free);
      supported_fs = udisks_manager_get_supported_filesystems (udisks_client_get_manager (client));
      for (gsize i = 0; supported_fs[i] != NULL; i++)
        {
          GVariant *out_available;

          if (udisks_manager_call_can_check_sync (udisks_client_get_manager (client),
                                                  supported_fs[i], &out_available, NULL, NULL))
            {
              UtilCacheEntry *entry;

              entry = g_new0 (UtilCacheEntry, 1);
              g_variant_get (out_available, "(bs)", &entry->available, &entry->missing_util);
              g_variant_unref (out_available);
              g_hash_table_insert (cache, g_strdup (supported_fs[i]), entry);
            }
        }
    }
  G_UNLOCK (can_check_lock);

  result = g_hash_table_lookup (cache, fstype);
  if (missing_util_out != NULL)
    *missing_util_out = result ? g_strdup (result->missing_util) : NULL;

  return result ? result->available : FALSE;
}


/* ---------------------------------------------------------------------------------------------------- */

guint
gdu_utils_get_max_label_length (const gchar *fstype)
{
  guint max_length = G_MAXUINT;

  if (g_strcmp0 (fstype, "exfat") == 0)
    {
      max_length = 15;
    }
  else if (g_strcmp0 (fstype, "vfat") == 0)
    {
      max_length = 11;
    }

  return max_length;
}

/* ---------------------------------------------------------------------------------------------------- */

gboolean
_gtk_entry_buffer_truncate_bytes (GtkEntryBuffer *gtk_entry_buffer,
                                  guint           max_bytes)
{
  guint max_utf8_length = max_bytes;

  while (gtk_entry_buffer_get_bytes (gtk_entry_buffer) > max_bytes)
    {
      gtk_entry_buffer_delete_text (gtk_entry_buffer, max_utf8_length, -1);
      max_utf8_length--;
    }

  return max_utf8_length != max_bytes;
}

/* ---------------------------------------------------------------------------------------------------- */

#ifdef __GNUC_PREREQ
# if __GNUC_PREREQ(4,6)
#  pragma GCC diagnostic pop
# endif
#endif

gboolean
gdu_util_is_same_size (GList   *blocks,
                       guint64 *out_min_size)
{
  gboolean ret = FALSE;
  guint64 min_size = G_MAXUINT64;
  guint64 max_size = 0;
  GList *l;

  if (blocks == NULL)
    goto out;

  for (l = blocks; l != NULL; l = l->next)
    {
      UDisksBlock *block = UDISKS_BLOCK (l->data);
      guint64 block_size = udisks_block_get_size (block);
      if (block_size > max_size)
        max_size = block_size;
      if (block_size < min_size)
        min_size = block_size;
    }

  /* Bail if there is more than a 1% difference and at least 1MiB */
  if (max_size - min_size > min_size * 101LL / 100LL &&
      max_size - min_size > 1048576)
    goto out;

  ret = TRUE;

 out:
  if (out_min_size != NULL)
    *out_min_size = min_size;
  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

gchar *
gdu_utils_get_pretty_uri (GFile *file)
{
  gchar *ret;
  gchar *s;

  if (g_file_is_native (file))
    {
      const gchar *homedir;

      ret = g_file_get_path (file);

      homedir = g_get_home_dir ();
      if (g_str_has_prefix (ret, homedir))
        {
          s = ret;
          ret = g_strdup_printf ("~/%s", ret + strlen (homedir) + 1);
          g_free (s);
        }
    }
  else
    {
      s = g_file_get_uri (file);
      ret = g_uri_unescape_string (s, NULL);
      g_free (s);
    }

  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

GList *
gdu_utils_get_all_contained_objects (UDisksClient *client,
                                     UDisksObject *object)
{
  UDisksBlock *block = NULL;
  UDisksDrive *drive = NULL;
  UDisksObject *block_object = NULL;
  UDisksPartitionTable *partition_table = NULL;
  GList *partitions = NULL;
  GList *l;
  GList *objects_to_check = NULL;

  drive = udisks_object_get_drive (object);
  if (drive != NULL)
    {
      block = udisks_client_get_block_for_drive (client,
                                                 drive,
                                                 FALSE /* get_physical */);
    }
  else
    {
      block = udisks_object_get_block (object);
    }

  if (block != NULL)
    {
      block_object = (UDisksObject *) g_dbus_interface_dup_object (G_DBUS_INTERFACE (block));
      if (block_object != NULL)
        {
          objects_to_check = g_list_prepend (objects_to_check, g_object_ref (block_object));

          /* if we're a partitioned block device, add all partitions */
          partition_table = udisks_object_get_partition_table (block_object);
          if (partition_table != NULL)
            {
              partitions = udisks_client_get_partitions (client, partition_table);
              for (l = partitions; l != NULL; l = l->next)
                {
                  UDisksPartition *partition = UDISKS_PARTITION (l->data);
                  UDisksObject *partition_object;
                  partition_object = (UDisksObject *) g_dbus_interface_dup_object (G_DBUS_INTERFACE (partition));
                  if (partition_object != NULL)
                    objects_to_check = g_list_append (objects_to_check, partition_object);
                }
            }
        }
    }

  /* Add LUKS objects */
  for (l = objects_to_check; l != NULL; l = l->next)
    {
      UDisksObject *object_iter = UDISKS_OBJECT (l->data);
      UDisksBlock *block_for_object;
      block_for_object = udisks_object_peek_block (object_iter);
      if (block_for_object != NULL)
        {
          UDisksBlock *cleartext;
          cleartext = udisks_client_get_cleartext_block (client, block_for_object);
          if (cleartext != NULL)
            {
              UDisksObject *cleartext_object;
              cleartext_object = (UDisksObject *) g_dbus_interface_dup_object (G_DBUS_INTERFACE (cleartext));
              if (cleartext_object != NULL)
                objects_to_check = g_list_append (objects_to_check, cleartext_object);
              g_object_unref (cleartext);
            }
        }
    }

  g_clear_object (&partition_table);
  g_list_free_full (partitions, g_object_unref);
  g_clear_object (&block_object);
  g_clear_object (&block);
  g_clear_object (&drive);

  return objects_to_check;
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
gdu_utils_is_in_use_full (UDisksClient      *client,
                          UDisksObject      *object,
                          UDisksFilesystem **filesystem_to_unmount_out,
                          UDisksEncrypted  **encrypted_to_lock_out,
                          gboolean          *last_out)
{
  UDisksFilesystem *filesystem_to_unmount = NULL;
  UDisksEncrypted *encrypted_to_lock = NULL;
  GList *l;
  GList *objects_to_check = NULL;
  gboolean ret = FALSE;
  gboolean last = TRUE;

  objects_to_check = gdu_utils_get_all_contained_objects (client, object);

  /* Check in reverse order, e.g. cleartext before LUKS, partitions before the main block device */
  objects_to_check = g_list_reverse (objects_to_check);
  for (l = objects_to_check; l != NULL; l = l->next)
    {
      UDisksObject *object_iter = UDISKS_OBJECT (l->data);
      UDisksBlock *block_for_object;
      UDisksFilesystem *filesystem_for_object;
      UDisksEncrypted *encrypted_for_object;

      block_for_object = udisks_object_peek_block (object_iter);

      filesystem_for_object = udisks_object_peek_filesystem (object_iter);
      if (filesystem_for_object != NULL)
        {
          const gchar *const *mount_points = udisks_filesystem_get_mount_points (filesystem_for_object);
          if (g_strv_length ((gchar **) mount_points) > 0)
            {
              if (ret)
                {
                  last = FALSE;
                  break;
                }

              filesystem_to_unmount = g_object_ref (filesystem_for_object);
              ret = TRUE;
            }
        }

      encrypted_for_object = udisks_object_peek_encrypted (object_iter);
      if (encrypted_for_object != NULL)
        {
          UDisksBlock *cleartext;
          cleartext = udisks_client_get_cleartext_block (client, block_for_object);
          if (cleartext != NULL)
            {
              g_object_unref (cleartext);

              if (ret)
                {
                  last = FALSE;
                  break;
                }

              encrypted_to_lock = g_object_ref (encrypted_for_object);
              ret = TRUE;
            }
        }

      if (ret && last_out == NULL)
        break;
    }

  if (filesystem_to_unmount_out != NULL)
    {
      *filesystem_to_unmount_out = (filesystem_to_unmount != NULL) ?
        g_object_ref (filesystem_to_unmount) : NULL;
    }
  if (encrypted_to_lock_out != NULL)
    {
      *encrypted_to_lock_out = (encrypted_to_lock != NULL) ?
        g_object_ref (encrypted_to_lock) : NULL;
    }
  if (last_out != NULL)
    *last_out = last;

  g_list_free_full (objects_to_check, g_object_unref);
  g_clear_object (&encrypted_to_lock);
  g_clear_object (&filesystem_to_unmount);

  return ret;
}

gboolean
gdu_utils_is_in_use (UDisksClient *client,
                     UDisksObject *object)
{
  return gdu_utils_is_in_use_full (client, object, NULL, NULL, NULL);
}

/* ---------------------------------------------------------------------------------------------------- */

typedef struct
{
  UDisksClient *client;
  GtkWindow *parent_window;
  GList *objects;
  GList *object_iter;
  GTask *task;
  GCancellable *cancellable; /* borrowed ref */
  guint last_mount_point_list_size; /* only for unuse_unmount_cb to check against a race in UDisks */
} UnuseData;

static void
unuse_data_free (UnuseData *data)
{
  g_clear_object (&data->client);
  g_list_free_full (data->objects, g_object_unref);
  g_clear_object (&data->task);
  g_slice_free (UnuseData, data);
}

static void
unuse_data_complete (UnuseData    *data,
                     const gchar  *error_message,
                     GError       *error)
{
  if (error != NULL)
    {
      gdu_utils_show_error (data->parent_window,
                            error_message,
                            error);
      g_task_return_error (data->task, error);
    }
  else
    {
      g_task_return_pointer (data->task, NULL, NULL);
    }
  unuse_data_free (data);
}

static void unuse_data_iterate (UnuseData *data);

static void
unuse_unmount_cb (UDisksFilesystem *filesystem,
                  GAsyncResult     *res,
                  gpointer          user_data)
{
  UnuseData *data = user_data;
  GError *error = NULL;

  if (!udisks_filesystem_call_unmount_finish (filesystem,
                                              res,
                                              &error))
    {
      unuse_data_complete (data, _("Error unmounting filesystem"), error);
    }
  else
    {
      gint64 end_usec;
      const gchar *const *mount_points;

      end_usec = g_get_monotonic_time () + (G_USEC_PER_SEC * 5);

      while (mount_points = udisks_filesystem_get_mount_points (filesystem),
             (mount_points ? g_strv_length ((gchar **) mount_points) : 0) == data->last_mount_point_list_size &&
             g_get_monotonic_time () < end_usec)
      {
        udisks_client_settle (data->client);
      }

      unuse_data_iterate (data);
    }
}

static void
unuse_lock_cb (UDisksEncrypted  *encrypted,
               GAsyncResult     *res,
               gpointer          user_data)
{
  UnuseData *data = user_data;
  GError *error = NULL;

  if (!udisks_encrypted_call_lock_finish (encrypted,
                                          res,
                                          &error))
    {
      unuse_data_complete (data, _("Error locking device"), error);
    }
  else
    {
      unuse_data_iterate (data);
    }
}

static void
unuse_set_autoclear_cb (UDisksLoop   *loop,
                        GAsyncResult *res,
                        gpointer      user_data)
{
  UnuseData *data = user_data;
  GError *error = NULL;

  if (!udisks_loop_call_set_autoclear_finish (loop,
                                              res,
                                              &error))
    {
      unuse_data_complete (data,
                           _("Error disabling autoclear for loop device"),
                           error);
    }
  else
    {
      unuse_data_iterate (data);
    }
}

static void
unuse_data_iterate (UnuseData *data)
{
  UDisksObject *object;
  UDisksFilesystem *filesystem_to_unmount = NULL;
  UDisksEncrypted *encrypted_to_lock = NULL;
  UDisksLoop *loop;
  UDisksBlock *block;
  gboolean last;

  object = UDISKS_OBJECT (data->object_iter->data);
  gdu_utils_is_in_use_full (data->client, object,
                            &filesystem_to_unmount, &encrypted_to_lock, NULL);
  block = udisks_object_peek_block (object);

  if (block != NULL && (filesystem_to_unmount != NULL || encrypted_to_lock != NULL))
    {
      loop = udisks_client_get_loop_for_block (data->client, block);

      if (loop != NULL)
        {
          if (udisks_loop_get_autoclear (loop))
            {
              gdu_utils_is_in_use_full (data->client,
                                        UDISKS_OBJECT (g_dbus_interface_get_object (G_DBUS_INTERFACE (loop))),
                                        NULL, NULL, &last);
              if (last)
                {
                  udisks_loop_call_set_autoclear (loop,
                                                  FALSE,
                                                  g_variant_new ("a{sv}", NULL),
                                                  data->cancellable,
                                                  (GAsyncReadyCallback) unuse_set_autoclear_cb,
                                                  data);
                  g_object_unref (loop);
                  g_clear_object (&encrypted_to_lock);
                  g_clear_object (&filesystem_to_unmount);
                  return;
                }
            }

          g_object_unref (loop);
        }
    }

  if (filesystem_to_unmount != NULL)
    {
      const gchar *const *mount_points;

      mount_points = udisks_filesystem_get_mount_points (filesystem_to_unmount);
      data->last_mount_point_list_size = mount_points ? g_strv_length ((gchar **) mount_points) : 0;
      udisks_filesystem_call_unmount (filesystem_to_unmount,
                                      g_variant_new ("a{sv}", NULL), /* options */
                                      data->cancellable, /* cancellable */
                                      (GAsyncReadyCallback) unuse_unmount_cb,
                                      data);
    }
  else if (encrypted_to_lock != NULL)
    {
      udisks_encrypted_call_lock (encrypted_to_lock,
                                  g_variant_new ("a{sv}", NULL), /* options */
                                  data->cancellable, /* cancellable */
                                  (GAsyncReadyCallback) unuse_lock_cb,
                                  data);
    }
  else
    {
      /* nothing left to do, move on to next object */
      data->object_iter = data->object_iter->next;
      if (data->object_iter != NULL)
        {
          unuse_data_iterate (data);
        }
      else
        {
          /* yay, no objects left, terminate without error */
          unuse_data_complete (data, NULL, NULL);
        }
    }

  g_clear_object (&encrypted_to_lock);
  g_clear_object (&filesystem_to_unmount);
}

void
gdu_utils_ensure_unused_list (UDisksClient         *client,
                              GtkWindow            *parent_window,
                              GList                *objects,
                              GAsyncReadyCallback   callback,
                              GCancellable         *cancellable,
                              gpointer              user_data)
{
  UnuseData *data;

  g_return_if_fail (objects != NULL);
  g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));

  data = g_slice_new0 (UnuseData);
  data->client = g_object_ref (client);
  data->parent_window = (parent_window != NULL) ? g_object_ref (parent_window) : NULL;
  data->objects = g_list_copy (objects);
  g_list_foreach (data->objects, (GFunc) g_object_ref, NULL);
  data->object_iter = data->objects;
  data->cancellable = cancellable;
  data->task = g_task_new (G_OBJECT (client),
                           cancellable,
                           callback,
                           user_data);

  unuse_data_iterate (data);
}

gboolean
gdu_utils_ensure_unused_list_finish (UDisksClient  *client,
                                     GAsyncResult  *res,
                                     GError       **error)
{
  GTask *task = G_TASK (res);

  g_return_val_if_fail (G_IS_TASK (res), FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  if (g_task_had_error (task))
    {
      g_task_propagate_pointer (task, error);
      return FALSE;
    }

  return TRUE;
}

/* ---------------------------------------------------------------------------------------------------- */

void
gdu_utils_ensure_unused (UDisksClient         *client,
                         GtkWindow            *parent_window,
                         UDisksObject         *object,
                         GAsyncReadyCallback   callback,
                         GCancellable         *cancellable,
                         gpointer              user_data)
{
  GList *objects;
  objects = g_list_append (NULL, object);
  gdu_utils_ensure_unused_list (client, parent_window, objects, callback, cancellable, user_data);
  g_list_free (objects);
}

gboolean
gdu_utils_ensure_unused_finish (UDisksClient  *client,
                                GAsyncResult  *res,
                                GError       **error)
{
  return gdu_utils_ensure_unused_list_finish (client, res, error);
}

/* ---------------------------------------------------------------------------------------------------- */

guint64
gdu_utils_calc_space_to_grow (UDisksClient *client,
                              UDisksPartitionTable *table,
                              UDisksPartition *partition)
{
  GList *partitions, *l;
  guint64 next_pos, current_end;
  UDisksObject *table_object;

  table_object = UDISKS_OBJECT (g_dbus_interface_get_object (G_DBUS_INTERFACE (table)));
  next_pos = udisks_block_get_size (udisks_object_peek_block (table_object));
  current_end = udisks_partition_get_offset (partition) + udisks_partition_get_size (partition);
  partitions = udisks_client_get_partitions (client, table);
  for (l = partitions; l != NULL; l = l->next)
    {
      UDisksPartition *tmp_partition = UDISKS_PARTITION (l->data);
      guint64 start;
      guint64 end;

      if (udisks_partition_get_number (partition) == udisks_partition_get_number (tmp_partition))
        continue;

      start = udisks_partition_get_offset (tmp_partition);
      end = start + udisks_partition_get_size (tmp_partition);
      if (end > current_end && (end < next_pos))
        next_pos = end;
      if (start >= current_end && (start < next_pos))
        next_pos = start;
    }

  g_list_foreach (partitions, (GFunc) g_object_unref, NULL);
  g_list_free (partitions);

  return next_pos - udisks_partition_get_offset (partition);
}

/* ---------------------------------------------------------------------------------------------------- */

guint64
gdu_utils_calc_space_to_shrink_extended (UDisksClient *client,
                                         UDisksPartitionTable *table,
                                         UDisksPartition *partition)
{
  GList *partitions, *l;
  guint64 minimum, maximum;

  g_assert (udisks_partition_get_is_container (partition));
  minimum = udisks_partition_get_offset (partition) + 1;
  maximum = minimum + udisks_partition_get_size (partition);
  partitions = udisks_client_get_partitions (client, table);
  for (l = partitions; l != NULL; l = l->next)
    {
      UDisksPartition *tmp_partition = UDISKS_PARTITION (l->data);
      guint64 end;

      if (udisks_partition_get_number (partition) == udisks_partition_get_number (tmp_partition))
        continue;

      end = udisks_partition_get_offset (tmp_partition) + udisks_partition_get_size (tmp_partition);
      if (end > minimum && end <= maximum)
        minimum = end;
    }

  g_list_foreach (partitions, (GFunc) g_object_unref, NULL);
  g_list_free (partitions);

  return minimum - udisks_partition_get_offset (partition);
}

/* ---------------------------------------------------------------------------------------------------- */

gint64
gdu_utils_get_unused_for_block (UDisksClient *client,
                                UDisksBlock  *block)
{
  gint64 ret = -1;
  UDisksFilesystem *filesystem = NULL;
  UDisksObject *object = NULL;
  const gchar *const *mount_points = NULL;
  struct statvfs statvfs_buf;

  object = (UDisksObject *) g_dbus_interface_get_object (G_DBUS_INTERFACE (block));
  if (object == NULL)
    goto out;

  filesystem = udisks_object_peek_filesystem (object);
  if (filesystem == NULL)
    {
      /* TODO: Look at UDisksFilesystem property set from the udev db populated by blkid(8) */
      goto out;
    }

  mount_points = udisks_filesystem_get_mount_points (filesystem);
  if (mount_points == NULL || mount_points[0] == NULL)
    goto out;

  /* Don't warn, could be the filesystem is mounted in a place we have no
   * permission to look at
   */
  if (statvfs (mount_points[0], &statvfs_buf) != 0)
    goto out;

  ret = ((gint64) statvfs_buf.f_bfree) * ((gint64) statvfs_buf.f_bsize);

 out:
  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

gint
gdu_utils_get_default_unit (guint64 size)
{
  if (size > unit_sizes[TByte] * 10)
    {
      /* size > 10TB -> TB */
      return TByte;
    }
  else if (size > unit_sizes[GByte] * 10)
    {
      /* 10TB > size > 10GB -> GB */
      return GByte;
    }
  else if (size > unit_sizes[MByte] * 10)
    {
      /* 10GB > size > 10MB -> MB */
      return MByte;
    }
  else if (size > unit_sizes[KByte] * 10)
    {
      /* 10MB > size > 10KB -> KB */
      return KByte;
    }
  else
    {
      /* 10kB > size > 0 -> bytes */
      return Byte;
    }
}
