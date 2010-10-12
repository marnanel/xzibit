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
#include "select-window.h"
#include <gdk/gdkx.h>

#define _(x) (x)

typedef struct _MainWindowContext
{
  main_window_cb *callback;
  gpointer user_data;
  GtkWidget *main;
  GtkWidget *label;
  GtkWidget *button;
} MainWindowContext;

static gboolean
main_window_closed (GtkWidget *window,
                    GdkEvent *event,
                    gpointer context)
{
  g_free (context);
  return FALSE;
}

static void
update_label (MainWindowContext *context)
{
  char *string;

  if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (context->button)))
    {
      string = _("Please choose the window you'd like to share.");
    }
  else
    {
      string = _("Click the button to share a window.");
    }

  gtk_label_set_text (GTK_LABEL (context->label),
                      string);
}

gboolean
select_a_window (gpointer user_data)
{
  MainWindowContext *context =
    (MainWindowContext*) user_data;
  GdkWindow *window;
  Window result;

  window = GTK_WIDGET (context->main)->window;

  /* This blocks. */
  result = Select_Window (GDK_WINDOW_XDISPLAY (window),
                          GDK_SCREEN_XNUMBER (gdk_screen_get_default()));

  /* We're done, so un-toggle the button. */
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (context->button),
                                FALSE);

  context->callback ((long int) result,
                     context->user_data);

  return FALSE;
}

static void
button_toggled (GtkToggleButton *button,
                gpointer user_data)
{
  MainWindowContext *context =
    (MainWindowContext*) user_data;

  update_label (context);

  if (gtk_toggle_button_get_active (button))
    {
      g_timeout_add (10,
                     select_a_window,
                     context);
    }
}

GtkWidget *
show_main_window (main_window_cb callback,
                  gpointer user_data)
{
  MainWindowContext *context;
  GtkWidget *result;
  GtkWidget *vbox =
    gtk_vbox_new (0, FALSE);
  GdkGeometry geometry;

  context = g_malloc (sizeof (MainWindowContext));
  context->callback = callback;
  context->user_data = user_data;
  context->label = gtk_label_new ("");
  context->button = gtk_toggle_button_new_with_label (_("Share"));

  result = gtk_window_new (GTK_WINDOW_TOPLEVEL);
  context->main = result;

  geometry.min_width = 400;
  geometry.min_height = 100;

  gtk_window_set_geometry_hints (GTK_WINDOW (result),
				 NULL,
				 &geometry,
				 GDK_HINT_MIN_SIZE);

  gtk_window_set_title (GTK_WINDOW (result),
                        _("Xzibit"));

  g_signal_connect (result,
		    "delete-event",
		    G_CALLBACK (main_window_closed),
		    context);

  g_signal_connect (context->button,
		    "toggled",
		    G_CALLBACK (button_toggled),
		    context);

  gtk_container_add (GTK_CONTAINER (result),
		     vbox);

  gtk_box_pack_end (GTK_BOX (vbox),
		    context->button,
		    TRUE, TRUE, 0);
  gtk_box_pack_end (GTK_BOX (vbox),
		    context->label,
		    FALSE, FALSE, 0);

  update_label (context);

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
