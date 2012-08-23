/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*-
 *
 * Copyright (C) 2008-2012 Red Hat, Inc.
 *
 * Licensed under GPL version 2 or later.
 *
 * Author: David Zeuthen <zeuthen@gmail.com>
 */

#include "config.h"
#include <glib/gi18n.h>

#include <math.h>

#include "gduutils.h"

#if defined(HAVE_LIBSYSTEMD_LOGIN)
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

void
gdu_utils_configure_file_chooser_for_disk_images (GtkFileChooser *file_chooser,
                                                  gboolean        set_file_types)
{
  GtkFileFilter *filter;
  gchar *folder;
  GSettings *settings;

  gtk_file_chooser_set_local_only (file_chooser, FALSE);

  /* Get folder from GSettings, and default to the "Documents" folder if not set */
  settings = g_settings_new ("org.gnome.Disks");
  folder = g_settings_get_string (settings, "image-dir-uri");
  if (folder == NULL || strlen (folder) == 0)
    {
      g_free (folder);
      folder = g_strdup_printf ("file://%s", g_get_user_special_dir (G_USER_DIRECTORY_DOCUMENTS));
    }
  g_object_set_data_full (G_OBJECT (file_chooser), "x-gdu-orig-folder", g_strdup (folder), g_free);
  gtk_file_chooser_set_current_folder_uri (file_chooser, folder);

  /* TODO: define proper mime-types */
  if (set_file_types)
    {
      filter = gtk_file_filter_new ();
      gtk_file_filter_set_name (filter, _("All Files"));
      gtk_file_filter_add_pattern (filter, "*");
      gtk_file_chooser_add_filter (file_chooser, filter); /* adopts filter */
      filter = gtk_file_filter_new ();
      gtk_file_filter_set_name (filter, _("Disk Images (*.img, *.iso)"));
      gtk_file_filter_add_pattern (filter, "*.img");
      gtk_file_filter_add_pattern (filter, "*.iso");
      gtk_file_chooser_add_filter (file_chooser, filter); /* adopts filter */
      gtk_file_chooser_set_filter (file_chooser, filter);
    }

  g_clear_object (&settings);
  g_free (folder);
}

/* should be called when user chooses file/dir from @file_chooser */
void
gdu_utils_file_chooser_for_disk_images_update_settings (GtkFileChooser *file_chooser)
{
  const gchar *orig_folder;
  gchar *cur_folder;

  orig_folder = g_object_get_data (G_OBJECT (file_chooser), "x-gdu-orig-folder");
  cur_folder = gtk_file_chooser_get_current_folder_uri (file_chooser);
  /* NOTE: cur_folder may be NULL if e.g. something in "Search" or
   * "Recently Used" is selected... in that case, do not update
   * the GSetting
   */
  if (cur_folder != NULL && g_strcmp0 (orig_folder, cur_folder) != 0)
    {
      GSettings *settings = g_settings_new ("org.gnome.Disks");
      g_settings_set_string (settings, "image-dir-uri", cur_folder);
      g_clear_object (&settings);
    }
  g_free (cur_folder);
}

/* ---------------------------------------------------------------------------------------------------- */

/* wouldn't need this if glade would support GtkInfoBar #$@#$@#!!@# */

GtkWidget *
gdu_utils_create_info_bar (GtkMessageType   message_type,
                           const gchar     *markup,
                           GtkWidget      **out_label)
{
  GtkWidget *info_bar;
  GtkWidget *hbox;
  GtkWidget *label;
  GtkWidget *image;
  const gchar *stock_id;

  info_bar = gtk_info_bar_new ();
  gtk_info_bar_set_message_type (GTK_INFO_BAR (info_bar), message_type);

  hbox = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
  gtk_box_pack_start (GTK_BOX (gtk_info_bar_get_content_area (GTK_INFO_BAR (info_bar))),
                      hbox, TRUE, TRUE, 0);

  switch (message_type)
    {
    case GTK_MESSAGE_QUESTION:
      stock_id = GTK_STOCK_DIALOG_QUESTION;
      break;

    default:                 /* explicit fall-through */
    case GTK_MESSAGE_OTHER:  /* explicit fall-through */
    case GTK_MESSAGE_INFO:
      stock_id = GTK_STOCK_DIALOG_INFO;
      break;

    case GTK_MESSAGE_WARNING:
      stock_id = GTK_STOCK_DIALOG_WARNING;
      break;

    case GTK_MESSAGE_ERROR:
      stock_id = GTK_STOCK_DIALOG_ERROR;
      break;
    }
  image = gtk_image_new_from_stock (stock_id, GTK_ICON_SIZE_BUTTON);
  gtk_box_pack_start (GTK_BOX (hbox), image, FALSE, TRUE, 0);

  label = gtk_label_new (NULL);
  gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
  gtk_label_set_line_wrap (GTK_LABEL (label), TRUE);
  gtk_label_set_markup (GTK_LABEL (label), markup);
  gtk_box_pack_start (GTK_BOX (hbox), label, TRUE, TRUE, 0);

  if (out_label != NULL)
    *out_label = label;

  gtk_widget_show (hbox);
  gtk_widget_show (image);
  gtk_widget_show (label);

  return info_bar;
}

/* ---------------------------------------------------------------------------------------------------- */

gchar *
gdu_utils_unfuse_path (const gchar *path)
{
  gchar *ret;
  GFile *file;
  gchar *uri;

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

  options = g_strsplit (gtk_entry_get_text (GTK_ENTRY (options_entry)), ",", 0);
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
  text = gtk_entry_get_text (GTK_ENTRY (options_entry));
  s = g_strdup_printf ("%s%s%s%s",
                       add_to_front ? option : text,
                       strlen (text) > 0 ? "," : "",
                       prefix,
                       add_to_front ? text : option);
  gtk_entry_set_text (GTK_ENTRY (options_entry), s);
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
  options = g_strsplit (gtk_entry_get_text (GTK_ENTRY (options_entry)), ",", 0);
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
  gtk_entry_set_text (GTK_ENTRY (options_entry), str->str);
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
  ui = !! gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (check_button));
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
            gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (check_button), !opts);
          else
            gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (check_button), opts);
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
  ui = gtk_entry_get_text (GTK_ENTRY (entry));
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
          gtk_entry_set_text (GTK_ENTRY (entry), opts_unescaped);
          g_free (opts_unescaped);
        }
    }
  g_free (ui_escaped);
  g_free (opts);
}

#if defined(HAVE_LIBSYSTEMD_LOGIN)
#include <systemd/sd-login.h>

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

/* ---------------------------------------------------------------------------------------------------- */

void
gdu_utils_show_error (GtkWindow   *parent_window,
                      const gchar *message,
                      GError      *error)
{
  GtkWidget *dialog;
  GError *fixed_up_error;

  /* Never show an error if it's because the user dismissed the
   * authentication dialog himself
   *
   * ... or if the user cancelled the operation
   */
  if ((error->domain == UDISKS_ERROR && error->code == UDISKS_ERROR_NOT_AUTHORIZED_DISMISSED) ||
      (error->domain == UDISKS_ERROR && error->code == UDISKS_ERROR_CANCELLED))
    goto no_dialog;

  fixed_up_error = g_error_copy (error);
  if (g_dbus_error_is_remote_error (fixed_up_error))
    g_dbus_error_strip_remote_error (fixed_up_error);

  /* TODO: probably provide the error-domain / error-code / D-Bus error name
   * in a GtkExpander.
   */
  dialog = gtk_message_dialog_new_with_markup (parent_window,
                                               GTK_DIALOG_MODAL,
                                               GTK_MESSAGE_ERROR,
                                               GTK_BUTTONS_CLOSE,
                                               "<big><b>%s</b></big>",
                                               message);
  gtk_message_dialog_format_secondary_markup (GTK_MESSAGE_DIALOG (dialog),
                                              "%s (%s, %d)",
                                              fixed_up_error->message,
                                              g_quark_to_string (error->domain),
                                              error->code);
  g_error_free (fixed_up_error);
  gtk_dialog_run (GTK_DIALOG (dialog));
  gtk_widget_destroy (dialog);

 no_dialog:
  ;
}

/* ---------------------------------------------------------------------------------------------------- */

gboolean
gdu_utils_show_confirmation (GtkWindow   *parent_window,
                             const gchar *message,
                             const gchar *secondary_message,
                             const gchar *affirmative_verb)
{
  GtkWidget *dialog;
  gint response;

  dialog = gtk_message_dialog_new_with_markup (parent_window,
                                               GTK_DIALOG_MODAL,
                                               GTK_MESSAGE_INFO,
                                               GTK_BUTTONS_CANCEL,
                                               "<big><b>%s</b></big>",
                                               message);
  gtk_message_dialog_format_secondary_markup (GTK_MESSAGE_DIALOG (dialog),
                                              "%s",
                                              secondary_message);

  gtk_dialog_add_button (GTK_DIALOG (dialog),
                         affirmative_verb,
                         GTK_RESPONSE_OK);

  response = gtk_dialog_run (GTK_DIALOG (dialog));

  gtk_widget_destroy (dialog);

  return response == GTK_RESPONSE_OK;
}

/* ---------------------------------------------------------------------------------------------------- */

gboolean
gdu_utils_is_ntfs_available (void)
{
  static gsize once = 0;
  static gboolean available = FALSE;

  if (g_once_init_enter (&once))
    {
      gchar *path;
      path = g_find_program_in_path ("mkntfs");
      if (path != NULL)
        available = TRUE;
      g_free (path);
      g_once_init_leave (&once, (gsize) 1);
    }
  return available;
}


