#include <gtk/gtk.h>
#include <gdk/gdkx.h>
#include <vncdisplay.h>
#include <sys/socket.h>
#include <X11/X.h>

/****************************************************************
 * Some globals.  FIXME: most of them have been superseded.
 ****************************************************************/

int remote_server = 1;
int port = 7177;
int id = 0;
int following_fd = -1;

/****************************************************************
 * Definitions used for buffer reading.
 ****************************************************************/

typedef enum {
  STATE_START,
  STATE_SEEN_CHANNEL,
  STATE_SEEN_LENGTH,
} FdReadState;

FdReadState fd_read_state = STATE_START;
int fd_read_through = 0;
int fd_read_channel = 0;
int fd_read_length = 0;
char* fd_read_buffer = NULL;

/**
 * What we know about each received window.
 */

typedef struct {
  /**
   * The FD we're using to talk to gtk-vnc about
   * this window.
   */
  int fd;
  /**
   * Pointer to the window itself.
   */
  GtkWidget *window;
  /**
   * The xzibit ID of this window.
   */
  int id;

} XzibitReceivedWindow;

GHashTable *received_windows = NULL;
GHashTable *postponed_metadata = NULL;

/****************************************************************
 * Options.
 ****************************************************************/

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

static gboolean
check_for_rfb_replies (GIOChannel *source,
		       GIOCondition condition,
		       gpointer data)
{
  XzibitReceivedWindow *received = data;
  char buffer[1024];
  int fd = g_io_channel_unix_get_fd (source);
  int count;
  char header[4];

  count = read (fd, &buffer, sizeof(buffer));

  if (count<0)
    {
      /* FIXME this is silly */
      perror ("xzibit");
      g_error ("Something died downstream");
    }

  if (count==0)
    {
      return;
    }

  g_print ("We have %d bytes TO the rfb server\n", count);

  header[0] = received->id % 256;
  header[1] = received->id / 256;
  header[2] = count % 256;
  header[3] = count / 256;

  /* FIXME: check results */
  write (following_fd, header, sizeof (header));
  write (following_fd, buffer, count);
}

typedef void (MessageHandler) (int, unsigned char*, unsigned int);
GHashTable *message_handlers = NULL;

static void
handle_video_message (int channel,
		      unsigned char *buffer,
		      unsigned int length)
{
  XzibitReceivedWindow *received =
    g_hash_table_lookup (received_windows,
			 &channel);
      
  g_print ("We have %d bytes FROM the rfb server\n", length);
  /* FIXME: error checking; it could run short */
  write (received->fd,
	 buffer, length);
}

static void
handle_audio_message (int channel,
		      unsigned char *buffer,
		      unsigned int length)
{
  g_print ("We have %d bytes of audio to play.  FIXME.\n", length);
}

static void
apply_metadata_now (XzibitReceivedWindow *target,
		    int metadata_id,
		    unsigned char *buffer,
		    int length)
{
  /* FIXME */
  g_print ("Applying metadata type %d.\n",
	   metadata_id);
}

typedef struct _PostponedMetadata {
  int id;
  gsize length;
  char *content;
} PostponedMetadata;

static gboolean
exposed_window (GtkWidget *widget,
		GdkEventExpose *event,
		gpointer data)
{
  GSList *postponements, *cursor;
  int xzibit_id = GPOINTER_TO_INT (data);

  if (!postponed_metadata)
    {
      return FALSE;
    }

  postponements = g_hash_table_lookup (postponed_metadata,
				       GINT_TO_POINTER (xzibit_id));

  if (!postponements)
    {
      return FALSE;
    }

  g_hash_table_remove (postponed_metadata,
		       GINT_TO_POINTER (xzibit_id));

  cursor = postponements;

  while (cursor)
    {
      PostponedMetadata *metadata = cursor->data;
      XzibitReceivedWindow *received =
	g_hash_table_lookup (received_windows,
		     &xzibit_id);

      apply_metadata_now (received,
			  metadata->id,
			  metadata->content,
			  metadata->length);
      
      g_free (metadata->content);
      g_free (metadata);
      cursor = cursor->next;
    }

  g_slist_free (postponements);

  return FALSE;
}

static void
open_new_channel (int channel_id)
{
  XzibitReceivedWindow *received;
  GtkWidget *window;
  GtkWidget *vnc;
  int sockets[2];
  int *key;
  GIOChannel *channel;

  g_print ("Opening RFB channel %x\n",
	   channel_id);

  if (received_windows==NULL)
    {
      received_windows =
	g_hash_table_new_full (g_int_hash,
			       g_int_equal,
			       g_free,
			       g_free);
    }

  if (g_hash_table_lookup (received_windows,
                           &channel_id))
    {
      g_warning ("But %x is already open.\n",
		 channel_id);
      return;
    }

  received =
    g_malloc (sizeof (XzibitReceivedWindow));

  key =
    g_malloc (sizeof (int));
  *key = channel_id;

  g_hash_table_insert (received_windows,
		       key,
		       received);

  key =
    g_malloc (sizeof (int));
  *key = channel_id;

  g_hash_table_insert (message_handlers,
		       key,
		       handle_video_message);

  socketpair (AF_LOCAL,
	      SOCK_STREAM,
	      0,
	      sockets);

  window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
  g_signal_connect (window, "delete_event", G_CALLBACK (gtk_main_quit), NULL);

  /* for now, it's not resizable */
  gtk_window_set_resizable (GTK_WINDOW (window), FALSE);

  vnc = vnc_display_new();
  /* FIXME: We really don't want to quit
     ALL windows if the VNC for ONE gets
     disconnected.
  */
  g_signal_connect(vnc, "vnc-disconnected", G_CALLBACK(gtk_main_quit), NULL);

  vnc_display_open_fd (VNC_DISPLAY (vnc), sockets[1]);

  gtk_container_add (GTK_CONTAINER (window), vnc);

  gtk_widget_show_all (window);

  received->window = window;
  received->fd = sockets[0];
  received->id = channel_id;

  channel = g_io_channel_unix_new (sockets[0]);
  g_io_add_watch (channel,
		  G_IO_IN,
		  check_for_rfb_replies,
		  received);

  g_signal_connect (vnc,
		    "expose-event",
		    G_CALLBACK(exposed_window),
		    GINT_TO_POINTER (channel_id));

}

static void
apply_metadata (int metadata_id,
		int xzibit_id,
		unsigned char *buffer,
		int length)
{
  XzibitReceivedWindow *received;

  g_print ("Setting metadata %d on window %d; %d bytes\n",
	   metadata_id, xzibit_id, length);

  received = g_hash_table_lookup (received_windows,
				  &xzibit_id);

  if (received)
    {
      apply_metadata_now (received,
			  metadata_id,
			  buffer,
			  length);
    }
  else
    {
      PostponedMetadata *postponed =
	g_malloc (sizeof (PostponedMetadata));
      GSList *current;

      postponed->id = metadata_id;
      postponed->length = length;
      postponed->content = g_memdup (buffer,
				     length);

      if (postponed_metadata==NULL)
	{
	  postponed_metadata =
	    g_hash_table_new_full (g_direct_hash,
				   g_direct_equal,
				   NULL,
				   NULL);
	}

      current =
	g_hash_table_lookup (postponed_metadata,
			     GINT_TO_POINTER (xzibit_id));

      if (current==NULL)
	{
	  g_hash_table_insert (postponed_metadata,
			       GINT_TO_POINTER (xzibit_id),
			       g_slist_prepend (NULL,
						postponed));
	}
      else
	{
	  current = g_slist_prepend (current,
				    postponed);
	  
	  g_hash_table_replace (postponed_metadata,
				GINT_TO_POINTER (xzibit_id),
				current);
	}

    }
}

static void
handle_control_channel_message (int channel,
				unsigned char *buffer,
				unsigned int length)
{
  unsigned char opcode;

  if (length==0)
    /* Empty messages are valid but ignored. */
    return;

  opcode = buffer[0];

  switch (opcode)
    {
    case 1: /* Open */
      if (length!=7) {
	g_print ("Open message; bad length (%d)\n",
		 length);
	return;
      }
      
      open_new_channel (buffer[5]|buffer[6]*256);
      break;

    case 2: /* Close */
      {
	int victim = buffer[1]|buffer[2]*256;

	if (victim==0)
	  return; /* that's silly */

	g_hash_table_remove (message_handlers,
			     &victim);

	/* FIXME: should also kill the associated window */
      }
      break;

    case 3: /* Set */
      if (length<4)
	{
	  g_warning ("Attempt to set metadata with short buffer");
	  return;
	}
      
      apply_metadata (buffer[1]|buffer[2]*256,
		      buffer[3]|buffer[4]*256,
		      buffer+4,
		      length-4);
      
      break;

    case 4: /* Wall */
      g_print ("Wall; ignored for now\n");
      break;

    case 5: /* Respawn */
      g_print ("Respawn; ignored for now\n");
      break;
      
    case 6: /* Avatar */
      g_print ("Avatar; ignored for now\n");
      break;

    case 7: /* Listen */
      {
	/* we don't pay attention to the associated
	 * video channel at present; we just mix all
	 * audio channels together */

	int* audio = g_malloc (sizeof (int));

	*audio = buffer[3]|buffer[4]*256;

	g_hash_table_insert (message_handlers,
			     audio,
			     handle_audio_message);
      }
      break;

    case 8: /* Mouse */
      g_print ("Mouse; ignored for now\n");
      break;

    default:
      g_warning ("Unknown control channel opcode %x\n",
		 opcode);
    }
  
}

static void
handle_xzibit_message (int channel,
		       unsigned char *buffer,
		       unsigned int length)
{
  MessageHandler *handler;
  g_print ("x-r-c: Handling xzibit message of %d bytes on channel %d\n",
	   length, channel);

  handler = g_hash_table_lookup (message_handlers,
				 &channel);

  if (!handler)
    {
      g_warning ("A message was received for channel %d, which is not open.",
		 channel);
      return;
    }

  handler (channel,
	   buffer,
	   length);
}

static gboolean
check_for_fd_input (GIOChannel *source,
		    GIOCondition condition,
		    gpointer data)
{
  unsigned char buffer[1024];
  int fd = g_io_channel_unix_get_fd (source);
  int count, i;
  count = read (fd, &buffer, sizeof(buffer));

  if (count<0) {
    perror ("xzibit");
    return;
  }
  if (count==0) {
    return;
  }

  /*
   * We don't have to deal with the header.
   * That's done for us, upstream.
   */
  
  for (i=0; i<count; i++)
    {

#if 0
      g_print ("(%d/%d) Received %02x in state %d\n",
	       i+1, count,
	       buffer[i], fd_read_state);
#endif

      switch (fd_read_state)
	{
	case STATE_START:
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
	      g_print ("Read length is %d\n", fd_read_length);
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
	      fd_read_state = STATE_START;
	    }
	}
      
    }

  /* FIXME: return value?? */
}

static void
prepare_message_handlers (void)
{
  int *zero;

  if (message_handlers)
    return;

  message_handlers =
    g_hash_table_new_full (g_int_hash,
			   g_int_equal,
			   g_free,
			   g_free);

  g_print ("Installing channel zero\n");

  zero = g_malloc (sizeof (int));
  *zero = 0;

  g_hash_table_insert (message_handlers,
		       zero,
		       handle_control_channel_message);
}

int
main (int argc, char **argv)
{
  char *port_as_string;
  GOptionContext *context;
  GError *error = NULL;

  gtk_init (&argc, &argv);

  g_print ("RFB client starting...\n");

  prepare_message_handlers ();

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

#if 0
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
#endif

  gtk_main ();
}
