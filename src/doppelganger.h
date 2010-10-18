/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Doppelganger pointers.
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

#ifndef DOPPELGANGER_H
#define DOPPELGANGER_H 1

#include <gdk-pixbuf/gdk-pixbuf.h>

typedef struct _Doppelganger Doppelganger;

/**
 * Creates a doppelganger.  A doppelganger is
 * a mouse pointer under the control of a remote user.
 *
 * \param name    An internal identifier for the mouse
 *                pointer as used by XInput2.
 */
Doppelganger* doppelganger_new (char *name);

/**
 * Sets an image on a doppelganger.
 *
 * \param pixbuf  An icon representing the remote
 *                user.  It will be scaled and used
 *                as part of the new mouse pointer.
 *                If this is NULL, there will be
 *                no custom image, just an arrow.
 */
void doppelganger_set_image (Doppelganger *dg,
                             GdkPixbuf *pixbuf);

/**
 * Moves a doppelganger around the screen.
 * If the doppelganger is invisible, it will
 * move, but you won't see it.
 *
 * \param dg  The doppelganger.
 * \param x   The X coordinate, relative to
 *            the top left corner of the screen.
 * \param y   The Y coordinate, relative to
 *            the top left corner of the screen.
 */
void doppelganger_move (Doppelganger *dg,
			int x, int y);

/**
 * Sets a doppelganger to be invisible.
 *
 * \param dg  The doppelganger.
 */
void doppelganger_hide (Doppelganger *dg);

/**
 * Sets a doppelganger to be visible.
 * This is the default.
 *
 * \param dg  The doppelganger.
 */
void doppelganger_show (Doppelganger *dg);

/**
 * Destroys a doppelganger.
 *
 * \param dg  The doppelganger.
 */
void doppelganger_free (Doppelganger *dg);

#endif /* DOPPELGANGER_H */
