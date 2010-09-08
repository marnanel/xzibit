/*
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
#include <glib/gstdio.h>
#include <gio/gunixsocketaddress.h>

#include "client-helpers.h"
#include "common.h"

typedef struct
{
  GSocketConnection *connection;
  TpChannel *channel;

  gulong cancelled_id;
  gulong invalidated_id;

  GCancellable *global_cancellable;
  GCancellable *op_cancellable;
  TpProxyPendingCall *offer_call;
  gchar *unix_path;
} CreateTubeData;

static void
unix_path_destroy (gchar *unix_path)
{
  if (unix_path != NULL)
    {
      gchar *p;

      g_unlink (unix_path);
      p = g_strrstr (unix_path, G_DIR_SEPARATOR_S);
      *p = '\0';
      g_rmdir (unix_path);
      g_free (unix_path);
    }
}

static void
create_tube_data_free (CreateTubeData *data)
{
  tp_clear_object (&data->connection);
  tp_clear_object (&data->channel);

  tp_clear_object (&data->global_cancellable);
  tp_clear_object (&data->op_cancellable);
  tp_clear_pointer (&data->unix_path, unix_path_destroy);

  g_slice_free (CreateTubeData, data);
}

static void
create_tube_complete (GSimpleAsyncResult *simple, const GError *error)
{
  CreateTubeData *data;

  data = g_simple_async_result_get_op_res_gpointer (simple);

  if (data->op_cancellable != NULL)
    g_cancellable_cancel (data->op_cancellable);

  if (data->offer_call != NULL)
    tp_proxy_pending_call_cancel (data->offer_call);

  if (data->cancelled_id != 0)
    g_cancellable_disconnect (data->global_cancellable, data->cancelled_id);
  data->cancelled_id = 0;

  if (data->invalidated_id != 0)
    g_signal_handler_disconnect (data->channel, data->invalidated_id);
  data->invalidated_id = 0;

  if (error != NULL)
    g_simple_async_result_set_from_error (simple, error);
  g_simple_async_result_complete_in_idle (simple);
}

static void
create_tube_cancelled_cb (GCancellable *cancellable,
    GSimpleAsyncResult *simple)
{
  CreateTubeData *data;
  GError *error = NULL;

  data = g_simple_async_result_get_op_res_gpointer (simple);

  if (data->cancelled_id != 0)
    g_signal_handler_disconnect (cancellable, data->cancelled_id);
  data->cancelled_id = 0;

  g_assert (g_cancellable_set_error_if_cancelled (cancellable, &error));
  create_tube_complete (simple, error);
  g_clear_error (&error);
}

static void
create_tube_channel_invalidated_cb (TpProxy *proxy,
    guint domain,
    gint code,
    gchar *message,
    GSimpleAsyncResult *simple)
{
    create_tube_complete (simple,
        tp_proxy_get_invalidated (proxy));
}

static void
create_tube_socket_connected_cb (GObject *source_object,
    GAsyncResult *res,
    gpointer user_data)
{
  GSimpleAsyncResult *simple = user_data;
  CreateTubeData *data;
  GSocketListener *listener = G_SOCKET_LISTENER (source_object);
  GError *error = NULL;

  data = g_simple_async_result_get_op_res_gpointer (simple);

  if (g_cancellable_is_cancelled (data->op_cancellable))
    {
      g_object_unref (simple);
      return;
    }

  data->connection = g_socket_listener_accept_finish (listener, res, NULL,
      &error);

  if (data->connection != NULL)
    {
      /* Transfer ownership of unix path */
      g_object_set_data_full (G_OBJECT (data->connection), "unix-path",
          data->unix_path, (GDestroyNotify) unix_path_destroy);
      data->unix_path = NULL;
    }

  create_tube_complete (simple, error);

  g_clear_error (&error);
  g_object_unref (simple);
}

static void
create_tube_offer_cb (TpChannel *channel,
    const GError *error,
    gpointer user_data,
    GObject *weak_object)
{
  GSimpleAsyncResult *simple = user_data;
  CreateTubeData *data;

  data = g_simple_async_result_get_op_res_gpointer (simple);
  data->offer_call = NULL;

  if (error != NULL)
    create_tube_complete (simple, error);
}

static void
create_channel_cb (GObject *acr,
    GAsyncResult *res,
    gpointer user_data)
{
  GSimpleAsyncResult *simple = user_data;
  CreateTubeData *data;
  GSocketListener *listener = NULL;
  gchar *dir;
  GSocket *socket = NULL;
  GSocketAddress *socket_address = NULL;
  GValue *address;
  GHashTable *parameters;
  GError *error = NULL;

  data = g_simple_async_result_get_op_res_gpointer (simple);

  if (g_cancellable_is_cancelled (data->op_cancellable))
    {
      g_object_unref (simple);
      return;
    }

  data->channel = tp_account_channel_request_create_and_handle_channel_finish (
      TP_ACCOUNT_CHANNEL_REQUEST (acr), res, NULL, &error);
   if (data->channel == NULL)
    goto OUT;

  data->invalidated_id = g_signal_connect (data->channel, "invalidated",
      G_CALLBACK (create_tube_channel_invalidated_cb), simple);

  /* We are client side, but we have to offer a socket... So we offer an unix
   * socket on which the service side can connect. We also create an IPv4 socket
   * on which the ssh client can connect. When both sockets are connected,
   * we can forward all communications between them. */

  listener = g_socket_listener_new ();

  /* Create temporary file for our unix socket */
  dir = g_build_filename (g_get_tmp_dir (), "telepathy-ssh-XXXXXX", NULL);
  dir = mkdtemp (dir);
  data->unix_path = g_build_filename (dir, "unix-socket", NULL);
  g_free (dir);

  /* Create the unix socket, and listen for connection on it */
  socket = g_socket_new (G_SOCKET_FAMILY_UNIX, G_SOCKET_TYPE_STREAM,
      G_SOCKET_PROTOCOL_DEFAULT, &error);
  if (socket == NULL)
    goto OUT;
  socket_address = g_unix_socket_address_new (data->unix_path);
  if (!g_socket_bind (socket, socket_address, FALSE, &error))
    goto OUT; 
  if (!g_socket_listen (socket, &error))
    goto OUT;
  if (!g_socket_listener_add_socket (listener, socket, NULL, &error))
    goto OUT;

  g_socket_listener_accept_async (listener, data->op_cancellable,
    create_tube_socket_connected_cb, g_object_ref (simple));

  /* Offer the socket */
  address = tp_address_variant_from_g_socket_address (socket_address,
      TP_SOCKET_ADDRESS_TYPE_UNIX, &error);
  if (address == NULL)
    goto OUT;
  parameters = g_hash_table_new (NULL, NULL);
  data->offer_call = tp_cli_channel_type_stream_tube_call_offer (data->channel,
      -1,
      TP_SOCKET_ADDRESS_TYPE_UNIX, address,
      TP_SOCKET_ACCESS_CONTROL_LOCALHOST, parameters,
      create_tube_offer_cb, g_object_ref (simple), g_object_unref, NULL);
  tp_g_value_slice_free (address);
  g_hash_table_unref (parameters);

OUT:

  if (error != NULL)
    create_tube_complete (simple, error);

  tp_clear_object (&listener);
  tp_clear_object (&socket);
  tp_clear_object (&socket_address);
  g_clear_error (&error);
  g_object_unref (simple);
}

void
_client_create_tube_async (const gchar *account_path,
    const gchar *contact_id,
    GCancellable *cancellable,
    GAsyncReadyCallback callback,
    gpointer user_data)
{
  GSimpleAsyncResult *simple;
  CreateTubeData *data;
  GHashTable *request;
  TpDBusDaemon *dbus;
  TpAccount *account = NULL;
  TpAccountChannelRequest *acr;
  GError *error = NULL;

  if (g_cancellable_is_cancelled (cancellable))
    {
      g_simple_async_report_error_in_idle (NULL, callback,
          user_data, G_IO_ERROR, G_IO_ERROR_CANCELLED,
          "Operation has been cancelled");
      return;
    }

  dbus = tp_dbus_daemon_dup (&error);
  if (dbus != NULL)
    account = tp_account_new (dbus, account_path, &error);
  if (account == NULL)
    {
      g_simple_async_report_gerror_in_idle (NULL, callback, user_data, error);
      g_clear_error (&error);
      tp_clear_object (&dbus);
      return;
    }

  simple = g_simple_async_result_new (NULL, callback, user_data,
      _client_create_tube_finish);

  data = g_slice_new0 (CreateTubeData);
  data->op_cancellable = g_cancellable_new ();
  if (cancellable != NULL)
    {
      data->global_cancellable = g_object_ref (cancellable);
      data->cancelled_id = g_cancellable_connect (data->global_cancellable,
          G_CALLBACK (create_tube_cancelled_cb), simple, NULL);
    }

  g_simple_async_result_set_op_res_gpointer (simple, data,
      (GDestroyNotify) create_tube_data_free);

  request = tp_asv_new (
      TP_PROP_CHANNEL_CHANNEL_TYPE, G_TYPE_STRING,
        TP_IFACE_CHANNEL_TYPE_STREAM_TUBE,
      TP_PROP_CHANNEL_TARGET_HANDLE_TYPE, G_TYPE_UINT,
        TP_HANDLE_TYPE_CONTACT,
      TP_PROP_CHANNEL_TARGET_ID, G_TYPE_STRING,
        contact_id,
      TP_PROP_CHANNEL_TYPE_STREAM_TUBE_SERVICE, G_TYPE_STRING,
        TUBE_SERVICE,
      NULL);

  acr = tp_account_channel_request_new (account, request, G_MAXINT64);
  tp_account_channel_request_create_and_handle_channel_async (acr,
      data->op_cancellable, create_channel_cb, simple);

  g_hash_table_unref (request);
  g_object_unref (dbus);
  g_object_unref (account);
  g_object_unref (acr);
}

GSocketConnection  *
_client_create_tube_finish (GAsyncResult *result,
    TpChannel **channel,
    GError **error)
{
  GSimpleAsyncResult *simple;
  CreateTubeData *data;

  g_return_val_if_fail (G_IS_SIMPLE_ASYNC_RESULT (result), NULL);

  simple = G_SIMPLE_ASYNC_RESULT (result);

  if (g_simple_async_result_propagate_error (simple, error))
    return NULL;

  g_return_val_if_fail (g_simple_async_result_is_valid (result, NULL,
      _client_create_tube_finish), NULL);

  data = g_simple_async_result_get_op_res_gpointer (
      G_SIMPLE_ASYNC_RESULT (result));

  if (channel != NULL)
    *channel = g_object_ref (data->channel);

  return g_object_ref (data->connection);
}

GSocket *
_client_create_local_socket (GError **error)
{
  GSocket *socket = NULL;
  GInetAddress * inet_address = NULL;
  GSocketAddress *socket_address = NULL;

  /* Create the IPv4 socket, and listen for connection on it */
  socket = g_socket_new (G_SOCKET_FAMILY_IPV4, G_SOCKET_TYPE_STREAM,
      G_SOCKET_PROTOCOL_DEFAULT, error);
  if (socket != NULL)
    {
      inet_address = g_inet_address_new_loopback (G_SOCKET_FAMILY_IPV4);
      socket_address = g_inet_socket_address_new (inet_address, 0);
      g_socket_bind (socket, socket_address, FALSE, error);
    }

  tp_clear_object (&inet_address);
  tp_clear_object (&socket_address);

  return socket;
}

GStrv
_client_create_exec_args (GSocket *socket,
    const gchar *contact_id,
    const gchar *username)
{
  GPtrArray *args;
  GSocketAddress *socket_address;
  GInetAddress *inet_address;
  guint16 port;
  gchar *host;
  gchar *str;

  /* Get the local host and port on which sshd is running */
  socket_address = g_socket_get_local_address (socket, NULL);
  inet_address = g_inet_socket_address_get_address (
      G_INET_SOCKET_ADDRESS (socket_address));
  port = g_inet_socket_address_get_port (
      G_INET_SOCKET_ADDRESS (socket_address));
  host = g_inet_address_to_string (inet_address);

  /* Create ssh client args */
  args = g_ptr_array_new_with_free_func (g_free);
  g_ptr_array_add (args, g_strdup ("ssh"));
  g_ptr_array_add (args, host);

  g_ptr_array_add (args, g_strdup ("-p"));
  str = g_strdup_printf ("%d", port);
  g_ptr_array_add (args, str);

  if (contact_id != NULL)
    {
      str = g_strdup_printf ("-oHostKeyAlias=%s", contact_id);
      g_ptr_array_add (args, str);
    }

  if (username != NULL && *username != '\0')
    {
      g_ptr_array_add (args, g_strdup ("-l"));
      g_ptr_array_add (args, g_strdup (username));
    }

  g_ptr_array_add (args, NULL);

  return (gchar **) g_ptr_array_free (args, FALSE);
}

gboolean
_capabilities_has_stream_tube (TpCapabilities *caps)
{
  GPtrArray *classes;
  guint i;

  if (caps == NULL)
    return FALSE;

  classes = tp_capabilities_get_channel_classes (caps);
  for (i = 0; i < classes->len; i++)
    {
      GValueArray *arr = g_ptr_array_index (classes, i);
      GHashTable *fixed;
      const gchar *chan_type;
      const gchar *service;
      TpHandleType handle_type;

      fixed = g_value_get_boxed (g_value_array_get_nth (arr, 0));
      chan_type = tp_asv_get_string (fixed, TP_PROP_CHANNEL_CHANNEL_TYPE);
      service = tp_asv_get_string (fixed,
          TP_PROP_CHANNEL_TYPE_STREAM_TUBE_SERVICE);
      handle_type = tp_asv_get_uint32 (fixed,
          TP_PROP_CHANNEL_TARGET_HANDLE_TYPE, NULL);

      if (!tp_strdiff (chan_type, TP_IFACE_CHANNEL_TYPE_STREAM_TUBE) &&
          handle_type == TP_HANDLE_TYPE_CONTACT &&
          (!tp_capabilities_is_specific_to_contact (caps) ||
           !tp_strdiff (service, TUBE_SERVICE)))
        return TRUE;
    }

  return FALSE;
}
