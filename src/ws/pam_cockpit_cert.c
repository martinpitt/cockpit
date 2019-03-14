/*
 * This file is part of Cockpit.
 *
 * Copyright (C) 2019 Red Hat, Inc.
 *
 * Cockpit is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation; either version 2.1 of the License, or
 * (at your option) any later version.
 *
 * Cockpit is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with Cockpit; If not, see <http://www.gnu.org/licenses/>.
 */

/* Define which PAM interfaces we provide */
#define PAM_SM_AUTH

#include "config.h"

#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <assert.h>
#include <security/pam_appl.h>
#include <security/pam_modules.h>

#include <systemd/sd-bus.h>

int enable_debug = 0;
const char CERT_PREFIX[] = "-----BEGIN CERTIFICATE";

#define debug(format, ...) { if (enable_debug) syslog (LOG_DEBUG, "pam_cockpit_cert: " format, ##__VA_ARGS__); }
#define error(format, ...) syslog (LOG_ERR, "pam_cockpit_cert: " format, ##__VA_ARGS__)

static char*
unescape_newlines (const char *escaped)
{
  char *result = strdup (escaped);
  char *in, *out;

  for (in = out = result; *in != '\0'; ++in)
    {
      if (*in == '\\' && in[1] == 'n')
        {
          *out++ = '\n';
          ++in;
        }
      else
        {
          *out++ = *in;
        }
    }
  *out = '\0';

  return result;
}

/* Parse the module arguments */
static void
parse_args (int argc,
            const char **argv)
{
  for (int i = 0; i < argc; i++)
    {
      if (strcmp (argv[i], "debug") == 0)
        enable_debug = 1;
      else
        error ("invalid option: %s", argv[i]);
    }
}

static int
sssd_map_certificate (const char *certificate, char** username)
{
  int result = PAM_SERVICE_ERR;
  char *unescaped_certificate = NULL;
  sd_bus_error err = SD_BUS_ERROR_NULL;
  sd_bus *bus = NULL;
  sd_bus_message *user_obj_msg = NULL;
  const char *user_obj_path = NULL;
  int r;

  r = sd_bus_open_system (&bus);
  if (r < 0)
    {
      error ("Failed to connect to system bus: %s", strerror (-r));
      result = PAM_AUTHINFO_UNAVAIL;
      goto out;
    }

  unescaped_certificate = unescape_newlines (certificate);

  r = sd_bus_call_method (bus,
                          "org.freedesktop.sssd.infopipe",
                          "/org/freedesktop/sssd/infopipe/Users",
                          "org.freedesktop.sssd.infopipe.Users",
                          "FindByCertificate",
                          &err,
                          &user_obj_msg,
                          "s",
                          unescaped_certificate);

  free (unescaped_certificate);

  if (r < 0)
    {
      /* The error name is a bit confusing, and this is the common case; translate to readable error */
      if (sd_bus_error_has_name (&err, "sbus.Error.NotFound"))
        {
          error ("No matching user for certificate");
          result = PAM_USER_UNKNOWN;
          goto out;
        }

      error ("Failed to map certificate to user: [%s] %s", err.name, err.message);
      result = PAM_AUTHINFO_UNAVAIL;
      goto out;
    }

  assert (user_obj_msg);

  r = sd_bus_message_read (user_obj_msg, "o", &user_obj_path);
  if (r < 0)
    {
      error ("Failed to parse response message: %s", strerror (-r));
      goto out;
    }

  debug ("certificate mapped to user object path %s", user_obj_path);

  r = sd_bus_get_property_string (bus,
                                  "org.freedesktop.sssd.infopipe",
                                  user_obj_path,
                                  "org.freedesktop.sssd.infopipe.Users.User",
                                  "name",
                                  &err,
                                  username);

  if (r < 0)
    {
      error ("Failed to map user object to name: [%s] %s", err.name, err.message);
      goto out;
    }

  assert (*username);
  debug ("mapped certificate to user %s", *username);
  result = PAM_SUCCESS;

out:
  sd_bus_error_free(&err);
  sd_bus_message_unref(user_obj_msg);
  sd_bus_unref(bus);
  return result;
}

PAM_EXTERN int
pam_sm_authenticate (pam_handle_t *pamh,
                     int flags,
                     int argc,
                     const char **argv)
{
  int result = PAM_IGNORE;
  int r;
  const char *pam_user = NULL;
  char *sssd_user = NULL;

  parse_args (argc, argv);

  r = pam_get_item (pamh, PAM_USER, (const void**) &pam_user);
  if (r != PAM_SUCCESS)
    {
      error ("couldn't get pam user: %s", pam_strerror (pamh, r));
      goto out;
    }

  if (!pam_user)
    {
      debug ("user is NULL");
      result = PAM_USER_UNKNOWN;
      goto out;
    }

  /* certificate gets passed as initial user name; but this PAM module is also
   * being used for password auth */
  if (strncmp (pam_user, CERT_PREFIX, sizeof(CERT_PREFIX) - 1) != 0)
    {
      debug ("user %s is not a certificate", pam_user);
      result = PAM_IGNORE;
      goto out;
    }

  result = sssd_map_certificate (pam_user, &sssd_user);

  debug ("sssd user: %s, cert: %s, result: %s", sssd_user, pam_user, pam_strerror (pamh, result));

  /* sssd_user may be NULL here, which is okay -- we want PAM to know it's an unknown user */
  r = pam_set_item (pamh, PAM_USER, sssd_user);
  if (r != PAM_SUCCESS)
    {
      error ("couldn't set pam user: %s", pam_strerror (pamh, r));
      result = r;
      goto out;
    }

out:
  free (sssd_user);
  return result;
}

PAM_EXTERN int
pam_sm_setcred (pam_handle_t *pamh,
                int flags,
                int argc,
                const char *argv[])
{
  return PAM_SUCCESS;
}
