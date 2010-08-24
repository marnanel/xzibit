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

#ifndef XZIBIT_CLIENT
#define XZIBIT_CLIENT 1

#include <gdk-pixbuf/gdk-pixbuf.h>

typedef struct _XzibitClient XzibitClient;

/**
 * Creates an xzibit client connected to the
 * xzibit server currently running on localhost.
 *
 * \return  The new client.
 */
XzibitClient* xzibit_client_new (void);

/**
 * Destroys an xzibit client and frees the
 * memory.
 *
 * \param client  The client.
 */
void xzibit_client_free (XzibitClient *client);

/**
 * Sends an avatar to the xzibit server.
 *
 * \param client  The client.
 * \param avatar  The image to use.
 */
void xzibit_client_send_avatar (XzibitClient *client,
                                GdkPixbuf *avatar);

/**
 * Creates a video channel on an xzibit client.
 *
 * \param client  The client.
 * \return  The channel ID.
 */
int xzibit_client_open_channel (XzibitClient *client);

/**
 * Closes a video channel on an xzibit client.
 *
 * \bug Should close associated audio channels
 *      and doesn't.
 *
 * \param client  The client.
 * \param channel The channel ID.
 */
void xzibit_client_close_channel (XzibitClient *client,
                                  int channel);

/**
 * Sets the window title for a channel.
 *
 * \param client  The client.
 * \param channel The channel ID.
 * \param title   The new title, UTF-8 encoded.
 */
void xzibit_client_set_title (XzibitClient *client,
                              int channel,
                              char *title);
/**
 * Sets the window icon for a channel.  The
 * image will be scaled on the client side as
 * appropriate.
 *
 * \param client  The client.
 * \param channel The channel ID.
 * \param icon    The icon to use.
 */
void xzibit_client_set_icon (XzibitClient *client,
                             int channel,
                             GdkPixbuf *icon);

/**
 * Sends a still image (and may be misnamed).
 *
 * \param client  The client.
 * \param channel The channel ID.
 * \param image   The image to send.
 */
void xzibit_client_send_video (XzibitClient *client,
                               int channel,
                               GdkPixbuf *image);
/**
 * Sends audio.
 *
 * \param client  The client.
 * \param channel The channel ID of the video channel.
 *                Don't bother working out audio channel
 *                IDs; the client does that for you.
 * \param wave    16-bit PCM data at 44100 Hz.
 * \param length  Length of "wave", in bytes.
 */
void xzibit_client_send_audio (XzibitClient *client,
                               int channel,
                               gpointer wave,
                               gsize length);

/**
 * Records that the pointer is at a particular position.
 *
 * \param client  The client.
 * \param channel The channel ID.
 * \param x       The X coordinate.
 * \param y       The Y coordinate.
 */
void xzibit_client_move_pointer (XzibitClient *client,
                                 int channel,
                                 int x,
                                 int y);
/**
 * Displays an alert dialogue on the remote host.
 * (The remote host may choose to ignore this.)
 *
 * \param client  The client.
 * \param error   An ill-defined error ID, but at least
 *                we specify that you should use 0 for no error.
 * \param message A UTF-8 formatted message to display.
 */
void xzibit_client_send_wall (XzibitClient *client,
                              int error,
                              char *message);


#endif
