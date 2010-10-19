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

Window xid = 0;
GtkWidget *window, *vbox, *label, *transient;
gboolean pulsing = FALSE;
gboolean showing_events = FALSE;
gboolean no_share = FALSE;
gboolean menu = FALSE;
gboolean show_dialogue = FALSE;
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
	{ NULL, 0, 0, G_OPTION_ARG_NONE, NULL, NULL, 0 }

        /*
          A thought: what would be useful here is a switch "-L",
          loopback, which found any two accounts you were logged in to
          and had one another on their rosters.
        */
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
            g_warning ("%s on %x\n", source,
                       (int) GDK_WINDOW_XID (window->window));

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
 * Terminates the process with errorlevel 1
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
    exit (1);
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

  window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
  g_signal_connect (window, "delete_event", G_CALLBACK (gtk_main_quit), NULL);
  gtk_window_set_default_size (GTK_WINDOW (window),
			       100, 100);

  vbox = gtk_vbox_new (FALSE, 0);

  label = gtk_label_new ("xzibit");
  gtk_container_add (GTK_CONTAINER (vbox),
		     label);
  gtk_container_add (GTK_CONTAINER (window),
		     vbox);

  gtk_widget_show_all (window);

  gdk_window_set_events (window->window,
                         gdk_window_get_events (window->window) |
                         GDK_KEY_PRESS_MASK);
  g_signal_connect (window,
                    "key_press_event",
                    G_CALLBACK (key_pressed),
                    NULL);

  xid = GDK_WINDOW_XID (window->window);

  if (showing_events)
    {
      xev (xid);
    }

  g_timeout_add (1000, embiggen, NULL);
  g_timeout_add (7000, do_popups, NULL);

  gtk_main ();

}
