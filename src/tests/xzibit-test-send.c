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
#include <X11/keysym.h>
#include <X11/extensions/XInput2.h>
#include <X11/extensions/XI2.h>
#include <X11/extensions/XInput.h>

const char *message =
  "All work and no play makes Jack a dull boy.";
const char *typing_cursor = NULL;
unsigned int window_id = 0;
gboolean received = FALSE;
gboolean fake_keypresses = FALSE;
gboolean fake_mouseclicks = FALSE;
gboolean use_xinput1 = FALSE;
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
        { "xinput1", '1', 0, G_OPTION_ARG_NONE, &use_xinput1,
          "Use XInput1 instead of XInput2", NULL},
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

      if (window_id==0)
        {
          g_print("%s: You requested a received window, but no windows "
                  "on this display are received.\n",
                  argv[0]);
          exit(3);
        }

      if (window_id==-1)
        {
          g_print("%s: You requested a received window, but multiple windows "
                  "on this display are received; specify which using --id.\n",
                  argv[0]);
          exit(3);
        }
    }
}

/**
 * Initialises appropriate X extensions.
 */
static void
initialise_extensions (void)
{
  if (!use_xinput1)
    {
      int major = 2, minor = 0;
      if (XIQueryVersion (gdk_x11_get_default_xdisplay (),
                          &major, &minor) == BadRequest) {
        g_error("XI2 not available. Server supports %d.%d.  (Try using '-1'.)\n",
                major, minor);
      }
    }
}

/**
 * Removes a pointer.
 *
 * \param mpx  An XID; this MUST be a master device.
 */
static void
drop_mpx (int mpx)
{
  XIRemoveMasterInfo drop;

  drop.type = XIRemoveMaster;
  drop.deviceid = mpx;
  drop.return_mode = XIAttachToMaster;
  drop.return_pointer = 2;
  drop.return_keyboard = 3;

  XIChangeHierarchy (gdk_x11_get_default_xdisplay (),
                     (XIAnyHierarchyChangeInfo*) &drop,
                     1);
}

static void
add_mpx_for_window (char *name,
                    int *master_kbd,
                    int *slave_kbd,
                    int *master_ptr)
{
  XIAddMasterInfo add;
  int ndevices;
  XIDeviceInfo *devices, *device;
  int i;

  /* add the device */

  add.type = XIAddMaster;
  add.name = name;
  add.send_core = True;
  add.enable = True;

  XIChangeHierarchy (gdk_x11_get_default_xdisplay (),
		     (XIAnyHierarchyChangeInfo*) &add,
		     1);

  /* now see whether it's in the list */

  *master_kbd = -1;
  *slave_kbd = -1;
  *master_ptr = -1;

  devices = XIQueryDevice(gdk_x11_get_default_xdisplay (),
			  XIAllDevices, &ndevices);

  for (i = 0; i < ndevices; i++) {
    device = &devices[i];

    if (g_str_has_prefix (device->name,
			  name))
      {
	switch (device->use)
	  {
	  case XIMasterKeyboard:
	    *master_kbd = device->deviceid;
	    break;

	  case XISlaveKeyboard:
	    *slave_kbd = device->deviceid;
	    break;

	  case XIMasterPointer:
	    *master_ptr = device->deviceid;
	    break;
	  }
      }
  }

  if (*master_kbd==-1 || *slave_kbd==-1 || *master_ptr==-1)
    {
      g_warning ("The new pointer '%s' could not be created.",
		 name);
    }

  XIFreeDeviceInfo(devices);
}

static void
fake_keystroke (int window_id,
                char symbol)
{
  int code = XKeysymToKeycode (gdk_x11_get_default_xdisplay (),
                               symbol);

  int dummy[1] = { 0 };

  if (use_xinput1)
    {
      if (XTestFakeKeyEvent(gdk_x11_get_default_xdisplay (),
                            code,
                            True, CurrentTime)==0)
        {
          g_warning ("Faking key event failed.");
        }

      if (XTestFakeKeyEvent(gdk_x11_get_default_xdisplay (),
                            code,
                            False, CurrentTime)==0)
        {
          g_warning ("Faking key event failed.");
        }
    }
  else
    {
      int xid_master_kbd, xid_slave_kbd, xid_master_ptr;
      int current_pointer;
      XDevice *dev;

      add_mpx_for_window ("xzibit-test-send",
                          &xid_master_kbd,
                          &xid_slave_kbd,
                          &xid_master_ptr);

      dev = XOpenDevice (gdk_x11_get_default_xdisplay (),
                         xid_slave_kbd);

      XIGetClientPointer (gdk_x11_get_default_xdisplay (),
                          None,
                          &current_pointer);

      XISetClientPointer (gdk_x11_get_default_xdisplay (),
                          None,
                          xid_master_ptr);

      XSetInputFocus (gdk_x11_get_default_xdisplay (),
                      (Window) window_id, PointerRoot,
                      CurrentTime);

      if (XTestFakeDeviceKeyEvent (gdk_x11_get_default_xdisplay (),
                                   dev,
                                   code,
                                   True,
                                   dummy, 0, CurrentTime)==0)
        {
          g_warning ("Faking key event failed.");
        }
      XFlush (gdk_x11_get_default_xdisplay ());

      if (XTestFakeDeviceKeyEvent (gdk_x11_get_default_xdisplay (),
                                   dev,
                                   code,
                                   False,
                                   dummy, 0, CurrentTime)==0)
        {
          g_warning ("Faking key event failed.");
        }
      XFlush (gdk_x11_get_default_xdisplay ());

      XISetClientPointer (gdk_x11_get_default_xdisplay (),
                          None,
                          current_pointer);
  
      XCloseDevice (gdk_x11_get_default_xdisplay (),
                    dev);

      drop_mpx (xid_master_kbd);
    }

  XFlush (gdk_x11_get_default_xdisplay ());
}

static void
fake_mouseclick (int window_id,
                 int x, int y)
{

  if (use_xinput1)
    {
      /*
       * we actually do warp the pointer, not just
       * fake warping it, for simplicity
       */
      XWarpPointer (gdk_x11_get_default_xdisplay (),
                    (Window) None, (Window) window_id,
                    0, 0, 0, 0,
                    x, y);
      XFlush (gdk_x11_get_default_xdisplay ());

      if (XTestFakeButtonEvent (gdk_x11_get_default_xdisplay (),
                                1, True,
                                CurrentTime)==0)
        {
          g_warning ("Faking button press failed.");
        }
      XFlush (gdk_x11_get_default_xdisplay ());

      if (XTestFakeButtonEvent (gdk_x11_get_default_xdisplay (),
                                1, False,
                                CurrentTime)==0)
        {
          g_warning ("Faking button release failed.");
        }
      XFlush (gdk_x11_get_default_xdisplay ());
    }
  else
    {
      g_warning ("XInput2 for clicks not yet written");
    }

  XFlush (gdk_x11_get_default_xdisplay ());
}

static gboolean
type_stuff (gpointer dummy)
{
  if (typing_cursor==NULL)
    typing_cursor = message;

  if (*typing_cursor==0)
    return FALSE;

  fake_keystroke (window_id,
                  *typing_cursor);

  typing_cursor++;

  return TRUE;
}

static gboolean
click_stuff (gpointer dummy)
{
  fake_mouseclick (window_id,
                   x, y);

  return FALSE;
}

int
main(int argc, char **argv)
{
  gtk_init (&argc, &argv);

  initialise_extensions ();

  parse_options (argc, argv);

  if (fake_keypresses)
    {
      g_timeout_add (50,
                     type_stuff,
                     NULL);
    }

  if (fake_mouseclicks)
    {
      g_timeout_add (100,
                     click_stuff,
                     NULL);
    }

  gtk_main ();
}
