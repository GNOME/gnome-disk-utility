/*
 * Copyright (C) 2017 Kai Lüke
 *
 * Licensed under GPL version 2 or later.
 *
 * Author: Kai Lüke <kailueke@riseup.net>
 */

#pragma once

#include <gtk/gtk.h>
#include "gdutypes.h"

#include <pwquality.h>

G_BEGIN_DECLS

#define GDU_TYPE_CREATE_PASSWORD_PAGE gdu_create_password_page_get_type ()
G_DECLARE_FINAL_TYPE (GduCreatePasswordPage, gdu_create_password_page, GDU, CREATE_PASSWORD_PAGE, AdwBin)

GduCreatePasswordPage *gdu_create_password_page_new          (void);

const gchar *          gdu_create_password_page_get_password (GduCreatePasswordPage *page);

G_END_DECLS

static const gchar *
pw_error_hint (gint error)
{
  switch (error)
    {
      case PWQ_ERROR_CASE_CHANGES_ONLY:
        return C_("Password hint", "Try changing some letters and numbers.");
      case PWQ_ERROR_TOO_SIMILAR:
        return C_("Password hint", "Try changing the password a bit more.");
      case PWQ_ERROR_BAD_WORDS:
        return C_("Password hint", "Try to avoid some of the words included in the password.");
      case PWQ_ERROR_ROTATED:
        return C_("Password hint", "Try changing the password a bit more.");
      case PWQ_ERROR_CRACKLIB_CHECK:
        return C_("Password hint", "Try to avoid common words.");
      case PWQ_ERROR_PALINDROME:
        return C_("Password hint", "Try to avoid reordering existing words.");
      case PWQ_ERROR_MIN_DIGITS:
        return C_("Password hint", "Try to use more numbers.");
      case PWQ_ERROR_MIN_UPPERS:
        return C_("Password hint", "Try to use more uppercase letters.");
      case PWQ_ERROR_MIN_LOWERS:
        return C_("Password hint", "Try to use more lowercase letters.");
      case PWQ_ERROR_MIN_OTHERS:
        return C_("Password hint", "Try to use more special characters, like punctuation.");
      case PWQ_ERROR_MIN_CLASSES:
        return C_("Password hint", "Try to use a mixture of letters, numbers and punctuation.");
      case PWQ_ERROR_MAX_CONSECUTIVE:
        return C_("Password hint", "Try to avoid repeating the same character.");
      case PWQ_ERROR_MAX_CLASS_REPEAT:
        return C_("Password hint", "Try to avoid repeating the same type of character: you need to mix up letters, numbers and punctuation.");
      case PWQ_ERROR_MAX_SEQUENCE:
        return C_("Password hint", "Try to avoid sequences like 1234 or abcd.");
      case PWQ_ERROR_EMPTY_PASSWORD:
        return C_("Password hint", "Mix uppercase and lowercase and try to use a number or two.");
      default:
        return C_("Password hint", "Adding more letters, numbers and punctuation will make the password stronger.");
    }
}

static pwquality_settings_t *
get_pwq (void)
{
  static pwquality_settings_t *settings = NULL;

  if (settings == NULL)
    {
      gchar *err = NULL;
      settings = pwquality_default_settings ();
      if (pwquality_read_config (settings, NULL, (gpointer)&err) < 0)
        {
          g_error ("Failed to read pwquality configuration: %s\n", err);
        }
    }

  return settings;
}

static gdouble
pw_strength (const gchar  *password,
             const gchar **hint,
             gint         *strength_level)
{
  gint rv, level, length = 0;
  gdouble strength = 0.0;
  void *auxerror;

  rv = pwquality_check (get_pwq (),
                        password,
                        NULL, /* old_password */
                        NULL, /* username */
                        &auxerror);

  if (password != NULL)
    length = strlen (password);

  strength = CLAMP (0.01 * rv, 0.0, 1.0);
  if (rv < 0) {
    level = (length > 0) ? 1 : 0;
  } else if (strength < 0.50) {
    level = 2;
  } else if (strength < 0.75) {
    level = 3;
  } else if (strength < 0.90) {
    level = 4;
  } else {
    level = 5;
  }

  
  *hint = pw_error_hint (rv);

  if (strength_level)
    *strength_level = level;

  return strength;
}
