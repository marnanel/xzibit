#include <gtk/gtk.h>
#include <gdk/gdkx.h>
#include <vncdisplay.h>
#include <X11/X.h>

int remote_server = 1;
int port = 7177;
int id = 0;
gboolean is_override_redirect = FALSE;
int following_fd = -1;

typedef enum {
  STATE_START,
  STATE_SEEN_HEADER,
  STATE_SEEN_CHANNEL,
  STATE_SEEN_LENGTH,
} FdReadState;
FdReadState fd_read_state = STATE_START;
int fd_read_through = 0;
int fd_read_channel = 0;
int fd_read_length = 0;
char* fd_read_buffer = NULL;

static const GOptionEntry options[] =
{
	{
	  "port", 'p', 0, G_OPTION_ARG_INT, &port,
	  "The port number on localhost to connect to", NULL },
	{
	  "id", 'i', 0, G_OPTION_ARG_INT, &id,
	  "The Xzibit ID of the window", NULL },
	{
	  "fd", 'f', 0, G_OPTION_ARG_INT, &following_fd,
	  "The file descriptor which conveys the RFB protocol", NULL },
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

static void
handle_xzibit_message (int channel,
		       unsigned char *buffer,
		       unsigned int length)
{
  if (channel==0)
    {
      /* Control channel. */
      unsigned char opcode;

      if (length==0)
	/* Empty messages are valid but ignored. */
	return;

      opcode = buffer[0];

      g_print ("Control channel opcode %x\n", opcode);
    }
  else
    {
      /* One of the RFB channels. */

      g_print ("RFB channel %d\n", channel);
    }
}

static gboolean
check_for_fd_input (GIOChannel *source,
		    GIOCondition condition,
		    gpointer data)
{
  char buffer[1024];
  int fd = g_io_channel_unix_get_fd (source);
  int count, i;
  char want_header[] = "Xz 000.001\r\n";

  count = read (fd, &buffer, sizeof(buffer));

  if (count<0) {
    perror ("xzibit");
    return;
  }
  if (count==0) {
    return;
  }
  
  for (i=0; i<count; i++)
    {
      switch (fd_read_state)
	{
	case STATE_START:
	  if (want_header[fd_read_through] != buffer[i])
	    {
	      g_error ("Header not received");
	    }

	  fd_read_through++;

	  if (want_header[fd_read_through]==0)
	    {
	      fd_read_through = 0;
	      fd_read_state = STATE_SEEN_HEADER;
	    }
	  break;

	case STATE_SEEN_HEADER:
	  /* Seen header; read channel */
	  switch (fd_read_through)
	    {
	    case 0:
	      fd_read_channel = buffer[i];
	      fd_read_through = 1;
	      break;

	    case 1:
	      fd_read_channel |= buffer[i]*256;
	      fd_read_through = 0;
	      fd_read_state = STATE_SEEN_CHANNEL;
	      break;
	    }
	  break;

	case STATE_SEEN_CHANNEL:
	  /* Seen channel; read length */
	  switch (fd_read_through)
	    {
	    case 0:
	      fd_read_length = buffer[i];
	      fd_read_through = 1;
	      break;

	    case 1:
	      fd_read_length |= buffer[i]*256;
	      fd_read_buffer = g_malloc (fd_read_length);
	      fd_read_through = 0;
	      fd_read_state = STATE_SEEN_LENGTH;
	      break;
	    }
	  break;

	case STATE_SEEN_LENGTH:
	  /* Seen length; read data */
	  fd_read_buffer[fd_read_through] = buffer[i];
	  fd_read_through++;
	  if (fd_read_through==fd_read_length)
	    {
	      handle_xzibit_message (fd_read_channel,
				     fd_read_buffer,
				     fd_read_length);
	      g_free (fd_read_buffer);
	      fd_read_through = 0;
	      fd_read_state = STATE_SEEN_HEADER;
	    }
	}
      
    }
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

  if (following_fd!=-1)
    {
      GIOChannel *channel;
      g_print ("Following FD %d\n", following_fd);
      channel = g_io_channel_unix_new (following_fd);
      g_io_add_watch (channel,
                      G_IO_IN,
                      check_for_fd_input,
                      NULL);
    }

  window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
  g_signal_connect (window, "delete_event", G_CALLBACK (gtk_main_quit), NULL);

  /* for now, it's not resizable */
  gtk_window_set_resizable (GTK_WINDOW (window), FALSE);

  vnc = vnc_display_new();
  g_signal_connect(vnc, "vnc-initialized", G_CALLBACK(vnc_initialized), window);
  g_signal_connect(vnc, "vnc-disconnected", G_CALLBACK(gtk_main_quit), NULL);

#if 0
  port_as_string = g_strdup_printf ("%d", port);
  vnc_display_open_host (VNC_DISPLAY (vnc),
			 "127.0.0.1",
			 port_as_string);
  /* FIXME: instead, use
     vnc_display_open_fd(VNC_DISPLAY (vnc), fd);
     */
  g_free (port_as_string);
#endif

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
       * It happens because the image is drawn on the expose
       * event, and GTK never calls the expose event handler
       * on override-redirect windows.
       * (at least, on classes subclassing GtkWidget; if you
       * do it with a signal it works fine.  Need to investigate
       * this further.)
       */

       }
  gtk_widget_show_all (window);

  g_warning ("RFB client shown window.\n");
  set_window_remote (window);
  set_window_id (window, remote_server, id);

  if (is_override_redirect)
    {
      /* all right, it's been override-redirect
       * for quite long enough.
       */
      gdk_window_set_override_redirect (GDK_WINDOW (window->window),
					FALSE);
     }

  gtk_main ();
}
