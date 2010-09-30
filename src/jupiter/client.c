/*
 * xzibit-jupiter - basic client for xzibit,
 *                  testing ability to create tubes.
 *
 * Copyright (C) 2010 Collabora Ltd.
 * Based on telepathy-ssh:
 * Copyright (C) 2010 Xavier Claessens <xclaesse@gmail.com>
 * Copyright (C) 2010 Collabora Ltd.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA  02110-1301  USA
 */

#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <glib/gstdio.h>
#include <gio/gunixsocketaddress.h>
#include <telepathy-glib/telepathy-glib.h>

#include "client-helpers.h"
#include "common.h"
#include "jupiter.h"

typedef struct
{
  GMainLoop *loop;
  gchar *argv0;

  gchar *account_path;
  gchar *contact_id;

  GList *accounts;
  guint n_readying_connections;
  TpAccount *account;

  TpChannel *channel;
  GSocketConnection *tube_connection;

  gboolean account_set:1;
  gboolean contact_set:1;
  gboolean success:1;
} ClientContext;

static void
throw_error_message (ClientContext *context,
    const gchar *message)
{
  g_print ("Error: %s\n", message);
  context->success = FALSE;
  g_main_loop_quit (context->loop);
}

static void
throw_error (ClientContext *context,
    const GError *error)
{
  throw_error_message (context, error ? error->message : "No error message");
}

static void
ssh_client_watch_cb (GPid pid,
    gint status,
    gpointer user_data)
{
  ClientContext *context = user_data;

  g_main_loop_quit (context->loop);
  g_spawn_close_pid (pid);
}

static void
splice_cb (GObject *source_object,
    GAsyncResult *res,
    gpointer user_data)
{
  ClientContext *context = user_data;
  GError *error = NULL;

  if (!_g_io_stream_splice_finish (res, &error))
    throw_error (context, error);
  else
    g_main_loop_quit (context->loop);

  g_clear_error (&error);
}

static void
create_tube_cb (GObject *source_object,
    GAsyncResult *res,
    gpointer user_data)
{
  ClientContext *context = user_data;
  GSocket *socket;
  GPid pid;
  GError *error = NULL;
  int fd;

  context->tube_connection = _client_create_tube_finish (res, &context->channel,
          &error);

  if (error != NULL)
    {
      throw_error (context, error);
      g_clear_error (&error);
      return;
    }

  socket = g_socket_connection_get_socket (context->tube_connection);

  if (socket == NULL)
    {
      /* FIXME: unlikely, but we should throw an error or something here */
      g_warning ("The connection had no socket");
      return;
    }

  fd = g_socket_get_fd (socket);

  set_up_jupiter (fd);
}

static void
start_tube (ClientContext *context)
{
  if (!context->account_set || !context->contact_set)
    {
      g_print ("\nTo avoid interactive mode, you can use that command:\n"
          "%s --account %s --contact %s\n", context->argv0,
          context->account_path, context->contact_id);
    }

  _client_create_tube_async (context->account_path,
    context->contact_id, NULL, create_tube_cb, context);
}

static void
got_contacts_cb (TpConnection *connection,
    guint n_contacts,
    TpContact * const *contacts,
    guint n_failed,
    const TpHandle *failed,
    const GError *error,
    gpointer user_data,
    GObject *weak_object)
{
  ClientContext *context = user_data;
  guint i;
  GList *candidates = NULL, *l;
  guint count = 0;
  gchar buffer[10];
  gchar *str;

  if (error != NULL)
    {
      throw_error (context, error);
      return;
    }

  /* Build a list of all contacts supporting StreamTube */
  for (i = 0; i < n_contacts; i++)
    if (_capabilities_has_stream_tube (tp_contact_get_capabilities (contacts[i])))
      candidates = g_list_prepend (candidates, contacts[i]);

  if (candidates == NULL)
    {
      throw_error_message (context, "No suitable contact");
      return;
    }

  /* Ask the user which candidate to use */
  for (l = candidates; l != NULL; l = l->next)
    {
      TpContact *contact = l->data;

      g_print ("%d) %s (%s)\n", ++count, tp_contact_get_alias (contact),
          tp_contact_get_identifier (contact));
    }

  g_print ("Which contact to use? ");
  str = fgets (buffer, sizeof (buffer), stdin);
  if (str != NULL)
    {
      str[strlen (str) - 1] = '\0';
      l = g_list_nth (candidates, atoi (str) - 1);
    }
  if (l == NULL)
    {
      throw_error_message (context, "Invalid contact number");
      return;
    }

  context->contact_id = g_strdup (tp_contact_get_identifier (l->data));
  start_tube (context);

  g_list_free (candidates);
}

static void
stored_channel_prepare_cb (GObject *object,
    GAsyncResult *res,
    gpointer user_data)
{
  ClientContext *context = user_data;
  TpChannel *channel = TP_CHANNEL (object);
  TpConnection *connection;
  TpContactFeature features[] = { TP_CONTACT_FEATURE_ALIAS,
      TP_CONTACT_FEATURE_CAPABILITIES };
  const TpIntSet *set;
  GArray *handles;
  GError *error = NULL;

  if (!tp_proxy_prepare_finish (channel, res, &error))
    {
      throw_error (context, error);
      g_clear_error (&error);
      return;
    }

  connection = tp_channel_borrow_connection (channel);
  set = tp_channel_group_get_members (channel);
  handles = tp_intset_to_array (set);

  tp_connection_get_contacts_by_handle (connection, handles->len,
      (TpHandle *) handles->data, G_N_ELEMENTS (features), features,
      got_contacts_cb, context, NULL, NULL);

  g_array_unref (handles);
}

static void
ensure_stored_channel_cb (TpConnection *connection,
    gboolean yours,
    const gchar *channel_path,
    GHashTable *properties,
    const GError *error,
    gpointer user_data,
    GObject *weak_object)
{
  ClientContext *context = user_data;
  TpChannel *channel;
  GQuark features[] = { TP_CHANNEL_FEATURE_GROUP, 0 };
  GError *err = NULL;

  if (error != NULL)
    {
      throw_error (context, error);
      return;
    }

  channel = tp_channel_new_from_properties (connection, channel_path,
      properties, &err);
  if (channel == NULL)
    {
      throw_error (context, err);
      g_clear_error (&err);
      return;
    }

  tp_proxy_prepare_async (TP_PROXY (channel), features,
      stored_channel_prepare_cb, context);

  g_object_unref (channel);
}

static void
chooser_contact (ClientContext *context)
{
  TpConnection *connection;
  GHashTable *request;

  /* If a contact ID was passed in the options, use it */
  if (context->contact_set)
    {
      g_assert (context->contact_id != NULL);
      start_tube (context);
      return;
    }

  /* Otherwise, we'll get TpContact objects for all stored contacts on that
   * account. */
  request = tp_asv_new (
      TP_PROP_CHANNEL_CHANNEL_TYPE, G_TYPE_STRING,
        TP_IFACE_CHANNEL_TYPE_CONTACT_LIST,
      TP_PROP_CHANNEL_TARGET_HANDLE_TYPE, G_TYPE_UINT,
        TP_HANDLE_TYPE_LIST,
      TP_PROP_CHANNEL_TARGET_ID, G_TYPE_STRING,
        "stored",
      NULL);

  connection = tp_account_get_connection (context->account);
  tp_cli_connection_interface_requests_call_ensure_channel (connection, -1,
      request, ensure_stored_channel_cb, context, NULL, NULL);

  g_hash_table_unref (request);
}

static void
chooser_account (ClientContext *context)
{
  GList *l;
  guint count = 0;
  gchar buffer[10];
  gchar *str;

  if (context->accounts == NULL)
    {
      throw_error_message (context, "No suitable account");
      return;
    }

  if (context->account_set)
    {
      g_assert (context->account != NULL);
      g_assert (context->account_path != NULL);
      chooser_contact (context);
      return;
    }

  for (l = context->accounts; l != NULL; l = l->next)
    {
      g_print ("%d) %s (%s)\n", ++count,
          tp_account_get_display_name (l->data),
          tp_account_get_protocol (l->data));
    }

  g_print ("Which account to use? ");
  str = fgets (buffer, sizeof (buffer), stdin);
  if (str != NULL)
    {
      str[strlen (str) - 1] = '\0';
      l = g_list_nth (context->accounts, atoi (str) - 1);
    }
  if (l == NULL)
    {
      throw_error_message (context, "Invalid account number");
      return;
    }

  context->account = g_object_ref (l->data);
  context->account_path = g_strdup (tp_proxy_get_object_path (context->account));

  chooser_contact (context);
}

static void
connection_prepare_cb (GObject *object,
    GAsyncResult *res,
    gpointer user_data)
{
  TpConnection *connection = TP_CONNECTION (object);
  ClientContext *context = user_data;

  if (!tp_proxy_prepare_finish (TP_PROXY (connection), res, NULL) ||
      !_capabilities_has_stream_tube (tp_connection_get_capabilities (connection)))
    {
      GList *l;

      /* Remove the account that has that connection from the list */
      for (l = context->accounts; l != NULL; l = l->next)
        if (tp_account_get_connection (l->data) == connection)
          {
            g_object_unref (l->data);
            context->accounts = g_list_delete_link (context->accounts, l);
            break;
          }
    }

  if (--context->n_readying_connections == 0)
    chooser_account (context);
}

static void
account_manager_prepare_cb (GObject *object,
    GAsyncResult *res,
    gpointer user_data)
{
  TpAccountManager *manager = TP_ACCOUNT_MANAGER (object);
  ClientContext *context = user_data;
  GList *l, *next;
  GError *error = NULL;

  if (!tp_proxy_prepare_finish (TP_PROXY (manager), res, &error))
    {
      throw_error (context, error);
      g_clear_error (&error);
      return;
    }

  /* We want to list all accounts which has a connection that have StreamTube
   * support. So first we prepare all connections, and we keep in
   * context->accounts those that are suitable. */

  context->accounts = tp_account_manager_get_valid_accounts (manager);
  g_list_foreach (context->accounts, (GFunc) g_object_ref, NULL);

  for (l = context->accounts; l != NULL; l = next)
    {
      GQuark features[] = { TP_CONNECTION_FEATURE_CAPABILITIES, 0 };
      TpAccount *account = l->data;
      TpConnection *connection;

      next = l->next;

      connection = tp_account_get_connection (account);
      if (connection == NULL)
        {
          g_object_unref (account);
          context->accounts = g_list_delete_link (context->accounts, l);
          continue;
        }

      context->n_readying_connections++;
      tp_proxy_prepare_async (TP_PROXY (connection), features,
          connection_prepare_cb, context);
    }

  if (context->n_readying_connections == 0)
    chooser_account (context);
}

static void
account_prepare_cb (GObject *object,
    GAsyncResult *res,
    gpointer user_data)
{
  TpAccount *account = TP_ACCOUNT (object);
  ClientContext *context = user_data;
  GQuark features[] = { TP_CONNECTION_FEATURE_CAPABILITIES, 0 };
  TpConnection *connection;
  GError *error = NULL;

  /* We are in the case where an account was specified in options, so we have
   * only one candidate, if that accounts has no connection or the connection
   * has no StreamTube support, we'll fail. */

  if (!tp_proxy_prepare_finish (TP_PROXY (account), res, &error))
    {
      throw_error (context, error);
      return;
    }

  connection = tp_account_get_connection (account);
  if (connection == NULL)
    {
      throw_error_message (context, "Account not online");
      return;
    }

  /* Prepare account's connection with caps feature */
  context->accounts = g_list_prepend (NULL, g_object_ref (account));
  context->n_readying_connections = 1;
  tp_proxy_prepare_async (TP_PROXY (connection), features,
      connection_prepare_cb, context);
}

static void
client_context_clear (ClientContext *context)
{
  tp_clear_pointer (&context->loop, g_main_loop_unref);
  g_free (context->argv0);
  g_free (context->account_path);
  g_free (context->contact_id);

  g_list_foreach (context->accounts, (GFunc) g_object_unref, NULL);
  g_list_free (context->accounts);
  tp_clear_object (&context->account);

  tp_clear_object (&context->channel);
  tp_clear_object (&context->tube_connection);
}

int
main (gint argc, gchar *argv[])
{
  TpDBusDaemon *dbus = NULL;
  GError *error = NULL;
  ClientContext context = { 0, };
  GOptionContext *optcontext;
  GOptionEntry options[] = {
      { "account", 'a',
        0, G_OPTION_ARG_STRING, &context.account_path,
        "The account ID",
        NULL },
      { "contact", 'c',
        0, G_OPTION_ARG_STRING, &context.contact_id,
        "The contact ID",
        NULL },
      { NULL }
  };

  g_type_init ();

  optcontext = g_option_context_new ("- ssh-contact");
  g_option_context_add_main_entries (optcontext, options, NULL);
  if (!g_option_context_parse (optcontext, &argc, &argv, &error))
    {
      g_print ("%s\nRun '%s --help' to see a full list of available command "
          "line options.\n", error->message, argv[0]);
      return EXIT_FAILURE;
    }
  g_option_context_free (optcontext);

  g_set_application_name (PACKAGE_NAME);

  dbus = tp_dbus_daemon_dup (&error);
  if (dbus == NULL)
    goto OUT;

  context.argv0 = g_strdup (argv[0]);
  if (context.account_path != NULL)
    context.account_set = TRUE;
  if (context.contact_id != NULL)
    context.contact_set = TRUE;

  /* If an account id was specified in options, then prepare it, otherwise
   * we get the account manager to get a list of all accounts */
  if (context.account_set)
    {
      if (!g_str_has_prefix (context.account_path, TP_ACCOUNT_OBJECT_PATH_BASE))
        {
          gchar *account_id = context.account_path;

          context.account_path = g_strconcat (TP_ACCOUNT_OBJECT_PATH_BASE,
            account_id, NULL);

          g_free (account_id);
        }

      context.account = tp_account_new (dbus, context.account_path, &error);
      if (context.account == NULL)
        goto OUT;

      tp_proxy_prepare_async (TP_PROXY (context.account), NULL,
          account_prepare_cb, &context);
    }
  else
    {
      TpAccountManager *manager;

      manager = tp_account_manager_new (dbus);
      tp_proxy_prepare_async (TP_PROXY (manager), NULL,
          account_manager_prepare_cb, &context);

      g_object_unref (manager);
    }

  context.loop = g_main_loop_new (NULL, FALSE);
  g_main_loop_run (context.loop);

OUT:

  if (error != NULL)
    {
      context.success = FALSE;
      g_debug ("Error: %s", error->message);
    }

  tp_clear_object (&dbus);
  g_clear_error (&error);
  client_context_clear (&context);

  return context.success ? EXIT_SUCCESS : EXIT_FAILURE;
}

