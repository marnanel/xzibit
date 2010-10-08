/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Mutter plugin for xzibit.
 *
 * Author: Thomas Thurman <thomas.thurman@collabora.co.uk>
 *
 * Based on the default Mutter plugin by
 * Tomas Frydrych <tf@linux.intel.com>
 *
 * Based on original ssh-contacts code from Telepathy by
 * Xavier Claessens <xclaesse@gmail.com>
 * 
 * Copyright (c) 2010 Collabora Ltd.
 * Copyright (c) 2008 Intel Corp.
 * Copyright (c) 2010 Xavier Claessens <xclaesse@gmail.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 */

#include "mutter-plugin.h"
#include "vnc.h"
#include <gdk/gdk.h>
#include <gdk/gdkx.h>
#include <X11/extensions/XI2.h>
#include <stdarg.h>
#include <stdlib.h>

#include <libintl.h>
#define _(x) dgettext (GETTEXT_PACKAGE, x)
#define N_(x) x

#include <clutter/clutter.h>
#include <gmodule.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <telepathy-glib/telepathy-glib.h>
#include <gio/gunixsocketaddress.h>

#define XZIBIT_PORT 1770
#define TUBE_SERVICE "x-xzibit"

#define MUTTER_TYPE_XZIBIT_PLUGIN            (mutter_xzibit_plugin_get_type ())
#define MUTTER_XZIBIT_PLUGIN(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), MUTTER_TYPE_XZIBIT_PLUGIN, MutterXzibitPlugin))
#define MUTTER_XZIBIT_PLUGIN_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  MUTTER_TYPE_XZIBIT_PLUGIN, MutterXzibitPluginClass))
#define MUTTER_IS_XZIBIT_PLUGIN(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MUTTER_XZIBIT_PLUGIN_TYPE))
#define MUTTER_IS_XZIBIT_PLUGIN_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  MUTTER_TYPE_XZIBIT_PLUGIN))
#define MUTTER_XZIBIT_PLUGIN_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  MUTTER_TYPE_XZIBIT_PLUGIN, MutterXzibitPluginClass))

#define MUTTER_XZIBIT_PLUGIN_GET_PRIVATE(obj) \
(G_TYPE_INSTANCE_GET_PRIVATE ((obj), MUTTER_TYPE_XZIBIT_PLUGIN, MutterXzibitPluginPrivate))

typedef struct _MutterXzibitPlugin        MutterXzibitPlugin;
typedef struct _MutterXzibitPluginClass   MutterXzibitPluginClass;
typedef struct _MutterXzibitPluginPrivate MutterXzibitPluginPrivate;

static GList *channel_list = NULL;

/**
 * The highest channel we have yet allocated.
 * Channel 0 is always allocated, because it's
 * the control channel.
 */
int highest_channel = 0;

/**
 * The header string that gets set to anyone
 * who connects.
 */
char xzibit_header[] = "Xz 000.001\r\n";

/**
 * The plugin we expose to the outside world.
 * There are no user-servicable parts.
 */
struct _MutterXzibitPlugin
{
  MutterPlugin parent;

  MutterXzibitPluginPrivate *priv;
};

/**
 * The subclass of Mutter plugins which
 * implements Xzibit functionality.
 */
struct _MutterXzibitPluginClass
{
  MutterPluginClass parent_class;
};

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

static void start (MutterPlugin *plugin);
static gboolean xevent_filter (MutterPlugin *plugin,
                               XEvent      *event);

static gboolean copy_top_to_server (GIOChannel *source,
                                    GIOCondition condition,
                                    gpointer data);
static gboolean copy_bottom_to_client (GIOChannel *source,
                                       GIOCondition condition,
                                       gpointer data);
static gboolean accept_connections (GIOChannel *source,
                                    GIOCondition condition,
                                    gpointer data);

static const MutterPluginInfo * plugin_info (MutterPlugin *plugin);

static void window_set_result_property (Display *dpy,
                                        Window window,
                                        guint32 value);

MUTTER_PLUGIN_DECLARE(MutterXzibitPlugin, mutter_xzibit_plugin);

/**
 * Plugin private data.
 */
struct _MutterXzibitPluginPrivate
{
  /**
   * Parent data.
   */
  MutterPluginInfo       info;

  /*
   * File descriptors:
   * They work as follows.   () = fd
   *
   * [xzibit-rfb-client]---()=server_fd
   *                       ||
   *                top_fd=()<--()=listening_fd (on port 1770+display)
   *                        \\  ||
   *                          {TUBES}
   *                            \\
   *                             ()=bottom_fd
   *                             ||
   *            [libvncserver]---()=client_fd (in forwarded_windows*)
   */

  /**
   * File descriptor representing the listening socket
   * itself.
   */
  int listening_fd;

  /**
   * File descriptor connected remotely to the
   * listening socket.
   */
  int bottom_fd;

  /**
   * Forwarded windows, with connections to libvncserver.
   */
  GHashTable *forwarded_windows_by_xzibit_id;
  GHashTable *forwarded_windows_by_x11_id;

  /**
   * X display we're using, if known.
   * (We store this as soon as we know it,
   * but it'll be NULL if we haven't been
   * told yet.)
   */
  Display *dpy;

  /**
   * State of the data received at bottom_fd.
   * Below -4: receiving original header.
   * -4, -3: receiving channel number.
   * -2, -1: receiving block length.
   * Non-negative: "n" through receiving the block.
   */
  int bottom_stage;
  /**
   * Target channel of the block currently being
   * received from bottom_fd, if known.
   */
  int bottom_channel;
  /**
   * Length of the block currently being
   * received from bottom_fd, if known.
   */
  int bottom_length;
  /**
   * Dynamic buffer to hold the block currently
   * being received from bottom_fd; null if we're
   * not currently receiving a block.
   */
  unsigned char *bottom_buffer;
  /**
   * A handle on the bus.
   */
  TpDBusDaemon *dbus;
  /**
   * The Telepathy account we're sending from.
   */
  TpAccount *sending_account;
};

/**
 * Everything we need to know about one instance
 * of xzibit-rfb-client, which is serving one
 * connection from the remote xzibit.
 */
typedef struct _XzibitRfbClient {
  /**
   * The plugin it's associated with.
   */
  MutterPlugin *plugin;

  /**
   * The serial ID of this connection.
   *
   * \todo  Later we need to add a four-byte
   *        space to store the respawn ID.
   */
  unsigned int id;

  /**
   * A TCP socket connected to the remote
   * xzibit.
   */
  int top_fd;

  /**
   * A socket connected to the xzibit-rfb-client
   * program.
   */
  int server_fd;

} XzibitRfbClient;

/**
 * Everything we need to know about one
 * shared window.
 */
typedef struct _ForwardedWindow {
  /**
   * A handle on the Mutter plugin which
   * is holding this object.
   */
  MutterPlugin *plugin;
  /**
   * The xzibit ID of the window we represent.
   */
  int channel;
  /**
   * The X ID of the window we represent.
   */
  Window window;
  /**
   * The file descriptor which connects us to
   * libvncserver.
   */
  int client_fd;

} ForwardedWindow;

typedef struct _XzibitSendingWindow {
  Display *dpy;
  Window window;
  gchar *source;
  gchar *target;

  TpChannel *channel;
  GSocketConnection *tube_connection;

  TpAccount *account;
  int *target_fd;
  MutterPlugin *plugin;

  ForwardedWindow *forward_data;

} XzibitSendingWindow;

/* leave it turned off for now */
#if 0

/**
 * Dumps a stream of data as it's on its way somewhere.
 * Usually this is turned off.
 */
static void
debug_flow (const char *place,
            unsigned char *buffer, int count)
{
  int i;

  g_print ("[%s] %d bytes of data %s:",
           gdk_display_get_name (gdk_display_get_default()),
           count,
           place);

  for (i=0; i<count; i++) {
    g_print (" %02x", buffer[i]);
  }

  g_print ("\n");
}

#define DEBUG_FLOW(place, buffer, count) \
  debug_flow (place, buffer, count);
#else
#define DEBUG_FLOW(place, buffer, count) ;
#endif

/**
 * Destructor.
 */
static void
mutter_xzibit_plugin_dispose (GObject *object)
{
  G_OBJECT_CLASS (mutter_xzibit_plugin_parent_class)->dispose (object);
}

/**
 * Finaliser.
 */
static void
mutter_xzibit_plugin_finalize (GObject *object)
{
  G_OBJECT_CLASS (mutter_xzibit_plugin_parent_class)->finalize (object);
}

/**
 * Theoretically allows us to set properties.
 * In practice we have no properties to set.
 */
static void
mutter_xzibit_plugin_set_property (GObject      *object,
			    guint         prop_id,
			    const GValue *value,
			    GParamSpec   *pspec)
{
  switch (prop_id)
    {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

/**
 * Theoretically allows us to retrieve properties.
 * In practice we have no properties to get.
 */
static void
mutter_xzibit_plugin_get_property (GObject    *object,
			    guint       prop_id,
			    GValue     *value,
			    GParamSpec *pspec)
{
  switch (prop_id)
    {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void channel_invalidated_cb (TpChannel *channel, guint domain, gint code,
    gchar *message, gpointer user_data);

static void
session_complete (TpChannel *channel, const GError *error)
{
  if (error != NULL)
    {
      g_debug ("Error for channel %p: %s", channel,
               error->message);
    }

  g_signal_handlers_disconnect_by_func (channel, channel_invalidated_cb, NULL);
  tp_cli_channel_call_close (channel, -1, NULL, NULL, NULL, NULL);
  channel_list = g_list_remove (channel_list, channel);
  g_object_unref (channel);
}

static void
channel_invalidated_cb (TpChannel *channel,
    guint domain,
    gint code,
    gchar *message,
    gpointer user_data)
{
  session_complete (channel, tp_proxy_get_invalidated (TP_PROXY (channel)));
}

static void
splice_cb (GObject *source_object,
    GAsyncResult *res,
    gpointer channel)
{
  GError *error = NULL;

  _g_io_stream_splice_finish (res, &error);
  session_complete (channel, error);
  g_clear_error (&error);
}

static void
accept_tube_cb (TpChannel *channel,
    const GValue *address,
    const GError *error,
    gpointer user_data,
    GObject *weak_object)
{
  GSocketAddress *socket_address = NULL;
  GInetAddress *inet_address = NULL;
  GSocket *socket = NULL;
  GSocketConnection *tube_connection = NULL;
  GSocketConnection *sshd_connection = NULL;
  GError *err = NULL;

  g_warning ("Accept tube cb\n");

  if (error != NULL)
    {
      session_complete (channel, error);
      return;
    }

  /* Connect to the unix socket we received */
  socket_address = tp_g_socket_address_from_variant (
      TP_SOCKET_ADDRESS_TYPE_UNIX, address, &err);
  if (socket_address == NULL)
    goto OUT;
  socket = g_socket_new (G_SOCKET_FAMILY_UNIX, G_SOCKET_TYPE_STREAM,
      G_SOCKET_PROTOCOL_DEFAULT, &err);
  if (socket == NULL)
    goto OUT;
  if (!g_socket_connect (socket, socket_address, NULL, &err))
    goto OUT;
  tube_connection = g_socket_connection_factory_create_connection (socket);
  tp_clear_object (&socket_address);
  tp_clear_object (&socket);

  /* Connect to the xzibit daemon */
  inet_address = g_inet_address_new_loopback (G_SOCKET_FAMILY_IPV4);
  socket_address = g_inet_socket_address_new (inet_address,
                                              XZIBIT_PORT);
  socket = g_socket_new (G_SOCKET_FAMILY_IPV4, G_SOCKET_TYPE_STREAM,
      G_SOCKET_PROTOCOL_DEFAULT, &err);
  if (socket == NULL)
    goto OUT;
  if (!g_socket_connect (socket, socket_address, NULL, &err))
    goto OUT;
  sshd_connection = g_socket_connection_factory_create_connection (socket);

  /* Splice tube and xzibit connections */
  _g_io_stream_splice_async (G_IO_STREAM (tube_connection),
      G_IO_STREAM (sshd_connection), splice_cb, channel);

OUT:

  if (err != NULL)
    session_complete (channel, err);

  tp_clear_object (&inet_address);
  tp_clear_object (&socket_address);
  tp_clear_object (&socket);
  tp_clear_object (&tube_connection);
  tp_clear_object (&sshd_connection);
  g_clear_error (&err);
}

static void
got_channel_cb (TpSimpleHandler *handler,
    TpAccount *account,
    TpConnection *connection,
    GList *channels,
    GList *requests_satisfied,
    gint64 user_action_time,
    TpHandleChannelsContext *context,
    gpointer user_data)
{
  GValue value = { 0, };
  GList *l;

  g_warning ("Got channel cb\n");

  /* FIXME: Dummy value because passing NULL makes tp-glib crash */
  g_value_init (&value, G_TYPE_STRING);

  for (l = channels; l != NULL; l = l->next)
    {
      TpChannel *channel = l->data;

      /* FIXME: this is a debug print; we should probably remove it */
      if (tp_strdiff (tp_channel_get_channel_type (channel),
          TP_IFACE_CHANNEL_TYPE_STREAM_TUBE))
        {
          g_print ("%s\n", tp_channel_get_channel_type (channel));
          continue;
        }

      channel_list = g_list_prepend (channel_list, g_object_ref (channel));
      g_signal_connect (channel, "invalidated",
          G_CALLBACK (channel_invalidated_cb), NULL);

      tp_cli_channel_type_stream_tube_call_accept (channel, -1,
          TP_SOCKET_ADDRESS_TYPE_UNIX,
          TP_SOCKET_ACCESS_CONTROL_LOCALHOST, &value,
          accept_tube_cb, NULL, NULL, NULL);

    }
  tp_handle_channels_context_accept (context);

  g_value_reset (&value);
}

typedef enum {
  XZIBIT_START_MODE_TUBES,
  XZIBIT_START_MODE_TEST_CLIENT,
  XZIBIT_START_MODE_TEST_SERVER,
} XzibitStartMode;

/**
 * Sets up the whole system and gets us underway.
 *
 * \param plugin  The plugin.
 */
static void
start (MutterPlugin *plugin)
{
  MutterXzibitPluginPrivate *priv   = MUTTER_XZIBIT_PLUGIN (plugin)->priv;
  const gchar *test_command = g_getenv("XZIBIT_TEST");
  XzibitStartMode start_mode = XZIBIT_START_MODE_TUBES;
  GError *error = NULL;

  g_warning ("(xzibit plugin is starting)");

  if (mutter_plugin_debug_mode (plugin))
    {
      g_warning ("(xzibit plugin is in debug mode)");

      /* which means nothing at all at the moment */
    }

  priv->dpy = NULL;

  priv->bottom_stage = -3 - sizeof(xzibit_header);
  priv->bottom_channel = 0;
  priv->bottom_length = 0;
  priv->bottom_buffer = NULL;

  if (!test_command || strcmp (test_command, "")==0)
    start_mode = XZIBIT_START_MODE_TUBES;
  else if (strcmp(test_command, "CLIENT")==0)
    start_mode = XZIBIT_START_MODE_TEST_CLIENT;
  else
    start_mode = XZIBIT_START_MODE_TEST_SERVER;

  g_warning ("In ordinary tubes mode.");

  priv->dbus = tp_dbus_daemon_dup (&error);
  if (priv->dbus == NULL)
    {
      g_error ("Cannot get a handle on D-Bus.");
      return;
    }

  if (start_mode==XZIBIT_START_MODE_TUBES)
    {
      // This is not a test.

      TpBaseClient *client = NULL;
      gboolean success = TRUE;

      client = tp_simple_handler_new (priv->dbus, FALSE, FALSE, "Xzibit",
                                      FALSE, got_channel_cb, NULL, NULL);

      tp_base_client_take_handler_filter (client, tp_asv_new (
                                                              TP_PROP_CHANNEL_CHANNEL_TYPE, G_TYPE_STRING,
                                                              TP_IFACE_CHANNEL_TYPE_STREAM_TUBE,
                                                              TP_PROP_CHANNEL_TARGET_HANDLE_TYPE, G_TYPE_UINT,
                                                              TP_HANDLE_TYPE_CONTACT,
                                                              TP_PROP_CHANNEL_TYPE_STREAM_TUBE_SERVICE, G_TYPE_STRING,
                                                              TUBE_SERVICE,
                                                              TP_PROP_CHANNEL_REQUESTED, G_TYPE_BOOLEAN,
                                                              FALSE,
                                                              NULL));

      if (!tp_base_client_register (client, &error))
        {
          g_error ("Could not register the client.");
          return;
        }
    }

  if (start_mode==XZIBIT_START_MODE_TEST_CLIENT) {
      /* This is a special case for testing
       * on a single host:
       * we don't create a listening socket,
       * but instead we connect to the socket
       * created by the other process running
       * on the same machine.
       */
      priv->listening_fd = -1;

      g_warning ("In testing mode.");
    }
  else
    {
      /*
       * Either we're in ordinary tubes mode,
       * or we're running under the test harness
       * as a server.  Either way, we need to
       * set up the port to listen.
       */

      GIOChannel *channel;
      struct sockaddr_in addr;
      int one = 1;

      memset (&addr, 0, sizeof (addr));
      addr.sin_family = AF_INET;
      addr.sin_addr.s_addr = htonl (INADDR_ANY);
      /* Here we *should* add the number of the current
       * display, but getting this out of Xlib is like
       * getting blood from a stone.  So, maybe in a
       * later version.
       */
      addr.sin_port = htons (XZIBIT_PORT);

      priv->listening_fd = socket (PF_INET,
                                   SOCK_STREAM,
                                   IPPROTO_TCP);

      if (priv->listening_fd < 0)
        {
          perror ("xzibit");
          g_error ("Could not create a socket; things will break\n");
        }

      if (setsockopt (priv->listening_fd,
                      SOL_SOCKET,
                      SO_REUSEADDR,
                      &one, sizeof(one))<0)
        {
          perror ("xzibit");
          g_error ("Could not set socket options.");
        }


      if (bind (priv->listening_fd,
                (struct sockaddr*) &addr,
                sizeof(struct sockaddr_in))<0)
        {
          perror ("xzibit");
          g_error ("Could not bind socket.");
        }

      if (listen (priv->listening_fd,
                  4096)<0)
        {
          perror ("xzibit");
          g_error ("Could not listen on socket.");
        }
        

      channel = g_io_channel_unix_new (priv->listening_fd);

      g_io_add_watch (channel,
                      G_IO_IN,
                      accept_connections,
                      plugin);
    }

  priv->bottom_fd = -1;
  priv->forwarded_windows_by_xzibit_id =
    g_hash_table_new_full (g_int_hash,
                           g_int_equal,
                           g_free,
                           g_free);
  priv->forwarded_windows_by_x11_id =
    g_hash_table_new_full (g_int_hash,
                           g_int_equal,
                           g_free,
                           NULL);
}

/**
 * Constructor of the Xzibit plugin class.
 */
static void
mutter_xzibit_plugin_class_init (MutterXzibitPluginClass *klass)
{
  GObjectClass      *gobject_class = G_OBJECT_CLASS (klass);
  MutterPluginClass *plugin_class  = MUTTER_PLUGIN_CLASS (klass);

  gobject_class->finalize        = mutter_xzibit_plugin_finalize;
  gobject_class->dispose         = mutter_xzibit_plugin_dispose;
  gobject_class->set_property    = mutter_xzibit_plugin_set_property;
  gobject_class->get_property    = mutter_xzibit_plugin_get_property;

  plugin_class->xevent_filter    = xevent_filter;

  g_type_class_add_private (gobject_class, sizeof (MutterXzibitPluginPrivate));
}

/**
 * Constructor of the Xzibit plugin.
 */
static void
mutter_xzibit_plugin_init (MutterXzibitPlugin *self)
{
  MutterXzibitPluginPrivate *priv;

  self->priv = priv = MUTTER_XZIBIT_PLUGIN_GET_PRIVATE (self);

  priv->info.name        = "Xzibit";
  priv->info.version     = "0.1";
  priv->info.author      = "Collabora Ltd.";
  priv->info.license     = "GPL";
  priv->info.description = "Allows you to share windows across IM.";
}

/**
 * Sends a series of bytes from a particular channel from
 * the side that's sending windows.
 *
 * \todo  This isn't used anywhere near as much as I thought
 *        it would be.  Probably it should be replaced by
 *        send_buffer_from_bottom.
 */
static void
send_from_bottom (MutterPlugin *plugin,
                  int channel,
                  ...)
{
  MutterXzibitPluginPrivate *priv   = MUTTER_XZIBIT_PLUGIN (plugin)->priv;
  va_list ap;
  unsigned char *buffer = NULL;
  int count=0, i;

  va_start (ap, channel);
  while (va_arg (ap, int)!=-1)
    {
      count++;
    }
  va_end (ap);

  buffer = g_malloc (count+4);

  va_start (ap, channel);
  for (i=0; i<count; i++)
    {
      buffer[i+4] = (unsigned char) (va_arg (ap, int));
    }
  va_end (ap); 

  /* add in the header */
  buffer[0] = channel % 256;
  buffer[1] = channel / 256;
  buffer[2] = count % 256;
  buffer[3] = count / 256;

  DEBUG_FLOW ("sent normal from BOTTOM towards TOP",
              buffer, count);

  if (write (priv->bottom_fd, buffer, count+4)!=count+4)
    {
      g_warning ("Could not send message; things may break");
    }

  fsync (priv->bottom_fd);

  g_free (buffer);
}

/**
 * Sends the contents of a block of memory to a
 * particular channel from the side that's sending
 * windows.
 */
static void
send_buffer_from_bottom (MutterPlugin *plugin,
                         int channel,
                         unsigned char *buffer,
                         int length)
{
  MutterXzibitPluginPrivate *priv   = MUTTER_XZIBIT_PLUGIN (plugin)->priv;
  char header_buffer[4];

  if (length==-1)
    length = strlen (buffer);

  DEBUG_FLOW ("sent buffer from BOTTOM towards TOP",
              buffer, length);

  header_buffer[0] = channel % 256;
  header_buffer[1] = channel / 256;
  header_buffer[2] = length % 256;
  header_buffer[3] = length / 256;

  if (write (priv->bottom_fd, header_buffer, 4)!=4 ||
          write (priv->bottom_fd, buffer, length)!=length)
    {
      g_warning ("Could not send buffer; things may break");
    }

  fsync (priv->bottom_fd);
}

/**
 * Sends metadata to the control channel, from the side
 * that's sending windows.
 *
 * \param xzibit_id       ID of the window we're talking about
 * \param metadata_type   The type of the metadata (see spec)
 * \param metadata        Pointer to the metadata itself
 * \param metadata_length Length of the metadata
 */
static void
send_metadata_from_bottom (MutterPlugin *plugin,
                           int xzibit_id,
                           int metadata_type,
                           char *metadata,
                           int metadata_length)
{
  MutterXzibitPluginPrivate *priv   = MUTTER_XZIBIT_PLUGIN (plugin)->priv;
  char preamble[9];

  if (metadata_length==-1)
    metadata_length = strlen (metadata);

  DEBUG_FLOW ("sent metadata from BOTTOM towards TOP",
              metadata, metadata_length);

  preamble[0] = 0; /* channel 0, always */
  preamble[1] = 0;
  preamble[2] = (metadata_length+5) % 256;
  preamble[3] = (metadata_length+5) / 256;
  preamble[4] = 3; /* set metadata */
  preamble[5] = xzibit_id % 256;
  preamble[6] = xzibit_id / 256;
  preamble[7] = metadata_type % 256;
  preamble[8] = metadata_type / 256;

  if (write (priv->bottom_fd, preamble,
              sizeof(preamble))!=sizeof(preamble) ||
          write (priv->bottom_fd, metadata,
              metadata_length)!=metadata_length)
    {
      g_warning ("Could not write metadata; things may break");
    }

  fsync (priv->bottom_fd);
}

/**
 * Copies RFB data received from libvncserver,
 * regarding windows which are being sent,
 * to the TCP connection.
 */
static gboolean
copy_client_to_bottom (GIOChannel *source,
                       GIOCondition condition,
                       gpointer data)
{
  ForwardedWindow *forward_data =
    (ForwardedWindow*) data;
  char buffer[1024];
  int count;

  count = recv (g_io_channel_unix_get_fd (source),
                buffer,
                sizeof(buffer),
                MSG_DONTWAIT);

  DEBUG_FLOW ("forwarded from CLIENT to BOTTOM",
              buffer, count);

  if (count<0)
    {
      if (errno==EWOULDBLOCK)
        return;

      g_error ("xzibit bus has died; can't really carry on");
    }

  send_buffer_from_bottom (forward_data->plugin,
                           forward_data->channel,
                           buffer,
                           count);

  return TRUE;
}

/**
 * Finishes sharing a window; this is the second half of share_window().
 * It's either called immediately, if the tube is already open, or later,
 * if the tube wasn't open, when the tube is ready.
 */
static void
share_window_finish (Display *dpy,
                     XzibitSendingWindow* window, MutterPlugin *plugin,
                     int xzibit_id,
                     ForwardedWindow *forward_data)
{
  GIOChannel *channel;
  Atom actual_type;
  int actual_format;
  unsigned long n_items, bytes_after;
  unsigned char *property;
  unsigned char *name_of_window = "";
  unsigned char type_of_window[2] = { 0, 0 };

  /* Kick off VNC as appropriate */

  if (forward_data->client_fd==-1)
    {
      /* Not yet opened: open it */
      vnc_create (window->window);
      forward_data->client_fd = vnc_fd (window->window);
    }

  /* Tell our counterpart about it */

  send_from_bottom (plugin,
                    0, /* control channel */
                    1, /* opcode */
                    xzibit_id % 256,
                    xzibit_id / 256,
                    -1);

  /* If we receive data from VNC, send it on. */

  channel = g_io_channel_unix_new (forward_data->client_fd);
  g_io_add_watch (channel,
                  G_IO_IN,
                  copy_client_to_bottom,
                  forward_data);

  /* also supply metadata */

  /* FIXME: We should also consider WM_NAME */
  if (XGetWindowProperty(dpy,
                         window->window,
                         gdk_x11_get_xatom_by_name ("_NET_WM_NAME"),
                         0,
                         1024,
                         False,
                         gdk_x11_get_xatom_by_name ("UTF8_STRING"),
                         &actual_type,
                         &actual_format,
                         &n_items,
                         &bytes_after,
                         &property)==Success)
    {
      name_of_window = property;
    }

  if (XGetWindowProperty(dpy,
                         window->window,
                         gdk_x11_get_xatom_by_name ("_NET_WM_WINDOW_TYPE"),
                         0,
                         1,
                         False,
                         gdk_x11_get_xatom_by_name ("ATOM"),
                         &actual_type,
                         &actual_format,
                         &n_items,
                         &bytes_after,
                         &property)==Success)
    {
      char *type = XGetAtomName(dpy,
                                *((int*) property));
      int i=0;

      /* FIXME: Presumably that can fail */

      while (window_types[i][0])
        {
          if (strcmp(window_types[i][1], type)==0)
            {
              type_of_window[0] = window_types[i][0][0];
              break;
            }
          i++;
        }
    }

  g_print ("Name of window==[%s]; type==[%s]\n",
             name_of_window,
             type_of_window);

  send_metadata_from_bottom (plugin,
                             xzibit_id,
                             XZIBIT_METADATA_NAME,
                             name_of_window,
                             -1);

  send_metadata_from_bottom (plugin,
                             xzibit_id,
                             XZIBIT_METADATA_TYPE,
                             type_of_window,
                             1);

  /* we don't supply icons yet. */

  /* Now start things going... */

  vnc_start (window->window);

  /* ...and clean up after ourselves. */

  XFree (name_of_window);
  g_free (window);
}

/**
 * From client-helpers.c by Xavier Classens
 * Copyright (C) 2010 Xavier Claessens <xclaesse@gmail.com>
 * Copyright (C) 2010 Collabora Ltd.
 */
static void
create_tube_complete (GSimpleAsyncResult *simple, const GError *error)
{
  CreateTubeData *data;

  g_warning ("CTC 1");
  data = g_simple_async_result_get_op_res_gpointer (simple);
  g_warning ("CTC 2");

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

/**
 * From client-helpers.c by Xavier Classens
 * Copyright (C) 2010 Xavier Claessens <xclaesse@gmail.com>
 * Copyright (C) 2010 Collabora Ltd.
 */
static void
create_tube_offer_cb (TpChannel *channel,
    const GError *error,
    gpointer user_data,
    GObject *weak_object)
{
  GSimpleAsyncResult *simple = user_data;
  CreateTubeData *data;

  g_warning ("CTOC 1");
  data = g_simple_async_result_get_op_res_gpointer (simple);
  g_warning ("CTOC 2");
  data->offer_call = NULL;

  if (error != NULL)
    create_tube_complete (simple, error);
}

/**
 * From client-helpers.c by Xavier Classens
 * Copyright (C) 2010 Xavier Claessens <xclaesse@gmail.com>
 * Copyright (C) 2010 Collabora Ltd.
 */
GSocketConnection*
client_create_tube_finish (GAsyncResult *result,
    TpChannel **channel,
    GError **error)
{
  GSimpleAsyncResult *simple;
  CreateTubeData *data;

  g_warning ("client_create_tube_finish");

  g_return_val_if_fail (G_IS_SIMPLE_ASYNC_RESULT (result), NULL);

  simple = G_SIMPLE_ASYNC_RESULT (result);

  if (g_simple_async_result_propagate_error (simple, error))
    {
      return NULL;
    }

  g_return_val_if_fail (g_simple_async_result_is_valid (result, NULL,
                                                        client_create_tube_finish),
                        NULL);

  g_warning ("Thing 1");
  data = g_simple_async_result_get_op_res_gpointer (
      G_SIMPLE_ASYNC_RESULT (result));
  g_warning ("Thing 2");

  if (channel != NULL)
    *channel = g_object_ref (data->channel);

  return g_object_ref (data->connection);
}

/**
 * Part four of setting up the tube.
 */
static void
create_tube_cb (GObject *source_object,
    GAsyncResult *res,
    gpointer user_data)
{
  XzibitSendingWindow* window = user_data;
  GSocket *socket = NULL;
  int fd = 0;
  GIOChannel *channel;
  GError *error = NULL;

  g_warning ("create_tube_cb");

  window->tube_connection = client_create_tube_finish (res,
                                                       &window->channel,
                                                       &error);

  if (error != NULL)
    {
      g_warning ("Couldn't finish the tube: %s",
                 error->message);
      window_set_result_property (window->dpy, window->window,
                                  301);
      g_clear_error (&error);
      return;
    }

  socket = g_socket_connection_get_socket (window->tube_connection);

  if (socket == NULL)
    {
      g_warning ("The connection had no socket");
      window_set_result_property (window->dpy, window->window,
                                  301);
      return;
    }

  fd = g_socket_get_fd (socket);

  g_warning ("Part four!  The socket is %d", fd);

  /* 
   * So we now have a socket.  Set the result to say
   * that all is well...
   */

  window_set_result_property (window->dpy, window->window,
                              102);

  /*
   * ...and share the window.
   */

  *(window->target_fd) = fd;

  channel = g_io_channel_unix_new (fd);
  g_io_add_watch (channel,
                  G_IO_IN,
                  copy_bottom_to_client,
                  window->plugin);

  share_window_finish (window->dpy,
                       window,
                       window->plugin,
                       window->forward_data->channel,
                       window->forward_data);
}

/**
 * From client-helpers.c by Xavier Classens
 * Copyright (C) 2010 Xavier Claessens <xclaesse@gmail.com>
 * Copyright (C) 2010 Collabora Ltd.
 */
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


/**
 * From client-helpers.c by Xavier Classens
 * Copyright (C) 2010 Xavier Claessens <xclaesse@gmail.com>
 * Copyright (C) 2010 Collabora Ltd.
 */
static void
create_tube_socket_connected_cb (GObject *source_object,
    GAsyncResult *res,
    gpointer user_data)
{
  GSimpleAsyncResult *simple = user_data;
  CreateTubeData *data;
  GSocketListener *listener = G_SOCKET_LISTENER (source_object);
  GError *error = NULL;

  g_warning ("Chose 1");
  data = g_simple_async_result_get_op_res_gpointer (simple);
  g_warning ("Chose 2");

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

/**
 * From client-helpers.c by Xavier Classens
 * Copyright (C) 2010 Xavier Claessens <xclaesse@gmail.com>
 * Copyright (C) 2010 Collabora Ltd.
 */
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

  g_warning ("Beth-yn-galw 1 %p", simple);
  data = g_simple_async_result_get_op_res_gpointer (simple);
  g_warning ("Beth-yn-galw 2");

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
      G_CALLBACK (channel_invalidated_cb), simple);

  /* We are client side, but we have to offer a socket... So we offer an unix
   * socket on which the service side can connect. We also create an IPv4 socket
   * on which the ssh client can connect. When both sockets are connected,
   * we can forward all communications between them. */

  listener = g_socket_listener_new ();

  /* Create temporary file for our unix socket */
  dir = g_build_filename (g_get_tmp_dir (), "xzibit-XXXXXX", NULL);
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


/**
 * Returns whether a set of capabilities contains
 * the capability of streaming tubes.
 *
 * From client-helpers.c by Xavier Classens
 * Copyright (C) 2010 Xavier Claessens <xclaesse@gmail.com>
 * Copyright (C) 2010 Collabora Ltd.
 *
 * \param caps  The capabilities to check.
 * \result TRUE if caps contains the ability to
 *         stream tubes; FALSE otherwise.
 */
gboolean
capabilities_has_stream_tube (TpCapabilities *caps)
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

/**
 * Destroys a CreateTubeData record.
 */
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

/**
 * Part three of setting up the tube.
 */
static void
connection_prepare_cb (GObject *object,
    GAsyncResult *res,
    gpointer user_data)
{
  GSimpleAsyncResult *simple;
  TpConnection *connection = TP_CONNECTION (object);
  XzibitSendingWindow* window = user_data;
  CreateTubeData *data;
  GHashTable *request;
  TpAccountChannelRequest *acr;

  if (!tp_proxy_prepare_finish (TP_PROXY (connection), res, NULL) ||
      !capabilities_has_stream_tube (tp_connection_get_capabilities (connection)))
    {
      g_warning ("Problem finishing preparation of source account");
      window_set_result_property (window->dpy, window->window,
                                  301);
      g_free (window);
      return;
    }

  simple = g_simple_async_result_new (NULL, create_tube_cb,
                                      user_data,
                                      client_create_tube_finish);

  data = g_slice_new0 (CreateTubeData);
  data->op_cancellable = g_cancellable_new ();

  g_warning ("Whatsit 1 %p", simple);
  g_simple_async_result_set_op_res_gpointer (simple, data,
      (GDestroyNotify) create_tube_data_free);
  g_warning ("Whatsit 2");

  request = tp_asv_new (
      TP_PROP_CHANNEL_CHANNEL_TYPE, G_TYPE_STRING,
        TP_IFACE_CHANNEL_TYPE_STREAM_TUBE,
      TP_PROP_CHANNEL_TARGET_HANDLE_TYPE, G_TYPE_UINT,
        TP_HANDLE_TYPE_CONTACT,
      TP_PROP_CHANNEL_TARGET_ID, G_TYPE_STRING,
        window->target,
      TP_PROP_CHANNEL_TYPE_STREAM_TUBE_SERVICE, G_TYPE_STRING,
        TUBE_SERVICE,
      NULL);

  acr = tp_account_channel_request_new (window->account,
                                        request, G_MAXINT64);

  tp_account_channel_request_create_and_handle_channel_async (acr,
      data->op_cancellable, create_channel_cb, simple);
}

/**
 * Part two of setting up the tube.
 */
static void
account_prepare_cb (GObject *object,
                    GAsyncResult *res,
                    gpointer user_data)
{
  TpAccount *account = TP_ACCOUNT (object);
  XzibitSendingWindow* window = user_data;
  GQuark features[] = { TP_CONNECTION_FEATURE_CAPABILITIES, 0 };
  TpConnection *connection;
  GError *error = NULL;

  if (!tp_proxy_prepare_finish (TP_PROXY (account), res, &error))
    {
      g_warning ("Problem creating source account: %s",
                 error->message);
      window_set_result_property (window->dpy, window->window,
                                  301);
      g_free (window);
      return;
    }

  connection = tp_account_get_connection (account);
  if (connection == NULL)
    {
      g_warning ("Account not online");
      /*
       * FIXME: Possibly this should have a different
       * error code from the others.  The account
       * does exist: it's just not online.
       */
      window_set_result_property (window->dpy, window->window,
                                  301);
      g_free (window);
      return;
    }

  /* okay, great, so set up the target account */
  tp_proxy_prepare_async (TP_PROXY (connection), features,
      connection_prepare_cb, window);
}

/**
 * Initiates the sharing of a given window.
 */
static void
share_window (Display *dpy,
              XzibitSendingWindow* window, MutterPlugin *plugin)
{
  MutterXzibitPluginPrivate *priv   = MUTTER_XZIBIT_PLUGIN (plugin)->priv;
  GError *error = NULL;
  unsigned char message[11];
  int xzibit_id = ++highest_channel;
  ForwardedWindow *forward_data;
  int *client_fd, *key;

  g_print ("[%s] Share window %x...",
           gdk_display_get_name (gdk_display_get_default()),
           (int) window->window
           );

  forward_data = g_malloc(sizeof(ForwardedWindow));
  forward_data->plugin = plugin;
  forward_data->channel = xzibit_id;
  forward_data->window = window->window;
  forward_data->client_fd = vnc_fd (window->window);

  key = g_malloc (sizeof (int));
  *key = xzibit_id;

  g_hash_table_insert (priv->forwarded_windows_by_xzibit_id,
                       key,
                       forward_data);

  key = g_malloc (sizeof (int));
  *key = (int) window->window;

  g_hash_table_insert (priv->forwarded_windows_by_x11_id,
                       key,
                       forward_data);

  /* make sure we have the bottom connection */

  if (priv->bottom_fd==-1)
    {
      /*
       * the tube isn't yet open.  Create it,
       * open a connection to it, and put the
       * fd in priv->bottom_fd.
       */

      if (window->source)
        {
          if (!g_str_has_prefix (window->source, TP_ACCOUNT_OBJECT_PATH_BASE))
            {
              gchar *account_id = window->source;
              
              window->source = g_strconcat (TP_ACCOUNT_OBJECT_PATH_BASE,
                                            account_id, NULL);

              g_free (account_id);
            }

          priv->sending_account =
            window->account = tp_account_new (priv->dbus,
                                              window->source, &error);
          if (priv->sending_account == NULL)
            {
              g_warning ("No such sending account: %s", window->source);
              window_set_result_property (dpy, window->window,
                                          301);
              g_free (window);
              return;
            }

          window->forward_data = forward_data;
          window->target_fd = &(priv->bottom_fd);
          window->plugin = plugin;

          tp_proxy_prepare_async (TP_PROXY (priv->sending_account),
                                  NULL,
                                  account_prepare_cb, window);
          return;
        }
      else
        {
          g_warning ("No sending account.");
          window_set_result_property (dpy, window->window,
                                      301);
          g_free (window);
          return;
        }
    }

  share_window_finish (dpy, window, plugin,
                       xzibit_id, forward_data);
}

/**
 * Forces a window to stop being shared.
 * (This is in response to the window closing or
 * having its sharing property removed; we don't
 * need to update anything on the X server.)
 */
static void
unshare_window (Display *dpy,
                Window window, MutterPlugin *plugin)
{
  MutterXzibitPluginPrivate *priv   = MUTTER_XZIBIT_PLUGIN (plugin)->priv;
  ForwardedWindow *fw;

  g_print ("[%s] Unshare window %x...",
           gdk_display_get_name (gdk_display_get_default()),
           (int) window
           );

  fw = g_hash_table_lookup (priv->forwarded_windows_by_x11_id,
                            &window);

  if (!fw)
    return;

  send_from_bottom (plugin,
                    0, /* control channel */
                    2, /* opcode */
                    fw->channel % 256,
                    fw->channel / 256,
                    -1);

  g_hash_table_remove (priv->forwarded_windows_by_x11_id,
                       &window);
  g_hash_table_remove (priv->forwarded_windows_by_xzibit_id,
                       &fw->channel);
}

static void
window_set_result_property (Display *dpy,
                            Window window,
                            guint32 value)
{
  XChangeProperty (dpy,
                   window,
                   gdk_x11_get_xatom_by_name ("_XZIBIT_RESULT"),
                   gdk_x11_get_xatom_by_name ("INTEGER"),
                   32,
                   PropModeReplace,
                   (const unsigned char*) &value,
                   1);
}

XzibitSendingWindow* sending_window_new (Display *dpy,
                                         Window window)
{
  XzibitSendingWindow *result = g_malloc (sizeof (XzibitSendingWindow));
  Atom actual_type;
  int actual_format;
  unsigned long n_items, bytes_after;
  unsigned char *property;
  
  g_warning ("Sending window is %p with a dpy of %p",
             result, dpy);
  result->dpy = dpy;
  result->window = window;

  if (XGetWindowProperty(dpy,
                         window,
                         gdk_x11_get_xatom_by_name ("_XZIBIT_SOURCE"),
                         0,
                         1024,
                         False,
                         gdk_x11_get_xatom_by_name ("UTF8_STRING"),
                         &actual_type,
                         &actual_format,
                         &n_items,
                         &bytes_after,
                         &property)==Success)
    {
      result->source = g_strdup (property);
    }
  else
    {
      result->source = NULL;
    }
  XFree (property);

  if (XGetWindowProperty(dpy,
                         window,
                         gdk_x11_get_xatom_by_name ("_XZIBIT_TARGET"),
                         0,
                         1024,
                         False,
                         gdk_x11_get_xatom_by_name ("UTF8_STRING"),
                         &actual_type,
                         &actual_format,
                         &n_items,
                         &bytes_after,
                         &property)==Success)
    {
      result->target = g_strdup (property);
    }
  else
    {
      result->target = NULL;
    }
  XFree (property);

  return result;
}

/**
 * Responds to a change in a window's sharing property,
 * such as by sharing or unsharing the window.
 */
static void
set_sharing_state (Display *dpy,
                   Window window, int sharing_state, MutterPlugin *plugin)
{
  XzibitSendingWindow *sending;

  if (sharing_state == 2 || sharing_state == 3)
    {
      /* not our concern, but set the response property */
      window_set_result_property (dpy, window,
                                  100 + sharing_state);
      return;
    }

  switch (sharing_state)
    {
    case 1:
      /* we are starting to share this window */
      sending = sending_window_new (dpy, window);
      share_window (dpy, sending, plugin);
      break;

    case 0:
      /* we have stopped sharing this window */
      unshare_window (dpy, window, plugin);
      window_set_result_property (dpy, window,
                                  100);
      break;
    }
}

/**
 * Copies data about received windows from our display
 * program out to the socket.  This data is already
 * in the Xzibit protocol.
 */
static gboolean
copy_server_to_top (GIOChannel *source,
                       GIOCondition condition,
                       gpointer data)
{
  XzibitRfbClient *server_details = (XzibitRfbClient*) data;
  MutterPlugin *plugin = server_details->plugin;
  MutterXzibitPluginPrivate *priv = MUTTER_XZIBIT_PLUGIN (plugin)->priv;
  char buffer[4096];
  int fd = g_io_channel_unix_get_fd (source);
  int count;

  count = read (fd, &buffer, sizeof(buffer));
  if (count<0)
    {
      perror ("xzibit");
      /* FIXME: something more sensible than this.
         Fixing this properly will involve the respawn
         subsystem, which is not something I want to
         do on the first release.
      */
      g_error ("xzibit-rfb-client seems to have died");
    }

  if (count==0)
    {
      return;
    }

  DEBUG_FLOW ("forwarding from SERVER to TOP", buffer, count);

  if (write (server_details->top_fd,
             buffer,
             count)<count)
    {
      g_warning ("Error in writing to our socket.");
    }

  return TRUE;
}

/**
 * Copies data received from our socket about received windows
 * to our display program.  If the display program isn't
 * running, it creates it first.
 */
static gboolean
copy_top_to_server (GIOChannel *source,
                    GIOCondition condition,
                    gpointer data)
{
  XzibitRfbClient *server_details = (XzibitRfbClient*) data;
  MutterPlugin *plugin = server_details->plugin;
  MutterXzibitPluginPrivate *priv = MUTTER_XZIBIT_PLUGIN (plugin)->priv;
  char buffer[1024];
  int count;
  int i;
  int q;

  if (server_details->server_fd == -1)
    {
      /* No remote client?  Make one. */

      char *argvl[6];
      int sockets[2];
      char *fd_as_string,
        *id_as_string;
      GIOChannel *channel;

      socketpair (AF_LOCAL,
                  SOCK_STREAM,
                  0,
                  sockets);

      server_details->server_fd = sockets[0];
      channel = g_io_channel_unix_new (server_details->server_fd);
      g_io_add_watch (channel,
                      G_IO_IN,
                      copy_server_to_top,
                      server_details);

      fd_as_string = g_strdup_printf ("%d",
                                      sockets[1]);
      id_as_string = g_strdup_printf ("%d",
                                      server_details->id);

      argvl[0] = "xzibit-rfb-client";
      argvl[1] = "-f";
      argvl[2] = fd_as_string;
      argvl[3] = "-r";
      argvl[4] = id_as_string;
      argvl[5] = 0;

      g_spawn_async (
                     "/",
                     (gchar**) argvl,
                     NULL,
                     G_SPAWN_SEARCH_PATH|
                     G_SPAWN_LEAVE_DESCRIPTORS_OPEN,
                     NULL, NULL,
                     NULL,
                     NULL /* FIXME: check errors */
                     );

      g_free (fd_as_string);
      g_free (id_as_string);
    }

  count = read (server_details->top_fd,
                buffer,
                sizeof(buffer));

  if (count==0)
    return;

  if (count<0)
    {
      if (errno==EWOULDBLOCK)
        return;

      perror ("xzibit");

      g_error ("xzibit bus has died; can't really carry on");
    }

  DEBUG_FLOW ("received at TOP", buffer, count);

  /* now, if we have an xzibit-rfb-client,
   * write the data out to it.
   */
  if (server_details->server_fd != -1)
    {
      DEBUG_FLOW ("sent to x-r-c", buffer, count);

      if (write (server_details->server_fd,
                  buffer,
                  count)!=count)
        {
          g_warning ("Could not send buffer to display program; "
                  "things may break.");
        }
    }
  else
    {
      g_error ("no server to talk to!");
    }

  return TRUE;
}

/**
 * Handles a connection on the passive socket
 * which has been waiting for someone to connect to it.
 */
static gboolean
accept_connections (GIOChannel *source,
                    GIOCondition condition,
                    gpointer data)
{
  static int highest_id = 0;
  MutterPlugin *plugin = (MutterPlugin*) data;
  MutterXzibitPluginPrivate *priv = MUTTER_XZIBIT_PLUGIN (plugin)->priv;
  GIOChannel *channel;
  XzibitRfbClient *server_details;

  g_print ("Connection on our socket.\n");

  server_details = g_malloc (sizeof (XzibitRfbClient));
  server_details->plugin = plugin;
  server_details->id = ++highest_id;
  /* Setting this to -1 tells the handler to invoke
   * xzibit-rfb-client when needed.
   */
  server_details->server_fd = -1;
  server_details->top_fd = accept (priv->listening_fd, NULL, NULL);

  DEBUG_FLOW ("sending header",
              xzibit_header,
              sizeof(xzibit_header)-1);
  if (write (server_details->top_fd,
              xzibit_header,
              sizeof(xzibit_header)-1 /* no trailing null */)
          !=sizeof(xzibit_header)-1)
    {
      g_warning ("Could not write header to remote socket; "
              "things will break");
    }
  fsync (server_details->top_fd);

  channel = g_io_channel_unix_new (server_details->top_fd);
  g_io_add_watch (channel,
                  G_IO_IN,
                  copy_top_to_server,
                  server_details);

  return TRUE;
}

/**
 * Forwards a block of data for a particular channel to the handler
 * for that channel.  This is a helper function for copy_bottom_to_client,
 * which is concerned with marshalling.
 */
static void
handle_message_to_client (MutterPlugin *plugin,
                          int channel,
                          char *buffer,
                          int length)
{
  MutterXzibitPluginPrivate *priv = MUTTER_XZIBIT_PLUGIN (plugin)->priv;
  ForwardedWindow *fw;

  if (channel==0)
    {
      g_warning ("Possibly a problem: don't know how to deal with ch0 on client messages\n");
      return;
    }

  fw = g_hash_table_lookup (priv->forwarded_windows_by_xzibit_id,
                            &channel);

  if (!fw) {
    g_warning ("Discarding message to channel %d because it doesn't have a handler",
               channel);
    return;
  }

  /* FIXME: error checking; it could run short */

  DEBUG_FLOW ("sent from TOP to CLIENT",
              buffer, length);
  if (write (fw->client_fd, buffer, length)!=length)
    {
      g_warning ("Could not send received data to client; "
              "things will break.");
    }
}

/**
 * Handles any data about received windows
 * arriving from the connection we made to the
 * other side's listening socket.  Also handles
 * recognising the signature at the start.
 */
static gboolean
copy_bottom_to_client (GIOChannel *source,
                       GIOCondition condition,
                       gpointer data)
{
  MutterPlugin *plugin = (MutterPlugin*) data;
  MutterXzibitPluginPrivate *priv = MUTTER_XZIBIT_PLUGIN (plugin)->priv;
  char buffer[1024];
  int count, i;
  Window *xid;
  
  count = read (priv->bottom_fd,
                buffer,
                sizeof(buffer));

  if (count<0)
    {
      perror ("xzibit");
      g_error ("Something downstream died.");
    }

  DEBUG_FLOW ("received at BOTTOM from TOP", buffer, count);
  
  for (i=0; i<count; i++)
    {
      if (count < 0)
        {
          perror ("xzibit");
          /* FIXME */
          g_error ("Something went wrong");
        }

      if (count==0)
        {
          return /* FIXME: TRUE? */;
        }

      switch (priv->bottom_stage)
        {
        case -4:
          priv->bottom_channel = buffer[i];
          break;

        case -3:
          priv->bottom_channel |= buffer[i] * 256;
          break;

        case -2:
          priv->bottom_length = buffer[i];
          break;

        case -1:
          priv->bottom_length |= buffer[i] * 256;
          priv->bottom_buffer = g_malloc (priv->bottom_length);
          break;

        default:
          if (priv->bottom_stage < 0)
            {
              /* still reading the header */
              if (xzibit_header[priv->bottom_stage+
                                sizeof(xzibit_header)+3]!=
                  buffer[i])
                {
                  g_error ("Connected to something that isn't xzibit");
                }
            }
          else
            {
              priv->bottom_buffer[priv->bottom_stage] =
                buffer[i];

              if (priv->bottom_stage==priv->bottom_length-1)
                {
                  handle_message_to_client (plugin,
                                            priv->bottom_channel,
                                            priv->bottom_buffer,
                                            priv->bottom_length);
                  
                  /* and reset */
                  g_free (priv->bottom_buffer);
                  priv->bottom_stage = -5;
                }
            }
        }

      priv->bottom_stage++;
    }

  return TRUE;
}

/**
 * Returns whether a given window is related to a
 * window that's being shared.
 *
 * \param dpy    The display
 * \param window The given window
 * \param relationship  How they might be related
 *       (in practice, usually "WM_TRANSIENT_FOR")
 */
static gboolean
related_to_shared_window (Display *dpy,
                          Window window,
                          gchar *relationship)
{
  Atom actual_type;
  int actual_format;
  unsigned long n_items, bytes_after;
  unsigned char *property;
  guint32 parent, sharing;

  if (XGetWindowProperty(dpy,
                         window,
                         gdk_x11_get_xatom_by_name (relationship),
                         0,
                         4,
                         False,
                         gdk_x11_get_xatom_by_name ("WINDOW"),
                         &actual_type,
                         &actual_format,
                         &n_items,
                         &bytes_after,
                         &property)!=Success)
    {
      g_warning ("We can't tell the %s of %x.\n",
                 relationship, (int)window);
      return FALSE; /* we can't tell */
    }
  if (property == NULL)
    {
      /* g_warning ("%x has no %s.\n", (int)window, relationship); */
      return FALSE; /* no, it isn't */
    }

  parent = *((guint32*) property);
  XFree (property);

  /* g_warning ("%s relation of %x is %x",
             relationship, (int)window,
             parent); */

  if (XGetWindowProperty(dpy,
                         parent,
                         gdk_x11_get_xatom_by_name ("_XZIBIT_SHARE"),
                         0,
                         4,
                         False,
                         gdk_x11_get_xatom_by_name ("CARDINAL"),
                         &actual_type,
                         &actual_format,
                         &n_items,
                         &bytes_after,
                         &property)!=Success)
    {
      /* g_warning ("can't read relationship %s\n", relationship); */
      return FALSE; /* we can't tell */
    }
  
  if (property == NULL)
    {
      /* g_warning ("no sharing on parent, %x", (int)parent); */
      return FALSE; /* no, it isn't */
    }

  sharing = *(guint32*) property;
  XFree (property);

  return sharing==1;
}

/**
 * When a window maps, this function checks whether
 * it's transient to a shared window, and if it is,
 * shares it as well.
 */
static void
share_transiency_on_map (MutterPlugin *plugin,
                         XEvent *event)
{
  MutterXzibitPluginPrivate *priv = MUTTER_XZIBIT_PLUGIN (plugin)->priv;
  XMapEvent *map_event = (XMapEvent*) event;
  int i;
  Window window = map_event->window;
  guint32 transiency;
  guint32 client_leader;
  guint32 *sharing;
  Display *dpy = map_event->display;
  XzibitSendingWindow *sending;

  /* g_warning ("Transiency check for %x starting\n", (int) map_event->window); */

  if (related_to_shared_window (dpy, window, "WM_TRANSIENT_FOR"))
    {
      /* This is what we've been looking for!
       * The window is related to a shared window.
       * It should be shared itself.
       */
      
      guint32 window_is_shared = 1;
            
      XChangeProperty (dpy,
                       window,
                       gdk_x11_get_xatom_by_name ("WM_TRANSIENT_FOR"),
                       gdk_x11_get_xatom_by_name ("CARDINAL"),
                       32,
                       PropModeReplace,
                       (const unsigned char*) &window_is_shared,
                       1);

      sending = sending_window_new (dpy,
                                    window);
            
      share_window (dpy,
                    sending,
                    plugin);
    }
}

/**
 * Makes sure we've recorded the current X display.
 *
 * \param plugin     The plugin.
 * \param known_good The current X display;
 *                   we'll record this if we don't
 *                   already know of one.
 */
static void
ensure_display(MutterPlugin *plugin,
               Display *known_good)
{
  MutterXzibitPluginPrivate *priv = MUTTER_XZIBIT_PLUGIN (plugin)->priv;

  if (priv->dpy==NULL)
    {
      priv->dpy = known_good;
    }
}

/**
 * Called on every X event.  We use this to spy on
 * changes to properties and so on.
 *
 * \param plugin  Our plugin
 * \param event   An X event
 */
static gboolean
xevent_filter (MutterPlugin *plugin, XEvent *event)
{
  MutterXzibitPluginPrivate *priv = MUTTER_XZIBIT_PLUGIN (plugin)->priv;
  int i;
  static gboolean first = TRUE;

  if (first) {
      start (plugin);
      first = FALSE;
  }

  gdk_error_trap_push ();

  switch (event->type)
    {
    case MapNotify:
      {
        ensure_display (plugin, ((XMapEvent*) event)->display);

        share_transiency_on_map (plugin, event);
      }
      break;

    case PropertyNotify:
      {
        XPropertyEvent *property = (XPropertyEvent*) event;
        int new_state = 0;
        Atom xzibit_share_atom = gdk_x11_get_xatom_by_name ("_XZIBIT_SHARE");

        ensure_display (plugin, property->display);
      
        if (property->atom != xzibit_share_atom)
          return FALSE;

        if (property->state == PropertyDelete)
          {
            /* "no value" is equivalent to zero */
            new_state = 0;
          }
        else
          {
            /* so, let's see what they put in there */

            Atom type;
            int format;
            unsigned long nitems, bytes_after;
            unsigned char *value;

            XGetWindowProperty(property->display,
                               property->window,
                               xzibit_share_atom,
                               0, 4, False,
                               AnyPropertyType,
                               &type, &format, &nitems, &bytes_after,
                               &value);

            new_state = *((unsigned long*)value);
            XFree (value);
          }

        set_sharing_state (property->display,
                           property->window,
                           new_state,
                           plugin);
      
      }
      break;

    case KeyPress:
    case KeyRelease:
    case ButtonPress:
    case ButtonRelease:
    case MotionNotify:
      {
        XButtonEvent *button = (XButtonEvent*) event;

        vnc_latestTime = button->time;
        vnc_latestSerial = button->serial;
      }
      break;

    case GenericEvent:
      switch (event->xcookie.type)
        {
        case XI_HierarchyChanged:
          break;
        }
      break;
      
    }

  gdk_error_trap_pop ();

  return FALSE;
}

/**
 * Returns information on this plugin.
 */
static const MutterPluginInfo *
plugin_info (MutterPlugin *plugin)
{
  MutterXzibitPluginPrivate *priv = MUTTER_XZIBIT_PLUGIN (plugin)->priv;

  return &priv->info;
}

/* EOF xzibit-plugin.c */
