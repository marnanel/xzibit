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

static gboolean check_for_bus_reads (GIOChannel *source,
                                     GIOCondition condition,
                                     gpointer data);

static const MutterPluginInfo * plugin_info (MutterPlugin *plugin);

MUTTER_PLUGIN_DECLARE(MutterXzibitPlugin, mutter_xzibit_plugin);

/*
 * Plugin private data that we store in the .plugin_private member.
 */
struct _MutterXzibitPluginPrivate
{
  MutterPluginInfo       info;

  /**
   * Atom values.
   */
  int xzibit_share_atom;
  int wm_transient_for_atom;
  int cardinal_atom;

  /**
   * The FD of the xzibit bus, which we're using until we switch
   * to using Telepathy.
   */
  int bus_fd;

  int bus_count;
  int bus_reading_size;
  long bus_size;
  unsigned char *bus_buffer;
};

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

  g_warning ("(xzibit plugin is starting)");

  if (mutter_plugin_debug_mode (plugin))
    {
      g_warning ("(xzibit plugin is in debug mode)");

      /* which means nothing at all at the moment */
    }

  priv->xzibit_share_atom = 0;
  priv->wm_transient_for_atom = 0;
  priv->cardinal_atom = 0;

  priv->bus_count = 0;
  priv->bus_reading_size = 1;
  priv->bus_size = 0;

  priv->bus_fd = socket (AF_UNIX, SOCK_STREAM, 0);

  if (priv->bus_fd < 0)
    {
      g_warning ("Could not create a socket; things will break\n");
    }
  else
    {
      struct sockaddr_un addr;
      char *message = "\001\000\000\000\177";
      int bufsize=4096;
      GIOChannel *channel;

      addr.sun_family = AF_UNIX;
      strcpy (addr.sun_path, "/tmp/xzibit-bus");
      connect (priv->bus_fd, (struct sockaddr*) &addr,
               strlen(addr.sun_path) + sizeof (addr.sun_family));

      /* dummy message for testing flushing out the bus */
      /* write (priv->bus_fd, message, 5); */

      channel = g_io_channel_unix_new (priv->bus_fd);
      g_io_add_watch (channel,
                      G_IO_IN,
                      check_for_bus_reads,
                      plugin);
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

static void
send_to_bus (MutterPlugin *plugin,
             ...)
{
  MutterXzibitPluginPrivate *priv   = MUTTER_XZIBIT_PLUGIN (plugin)->priv;
  va_list ap;
  unsigned char *buffer = NULL;
  int count=0, i, temp;

  va_start (ap, plugin);
  while (va_arg (ap, int)!=-1)
    {
      count++;
    }
  va_end (ap);

  buffer = g_malloc (count+4);

  va_start (ap, plugin);
  for (i=0; i<count; i++)
    {
      buffer[i+4] = (unsigned char) (va_arg (ap, int));
    }
  va_end (ap); 

  /* add in the length */
  temp = count;
  for (i=0; i<4; i++)
    {
      buffer[i] = temp % 256;
      temp >>= 8;
    }

  write (priv->bus_fd, buffer, count+4);

  g_free (buffer);
}

static void
share_window (Display *dpy,
              Window window, MutterPlugin *plugin)
{
  MutterXzibitPluginPrivate *priv   = MUTTER_XZIBIT_PLUGIN (plugin)->priv;
  GError *error = NULL;
  unsigned char message[11];
  int port;
      
  g_warning ("Share window...");
  port = vnc_port (window);
  g_warning ("Port is %d", port);

  if (port==0)
    {
      vnc_start (window);
      port = vnc_port (window);
      g_warning ("Port is really %d", port);
    }

  /* and tell our counterpart about it */

  send_to_bus(plugin,
              1, /* opcode */
              127, 0, 0, 1, /* IP address (ignored) */
              port % 256,
              port / 256,
              -1);

#if 0
  message[0] = 7; /* length */
  message[1] = message[2] = message[3] = 0;
  message[4] = 1; /* opcode */
  message[5] = 127; /* IP address (ignored) */
  message[6] = 0;
  message[7] = 0;
  message[8] = 1;
  message[9] = port % 256;
  message[10] = port / 256;

  write (priv->bus_fd, message, sizeof(message));
#endif

}

static gboolean
receive_window (gpointer data)
{
  int port = GPOINTER_TO_INT (data);
  GError *error = NULL;
  const char **argvl = g_malloc(sizeof (char*) * 4);
  char *port_as_string = g_strdup_printf("%d", port);

  g_warning ("Receiving window on port %d\n", port);

  argvl[0] = "xzibit-rfb-client";
  argvl[1] = "-p";
  argvl[2] = port_as_string;
  argvl[3] = 0;

  g_spawn_async (
                 "/",
                 (gchar**) argvl,
                 NULL,
                 G_SPAWN_SEARCH_PATH,
                 NULL, NULL,
                 NULL,
                 &error
                 );

  g_free (port_as_string);
  g_free (argvl);
  
  if (error)
    {
      meta_warning ("Attempting to launch window receiving service: %s\n", error->message);
      g_error_free (error);
    }

  return FALSE;
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

static void
handle_message_from_bus (unsigned char *buffer,
                         int size)
{
  int opcode;

  if (size==0)
    {
      return;
    }

  opcode = buffer[0];

  switch (opcode)
    {
    case 1:
      g_timeout_add (3000,
                     receive_window,
                     GINT_TO_POINTER (buffer[6]<<8 | buffer[5]));
      break;

    default:
      g_warning ("Unknown message type: %d\n", opcode);
    }

}

static gboolean
check_for_bus_reads (GIOChannel *source,
                     GIOCondition condition,
                     gpointer data)
{
  MutterPlugin *plugin = (MutterPlugin*) data;
  MutterXzibitPluginPrivate *priv = MUTTER_XZIBIT_PLUGIN (plugin)->priv;
  char buffer[1024];
  int count;
  int i;
  int q;

  count = recv (priv->bus_fd,
                buffer,
                sizeof(buffer),
                MSG_DONTWAIT);

  if (count<0)
    {
      if (errno==EWOULDBLOCK)
        return;

      g_error ("xzibit bus has died; can't really carry on");
    }

  for (i=0; i<count; i++) {
    if (priv->bus_reading_size) {
      priv->bus_size >>= 8;
      priv->bus_size |= (buffer[i]<< 8*3);
          
      if (priv->bus_count == 3)
        {
          priv->bus_reading_size = 0;
          priv->bus_count = -1;

          priv->bus_buffer = g_malloc (priv->bus_size);
        }
    } else {
      priv->bus_buffer[priv->bus_count] = buffer[i];

      if (priv->bus_count == priv->bus_size-1)
        {
          handle_message_from_bus (priv->bus_buffer,
                                   priv->bus_size);
          g_free (priv->bus_buffer);
          priv->bus_count = -1;
          priv->bus_reading_size = 1;
        }
    }

    priv->bus_count++;
    
  }

  return TRUE;
}

static gboolean
xevent_filter (MutterPlugin *plugin, XEvent *event)
{
  MutterXzibitPluginPrivate *priv = MUTTER_XZIBIT_PLUGIN (plugin)->priv;
  int i;

  switch (event->type)
    {
    case MapNotify:
      {
        XMapEvent *map_event = (XMapEvent*) event;
        Window window = map_event->window;
        Atom actual_type;
        int actual_format;
        unsigned long n_items, bytes_after;
        unsigned char *property;
        guint32 *transiency;
        guint32 *sharing;
        Display *dpy = map_event->display;

        if (priv->wm_transient_for_atom == 0)
          {
            priv->wm_transient_for_atom = XInternAtom(dpy,
                                                      "WM_TRANSIENT_FOR",
                                                      False);
          }
        
        if (priv->cardinal_atom == 0)
          {
            priv->cardinal_atom = XInternAtom(dpy,
                                              "CARDINAL",
                                              False);
          }

        /* is it transient? */
        if (XGetWindowProperty(dpy,
                               window,
                               priv->wm_transient_for_atom,
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
          break; /* we can't tell */

        if (property == NULL)
          break; /* no, it isn't */

        transiency = (guint32*) property;

        g_warning ("Transiency of %x = %x", window, *transiency);

        /*
        if (*transiency == gdk_x11_the_root_window (... FIXME ...)
            break; -- yes it is, but to the root
            */

        if (*transiency == window)
          break; /* yes it is, but to itself */

        /* So if we get here, it's transient to something.
        * Is it transient to a shared window? */

        /* FIXME: Need an ensure_atoms method */
        if (priv->xzibit_share_atom == 0)
          {
            priv->xzibit_share_atom = XInternAtom(dpy,
                                                  "_XZIBIT_SHARE",
                                                  False);
          }

        /* FIXME:
           Crazy values for WM_TRANSIENT_FOR will cause
           BadWindow here, which will mean we crash.
           We should not crash.
        */
        if (XGetWindowProperty(dpy,
                               *transiency,
                               priv->xzibit_share_atom,
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
            g_warning ("can't read sharing\n");
            XFree (transiency);
            break; /* we can't tell */
          }

        if (property == NULL)
          {
            g_warning ("no transiency");
            XFree (transiency);
            XFree (property);
            break; /* no, it isn't */
          }

        sharing = (guint32*) property;

        if (*sharing==1)
          {
            /* This is what we've been looking for!
             * The window is transient to a shared window.
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

        XFree (transiency);
        XFree (sharing);
      }
      return FALSE;

    case PropertyNotify:
      {
        XPropertyEvent *property = (XPropertyEvent*) event;
        int new_state = 0;

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
      
        return FALSE; /* we never handle events ourselves */
      }

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

    case GenericEvent:
      switch (event->xcookie.type)
        {
        case XI_HierarchyChanged:
          break;
        }
      break;
      
    default:
      return FALSE; /* we didn't handle it */
    }
}

static const MutterPluginInfo *
plugin_info (MutterPlugin *plugin)
{
  MutterXzibitPluginPrivate *priv = MUTTER_XZIBIT_PLUGIN (plugin)->priv;

  return &priv->info;
}
