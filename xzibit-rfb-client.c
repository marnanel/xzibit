#include <gtk/gtk.h>
#include <gdk/gdkx.h>
#include <vncdisplay.h>
#include <X11/X.h>

int remote_server = 1;
int port = 7177;
int id = 0;
gboolean is_override_redirect = FALSE;

static const GOptionEntry options[] =
{
	{
	  "port", 'p', 0, G_OPTION_ARG_INT, &port,
	  "The port number on localhost to connect to", NULL },
	{
	  "id", 'i', 0, G_OPTION_ARG_INT, &id,
	  "The Xzibit ID of the window", NULL },
	{
	  "remote-server", 'r', 0, G_OPTION_ARG_INT, &remote_server,
	  "The Xzibit code for the remote server", NULL },
	{"override-redirect", 'o', 0, G_OPTION_ARG_NONE, &is_override_redirect,
	"Make the client window override-redirect", NULL },
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

static void
set_window_id (GtkWidget *window,
	       guint32 remote_server,
	       guint32 id)
{
  guint32 ids[2] = { remote_server,
		     id };

  if (id==0)
    return;

  XChangeProperty (gdk_x11_get_default_xdisplay (),
		   GDK_WINDOW_XID (window->window),
		   gdk_x11_get_xatom_by_name("_XZIBIT_ID"),
		   gdk_x11_get_xatom_by_name("CARDINAL"),
		   32,
		   PropModeReplace,
		   (const unsigned char*) &ids,
		   2);
}

static void vnc_initialized(GtkWidget *vnc, GtkWidget *window)
{
  /* nothing */
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

  g_warning ("RFB client starting...\n");

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

  if (is_override_redirect)
    {
      /* We have a window ID but it won't have been
       * mapped yet.  Now is the ideal time to go
       * override-redirect.
       */
      gdk_window_set_override_redirect (GDK_WINDOW (window->window),
					TRUE);
      gdk_window_show (GDK_WINDOW (window->window));
      /* otherwise it won't map */
      
      /* and now we have another problem: gtk-vnc won't draw
       * on this window
       * This is a specific gtk-vnc issue: tests show that
       * gtk itself has no problem drawing on an override-redirect
       * window.
       */

       }
  gtk_widget_show_all (window);


  g_warning ("RFB client shown window.\n");
  set_window_remote (window);
  set_window_id (window, remote_server, id);

  gtk_main ();
}
