/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * xzibit-test-send - fakes keystrokes and mouse clicks.
 * Used to test xzibit, but could be more generally useful.
 *
 * Author: Thomas Thurman <thomas.thurman@collabora.co.uk>
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

#include <stdlib.h>
#include <gtk/gtk.h>
#include <X11/Xlib.h>
#include <gdk/gdkx.h>

const char *message =
  "All work and no play makes Jack a dull boy.";
unsigned int window_id = 0;
gboolean received = FALSE;
gboolean fake_keypresses = FALSE;
gboolean fake_mouseclicks = FALSE;
unsigned int x = 50, y = 50;

static const GOptionEntry options[] =
{
	{
	  "id", 'i', 0, G_OPTION_ARG_INT, &window_id,
	  "The window to send clicks and keypresses to", NULL },
	{
	  "received", 'r', 0, G_OPTION_ARG_NONE, &received,
	  "Send clicks and keypresses to the (single) xzibit-received window", NULL },
	{
	  "keypress", 'k', 0, G_OPTION_ARG_NONE, &fake_keypresses,
	  "Send keypresses to the window", NULL },
	{
	  "click", 'c', 0, G_OPTION_ARG_NONE, &fake_mouseclicks,
	  "Send mouse clicks to the window", NULL },
	{
	  "message", 'm', 0, G_OPTION_ARG_STRING, &message,
	  "Message to type into the window", NULL },
	{
	  "x", 'x', 0, G_OPTION_ARG_INT, &x,
	  "X-coordinate within the window to click at", NULL },
	{
	  "y", 'y', 0, G_OPTION_ARG_INT, &y,
	  "Y-coordinate within the window to click at", NULL },
	{ NULL, 0, 0, G_OPTION_ARG_NONE, NULL, NULL, 0 }
};

/**
 * Checks for the "received" flag on a window and all its
 * descendants.  Returns the ID of the window if a single
 * window has the "received" flag, 0 if none do, and -1 if
 * more than one does.
 *
 * \param window  The window ID.
 */
static int
id_of_received_window_tail (Window window)
{
  Window root, parent;
  Window *kids;
  Window found = 0;
  int window_is_shared;
  Atom actual_type;
  int actual_format;
  long n_items, bytes_after;
  unsigned char *prop_return;
  unsigned int n_kids, i;

  if (XQueryTree (gdk_x11_get_default_xdisplay (),
		  window,
		  &root,
		  &parent,
		  &kids,
		  &n_kids)==0)
    {
      return 0;
    }

  for (i=0; i<n_kids; i++)
    {
      Window candidate = id_of_received_window_tail (kids[i]);

      if (candidate && !found)
	{
	  found = candidate;
	}
      else if (candidate)
	{
	  found = -1;
	}
    }

  if (kids)
    {
      XFree (kids);
    }

  XGetWindowProperty(gdk_x11_get_default_xdisplay (),
		     window,
		     gdk_x11_get_xatom_by_name("_XZIBIT_SHARE"),
		     0, 4, False,
		     gdk_x11_get_xatom_by_name("CARDINAL"),
		     &actual_type,
		     &actual_format,
		     &n_items,
		     &bytes_after,
		     &prop_return);

  window_is_shared = 0;

  if (prop_return) {
    window_is_shared = *((int*) prop_return);
    XFree (prop_return);
  }

  if (window_is_shared==2)
    {
      if (found != 0)
	{
	  return -1; /* too many received windows */
	}
      else
	{
	  return (int) window;
	}
    }
  else
    {
      return found;
    }
}

static int
id_of_received_window (void)
{
  return id_of_received_window_tail (gdk_x11_get_default_root_xwindow ());
}

static void
parse_options (int argc, char **argv)
{
  GOptionContext *context;
  GError *error = NULL;

  context = g_option_context_new ("xzibit-test-send");
  g_option_context_add_main_entries (context, options, NULL);
  g_option_context_parse (context, &argc, &argv, &error);
  if (error)
    {
      g_print ("%s: %s\n",
	       argv[0],
	       error->message);
      g_error_free (error);
      exit (1);
    }

  if (!fake_keypresses &&
      !fake_mouseclicks)
    {
      g_print ("%s: Nothing to do!  Use --help for help.\n",
	       argv[0]);
      exit (2);
    }

  if (received)
    {
      window_id = id_of_received_window ();
    }

  g_print ("Results are: %x %d %d %s %d %d\n", 
	   window_id,
	   fake_keypresses, fake_mouseclicks,
	   message,
	   x, y);
}

/**
 * Initialises appropriate X extensions.
 */
static void
initialise_extensions (void)
{
  int major = 2, minor = 0;
  if (XIQueryVersion (gdk_x11_get_default_xdisplay (),
                      &major, &minor) == BadRequest) {
    g_error("XI2 not available. Server supports %d.%d\n", major, minor);
  }
}

int
main(int argc, char **argv)
{
  gtk_init (&argc, &argv);

  initialise_extensions ();

  parse_options (argc, argv);
}
