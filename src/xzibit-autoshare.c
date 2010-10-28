/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Autoshare - testing program for xzibit.
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
#include <gtk/gtk.h>
#include <gdk/gdkx.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>

#define _(x) (x)

Window xid = 0;
GtkWidget *window, *vbox, *label, *transient;
gboolean pulsing = FALSE;
gboolean showing_events = FALSE;
gboolean no_share = FALSE;
gboolean menu = FALSE;
gboolean show_dialogue = FALSE;
gboolean loopback = FALSE;
gchar *source = NULL;
gchar *target = NULL;

static const GOptionEntry options[] =
{
	{
	  "pulse", 'p', 0, G_OPTION_ARG_NONE, &pulsing,
	  "Change the contents of the window every second", NULL },
	{
	  "events", 'e', 0, G_OPTION_ARG_NONE, &showing_events,
	  "Display all events that occur on the window", NULL },
	{
	  "no-share", 'n', 0, G_OPTION_ARG_NONE, &no_share,
	  "Don't mark as shared after some delay", NULL },
        {
          "menu", 'm', 0, G_OPTION_ARG_NONE, &menu,
          "Pop up a menu after some delay", NULL },
        {
          "dialogue", 'd', 0, G_OPTION_ARG_NONE, &show_dialogue,
          "Pop up a transient dialogue after two seconds", NULL },
        {
          "source", 's', 0, G_OPTION_ARG_STRING, &source,
          "Account ID to send from (not needed if you only have one)", NULL },
        {
          "target", 't', 0, G_OPTION_ARG_STRING, &target,
          "Account ID to send to (must have capabilities)", NULL },
        {
          "loopback", 'L', 0, G_OPTION_ARG_NONE, &loopback,
          "Work out source and target automatically", NULL },
	{ NULL, 0, 0, G_OPTION_ARG_NONE, NULL, NULL, 0 }
};

static void
xev (Window xid)
{
  char **argv = g_malloc (sizeof(char*) * 6);

  argv[0] = "xev";
  argv[1] = "-id";
  argv[2] = g_strdup_printf("0x%x", (int) xid);
  argv[3] = "-display";
  argv[4] = g_strdup_printf("%s",
                            DisplayString (gdk_x11_get_default_xdisplay ()));
  argv[5] = NULL;

  g_spawn_async ("/",
		 argv,
		 NULL,
		 G_SPAWN_SEARCH_PATH,
		 NULL, NULL, NULL, NULL);

  g_free (argv[2]);
  g_free (argv);
}

static void
menu_position (GtkMenu *menu,
               gint *x,
               gint *y,
               gboolean *push_in,
               gpointer user_data)
{
  gdk_window_get_position (window->window,
                           x, y);
  *push_in = TRUE;
}

static void
pop_up_menu (void)
{
  GtkWidget *menubar = gtk_menu_bar_new ();
  GtkWidget *menu = gtk_menu_new ();
  GtkWidget *item = gtk_menu_item_new_with_mnemonic ("_Hello world!");
  GtkWidget *file = gtk_menu_item_new_with_mnemonic ("_File");

  gtk_widget_show_all (item);
  gtk_widget_show_all (file);
  gtk_widget_show_all (menu);
  gtk_widget_show_all (menubar);

  gtk_menu_append (GTK_MENU (menu),
                   item);

  gtk_menu_item_set_submenu (GTK_MENU_ITEM (file),
                             menu);

  gtk_menu_bar_append (GTK_MENU_BAR (menubar),
                       file);

  gtk_box_pack_start (GTK_BOX (vbox), menubar, FALSE, FALSE, 2);

  gtk_menu_item_select (GTK_MENU_ITEM (file));
}

static void
pop_up_transient (void)
{
  GtkWidget *label;

  if (transient)
    return;

  transient = gtk_window_new (GTK_WINDOW_TOPLEVEL);
  label = gtk_label_new ("Badgers");
  gtk_container_add (GTK_CONTAINER (transient),
		     label);

  gtk_window_set_transient_for (GTK_WINDOW (transient),
                                GTK_WINDOW (window));

  gtk_widget_show_all (transient);
}

static gboolean
embiggen (gpointer data)
{
  static gboolean big = FALSE;
  static gboolean shared = FALSE;
  const char *message;

  big = !big;

  if (big) {
    message = "<big><big><b>xzibit</b></big></big>";
  } else {
    message = "xzibit";

    if (!shared && !no_share)
      {
	guint32 window_is_shared = 1;

        if (source)
          {
            XChangeProperty (gdk_x11_get_default_xdisplay (),
                             GDK_WINDOW_XID (window->window),
                             gdk_x11_get_xatom_by_name("_XZIBIT_SOURCE"),
                             gdk_x11_get_xatom_by_name("UTF8_STRING"),
                             8,
                             PropModeReplace,
                             (const unsigned char*) source,
                             strlen(source));
          }

        if (target)
          {
            XChangeProperty (gdk_x11_get_default_xdisplay (),
                             GDK_WINDOW_XID (window->window),
                             gdk_x11_get_xatom_by_name("_XZIBIT_TARGET"),
                             gdk_x11_get_xatom_by_name("UTF8_STRING"),
                             8,
                             PropModeReplace,
                             (const unsigned char*) target,
                             strlen(target));
          }

	XChangeProperty (gdk_x11_get_default_xdisplay (),
			 GDK_WINDOW_XID (window->window),
			 gdk_x11_get_xatom_by_name("_XZIBIT_SHARE"),
			 gdk_x11_get_xatom_by_name("CARDINAL"),
			 32,
			 PropModeReplace,
			 (const unsigned char*) &window_is_shared,
			 1);
	
	shared = TRUE;
      }
  }

  if (pulsing)
    {
      gtk_label_set_markup (GTK_LABEL (label), message);
    }

  return TRUE;
}

static gboolean
do_popups (gpointer dummy)
{
  if (menu)
    {
      pop_up_menu ();
    }
  
  if (show_dialogue)
    {
      pop_up_transient ();
    }

    return FALSE;
}

/**
 * Handler for keypresses on the main window.
 * Terminates the process with errorlevel 101
 * if the user types the word "xzibit".
 */
static gboolean
key_pressed (GtkWidget *widget,
             GdkEventKey *event,
             gpointer user_data)
{
  static gchar *dismissal_word = "xzibit";
  static gchar *dismissal_cursor;

  if (dismissal_cursor==NULL)
    dismissal_cursor = dismissal_word;

  if (event->keyval==*dismissal_cursor)
    dismissal_cursor++;
  else
    dismissal_cursor = dismissal_word;

  if (!*dismissal_cursor)
    exit (101);

  return TRUE;
}

/**
 * Handler for keypresses on the main window.
 * Terminates the process with errorlevel 102
 * if the user clicks at exactly (77, 77).
 */
static gboolean
button_pressed (GtkWidget *widget,
                GdkEventButton *event,
                gpointer user_data)
{
  int x = floor (event->x);
  int y = floor (event->y);

  g_print ("Clicked at %d, %d\n",
           x, y);

  if (x==77 &&
      y==77)
    {
      exit (102);
    }

  return TRUE;
}

static GdkFilterReturn
event_filter (GdkXEvent *xevent,
	      GdkEvent *event,
	      gpointer user_data)
{
  XEvent *ev = (XEvent*) xevent;

  if (ev->type == PropertyNotify)
    {
      XPropertyEvent *propev =
	(XPropertyEvent*) xevent;

      if (propev->atom ==
          gdk_x11_get_xatom_by_name("_XZIBIT_RESULT") &&
	  propev->state == PropertyNewValue)
	{
	  Atom actual_type;
	  int actual_format;
	  long n_items, bytes_after;
	  unsigned char *prop_return;
	  int result_value = 0;
	  char *message = NULL;

	  XGetWindowProperty (gdk_x11_get_default_xdisplay (),
			      propev->window,
                              gdk_x11_get_xatom_by_name("_XZIBIT_RESULT"),
			      0, 4, False,
			      gdk_x11_get_xatom_by_name ("INTEGER"),
			      &actual_type,
			      &actual_format,
			      &n_items,
			      &bytes_after,
			      &prop_return);

	  if (prop_return)
	    {
	      result_value = *((int*) prop_return);
	      XFree (prop_return);
	    }

	  switch (result_value)
	    {
	    case 100:
	      message =
		g_strdup_printf (_("Window successfully unshared."));
	      break;

	    case 101:
	      message =
		g_strdup_printf (_("Window successfully shared."));
	      break;

	    case 103:
	      /*
	       * Window is being made unshareable; there's no
	       * need to tell the user about it.  We did need
	       * to monitor, though, because it's the first thing
	       * we do, and if xzibit isn't installed we'll know
	       * about it very quickly.
	       */
	      break;

	    case 200:
	      message =
		g_strdup_printf (_("Connecting; please wait."));
	      /*
	       * FIXME: Probably should pop up something with
	       * one of those non-percentage progress bars here.
	       */
	      break;

	    case 301:
	      message =
		g_strdup_printf (_("Can't share window: you are "
				   "not logged in from the correct "
				   "account."));
	      break;

	    case 302:
	      message =
		g_strdup_printf (_("Can't share window: the person "
				   "you selected is not one of "
				   "your contacts."));
	      break;

	    case 312:
	      message =
		g_strdup_printf (_("Can't share window: the contact "
				   "you selected is not running "
				   "xzibit."));
	      break;

	    case 322:
	      message =
		g_strdup_printf (_("Can't share window: the contact "
				   "you selected has rejected the "
				   "connection."));
	      break;

	    default:
	      message =
		g_strdup_printf (_("Xzibit sent a code I didn't understand: %d"),
				 result_value);
	    }


          g_print ("%s %d\n", message, result_value);
	}
    }

  return GDK_FILTER_CONTINUE;
}


static gboolean
draw_window (gpointer user_data)
{ 
  GOptionContext *context = user_data;

  window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
  g_signal_connect (window, "delete_event", G_CALLBACK (gtk_main_quit), NULL);
  gtk_window_set_default_size (GTK_WINDOW (window),
			       100, 100);

  gdk_window_add_filter (window->window,
			 event_filter,
			 NULL);

  vbox = gtk_vbox_new (FALSE, 0);

  label = gtk_label_new ("xzibit");
  gtk_container_add (GTK_CONTAINER (vbox),
		     label);
  gtk_container_add (GTK_CONTAINER (window),
		     vbox);

  gtk_widget_show_all (window);

  gdk_window_set_events (window->window,
                         gdk_window_get_events (window->window) |
                         GDK_KEY_PRESS_MASK|
                         GDK_BUTTON_PRESS_MASK);

  g_signal_connect (window,
                    "key_press_event",
                    G_CALLBACK (key_pressed),
                    NULL);
  g_signal_connect (window,
                    "button_press_event",
                    G_CALLBACK (button_pressed),
                    NULL);

  xid = GDK_WINDOW_XID (window->window);

  if (showing_events)
    {
      xev (xid);
    }

  g_timeout_add (1000, embiggen, NULL);
  g_timeout_add (7000, do_popups, NULL);

  return FALSE;
}

static void
loopback_found (const gchar *source_path,
                const gchar *source_id,
                const gchar *target_id,
                gpointer user_data)
{
  GOptionContext *context = user_data;

  if (source_path && target_id)
    {
      source = g_strdup (source_path);
      target = g_strdup (target_id);

      g_timeout_add (0, draw_window, context);
    }
  else
    {
      g_warning ("No loopback found; can't continue.");
    }
}

int
main(int argc, char **argv)
{
  GOptionContext *context;
  GError *error = NULL;

  gtk_init (&argc, &argv);

  context = g_option_context_new ("Autoshare");
  g_option_context_add_main_entries (context, options, NULL);
  g_option_context_parse (context, &argc, &argv, &error);
  if (error)
    {
      g_print ("%s\n", error->message);
      g_error_free (error);
      return 1;
    }

  if ((source || target) && loopback)
    {
      g_print ("You can't loopback and supply source or target.\n");
      return 2;
    }

  if (!(source && target) && !loopback)
    {
      g_print ("You must supply either --loopback or both --source and --target.\n");
      return 3;
    }

  if (loopback)
    {
      find_loopback (loopback_found,
                     "x-xzibit",
                     context);
    }
  else
    {
      /*
       * just call it.  Call it after GTK starts up
       * so that it's always called in the same phase
       * of things.
       */
      g_timeout_add (0, draw_window, context);
    }

  gtk_main ();

}
