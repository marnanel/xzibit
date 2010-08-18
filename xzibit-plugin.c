/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Mutter plugin for xzibit.
 *
 * Author: Thomas Thurman <thomas.thurman@collabora.co.uk>
 *
 * Based on the default Mutter plugin by
 * Tomas Frydrych <tf@linux.intel.com>
 *
 * Copyright (c) 2010 Collabora Ltd.
 * Copyright (c) 2008 Intel Corp.
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

#define XZIBIT_PORT 7177

#define ACTOR_DATA_KEY "MCCP-Xzibit-actor-data"

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

int highest_channel = 0;
char xzibit_header[] = "Xz 000.001\r\n";

struct _MutterXzibitPlugin
{
  MutterPlugin parent;

  MutterXzibitPluginPrivate *priv;
};

struct _MutterXzibitPluginClass
{
  MutterPluginClass parent_class;
};

static GQuark actor_data_quark = 0;

static gboolean   xevent_filter (MutterPlugin *plugin,
                            XEvent      *event);

static gboolean copy_top_to_server (GIOChannel *source,
                                    GIOCondition condition,
                                    gpointer data);
static gboolean copy_bottom_to_client (GIOChannel *source,
                                       GIOCondition condition,
                                       gpointer data);
static gboolean check_upwards (GIOChannel *source,
                                  GIOCondition condition,
                                  gpointer data);
static gboolean accept_connections (GIOChannel *source,
                                    GIOCondition condition,
                                    gpointer data);

static const MutterPluginInfo * plugin_info (MutterPlugin *plugin);

MUTTER_PLUGIN_DECLARE(MutterXzibitPlugin, mutter_xzibit_plugin);

/*
 * Plugin private data that we store in the .plugin_private member.
 */
struct _MutterXzibitPluginPrivate
{
  /**
   * Parent data.
   */
  MutterPluginInfo       info;

  /**
   * Atom values.
   * FIXME: gdk can do this for us, you know.
   */
  int xzibit_share_atom;
  int wm_transient_for_atom;
  int cardinal_atom;

  /*
   * File descriptors:
   * They work as follows.   () = fd
   *
   * [xzibit-rfb-client]---()=server_fd
   *                       ||
   *                top_fd=()<--()=listening_fd (on port 7177)
   *                        \\  ||
   *                          {TUBES}
   *                            \\
   *                             ()=bottom_fd
   *                             ||
   *            [libvncserver]---()=client_fd
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
   * File descriptor connected to libvncserver.
   * FIXME: it may not be necessary to store this
   *        (only its io channel).
   * FIXME: there need to be "n" of these, like in
   *        a hash table or something.
   */
  int client_fd;

  /**
   * Set if we're running under a test harness,
   * on the side that doesn't listen as a server.
   * FIXME: this is equivalent to listening_fd==-1
   * and perhaps we can do without it.
   */
  gboolean test_as_client;

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
   * Xzibit IDs mapped to XIDs on the current
   * server.  FIXME: Note that this assumes
   * that there's only one remote xzibit,
   * which we should reconsider.
   * FIXME: not in use and should be.
   */
  GHashTable *remote_xzibit_id_to_xid;
  /**
   * Lists of metadata we need to set
   * when a given window finally maps
   * FIXME: move this to xzibit-rfb-client.
   */
  GHashTable *postponed_metadata;
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

/* leave it turned off for now */
#if 0
#define DEBUG_FLOW(place, buffer, count) \
  debug_flow (place, buffer, count);
#else
#define DEBUG_FLOW(place, buffer, count) ;
#endif

static void
mutter_xzibit_plugin_dispose (GObject *object)
{
  G_OBJECT_CLASS (mutter_xzibit_plugin_parent_class)->dispose (object);
}

static void
mutter_xzibit_plugin_finalize (GObject *object)
{
  G_OBJECT_CLASS (mutter_xzibit_plugin_parent_class)->finalize (object);
}

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

static void
start (MutterPlugin *plugin)
{
  MutterXzibitPluginPrivate *priv   = MUTTER_XZIBIT_PLUGIN (plugin)->priv;
  int flags;
  GIOChannel *channel;

  g_warning ("(xzibit plugin is starting)");

  if (mutter_plugin_debug_mode (plugin))
    {
      g_warning ("(xzibit plugin is in debug mode)");

      /* which means nothing at all at the moment */
    }

  priv->xzibit_share_atom = 0;
  priv->wm_transient_for_atom = 0;
  priv->cardinal_atom = 0;

  priv->dpy = NULL;

  priv->bottom_stage = -3 - sizeof(xzibit_header);
  priv->bottom_channel = 0;
  priv->bottom_length = 0;
  priv->bottom_buffer = NULL;

  if (g_getenv("XZIBIT_TEST_AS_CLIENT"))
    {
      /* This is a special case for testing
       * on a single host:
       * we don't create a listening socket,
       * but instead we connect to the socket
       * created by the other process running
       * on the same machine.
       */
      g_print ("[%s] Here we would connect as a client.\n",
               gdk_display_get_name (gdk_display_get_default()));
      priv->test_as_client = TRUE;
      priv->listening_fd = -1;
    }
  else
    {
      struct sockaddr_in addr;
      int one = 1;

      g_print ("[%s] Here we listen as a server as usual.\n",
               gdk_display_get_name (gdk_display_get_default()));

      priv->test_as_client = FALSE;

      memset (&addr, 0, sizeof (addr));
      addr.sin_family = AF_INET;
      addr.sin_addr.s_addr = htonl (INADDR_ANY);
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

      g_print ("Socket %d is now LISTENING\n",
               priv->listening_fd);
    }

  priv->bottom_fd = -1;
  priv->client_fd = -1;

  priv->remote_xzibit_id_to_xid =
    g_hash_table_new_full (g_int_hash,
			   g_int_equal,
			   g_free,
			   g_free);

  priv->postponed_metadata =
    g_hash_table_new_full (g_int_hash,
			   g_int_equal,
			   g_free,
			   NULL); /* we want lists
                                     not to be freed
                                     if we replace them
                                     with later versions
                                     of themselves! */
}

typedef struct {
  unsigned long length;
  int type;
  char *data;
} XzibitMetadataInfo;

static void
apply_metadata_now (MutterPlugin *plugin,
                    Window window,
                    XzibitMetadataInfo *metadata)
{
  MutterXzibitPluginPrivate *priv   = MUTTER_XZIBIT_PLUGIN (plugin)->priv;
  Display *dpy = priv->dpy;

  switch (metadata->type)
    {
    case XZIBIT_METADATA_NAME:
      {
        /* FIXME: we also need to update WM_NAME */

        XChangeProperty (dpy,
                         window,
                         XInternAtom(dpy,
                                     "_NET_WM_NAME",
                                     False),
                         XInternAtom(dpy,
                                     "UTF8_STRING",
                                     False),
                         8,
                         PropModeReplace,
                         metadata->data,
                         metadata->length);

      }
      break;

    case XZIBIT_METADATA_TYPE:
      {
        int i=0;
        char type;

        if (metadata->length != 1)
          {
            meta_warning ("Type metadata must be a single byte; was %d",
                    metadata->length);
            return;
          }

        type = metadata->data[0];

        while (window_types[i][0])
          {
            if (window_types[i][0][0]==type)
              {
                Atom atom = 0;
                g_print ("We know this: it's %s",
                         window_types[i][1]);
                
                atom = XInternAtom (dpy,
                                    window_types[i][1],
                                    True);
                
                if (atom==0)
                  return;
       
                g_print ("Setting %d (%s) on %x",
                         (int) atom, window_types[i][1],
                         (int) window);
         
                XChangeProperty (dpy,
                                 window,
                                 XInternAtom(dpy,
                                             "_NET_WM_WINDOW_TYPE",
                                             False),
                                 XInternAtom(dpy,
                                             "ATOM",
                                             False),
                                 32,
                                 PropModeReplace,
                                 (unsigned char*) &atom,
                                 1);

                switch (type)
                  {
                  case 'R':
                  case 'P':
                    {
                      XSetWindowAttributes attr;

                      /* It's a menu.  Make it override-redirect. */
                      attr.override_redirect = True;
                      XChangeWindowAttributes (dpy,
                                               window,
                                               CWOverrideRedirect,
                                               &attr);

                    /* FIXME Pointer grab?? */
                    }
                    break;
                  }
                
                /* and we're done */
                return;
              }
            i++;
          }
        g_warning ("Request to set %x to an unknown type %c",
                   (int) window, type);
      }

    default:
      g_warning ("Request to set metadata on %x of type %d "
                 "which we don't know", (int) window,
                 metadata->type);
    }
}

static void
apply_metadata (MutterPlugin *plugin,
                unsigned char *buffer,
                int size)
{
  MutterXzibitPluginPrivate *priv   = MUTTER_XZIBIT_PLUGIN (plugin)->priv;
  int xzibit_id = 0, metadata_type = 0;
  Window *xid;

  xid = g_hash_table_lookup (priv->remote_xzibit_id_to_xid,
                             &xzibit_id);

  if (size<4)
    {
      g_warning ("Trying to set metadata but buffer ran short");
      return;
    }

  xzibit_id = buffer[1]*256 + buffer[0];
  metadata_type = buffer[3]*256 + buffer[2];

  if (xid)
    {
      XzibitMetadataInfo metadata = {
        /* Add four bytes to bypass headers */
        size-4,
        metadata_type,
        buffer+4
      };

      apply_metadata_now (plugin,
                          *xid,
                          &metadata);
    }
  else
    {
      Window *new_key = g_malloc(sizeof(Window));
      GList *list = g_hash_table_lookup (priv->postponed_metadata,
                                         &xzibit_id);
      /* add four to avoid the window ID and type */
      char *new_buffer = g_memdup (buffer+4,
                                   size-4);
      XzibitMetadataInfo *metadata = g_malloc (sizeof (XzibitMetadataInfo));

      metadata->length = size-4;
      metadata->data = new_buffer;
      metadata->type = metadata_type;
      
      *new_key = xzibit_id;
      list = g_list_prepend (list, metadata);

      g_hash_table_insert (priv->postponed_metadata,
                           new_key,
                           list);
    }
}

static void
mutter_xzibit_plugin_class_init (MutterXzibitPluginClass *klass)
{
  GObjectClass      *gobject_class = G_OBJECT_CLASS (klass);
  MutterPluginClass *plugin_class  = MUTTER_PLUGIN_CLASS (klass);

  gobject_class->finalize        = mutter_xzibit_plugin_finalize;
  gobject_class->dispose         = mutter_xzibit_plugin_dispose;
  gobject_class->set_property    = mutter_xzibit_plugin_set_property;
  gobject_class->get_property    = mutter_xzibit_plugin_get_property;

  plugin_class->start            = start;
  plugin_class->xevent_filter    = xevent_filter;

  g_type_class_add_private (gobject_class, sizeof (MutterXzibitPluginPrivate));
}

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
 *
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
  /* FIXME: i=4; ... would be more efficient */
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

  write (priv->bottom_fd, buffer, count+4);
  fsync (priv->bottom_fd);

  g_free (buffer);
}

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

  write (priv->bottom_fd, header_buffer, 4);
  write (priv->bottom_fd, buffer, length);  
  fsync (priv->bottom_fd);
}

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

  write (priv->bottom_fd, preamble,
         sizeof(preamble));
  write (priv->bottom_fd, metadata,
         metadata_length);  
  fsync (priv->bottom_fd);
}

typedef struct {
  MutterPlugin *plugin;
  int channel;
} ForwardRFBAcrossTubesData;

static gboolean
copy_client_to_bottom (GIOChannel *source,
                       GIOCondition condition,
                       gpointer data)
{
  ForwardRFBAcrossTubesData *forward_data =
    (ForwardRFBAcrossTubesData*) data;
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
}

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
  ForwardRFBAcrossTubesData *forward_data;
      
  g_print ("[%s] Share window %x...",
           gdk_display_get_name (gdk_display_get_default()),
           (int) window
           );

  /*
   * FIXME: This will obviously need to be
   * a hash table to support sharing
   * multiple windows.
   */
  priv->client_fd = vnc_fd (window);

  if (priv->client_fd==-1)
    {
      /* Not yet opened: open it */
      vnc_start (window);
      priv->client_fd = vnc_fd (window);

      g_print ("Created client FD.\n");
    }

  /* make sure we have the bottom connection */

  /* in real life, here we would connect over Tubes.
     instead, we connect to the other test instance.
  */

  if (priv->bottom_fd==-1)
    {
      struct sockaddr_in addr;
      GIOChannel *channel;

      g_print ("Connecting to port...\n");

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
                    127, 0, 0, 1, /* IP address (ignored) */
                    xzibit_id % 256,
                    xzibit_id / 256,
                    -1);

  /* If we receive data from VNC, send it on. */

  forward_data = g_malloc(sizeof(ForwardRFBAcrossTubesData));
  forward_data->plugin = plugin;
  forward_data->channel = xzibit_id;
  channel = g_io_channel_unix_new (priv->client_fd);
  g_io_add_watch (channel,
                  G_IO_IN,
                  copy_client_to_bottom,
                  forward_data);

  /* also supply metadata */

  /* FIXME: We should also consider WM_NAME */
  /* FIXME: We should cache the atoms */
  if (XGetWindowProperty(dpy,
                         window,
                         XInternAtom(dpy,
                                     "_NET_WM_NAME",
                                     False),
                         0,
                         1024,
                         False,
                         XInternAtom(dpy,
                                     "UTF8_STRING",
                                     False),
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
                         XInternAtom(dpy,
                                     "_NET_WM_WINDOW_TYPE",
                                     False),
                         0,
                         1,
                         False,
                         XInternAtom(dpy,
                                     "ATOM",
                                     False),
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

static void
set_sharing_state (Display *dpy,
                   Window window, int sharing_state, MutterPlugin *plugin)
{
  if (sharing_state == 2 || sharing_state == 3)
    {
      /* not our concern */
      return;
    }

  g_warning ("(xzibit plugin saw a change of sharing of %06lx to %d)\n",
             (unsigned long) window, sharing_state);

  switch (sharing_state)
    {
    case 1:
      /* we are starting to share this window */
      share_window (dpy, window, plugin);
      break;

    case 0:
      /* we have stopped sharing this window */
      /* FIXME: deal with this case. */
      break;
    }
}

static gboolean
copy_server_to_top (GIOChannel *source,
                       GIOCondition condition,
                       gpointer data)
{
  /* FIXME: what should the return result be? */
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
      /* FIXME: something more sensible than this */
      g_error ("xzibit-rfb-client seems to have died");
    }

  if (count==0)
    {
      return;
    }

  DEBUG_FLOW ("forwarding from SERVER to TOP", buffer, count);

  /* FIXME: check result */
  write (server_details->top_fd,
         buffer,
         count);
}

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

      write (server_details->server_fd,
             buffer,
             count);
    }
  else
    {
      g_error ("no server to talk to!");
    }

  return TRUE;
}

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
  write (server_details->top_fd,
         xzibit_header,
         sizeof(xzibit_header)-1 /* no trailing null */);
  fsync (server_details->top_fd);

  channel = g_io_channel_unix_new (server_details->top_fd);
  g_io_add_watch (channel,
                  G_IO_IN,
                  copy_top_to_server,
                  server_details);
}

static void
handle_message_to_client (MutterPlugin *plugin,
                          int channel,
                          char *buffer,
                          int length)
{
  MutterXzibitPluginPrivate *priv = MUTTER_XZIBIT_PLUGIN (plugin)->priv;
  int fd;

  if (channel==0)
    {
      g_warning ("Possibly a problem: don't know how to deal with ch0 on client messages\n");
      return;
    }

  g_print ("This is a message for channel %d\n", channel);
  /*
   * FIXME: Look it up in a hash table
   */
  fd = priv->client_fd;

  g_print ("Spidey-sense tells us the FD is %d\n", fd);

  /* FIXME: error checking; it could run short */

  DEBUG_FLOW ("sent from TOP to CLIENT",
              buffer, length);
  write (fd, buffer, length);
}

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
          g_print ("Channel is %d, length is %d",
                   priv->bottom_channel,
                   priv->bottom_length);
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

#if 0
  xid = g_hash_table_lookup (priv->remote_xzibit_id_to_xid,
                             &xzibit_id);

  if (xid)
    {
      g_print ("This is for window %x\n", (int) xid);
  /*
    fd = vnc_fd (window);
    ...
  */
    }
  else
    {
      g_print ("I don't know that window.\n");
    }
#endif      

  return TRUE;
}

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
                         XInternAtom(dpy,
                                     relationship,
                                     False),
                         0,
                         4,
                         False,
                         XInternAtom(dpy,
                                     "WINDOW",
                                     False),
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
      g_warning ("%x has no %s.\n", (int)window, relationship);
      return FALSE; /* no, it isn't */
    }

  parent = *((guint32*) property);
  XFree (property);

  g_warning ("%s relation of %x is %x",
             relationship, (int)window,
             parent);

  if (XGetWindowProperty(dpy,
                         parent,
                         XInternAtom(dpy,
                                     "_XZIBIT_SHARE",
                                     False),
                         0,
                         4,
                         False,
                         XInternAtom(dpy,
                                     "CARDINAL",
                                     False),
                         &actual_type,
                         &actual_format,
                         &n_items,
                         &bytes_after,
                         &property)!=Success)
    {
      g_warning ("can't read relationship %s\n", relationship);
      return FALSE; /* we can't tell */
    }
  
  if (property == NULL)
    {
      g_warning ("no sharing on parent, %x", (int)parent);
      return FALSE; /* no, it isn't */
    }

  sharing = *(guint32*) property;
  XFree (property);

  return sharing==1;
}

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

  g_warning ("Transiency check for %x starting\n", (int) map_event->window);

  if (related_to_shared_window (dpy, window, "WM_TRANSIENT_FOR"))
    {
      /* This is what we've been looking for!
       * The window is related to a shared window.
       * It should be shared itself.
       */
      
      guint32 window_is_shared = 1;
            
      XChangeProperty (dpy,
                       window,
                       priv->wm_transient_for_atom,
                       priv->cardinal_atom,
                       32,
                       PropModeReplace,
                       (const unsigned char*) &window_is_shared,
                       1);
            
      share_window (dpy,
                    window,
                    plugin);
    }
}

static void
check_for_pending_metadata_on_map (MutterPlugin *plugin,
                                   XEvent *event)
{
  MutterXzibitPluginPrivate *priv = MUTTER_XZIBIT_PLUGIN (plugin)->priv;
  XMapEvent *map_event = (XMapEvent*) event;
  Atom actual_type;
  int actual_format;
  unsigned long n_items, bytes_after;
  unsigned char *property;
  int xzibit_id;
  GList *metadata;
  Display *dpy = map_event->display;

  if (XGetWindowProperty(dpy,
                         map_event->window,
                         XInternAtom(dpy,
                                     "_XZIBIT_ID",
                                     False),
                         0,
                         4,
                         False,
                         priv->cardinal_atom,
                         &actual_type,
                         &actual_format,
                         &n_items,
                         &bytes_after,
                         &property)!=Success)
    {
      return;
    }

  if (n_items!=2)
    {
      if (n_items!=0)
        XFree (property);

      return;
    }

  /* ignore remote ID for now */
  xzibit_id = ((int*) property)[1];
  XFree (property);

  metadata = g_hash_table_lookup (priv->postponed_metadata,
                                  &xzibit_id);

  if (metadata)
    {
      GList *cursor = metadata;
      while (cursor)
        {
          apply_metadata_now (plugin,
                              map_event->window,
                              cursor->data);
          g_free (cursor->data);

          cursor = cursor->next;
        }
     
      g_list_free (cursor);
      g_hash_table_remove (priv->postponed_metadata,
             &xzibit_id); 
    }
}

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

static gboolean
xevent_filter (MutterPlugin *plugin, XEvent *event)
{
  MutterXzibitPluginPrivate *priv = MUTTER_XZIBIT_PLUGIN (plugin)->priv;
  int i;

  gdk_error_trap_push ();

  switch (event->type)
    {
    case MapNotify:
      {
        ensure_display (plugin, ((XMapEvent*) event)->display);

        share_transiency_on_map (plugin, event);
        check_for_pending_metadata_on_map (plugin, event);
      }
      break;

    case PropertyNotify:
      {
        XPropertyEvent *property = (XPropertyEvent*) event;
        int new_state = 0;

        ensure_display (plugin, property->display);

        if (priv->xzibit_share_atom == 0)
          {
            priv->xzibit_share_atom = XInternAtom(property->display,
                                                  "_XZIBIT_SHARE",
                                                  False);
          }
      
        if (property->atom != priv->xzibit_share_atom)
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
                               priv->xzibit_share_atom,
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

static const MutterPluginInfo *
plugin_info (MutterPlugin *plugin)
{
  MutterXzibitPluginPrivate *priv = MUTTER_XZIBIT_PLUGIN (plugin)->priv;

  return &priv->info;
}
