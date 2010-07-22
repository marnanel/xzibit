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

Window xid = 0;
GtkWidget *window, *label;
gboolean pulsing = FALSE;
gboolean showing_events = FALSE;
gboolean no_share = FALSE;

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
	  "Don't mark as shared after two seconds", NULL },
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

  label = gtk_label_new ("xzibit");
  gtk_container_add (GTK_CONTAINER (window),
		     label);

  gtk_widget_show_all (window);

  xid = GDK_WINDOW_XID (window->window);

  if (showing_events)
    {
      xev (xid);
    }

  g_timeout_add (1000, embiggen, NULL);

  gtk_main ();

}
