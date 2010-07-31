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
  Display *dpy;

  /**
   * Xzibit IDs mapped to XIDs on the current
   * server.  FIXME: Note that this assumes
   * that there's only one remote xzibit,
   * which we should reconsider.
   */
  GHashTable *remote_xzibit_id_to_xid;
  /**
   * Lists of metadata we need to set
   * when a given window finally maps
   */
  GHashTable *postponed_metadata;
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

  priv->dpy = NULL;
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

static void
apply_metadata_now (MutterPlugin *plugin,
                    Window window,
                    unsigned char *buffer)
{
  MutterXzibitPluginPrivate *priv   = MUTTER_XZIBIT_PLUGIN (plugin)->priv;
  int metadata_type = buffer[1]*256 + buffer[0];
  Display *dpy = priv->dpy;

  switch (metadata_type)
    {
    case XZIBIT_METADATA_NAME:
      {
        /* FIXME: we also need to update WM_NAME */
        /* FIXME: some weirdness about terminators */

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
                         buffer+2,
                         strlen(buffer+2));
      }
      break;

    case XZIBIT_METADATA_TYPE:
      {
        int i=0;
        char type = buffer[2];

        while (window_types[i][0])
          {
            g_print ("%c %c\n", window_types[i][0][0], type);
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
                 metadata_type);
    }
}

static void
apply_metadata (MutterPlugin *plugin,
                unsigned char *buffer,
                int size)
{
  MutterXzibitPluginPrivate *priv   = MUTTER_XZIBIT_PLUGIN (plugin)->priv;
  int xzibit_id = buffer[1]*256 + buffer[0];
  Window *xid;

  g_print("*** APPLY METADATA ***\n");

  xid = g_hash_table_lookup (priv->remote_xzibit_id_to_xid,
                             &xzibit_id);

  if (xid)
    {
      apply_metadata_now (plugin,
                          *xid,
                          buffer+2);
    }
  else
    {
      Window *new_key = g_malloc(sizeof(Window));
      GList *list = g_hash_table_lookup (priv->postponed_metadata,
                                         &xzibit_id);
      char *new_buffer = g_memdup (buffer+2,
                                   size-2);
      
      *new_key = xzibit_id;
      list = g_list_prepend (list, new_buffer);

      g_hash_table_insert (priv->postponed_metadata,
                           new_key,
                           list);

      g_print ("We remember a property for %x\n",
               (int) xzibit_id);
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
send_buffer_to_bus (MutterPlugin *plugin,
                    unsigned char *buffer,
                    int length)
{
  MutterXzibitPluginPrivate *priv   = MUTTER_XZIBIT_PLUGIN (plugin)->priv;
  char length_buffer[4];
  int i, temp;

  if (length==-1)
    length = strlen (buffer);

  temp = length;
  for (i=0; i<4; i++)
    {
      length_buffer[i] = temp % 256;
      temp >>= 8;
    }

  write (priv->bus_fd, length_buffer, 4);
  write (priv->bus_fd, buffer, length);  
}

static void
send_metadata_to_bus (MutterPlugin *plugin,
                      int xzibit_id,
                      int metadata_type,
                      char *metadata,
                      int metadata_length)
{
  MutterXzibitPluginPrivate *priv   = MUTTER_XZIBIT_PLUGIN (plugin)->priv;
  char preamble[9];
  int i, temp;

  if (metadata_length==-1)
    metadata_length = strlen (metadata);

  temp = metadata_length + 5;
  for (i=0; i<4; i++)
    {
      preamble[i] = temp % 256;
      temp >>= 8;
    }

  preamble[4] = 3; /* set metadata */
  preamble[5] = xzibit_id % 256;
  preamble[6] = xzibit_id / 256;
  preamble[7] = metadata_type % 256;
  preamble[8] = metadata_type / 256;

  write (priv->bus_fd, preamble,
         sizeof(preamble));
  write (priv->bus_fd, metadata,
         metadata_length);  
}

static void
share_window (Display *dpy,
              Window window, MutterPlugin *plugin)
{
  MutterXzibitPluginPrivate *priv   = MUTTER_XZIBIT_PLUGIN (plugin)->priv;
  GError *error = NULL;
  unsigned char message[11];
  int port;
  Atom actual_type;
  int actual_format;
  unsigned long n_items, bytes_after;
  unsigned char *property;
  unsigned char *name_of_window = "";
  unsigned char type_of_window[2] = { 0, 0 };
      
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

  send_metadata_to_bus (plugin,
                        port, /* == xzibit id */
                        XZIBIT_METADATA_NAME,
                        name_of_window,
                        -1);

  send_metadata_to_bus (plugin,
                        port,
                        XZIBIT_METADATA_TYPE,
                        type_of_window,
                        1);

  /* we don't supply icons yet. */

  XFree (name_of_window);
}

static gboolean
receive_window (gpointer data)
{
  guint32 * args = (int*) data;
  GError *error = NULL;
  const char **argvl = g_malloc(sizeof (char*) * 6);
  char *port_as_string = g_strdup_printf("%d", args[0]);
  char *id_as_string = g_strdup_printf("%d", args[1]);

  g_warning ("Receiving window on port %d\n", args[0]);

  argvl[0] = "xzibit-rfb-client";
  argvl[1] = "-p";
  argvl[2] = port_as_string;
  argvl[3] = "-i";
  argvl[4] = id_as_string;
  argvl[5] = 0;

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

  g_free (data);

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
handle_message_from_bus (MutterPlugin *plugin,
                         unsigned char *buffer,
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
    case 1: /* OPEN */
      {
        guint32 *data = g_malloc(sizeof(guint32)*2);

        /* xzibit ID is currently the same as port number */
        data[0] = buffer[6]<<8 | buffer[5];
        data[1] = data[0];

        g_timeout_add (3000,
                       receive_window,
                       data);
      }
      break;

    case 3: /* SET */
      {
        apply_metadata(plugin,
                       buffer+1,
                       size-1);
      }
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

          priv->bus_buffer = g_malloc (priv->bus_size+1);
          /* make sure it's null-terminated */
          priv->bus_buffer[priv->bus_size] = 0;
        }
    } else {
      priv->bus_buffer[priv->bus_count] = buffer[i];

      if (priv->bus_count == priv->bus_size-1)
        {
          handle_message_from_bus (plugin,
                                   priv->bus_buffer,
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
             relationship, window,
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
          /*
            FIXME:
          g_free (cursor->data);
          */

          cursor = cursor->next;
        }
     
      /*
        FIXME:
      g_list_free (cursor);
      And remove it from the hash
      */
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
