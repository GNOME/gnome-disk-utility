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

#include "gduutils.h"

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
gdu_utils_configure_file_chooser_for_disk_images (GtkFileChooser *file_chooser)
{
  GtkFileFilter *filter;
  const gchar *folder;

  /* Default to the "Documents" folder since that's where we save such images */
  folder = g_get_user_special_dir (G_USER_DIRECTORY_DOCUMENTS);
  if (folder != NULL)
    gtk_file_chooser_set_current_folder (file_chooser, folder);

  /* TODO: define proper mime-types */
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

/* ---------------------------------------------------------------------------------------------------- */

gchar *
gdu_utils_duration_to_string (guint    duration_sec,
                              gboolean include_second_precision)
{
  gchar *s;

  if (duration_sec < 60)
    {
      if (include_second_precision)
        {
          s = g_strdup_printf (dngettext (GETTEXT_PACKAGE,
                                          N_("%d second"),
                                          N_("%d seconds"),
                                          duration_sec),
                               duration_sec);
        }
      else
        {
          s = g_strdup (_("Less than a minute"));
        }
    }
  else if (duration_sec < 3600)
    {
      s = g_strdup_printf (dngettext (GETTEXT_PACKAGE,
                                      N_("%d minute"),
                                      N_("%d minutes"),
                                      duration_sec / 60),
                           duration_sec / 60);
    }
  else
    {
      s = g_strdup_printf (dngettext (GETTEXT_PACKAGE,
                                      N_("%d hour"),
                                      N_("%d hours"),
                                      duration_sec / 3600),
                           duration_sec / 3600);
    }
  return s;
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
                                 gboolean         add_to_front)
{
  gboolean opts, ui;
  opts = !! has_option (options_entry, option, FALSE, NULL);
  ui = !! gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (check_button));
  if (opts != ui)
    {
      if (widget == check_button)
        {
          if (ui)
            add_option (options_entry, "", option, add_to_front);
          else
            remove_option (options_entry, option, FALSE);
        }
      else
        {
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
