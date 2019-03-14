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

/*
 * This program opens a session for a given user and runs the bridge in
 * it, using TLS client certificate authentication and mapping it to a user
 * through sssd (https://www.freeipa.org/page/V4/User_Certificates).
 */

#include "config.h"
#include <stdlib.h>

#include "session-utils.h"

static char *last_txt_msg = NULL;
static char *conversation = NULL;


static int
pam_conv_func (int num_msg,
               const struct pam_message **msg,
               struct pam_response **ret_resp,
               void *appdata_ptr)
{
  /* we don't expect (nor can handle) any actual auth conversation here, but
   * PAM sometimes sends messages like "Creating home directory for USER" */
  for (int i = 0; i < num_msg; ++i)
      debug ("got PAM conversation message, ignoring: %s", msg[i]->msg);
  return PAM_CONV_ERR;
}

static pam_handle_t *
perform_tlscert (const char *rhost,
                 const char *auth_value)
{
  struct pam_conv conv = { pam_conv_func, };
  pam_handle_t *pamh;
  int res;

  if (!auth_value || !*auth_value )
    errx (EX, "empty authentication value");

  /* FIXME: This is insecure! This blindly trusts the certificate that
   * cockpit-ws sent us, but we don't want to trust ws. This needs to come from
   * a secure source. */
  debug ("start tls-cert authentication with %s", auth_value);

  /* Set the cert as the initial user name; pam_cockpit_cert changes it to the real one */
  res = pam_start ("cockpit", auth_value, &conv, &pamh);
  if (res != PAM_SUCCESS)
    errx (EX, "couldn't start pam: %s", pam_strerror (NULL, res));

  if (pam_set_item (pamh, PAM_RHOST, rhost) != PAM_SUCCESS)
    errx (EX, "couldn't setup pam rhost");

  res = pam_authenticate (pamh, 0);
  if (res == PAM_SUCCESS)
    res = open_session (pamh);

  /* Our exit code is a PAM code */
  if (res != PAM_SUCCESS)
    exit_init_problem (res);

  return pamh;
}

static int
session (char **env)
{
  char *argv[] = { "cockpit-bridge", NULL };
  debug ("executing bridge: %s", argv[0]);

  if (env)
    execvpe (argv[0], argv, env);
  else
    execvp (argv[0], argv);

  warn ("can't exec %s", argv[0]);
  return EX;
}

int
main (int argc,
      char **argv)
{
  pam_handle_t *pamh = NULL;
  const char *rhost;
  char *authorization;
  char *type = NULL;
  const char *auth_value;
  char **env;
  int status;
  int res;
  int i;

  program_name = basename (argv[0]);

  if (isatty (0))
    errx (2, "this command is not meant to be run from the console");

  if (argc != 2)
    errx (2, "invalid arguments to cockpit-cert-session");

  /* Cleanup the umask */
  umask (077);

  rhost = getenv ("COCKPIT_REMOTE_PEER") ?: "";

  save_environment ();

  /* When setuid root, make sure our group is also root */
  if (geteuid () == 0)
    {
      /* Always clear the environment */
      if (clearenv () != 0)
        err (1, "couldn't clear environment");

      /* set a minimal environment */
      setenv ("PATH", DEFAULT_PATH, 1);

      if (setgid (0) != 0 || setuid (0) != 0)
        err (1, "couldn't switch permissions correctly");
    }

  signal (SIGALRM, SIG_DFL);
  signal (SIGQUIT, SIG_DFL);
  signal (SIGTSTP, SIG_IGN);
  signal (SIGHUP, SIG_IGN);
  signal (SIGPIPE, SIG_IGN);

  cockpit_authorize_logger (authorize_logger, DEBUG_SESSION);

  /* Request authorization header */
  write_authorize_begin ();
  write_control_string ("challenge", "*");
  write_control_end ();

  /* And get back the authorization header */
  authorization = read_authorize_response ("authorization");
  auth_value = cockpit_authorize_type (authorization, &type);
  if (!auth_value)
    errx (EX, "invalid authorization header received");
  if (strcmp (type, "tls-cert") != 0)
    errx (2, "This authentication command only supports tls-cert");

  pamh = perform_tlscert (rhost, auth_value);

  cockpit_memory_clear (authorization, -1);
  free (authorization);
  free (type);

  for (i = 0; env_saved[i] != NULL; i++)
    pam_putenv (pamh, env_saved[i]);

  env = pam_getenvlist (pamh);
  if (env == NULL)
    errx (EX, "get pam environment failed");

  if (want_session)
    {
      assert (pwd != NULL);

      if (initgroups (pwd->pw_name, pwd->pw_gid) < 0)
        err (EX, "%s: can't init groups", pwd->pw_name);

      signal (SIGTERM, pass_to_child);
      signal (SIGINT, pass_to_child);
      signal (SIGQUIT, pass_to_child);

      utmp_log (1, rhost);

      status = fork_session (env, session);

      utmp_log (0, rhost);

      signal (SIGTERM, SIG_DFL);
      signal (SIGINT, SIG_DFL);
      signal (SIGQUIT, SIG_DFL);

      res = pam_setcred (pamh, PAM_DELETE_CRED);
      if (res != PAM_SUCCESS)
        err (EX, "%s: couldn't delete creds: %s", pwd->pw_name, pam_strerror (pamh, res));
      res = pam_close_session (pamh, 0);
      if (res != PAM_SUCCESS)
        err (EX, "%s: couldn't close session: %s", pwd->pw_name, pam_strerror (pamh, res));
    }
  else
    {
      status = session (env);
    }

  pam_end (pamh, PAM_SUCCESS);

  free (last_err_msg);
  last_err_msg = NULL;
  free (last_txt_msg);
  last_txt_msg = NULL;
  free (conversation);
  conversation = NULL;

  if (WIFEXITED(status))
    exit (WEXITSTATUS(status));
  else if (WIFSIGNALED(status))
    raise (WTERMSIG(status));
  else
    exit (EX);
}
