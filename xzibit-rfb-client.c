#include <gtk/gtk.h>
#include <gdk/gdkx.h>
#include <vncdisplay.h>
#include <X11/X.h>

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

int port = 7177;

static const GOptionEntry options[] =
{
	{
	  "port", 'p', 0, G_OPTION_ARG_INT, &port,
	  "The port number on localhost to connect to", NULL },
	{ NULL, 0, 0, G_OPTION_ARG_NONE, NULL, NULL, 0 }
};

static void
set_window_remote (GtkWidget *window)
{
  guint32 window_is_remote = 2;

  XChangeProperty (gdk_x11_get_default_xdisplay (),
		   GDK_WINDOW_XID (window->window),
		   gdk_x11_get_xatom_by_name("_XZIBIT_SHARE"),
		   gdk_x11_get_xatom_by_name("CARDINAL"),
		   32,
		   PropModeReplace,
		   (const unsigned char*) &window_is_remote,
		   1);
}

static void vnc_initialized(GtkWidget *vnc, GtkWidget *window)
{
  gtk_window_set_title (GTK_WINDOW (window),
			vnc_display_get_name (VNC_DISPLAY (vnc)));
}

int
main (int argc, char **argv)
{
  GtkWidget *window;
  GtkWidget *vnc;
  char *port_as_string;
  GOptionContext *context;
  GError *error = NULL;

  gtk_init (&argc, &argv);

  context = g_option_context_new ("Xzibit RFB client");
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

  /* for now, it's not resizable */
  gtk_window_set_resizable (GTK_WINDOW (window), FALSE);

  vnc = vnc_display_new();
  g_signal_connect(vnc, "vnc-initialized", G_CALLBACK(vnc_initialized), window);
  g_signal_connect(vnc, "vnc-disconnected", G_CALLBACK(gtk_main_quit), NULL);

  port_as_string = g_strdup_printf ("%d", port);
  vnc_display_open_host (VNC_DISPLAY (vnc),
			 "127.0.0.1",
			 port_as_string);
  g_free (port_as_string);

  gtk_container_add (GTK_CONTAINER (window), vnc);

  gtk_widget_show_all (window);
  set_window_remote (window);

  gtk_main ();
}
