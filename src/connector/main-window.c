/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Main window for xzibit.
 *
 * Author: Thomas Thurman <thomas.thurman@collabora.co.uk>
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

#include "main-window.h"

#define _(x) (x)

typedef struct _MainWindowContext
{
  main_window_cb *callback;
  gpointer user_data;
} MainWindowContext;

gboolean
main_window_closed (GtkWidget *window,
                    GdkEvent *event,
                    gpointer user_data)
{
  g_free (user_data);
  return FALSE;
}

GtkWidget *
show_main_window (main_window_cb callback,
                  gpointer user_data)
{
  MainWindowContext *context;
  GtkWidget *result;

  context = g_malloc (sizeof (MainWindowContext));
  context->callback = callback;
  context->user_data = user_data;

  result = gtk_window_new (GTK_WINDOW_TOPLEVEL);

  gtk_window_set_title (GTK_WINDOW (result),
                        _("Xzibit"));

  g_signal_connect (result,
		    "delete-event",
		    G_CALLBACK (main_window_closed),
		    context);

  gtk_widget_show_all (result);
  return result;
}

#ifdef MAIN_WINDOW_TEST

static void
dump_main_window_result (int window,
                         void *user_data)
{
  g_print ("Result was: window %x\n",
	     window);
}

int
main (int argc, char **argv)
{
  GtkWidget *window;

  gtk_init (&argc, &argv);
  
  window =
    show_main_window (dump_main_window_result,
                      NULL);

  g_signal_connect (window,
		    "delete-event",
		    G_CALLBACK (gtk_main_quit),
		    NULL);

  gtk_main ();
}

#endif /* CONTACT_CHOOSER_TEST */
