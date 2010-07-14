/*
 * gcc `pkg-config --cflags --libs gtk+-2.0` state-toggle.c -o state-toggle
 *
 * This is an X client which allows you to cycle through the possible states
 * of the _XZIBIT_SHARE property to see its effect on the UI.
 */
#include <gtk/gtk.h>
#include <gdk/gdkx.h>
#include <stdio.h>

GtkWidget *window;

static void
clicked_cb (GtkWidget *widget,
            gpointer   data)
{
  int param = GPOINTER_TO_INT(data);

  gdk_property_change (window->window,
		       gdk_atom_intern ("_XZIBIT_SHARE", FALSE),
		       gdk_atom_intern ("CARDINAL", FALSE),
		       8,
		       GDK_PROP_MODE_REPLACE,
		       (const guchar*) &param,
		       1);
}


int
main(int argc, char **argv)
{
  GtkWidget *vbox, *b1, *b2, *b3, *b4;

  gtk_init (&argc, &argv);

  window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
  g_signal_connect (window, "delete_event", G_CALLBACK (gtk_main_quit), NULL);

  vbox = gtk_vbox_new (FALSE, 0);

  b1 = gtk_button_new_with_label ("Ordinary unshared window");
  g_signal_connect (G_OBJECT (b1), "clicked", G_CALLBACK (clicked_cb), GINT_TO_POINTER(0));
  b2 = gtk_button_new_with_label ("Window being shared");
  g_signal_connect (G_OBJECT (b2), "clicked", G_CALLBACK (clicked_cb), GINT_TO_POINTER(1));
  b3 = gtk_button_new_with_label ("Window received from contact");
  g_signal_connect (G_OBJECT (b3), "clicked", G_CALLBACK (clicked_cb), GINT_TO_POINTER(2));
  b4 = gtk_button_new_with_label ("Unshareable window");
  g_signal_connect (G_OBJECT (b4), "clicked", G_CALLBACK (clicked_cb), GINT_TO_POINTER(3));

  gtk_box_pack_start (GTK_BOX (vbox),
		      b1,
		      TRUE, TRUE, 0);

  gtk_box_pack_start (GTK_BOX (vbox),
		      b2,
		      TRUE, TRUE, 0);

  gtk_box_pack_start (GTK_BOX (vbox),
		      b3,
		      TRUE, TRUE, 0);

  gtk_box_pack_start (GTK_BOX (vbox),
		      b4,
		      TRUE, TRUE, 0);

  gtk_container_add (GTK_CONTAINER(window), vbox);

  gtk_widget_show_all (window);

  gtk_main ();
}
