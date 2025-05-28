/*
 * Copyright (C) 2008-2013 Red Hat, Inc.
 *
 * Licensed under GPL version 2 or later.
 *
 * Author: David Zeuthen <zeuthen@gmail.com>
 */

#include "config.h"
#include <glib/gi18n.h>

#include <adwaita.h>

#include "gdu-application.h"
#include "gdu-log.h"

int
main (int argc, char *argv[])
{
  g_autoptr(GtkApplication) app = NULL;

  gdu_log_init ();

  /* Initialize gettext support */
  bindtextdomain (GETTEXT_PACKAGE, GNOMELOCALEDIR);
  bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
  textdomain (GETTEXT_PACKAGE);

  app = gdu_application_new ();

  return g_application_run (G_APPLICATION (app), argc, argv);
}
