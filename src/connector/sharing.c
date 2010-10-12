#include "sharing.h"
#include <gtk/gtk.h>

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

  gtk_main ();
}

#endif /* !SHARING_TEST */

/* EOF sharing.c */
