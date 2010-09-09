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

#define XZIBIT_PORT 1770

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

/**
 * Sets up the whole system and gets us underway.
 *
 * \param plugin  The plugin.
 */
static void
start (MutterPlugin *plugin)
{
  MutterXzibitPluginPrivate *priv   = MUTTER_XZIBIT_PLUGIN (plugin)->priv;
  gchar *test_command = g_getenv("XZIBIT_TEST");

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

  if (!test_command)
    {
      // This is not a test.
      //
      // FIXME: fill this in.
    }
  else if (strcmp(test_command, "CLIENT")==0)
    {
      /* This is a special case for testing
       * on a single host:
       * we don't create a listening socket,
       * but instead we connect to the socket
       * created by the other process running
       * on the same machine.
       */
      priv->listening_fd = -1;
    }
  else
    {
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
 * Initiates the sharing of a given window.
 */
static void
share_window (Display *dpy,
              Window window, MutterPlugin *plugin)
{
  MutterXzibitPluginPrivate *priv   = MUTTER_XZIBIT_PLUGIN (plugin)->priv;
  GError *error = NULL;
  unsigned char message[11];
  Atom actual_type;
  int actual_format;
  unsigned long n_items, bytes_after;
  unsigned char *property;
  unsigned char *name_of_window = "";
  unsigned char type_of_window[2] = { 0, 0 };
  int xzibit_id = ++highest_channel;
  GIOChannel *channel;
  ForwardedWindow *forward_data;
  int *client_fd, *key;

  g_print ("[%s] Share window %x...",
           gdk_display_get_name (gdk_display_get_default()),
           (int) window
           );

  forward_data = g_malloc(sizeof(ForwardedWindow));
  forward_data->plugin = plugin;
  forward_data->channel = xzibit_id;
  forward_data->window = window;
  forward_data->client_fd = vnc_fd (window);

  key = g_malloc (sizeof (int));
  *key = xzibit_id;

  g_hash_table_insert (priv->forwarded_windows_by_xzibit_id,
                       key,
                       forward_data);

  key = g_malloc (sizeof (int));
  *key = (int) window;

  g_hash_table_insert (priv->forwarded_windows_by_x11_id,
                       key,
                       forward_data);

  if (forward_data->client_fd==-1)
    {
      /* Not yet opened: open it */
      vnc_start (window);
      forward_data->client_fd = vnc_fd (window);
    }

  /* make sure we have the bottom connection */

  /* in real life, here we would connect over Tubes.
     instead, we connect to the other test instance.
  */

  if (priv->bottom_fd==-1)
    {
      struct sockaddr_in addr;
      GIOChannel *channel;

      memset (&addr, 0, sizeof (addr));
      addr.sin_family = AF_INET;
      addr.sin_port = htons (XZIBIT_PORT);
      addr.sin_addr.s_addr = htonl (INADDR_LOOPBACK);

      priv->bottom_fd = socket (PF_INET,
                                SOCK_STREAM,
                                IPPROTO_TCP);

      if (priv->bottom_fd < 0)
        {
          g_error ("Could not create a socket; things will break\n");
        }
      
      if (connect (priv->bottom_fd,
                   (struct sockaddr*) &addr,
                   sizeof(addr))!=0)
        {
          perror ("xzibit");

          g_error ("Could not talk to the other xzibit process.\n");
        }

      channel = g_io_channel_unix_new (priv->bottom_fd);

      g_io_add_watch (channel,
                      G_IO_IN,
                      copy_bottom_to_client,
                      plugin);
      
      g_print ("Okay, we should have a connection now\n");
    }

  /* and tell our counterpart about it */

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
                         window,
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
                         window,
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

  XFree (name_of_window);
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

/**
 * Responds to a change in a window's sharing property,
 * such as by sharing or unsharing the window.
 */
static void
set_sharing_state (Display *dpy,
                   Window window, int sharing_state, MutterPlugin *plugin)
{
  if (sharing_state == 2 || sharing_state == 3)
    {
      /* not our concern */
      return;
    }

  switch (sharing_state)
    {
    case 1:
      /* we are starting to share this window */
      share_window (dpy, window, plugin);
      break;

    case 0:
      /* we have stopped sharing this window */
      unshare_window (dpy, window, plugin);
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
            
      share_window (dpy,
                    window,
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
