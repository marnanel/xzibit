#include "sharing.h"
#include <gtk/gtk.h>
#include <gdk/gdkx.h>

guint
window_get_sharing (Window window)
{
  /* stub */
  return 0;
}

void
window_set_sharing (Window window,
		    guint sharing,
		    const char* source,
		    const char* target)
{
  /* stub */
}

#ifdef SHARING_TEST

int stage = 0;

static gboolean
timeout (gpointer user_data)
{
  GtkWidget *window =
    (GtkWidget*) user_data;
  Window id =
    GDK_WINDOW_XID (window->window);

  switch (++stage)
    {
    case 1:
    case 3:
      g_print ("State of window %x is %d\n",
	       id, window_get_sharing (id));
      break;

    case 2:
      g_print ("Sharing the window.\n");
      window_set_sharing (id, 1,
			  NULL, NULL);
      break;

    case 4:
      return FALSE;
    }

  return TRUE;
}

int
main (int argc, char **argv)
{
  GtkWidget *window;

  gtk_init (&argc, &argv);
  
  window =
    gtk_window_new (GTK_WINDOW_TOPLEVEL);

  g_signal_connect (window,
		    "delete-event",
		    G_CALLBACK (gtk_main_quit),
		    NULL);

  gtk_widget_show_all (window);

  g_timeout_add (1000,
		 timeout,
		 window);

  gtk_main ();
}

#endif /* !SHARING_TEST */

/* EOF sharing.c */
