/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Contact chooser dialogue for xzibit.
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

/*
 * TO DO:
 *
 * Sorting the list would be useful.
 */
#ifndef CONTACT_CHOOSER
#define CONTACT_CHOOSER 1

#include <gtk/gtk.h>

typedef void contact_chooser_cb (int,
				 const char*,
				 const char*);

GtkWidget *
show_contact_chooser (int window,
		      contact_chooser_cb callback);

#endif /* !CONTACT_CHOOSER */
