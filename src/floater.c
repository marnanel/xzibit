/*
  g_signal_connect (window, "delete_event", G_CALLBACK (gtk_main_quit), NULL);
*/
#include <gtk/gtk.h>
#include <gdk-pixbuf/gdk-pixbuf.h>

int
main(int argc, char **argv)
{
  GtkWidget *window;
  GtkWidget *image;
  GdkBitmap *mask;
  GdkPixbuf *pixbuf;
  GdkGC *gc;

  gtk_init (&argc, &argv);

  window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
  g_signal_connect (window, "delete_event", G_CALLBACK (gtk_main_quit), NULL);
  gtk_window_set_decorated (GTK_WINDOW (window), FALSE);

  pixbuf = gdk_pixbuf_new_from_file ("romeo.png",
				     NULL);

  if (!pixbuf)
    {
      g_error ("Could not load image");
    }

  mask = gdk_pixmap_new (NULL,
			 gdk_pixbuf_get_width (pixbuf),
			 gdk_pixbuf_get_height (pixbuf),
			 1);

  gdk_pixbuf_render_threshold_alpha (pixbuf,
				     mask,
				     0, 0, 0, 0,
				     -1, -1,
				     127);

  gtk_widget_shape_combine_mask (window,
				 mask,
				 0, 0);

  image = gtk_image_new_from_pixbuf (pixbuf);
  gtk_container_add (GTK_CONTAINER (window),
		     image);

  gtk_widget_show_all (window);

  gtk_main ();
}
