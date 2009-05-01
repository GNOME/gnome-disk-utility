/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*- */
/*
 *  nautilus-gdu.c
 *
 *  Copyright (C) 2008-2009 Red Hat, Inc.
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public
 *  License as published by the Free Software Foundation; either
 *  version 2 of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Library General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public
 *  License along with this library; if not, write to the Free
 *  Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 *  Author: Tomas Bzatek <tbzatek@redhat.com>
 *
 */

#include "config.h"

#include "nautilus-gdu.h"
#include <glib/gi18n-lib.h>
#include <gio/gio.h>
#include "gdu/gdu.h"



static void nautilus_gdu_instance_init (NautilusGdu      *gdu);
static void nautilus_gdu_class_init    (NautilusGduClass *klass);


static GType nautilus_gdu_type = 0;


#define DISK_FORMAT_UTILITY  "gdu-format-tool"

/*  TODO: push upstream  */
#define G_FILE_ATTRIBUTE_MOUNTABLE_UNIX_DEVICE_FILE   "mountable::unix-device-file"


/*  test if we're able to correctly find presentable from a device file  */
static gboolean
test_proper_device (gchar *device_file)
{
        GduPool *pool;
        GduDevice *device;
        GduPresentable *presentable = NULL;
        gboolean res = FALSE;

        if (device_file == NULL || strlen (device_file) <= 1)
                return FALSE;

        pool = gdu_pool_new ();
        device = gdu_pool_get_by_device_file (pool, device_file);
        if (device) {
                presentable = gdu_pool_get_volume_by_device (pool, device);
                if (presentable) {
                        res = TRUE;
                        g_object_unref (presentable);
                }
                g_object_unref (device);
        }
        g_object_unref (pool);

        return res;
}

static gchar *
find_device_from_nautilus_file (NautilusFileInfo *nautilus_file)
{
        GFile *file;
        GFileInfo *info;
        GError *error;
        GFileType file_type;
        GMount *mount;
        GVolume *volume;
        gchar *device_file = NULL;

        g_return_val_if_fail (nautilus_file != NULL, NULL);
        file = nautilus_file_info_get_location (nautilus_file);
        g_return_val_if_fail (file != NULL, NULL);
        file_type = nautilus_file_info_get_file_type (nautilus_file);

        /* first try to find mount target from a mountable  */
        if (file_type == G_FILE_TYPE_MOUNTABLE ||
            file_type == G_FILE_TYPE_SHORTCUT) {
                /* get a mount if exists and extract device file from it  */
                mount = nautilus_file_info_get_mount (nautilus_file);
                if (mount) {
                        volume = g_mount_get_volume (mount);
                        if (volume) {
                                device_file = g_volume_get_identifier (volume,
                                                                       G_VOLUME_IDENTIFIER_KIND_UNIX_DEVICE);
                                g_object_unref (volume);
                        }
                        g_object_unref (mount);
                }

                /* not mounted, assuming we've been spawned from computer://  */
                if (device_file == NULL) {
                        error = NULL;
                        /* retrieve DeviceKit device ID for non-mounted devices  */
                        info = g_file_query_info (file, G_FILE_ATTRIBUTE_MOUNTABLE_UNIX_DEVICE_FILE, G_FILE_QUERY_INFO_NONE, NULL, &error);
                        if (info) {
                                device_file = g_file_info_get_attribute_as_string (info, G_FILE_ATTRIBUTE_MOUNTABLE_UNIX_DEVICE_FILE);
                                g_object_unref (info);
                        }
                        if (error) {
                                g_warning ("unable to query info: %s\n", error->message);
                                g_error_free (error);
                        }
                 }
        }

        if (! test_proper_device (device_file)) {
                g_free (device_file);
                device_file = NULL;
        }

        return device_file;
}

static void
open_format_utility (NautilusMenuItem *item)
{
  gchar *argv[] = { NULL, NULL, NULL };
  gchar *device_file;
  GError *error = NULL;

  device_file = g_object_get_data (G_OBJECT (item), "device_file");

  argv[0] = g_build_filename (LIBEXECDIR, DISK_FORMAT_UTILITY, NULL);
  argv[1] = g_strdup (device_file);

  g_spawn_async (NULL, argv, NULL, 0, NULL, NULL, NULL, &error);
  if (error) {
    g_warning ("%s", error->message);
    g_error_free (error);
  }

  g_free (argv[0]);
  g_free (argv[1]);
}

static void
unmount_done (GObject      *object,
              GAsyncResult *res,
              gpointer      user_data)
{
  NautilusMenuItem *item = user_data;
  GError *error = NULL;

  if (g_mount_unmount_finish (G_MOUNT (object), res, &error))
    open_format_utility (item);
  else
    {
      GtkWidget *dialog;
      gchar *name, *p, *text;

      name = g_mount_get_name (G_MOUNT (object));
      dialog = gtk_message_dialog_new (NULL, 0,
                                       GTK_MESSAGE_ERROR,
                                       GTK_BUTTONS_OK,
                                       _("Could not unmount '%s'"), name);
      if (g_str_has_prefix (error->message, "org.freedesktop.DeviceKit"))
        {
          p = strchr (error->message, ':');
          if (p)
            text = p + 1;
          else
            text = error->message;
        }
      else
        text = error->message;

      gtk_message_dialog_format_secondary_text (GTK_MESSAGE_DIALOG (dialog),
                                                "%s", text);
      gtk_dialog_run (GTK_DIALOG (dialog));
      gtk_widget_destroy (dialog);
      g_error_free (error);
      g_free (name);
    }
}

static void
format_callback (NautilusMenuItem *item,
                 gpointer user_data)
{
  NautilusFileInfo *nautilus_file;
  GMount *mount;

  nautilus_file = g_object_get_data (G_OBJECT (item), "nautilus_file");
  mount = nautilus_file_info_get_mount (nautilus_file);
  if (mount)
    {
      g_mount_unmount (mount, G_MOUNT_UNMOUNT_NONE, NULL, unmount_done, item);
      g_object_unref (mount);
    }
  else
    open_format_utility (item);
}

static GList *
nautilus_gdu_get_file_items (NautilusMenuProvider *provider,
			     GtkWidget            *window,
			     GList                *files)
{
  NautilusMenuItem *item;
  NautilusFileInfo *nautilus_file;
  gchar *device_file;
  GList *items = NULL;

  if (g_list_length (files) != 1) {
    goto out;
  }

  nautilus_file = (NautilusFileInfo*)files->data;
  device_file = find_device_from_nautilus_file (nautilus_file);
  if (! device_file)
    goto out;

  item = nautilus_menu_item_new ("NautilusGdu::format",
                                 _("_Format..."),
                                 _("Create new filesystem on the selected device"),
                                 "nautilus-gdu");
  g_object_set_data_full (G_OBJECT (item), "device_file",
                          device_file,
                          (GDestroyNotify) g_free);
  g_object_set_data_full (G_OBJECT (item), "nautilus_file",
                          g_object_ref (nautilus_file),
                          (GDestroyNotify) g_object_unref);
  g_signal_connect (item, "activate",
                    G_CALLBACK (format_callback),
                    NULL);

  items = g_list_append (NULL, item);

out:
  return items;
}

static void
nautilus_gdu_menu_provider_iface_init (NautilusMenuProviderIface *iface)
{
  iface->get_file_items = nautilus_gdu_get_file_items;
}

static void
nautilus_gdu_instance_init (NautilusGdu *gdu)
{
}

static void
nautilus_gdu_class_init (NautilusGduClass *klass)
{
}

GType
nautilus_gdu_get_type (void)
{
  return nautilus_gdu_type;
}

void
nautilus_gdu_register_type (GTypeModule *module)
{
  const GTypeInfo info = {
    sizeof (NautilusGduClass),
    (GBaseInitFunc) NULL,
    (GBaseFinalizeFunc) NULL,
    (GClassInitFunc) nautilus_gdu_class_init,
    NULL,
    NULL,
    sizeof (NautilusGdu),
    0,
    (GInstanceInitFunc) nautilus_gdu_instance_init,
  };

  const GInterfaceInfo menu_provider_iface_info = {
    (GInterfaceInitFunc) nautilus_gdu_menu_provider_iface_init,
    NULL,
    NULL
  };

  nautilus_gdu_type = g_type_module_register_type (module,
                                                   G_TYPE_OBJECT,
                                                   "NautilusGdu",
                                                   &info, 0);

  g_type_module_add_interface (module,
                               nautilus_gdu_type,
                               NAUTILUS_TYPE_MENU_PROVIDER,
                               &menu_provider_iface_info);
}

