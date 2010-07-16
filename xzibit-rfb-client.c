#include <gtk/gtk.h>
#include <vncdisplay.h>
/*
 * things this should be able to do eventually:
 *
 * - display VNC!
 * - resize
 * - mark the window with _XZIBIT_SHARE==2
 * - display with no borders
 * - make the window a child of a given other window
 * - make the window transient to a given other window
 */

int
main (int argc, char **argv)
{
  GtkWidget *window;
  GtkWidget *vnc;

  gtk_init (&argc, &argv);

  window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
  g_signal_connect (window, "delete_event", G_CALLBACK (gtk_main_quit), NULL);

  /* for now, it's not resizable */
  gtk_window_set_resizable (GTK_WINDOW (window), FALSE);

  vnc = vnc_display_new();
  vnc_display_set_keyboard_grab (VNC_DISPLAY (vnc), TRUE);
  vnc_display_set_pointer_grab (VNC_DISPLAY (vnc), TRUE);
  vnc_display_open_host (VNC_DISPLAY (vnc),
			 "127.0.0.1",
			 "7177");

  gtk_container_add (GTK_CONTAINER (window), vnc);

  gtk_widget_show_all (window);

  gtk_main ();
}
