/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Client library for xzibit.
 * Not useful for most uses; most people will want to use
 * the mutter plugin, which should work transparently.
 *
 * Author: Thomas Thurman <thomas.thurman@collabora.co.uk>
 *
 * Copyright (c) 2010 Collabora Ltd.
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

#include <xzibit-client.h>
#include <sys/socket.h>
#include <string.h>
#include <netinet/in.h>
#include <gdk/gdk.h>
#include <rfb/rfbproto.h>
#include <rfb/rfb.h>

#define XZIBIT_PORT 7177

static char header[] = "Xz 000.001\r\n";

struct _XzibitClient {
  int xzibit_fd;
  int state;
  int length;
  int channel;
  int highest_channel;
  GIOChannel *gio_channel;
  char *buffer;
  GHashTable *vnc_servers;
  GHashTable *vnc_fds;
};

#define CONTROL_CHANNEL 0

#define COMMAND_OPEN 1
#define COMMAND_CLOSE 2
#define COMMAND_SET 3
#define COMMAND_WALL 4
#define COMMAND_RESPAWN 5
#define COMMAND_AVATAR 6
#define COMMAND_LISTEN 7
#define COMMAND_MOUSE 8

#define METADATA_TRANSIENCY 1
#define METADATA_TITLE 2
#define METADATA_TYPE 3
#define METADATA_ICON 4

static gboolean
received_from_xzibit (GIOChannel *source,
		      GIOCondition condition,
		      gpointer data)
{
  XzibitClient *client = (XzibitClient*) data;
  int count, i;
  char buffer[4096];

  count = read (client->xzibit_fd,
                buffer,
                sizeof(buffer));

  g_print ("Count is %d\n", count);

  for (i=0; i<count; i++)
    {
      g_print ("Received %02x in state %d\n", buffer[i],
               client->state);
      
      switch (client->state)
	{
        case -4:
          client->channel = buffer[i];
          break;

        case -3:
          client->channel |= buffer[i] * 256;
          break;

        case -2:
          client->length = buffer[i];
          break;

        case -1:
          client->length |= buffer[i] * 256;
          client->buffer = g_malloc (client->length);
          g_print ("(Length is %d)\n", client->length);
          break;

	default:
	  if (client->state<-4)
	    {
	      if (header[client->state + sizeof(header) + 3] !=
		  buffer[i])
		{
		  g_error ("Didn't get the header.");
		}
	    }
	  else
	    {
	      client->buffer[client->state] = buffer[i];
	      if (client->state==client->length-1)
		{
                  if (client->channel==CONTROL_CHANNEL)
                    {
                      g_print ("Received control message (FIXME)\n");
                    }
                  else
                    {
                      int *rfb_fd = g_hash_table_lookup (client->vnc_fds,
                                                         &(client->channel));
                      
                      g_print ("Copying %d bytes from xzibit to VNC on fd %d\n",
                               client->length, *rfb_fd);

                      write (*rfb_fd, client->buffer, client->length);
                    }

                  g_free (client->buffer);
                  client->state = -5;
		}
	    }
	}

      client->state++;
    }
}

static void
send_word (XzibitClient *client,
           guint16 word)
{
  char buffer[2] = {
    word % 256,
    word / 256
  };

  write (client->xzibit_fd, &buffer, 2);
}

static void
send_byte (XzibitClient *client,
           guint8 byte)
{
  char buffer[1] = {
    byte
  };

  write (client->xzibit_fd, &buffer, 1);
}

static void
send_block_header (XzibitClient *client,
                   int channel,
                   int length)
{
  send_word (client, channel);
  send_word (client, length);
}

void
xzibit_client_free (XzibitClient *client)
{
  g_free (client->buffer);
  g_free (client);
}

static gboolean
copy_vnc_to_xzibit (GIOChannel *source,
                    GIOCondition condition,
                    gpointer data)
{
  XzibitClient *client = data;
  char buffer[4096];
  int fd = g_io_channel_unix_get_fd (source);
  int count;

  count = read (fd, &buffer, sizeof(buffer));

  g_print ("Copying %d bytes from VNC to xzibit\n", count);

  send_block_header (client,
                     1, /* FIXME: this should not be hard-coded */
                     count);
  write (client->xzibit_fd, buffer, count);
}

static gboolean
keep_vnc_running (gpointer data)
{
  rfbScreenInfoPtr rfb_screen = data;

  rfbProcessEvents(rfb_screen,
		   40000);

  return TRUE;
}

int
xzibit_client_open_channel (XzibitClient *client)
{
  GIOChannel *gio_channel;
  int sockets[2];
  int *key = g_malloc (sizeof(key));
  int *rfb_fd;
  rfbScreenInfoPtr rfb_screen;

  client->highest_channel++;

  send_block_header (client,
                     CONTROL_CHANNEL,
                     7);

  send_byte (client, COMMAND_OPEN);
  send_byte (client, 127);
  send_byte (client, 0);
  send_byte (client, 0);
  send_byte (client, 1);
  send_word (client, client->highest_channel);

  fsync (client->xzibit_fd);

  socketpair (AF_LOCAL, SOCK_STREAM, 0, sockets);
  
  *key = client->highest_channel;
  rfb_screen = rfbGetScreen (0, NULL, /* we don't supply argc and argv */
                             /* FIXME don't hardcode these */
                             256, 256,
                             /* FIXME don't hardcode these */
                             8, 1, 4);

  rfb_screen->desktopName = "from client library";
  rfb_screen->autoPort = FALSE;
  rfb_screen->port = 0;
  rfb_screen->fdFromParent = sockets[1];
  rfb_screen->frameBuffer = NULL;

  rfbInitServer (rfb_screen);

  rfb_fd = g_malloc (sizeof(int));
  *rfb_fd = sockets[0];

  g_hash_table_insert (client->vnc_servers,
                       key, rfb_screen);
  g_hash_table_insert (client->vnc_fds,
                       key, rfb_fd);

  gio_channel = g_io_channel_unix_new (*rfb_fd);
      
  g_io_add_watch (gio_channel,
                  G_IO_IN,
                  copy_vnc_to_xzibit,
                  client);

  g_timeout_add (100,
                 keep_vnc_running,
                 rfb_screen);

  return client->highest_channel;
}

XzibitClient*
xzibit_client_new (void)
{
  struct sockaddr_in addr;
  XzibitClient *result = g_malloc(sizeof(XzibitClient));
  
  result->xzibit_fd = -1;
  result->state = -3 - (sizeof(header));
  result->length = 0;
  result->channel = 0;
  result->buffer = NULL;
  result->highest_channel = 0;

  result->vnc_servers =
    g_hash_table_new_full (g_int_hash,
                           g_int_equal,
                           g_free,
                           g_free);

  result->vnc_fds =
    g_hash_table_new_full (g_int_hash,
                           g_int_equal,
                           g_free,
                           g_free);

  memset (&addr, 0, sizeof (addr));
  addr.sin_family = AF_INET;
  addr.sin_port = htons (XZIBIT_PORT);
  addr.sin_addr.s_addr = htonl (INADDR_LOOPBACK);
  
  result->xzibit_fd = socket (PF_INET,
		       SOCK_STREAM,
		       IPPROTO_TCP);

  if (result->xzibit_fd < 0)
    {
      g_error ("Could not create a socket; things will break\n");
    }
      
  if (connect (result->xzibit_fd,
	       (struct sockaddr*) &addr,
	       sizeof(addr))!=0)
    {
      g_error ("Could not talk to xzibit.\n");
    }

  result->gio_channel = g_io_channel_unix_new (result->xzibit_fd);

  g_io_add_watch (result->gio_channel,
		  G_IO_IN,
		  received_from_xzibit,
		  result);

  g_print ("Monitoring %d.\n", result->xzibit_fd);

  return result;
}

void
xzibit_client_close_channel (XzibitClient *client,
                             int channel)
{
  send_block_header (client,
                     CONTROL_CHANNEL,
                     1);

  send_byte (client, COMMAND_CLOSE);
}

void
xzibit_client_send_avatar (XzibitClient *client,
                           GdkPixbuf *avatar)
{
  gchar *buffer;
  gsize buffer_size;

  /* FIXME: error checking */

  gdk_pixbuf_save_to_buffer (avatar,
                             &buffer,
                             &buffer_size,
                             "png",
                             NULL, NULL);

  g_print ("Avatar is %d bytes long as a PNG.\n",
           buffer_size);

  send_block_header (client,
                     CONTROL_CHANNEL,
                     buffer_size+1);

  send_byte (client, COMMAND_AVATAR);
  write (client->xzibit_fd,
         buffer,
         buffer_size);

  g_free (buffer);

}

void
xzibit_client_send_video (XzibitClient *client,
                          int channel,
                          GdkPixbuf *image)
{
  static GdkPixbuf *with_alpha = NULL;
  rfbScreenInfoPtr rfb_screen;
  
  rfb_screen = g_hash_table_lookup (client->vnc_servers,
                                    &channel);

  if (!rfb_screen)
    {
      g_error ("There is no VNC server for channel %d.  Crunch.",
               channel);
    }

  if (with_alpha)
    g_object_unref (with_alpha);

  with_alpha = gdk_pixbuf_add_alpha (image, FALSE, 0, 0, 0);
  rfb_screen->frameBuffer = gdk_pixbuf_get_pixels (with_alpha);

  rfbMarkRectAsModified(rfb_screen,
                        0, 0,
                        gdk_pixbuf_get_width (with_alpha),
                        gdk_pixbuf_get_height (with_alpha));
}

void
xzibit_client_move_pointer (XzibitClient *client,
                            int channel,
                            int x,
                            int y)
{
  send_block_header (client,
                     CONTROL_CHANNEL,
                     5);

  send_byte (client, COMMAND_MOUSE);
  send_word (client, x);
  send_word (client, y);

  g_print ("Mouse pointer now at (%d,%d).\n",
           x, y);
}

static void
send_metadata (XzibitClient *client,
               int channel,
               int metadata_type,
               int metadata_length,
               char *metadata)
{
  send_block_header (client,
                     CONTROL_CHANNEL,
                     metadata_length + 5);

  send_byte (client, COMMAND_SET);
  send_word (client, metadata_type);
  send_word (client, channel);

  write (client->xzibit_fd,
         metadata,
         metadata_length);
}

void
xzibit_client_set_title (XzibitClient *client,
                         int channel,
                         char *title)
{
  send_metadata (client,
                 channel,
                 METADATA_TITLE,
                 strlen (title),
                 title);
}

void
xzibit_client_set_icon (XzibitClient *client,
                        int channel,
                        GdkPixbuf *icon)
{
  GdkPixbuf *resized_icon = gdk_pixbuf_scale_simple (icon,
                                                     16, 16,
                                                     GDK_INTERP_BILINEAR);
  gchar *buffer;
  gsize buffer_size;

  /* FIXME: error checking */
  gdk_pixbuf_save_to_buffer (resized_icon,
                             &buffer,
                             &buffer_size,
                             "png",
                             NULL, NULL);

  send_metadata (client,
                 channel,
                 METADATA_ICON,
                 buffer_size,
                 buffer);

  g_object_unref (resized_icon);
}
