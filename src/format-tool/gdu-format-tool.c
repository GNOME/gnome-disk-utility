/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*- */
/*
 *  gnome-disk-utility-format.c
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

#include <glib-object.h>
#include <glib/gi18n-lib.h>
#include <glib/gthread.h>
#include <gio/gio.h>
#include <gdu/gdu.h>
#include <gtk/gtk.h>

#include "gdu-utils.h"
#include "format-window.h"



static gchar *_mount = NULL;
static gchar *_device = NULL;


static gboolean
setup_window ()
{
        GduPresentable *presentable = NULL;

        if (_mount != NULL)
                presentable = find_presentable_from_mount_path (_mount);
        if (_device != NULL && presentable == NULL)
                presentable = find_presentable_from_device_path (_device);

        if (! presentable)
                return FALSE;

        nautilus_gdu_spawn_dialog (presentable);
        g_object_unref (presentable);

        return TRUE;
}



int
main (int argc, char *argv[])
{
        static GOptionEntry entries[] =
                {
                        { "mount", 'm', 0, G_OPTION_ARG_STRING, &_mount, N_("Find volume by mount"), NULL },
                        { NULL }
                };

        GError *error = NULL;

        g_thread_init (NULL);

        /* Initialize gettext support */
        bindtextdomain (GETTEXT_PACKAGE, GNOMELOCALEDIR);
        bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
        textdomain (GETTEXT_PACKAGE);

        /*  Initialize gtk  */
        if (! gtk_init_with_args (&argc, &argv, "[device_file]", entries, GETTEXT_PACKAGE, &error)) {
                g_printerr ("Could not parse arguments: %s\n", error->message);
                g_error_free (error);
                return 1;
        }

        /*  Get the device parameter  */
        if (argc > 0 && argv[1])
                _device = g_strdup (argv[1]);

        if (! setup_window ()) {
                g_printerr ("Could not find presentable\n");
                return 1;
        }

        g_set_prgname ("gnome-disk-utility-format");
        g_set_application_name (_("Gnome Disk Formatter"));

        /*  Run the application  */
        gtk_main ();

        return 0;
}
