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

XzibitClient* xzibit_client_new (void);
void xzibit_client_free (XzibitClient *client);
void xzibit_client_send_avatar (XzibitClient *client,
                                GdkPixbuf *avatar);
int xzibit_client_open_channel (XzibitClient *client);
void xzibit_client_close_channel (XzibitClient *client,
                                  int channel);
void xzibit_client_set_title (XzibitClient *client,
                              int channel,
                              char *title);
void xzibit_client_set_icon (XzibitClient *client,
                             int channel,
                             GdkPixbuf *icon);
void xzibit_client_send_video (XzibitClient *client,
                               int channel,
                               GdkPixbuf *image);
void xzibit_client_send_audio (XzibitClient *client,
                               int channel,
                               gpointer wave,
                               gsize length);
void xzibit_client_move_pointer (XzibitClient *client,
                                 int channel,
                                 int x,
                                 int y);

#endif
