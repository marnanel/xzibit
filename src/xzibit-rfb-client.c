#include <gtk/gtk.h>
#include <gdk/gdkx.h>
#include <gio/gio.h>
#include <vncdisplay.h>
#include <sys/socket.h>
#include <string.h>
#include <X11/X.h>
#include <X11/extensions/XInput2.h>
#include <X11/extensions/XI2.h>
#include <X11/extensions/XInput.h>

#include "doppelganger.h"

/****************************************************************
 * Some globals.
 ****************************************************************/

/**
 * Since we may be receiving windows from multiple Xzibit servers,
 * each of which will have a running instance of xzibit-rfb-client,
 * this is a serial number identifying *this* connection.  It's used
 * to mark windows as belonging to us, amongst other things.
 */
int remote_server = 1;

/**
 * The file descriptor which links us to our parent Mutter process.
 */
int following_fd = -1;

/****************************************************************
 * Definitions used for buffer reading.
 ****************************************************************/

typedef enum {
  STATE_START,
  STATE_SEEN_CHANNEL,
  STATE_SEEN_LENGTH,
} FdReadState;

#define METADATA_TRANSIENCY 1
#define METADATA_TITLE 2
#define METADATA_TYPE 3
#define METADATA_ICON 4

FdReadState fd_read_state = STATE_START;
int fd_read_through = 0;
int fd_read_channel = 0;
int fd_read_length = 0;
char* fd_read_buffer = NULL;

typedef void (MessageHandler) (int, unsigned char*, unsigned int);

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
  /**
   * The doppelganger pointer: the pointer representing the
   * mouse movements of the person on the other end of
   * the connection.
   */
  int doppelganger_pointer;
  /**
   * The function which will deal with messages received
   * for this window.
   */
  MessageHandler *handler;
} XzibitReceivedWindow;

GHashTable *received_windows = NULL;
GHashTable *postponed_metadata = NULL;

/**
 * The doppelganger cursor.  We have only one per connection.
 */
Doppelganger *dg = NULL;
gboolean dg_is_hidden = FALSE;

/****************************************************************
 * Options.
 ****************************************************************/

static const GOptionEntry options[] =
{
	{
	  "fd", 'f', 0, G_OPTION_ARG_INT, &following_fd,
	  "The file descriptor which conveys the RFB protocol", NULL },
	{
	  "remote-server", 'r', 0, G_OPTION_ARG_INT, &remote_server,
	  "The Xzibit code for the remote server", NULL },
	{ NULL, 0, 0, G_OPTION_ARG_NONE, NULL, NULL, 0 }
};

/**
 * Marks a given toplevel window as "remote", using an
 * X property.
 *
 * \param window  The window.
 */
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

/**
 * Marks a given toplevel window with its Xzibit ID.
 *
 * \param window        The window.
 * \param remote_server The serial number of *this*
 *                      connection.  The same for all
 *                      windows managed by this program.
 * \param id            The Xzibit ID of the window.
 */
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

/**
 * Called when VNC is initialised.
 */
static void vnc_initialized(GtkWidget *vnc, GtkWidget *window)
{
  /* nothing */
}

/**
 * Adds the doppelganger cursor for a given window.
 *
 * \bugs  This is currently disabled, because it's
 *        broken.  It needs to be fixed soon.
 *
 * \param received  The window.
 */
static void
add_mpx_for_window (XzibitReceivedWindow *received)
{
  XIAddMasterInfo add;
  /* FIXME: Using the respawn ID here, if we have it,
   * would be less brittle.
   */
  gchar *name = g_strdup_printf("xzibit-r-%d-%d",
				remote_server,
				received->id);
  int ndevices;
  XIDeviceInfo *devices, *device;
  int i;
  int current_pointer;

  /* add the device */

  add.type = XIAddMaster;
  add.name = name;
  add.send_core = True;
  add.enable = True;

  g_warning ("Adding new pointer called %s", name);

  XIChangeHierarchy (gdk_x11_get_default_xdisplay (),
		     (XIAnyHierarchyChangeInfo*) &add,
		     0 /* <-- FIXME: This should be 1
		       * but that currently breaks with BadDevice,
		       * which the man page says can't happen;
		       * I think it's a bug in the version of
		       * XInput2 we're using; try again with sid */ );

  /* now see whether it's in the list */

  received->doppelganger_pointer = -1;

  devices = XIQueryDevice(gdk_x11_get_default_xdisplay (),
			  XIAllDevices, &ndevices);

  for (i = 0; i < ndevices; i++) {
    device = &devices[i];

    if (g_str_has_prefix (device->name,
			  name))
      {
	switch (device->use)
	  {
	  case XISlavePointer:
	    received->doppelganger_pointer = device->deviceid;
	    break;
	  }
      }
  }

  if (received->doppelganger_pointer==-1)
    {
      g_warning ("The doppelganger pointer for channel %d could not be created.",
		 received->id);
    }

  XIFreeDeviceInfo(devices);
  g_free (name);
}

/**
 * Writes a block of data to the upstream Mutter process,
 * with checking.
 *
 * \param buffer  The data to write.
 * \param size    The size of the buffer.
 */
static void
write_to_following_fd (gpointer buffer,
		       gsize size)
{
  int result = write (following_fd,
		      buffer, size);
  if (result < size)
    {
      g_warning ("Cannot communicate with upstream process.  Things will break.");
    }
}

/**
 * Called when data arrives from gtk-vnc.
 *
 * \bugs The name isn't very clear.
 */
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
      perror ("xzibit");
      g_error ("The controlling process closed the connection.");
    }

  if (count==0)
    {
      return;
    }

  header[0] = received->id % 256;
  header[1] = received->id / 256;
  header[2] = count % 256;
  header[3] = count / 256;

  write_to_following_fd (header, sizeof (header));
  write_to_following_fd (buffer, count);

  return TRUE;
}

/**
 * Handler for video (i.e. RFB) channels.
 */
static void
handle_video_message (int channel,
		      unsigned char *buffer,
		      unsigned int length)
{
  XzibitReceivedWindow *received =
    g_hash_table_lookup (received_windows,
			 &channel);
      
  if (write (received->fd, buffer, length) < length)
    {
      g_warning ("Writing to the VNC library ran short.  Things will break.");
    }
}

/**
 * Handler for audio channels.
 *
 * \bugs  Not implemented.
 */
static void
handle_audio_message (int channel,
		      unsigned char *buffer,
		      unsigned int length)
{
  g_print ("We have %d bytes of audio to play.  FIXME.\n", length);
}

/**
 * Applies metadata to a window, with the assumption that the
 * window is mapped and can have its metadata applied immediately.
 *
 * \param received     The window.
 * \param metadata_id  The ID of the metadata (see spec)
 * \param buffer       Pointer to a buffer holding the metadata
 * \param length       Size of the buffer.
 */
static void
apply_metadata_now (XzibitReceivedWindow *received,
		    int metadata_id,
		    unsigned char *buffer,
		    int length)
{
  GtkWindow *target = GTK_WINDOW (received->window);

  switch (metadata_id)
    {
    case METADATA_TRANSIENCY:
      g_warning ("Attempt to set transiency: not yet implemented\n");
      break;

    case METADATA_TITLE:
      {
	char *copy = g_malloc (length+1);
	int i;

	memcpy (copy, buffer, length);
	copy[length] = 0;

	gtk_window_set_title (target,
			      copy);
	g_free (copy);
      }
      break;

    case METADATA_TYPE:
      g_warning ("Attempt to set window type: not yet implemented\n");
      break;

    case METADATA_ICON:
      g_warning ("Attempt to set window icon: not yet implemented\n");
      break;

    default:
      g_warning ("Attempt to add unknown metadata of type %d "
		 "to window %d.", metadata_id, received->id);
    }
}

/**
 * A piece of metadata which was postponed because
 * the window it applies to was not yet mapped; it
 * will be applied as soon as the window maps.
 */
typedef struct _PostponedMetadata {
  /**
   * ID of the metadata (see spec)
   */
  int id;
  /**
   * Length of the buffer.
   */
  gsize length;
  /**
   * Pointer to a buffer of metadata.
   */
  char *content;
} PostponedMetadata;

/**
 * Called when a window is exposed; applies any postponed
 * metadata.
 */
static gboolean
exposed_window (GtkWidget *widget,
		GdkEventExpose *event,
		gpointer data)
{
  GSList *postponements = NULL;
  int xzibit_id = GPOINTER_TO_INT (data);
  XzibitReceivedWindow *received;

  if (postponed_metadata)
    postponements = g_hash_table_lookup (postponed_metadata,
					 GINT_TO_POINTER (xzibit_id));

  received = g_hash_table_lookup (received_windows,
				  &xzibit_id);

  if (postponements)
    {
      GSList *cursor = postponements;

      g_hash_table_remove (postponed_metadata,
			   GINT_TO_POINTER (xzibit_id));

      while (cursor)
	{
	  PostponedMetadata *metadata = cursor->data;
	  
	  apply_metadata_now (received,
			      metadata->id,
			      metadata->content,
			      metadata->length);
      
	  g_free (metadata->content);
	  g_free (metadata);
	  cursor = cursor->next;
	}

      g_slist_free (postponements);
    }

  return FALSE;
}

/**
 * Marks a channel as closed.  It must not be used again
 * after calling this function.
 *
 * \param data  The xzibit ID of the channel to close,
 *              cast to a gpointer using GINT_TO_POINTER().
 */
static void
close_channel (gpointer data)
{
  int channel_id = GPOINTER_TO_INT (data);
  XzibitReceivedWindow *received =
    g_hash_table_lookup (received_windows,
			 &channel_id);
  char buffer[7];

  if (!received)
    return;

  /* Firstly, if the window's not closed, close it.
     (This will also close the gtk-vnc instance that's
     running this window.) */

  gtk_widget_destroy (GTK_WIDGET (received->window));

  /* Remove the record of this channel.
     (This also frees "received", so don't use it
     afterwards.) */

  g_hash_table_remove (received_windows,
		       received);

  /* And tell upstream we've done so. */

  buffer[0] = 0; /* CONTROL_CHANNEL */
  buffer[1] = 0; /* ditto */
  buffer[2] = 3; /* length of this message */
  buffer[3] = 0;

  buffer[4] = 2; /* COMMAND_CLOSE */
  buffer[5] = channel_id % 256;
  buffer[6] = channel_id / 256;

  write_to_following_fd (buffer, sizeof(buffer));
}

/**
 * Opens a new video channel.
 *
 * \bugs  Should possibly have "video" in its name.
 *
 * \param channel_id  The ID of the new channel.
 */
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

  received->handler = handle_video_message;

  socketpair (AF_LOCAL,
	      SOCK_STREAM,
	      0,
	      sockets);

  window = gtk_window_new (GTK_WINDOW_TOPLEVEL);

  /* for now, it's not resizable */
  gtk_window_set_resizable (GTK_WINDOW (window), FALSE);

  vnc = vnc_display_new();
  g_signal_connect (window, "delete_event",
		    G_CALLBACK (close_channel), NULL);
  g_signal_connect(vnc, "vnc-disconnected",
		   G_CALLBACK (close_channel), NULL);

  vnc_display_open_fd (VNC_DISPLAY (vnc), sockets[1]);

  gtk_container_add (GTK_CONTAINER (window), vnc);

  gtk_widget_show_all (window);

  received->window = window;
  received->fd = sockets[0];
  received->id = channel_id;
  received->doppelganger_pointer = -1;

  channel = g_io_channel_unix_new (sockets[0]);
  g_io_add_watch (channel,
		  G_IO_IN,
		  check_for_rfb_replies,
		  received);

  g_signal_connect (vnc,
		    "expose-event",
		    G_CALLBACK(exposed_window),
		    GINT_TO_POINTER (channel_id));

  add_mpx_for_window (received);
}

/**
 * Applies metadata to a window; if the window is
 * not mapped, stores the metadata until it is.
 *
 * \param metadata_id  The ID of the metadata (see spec)
 * \param xzibit_id    The ID of the window to
 *                     apply it to
 * \param buffer       Pointer to a buffer holding
 *                     the data
 * \param length       Length of the buffer
 */
static void
apply_metadata (int metadata_id,
		int xzibit_id,
		unsigned char *buffer,
		int length)
{
  XzibitReceivedWindow *received;

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

/**
 * Handler for messages received on the control channel.
 *
 * \param channel  The channel ID (necessarily zero)
 * \param buffer   Pointer to buffer containing an
 *                 entire message
 * \param length   Length of the buffer
 */
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
      if (length!=3) {
	g_warning ("Open message; bad length (%d)\n",
		 length);
	return;
      }
      
      open_new_channel (buffer[1]|buffer[2]*256);
      break;

    case 2: /* Close */
      {
	int victim = buffer[1]|buffer[2]*256;

	if (victim==0)
	  return; /* that's silly */

	close_channel (GINT_TO_POINTER (victim));
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
		      buffer+5,
		      length-5);
      
      break;

    case 4: /* Wall */
      {
	int code;

	if (length<3)
	  return; /* ignore */

	code = buffer[1]|buffer[2]*256;

	if (g_utf8_validate (buffer+3,
			     -1, NULL))
	  {
	    GtkWidget *message =
	      gtk_message_dialog_new (NULL, 0,
				      code? GTK_MESSAGE_ERROR: GTK_MESSAGE_INFO,
				      GTK_BUTTONS_CLOSE,
				      "%s (%d)",
				      buffer+3, code);

	    g_signal_connect_swapped (message, "response",
				      G_CALLBACK (gtk_widget_destroy),
				      message);

	    gtk_widget_show (message);
	  }
      }
      break;

    case 5: /* Respawn */
      if (length!=5)
	{
	  g_warning ("Attempt to send respawn ID with bad block size");
	  return;
	}
      /* we're not storing this at present, because
         it's not clear it will be useful on this side.
      */
      break;
      
    case 6: /* Avatar */
      {
	GdkPixbuf *pixbuf;
	GError *error = NULL;
	GInputStream *source =
	  g_memory_input_stream_new_from_data (buffer+1,
					       length,
					       NULL);

	g_warning("--- AVATAR ---");

	pixbuf = gdk_pixbuf_new_from_stream (source,
					     NULL,
					     &error);

	g_input_stream_close (source, NULL, NULL);

	if (error)
	  {
	    /* free it, but ignore it */
	    g_error_free (error);
	    g_warning ("We were sent an invalid PNG as an avatar.");
	  }
	else
	  {

	    doppelganger_set_image (dg,
				    pixbuf);

	    gdk_pixbuf_unref (pixbuf);

	  }
      }
      break;

    case 7: /* Listen */
      {
	/* we don't pay attention to the associated
	 * video channel at present; we just mix all
	 * audio channels together */

	int *audio = g_malloc (sizeof (int));
	XzibitReceivedWindow *audio_channel =
	  g_malloc (sizeof (XzibitReceivedWindow));

	*audio = buffer[3]|buffer[4]*256;

	/* Most of the fields are ignored for
	   audio channels. */

	audio_channel->fd = -1;
	audio_channel->window = NULL;
	audio_channel->doppelganger_pointer = 0;

	audio_channel->id = *audio;
	audio_channel->handler = handle_audio_message;

	g_hash_table_insert (received_windows,
			     audio,
			     audio_channel);
      }
      break;

    case 8: /* Mouse */
      {
	gboolean offscreen = FALSE;
	guint x, y;

	if (length!=5)
	  {
	    offscreen = TRUE;
	  }
	else
	  {
	    x = buffer[1]|buffer[2]*256;
	    y = buffer[3]|buffer[4]*256;
	  }

	if (dg_is_hidden != offscreen)
	  {
	    if (offscreen)
	      doppelganger_hide (dg);
	    else
	      doppelganger_show (dg);

	    dg_is_hidden = offscreen;
	  }

	if (!offscreen)
	  {
	    doppelganger_move (dg, x, y);
	  }
      }
      break;

    default:
      g_warning ("Unknown control channel opcode %x\n",
		 opcode);
    }
}

/**
 * Handles a received xzibit message on any channel.
 * This is a front end for several handler routines;
 * it looks up the correct one and passes control to it.
 */
static void
handle_xzibit_message (int channel,
		       unsigned char *buffer,
		       unsigned int length)
{
  XzibitReceivedWindow *received;

  received = g_hash_table_lookup (received_windows,
				  &channel);

  if (!received)
    {
      g_warning ("A message was received for channel %d, which is not open.",
		 channel);
      return;
    }

  if (!received->handler)
    {
      g_warning ("A message was received for channel %d, which has no handler.",
		 channel);
      return;
    }

  received->handler (channel,
		     buffer,
		     length);
}

/**
 * Called when data arrives from the upstream Mutter
 * process.
 *
 * \bugs  The name isn't very clear.
 */
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

  return TRUE;
}

/**
 * Initialises the handlers hash table.
 */
static void
prepare_message_handlers (void)
{
  int *zero;
  XzibitReceivedWindow *channel_zero;

  if (received_windows)
    return;

  received_windows =
    g_hash_table_new_full (g_int_hash,
			   g_int_equal,
			   g_free,
			   g_free);

  zero = g_malloc (sizeof (int));
  *zero = 0;

  channel_zero = g_malloc (sizeof (XzibitReceivedWindow));

  channel_zero->fd = -1;
  channel_zero->window = NULL;
  channel_zero->doppelganger_pointer = 0;

  channel_zero->id = 0;
  channel_zero->handler = handle_control_channel_message;

  g_hash_table_insert (received_windows,
		       zero,
		       channel_zero);
}

/**
 * Initialises appropriate X extensions.
 */
static void
initialise_extensions (void)
{
  int major = 2, minor = 0;
  if (XIQueryVersion(gdk_x11_get_default_xdisplay (), &major, &minor) == BadRequest) {
    g_error("XI2 not available. Server supports %d.%d\n", major, minor);
  }
}

static void
create_doppelganger (void)
{
  gchar *name = g_strdup_printf ("xzibit-r-%d",
				 remote_server);

  dg = doppelganger_new (name);
  g_free (name);

  dg_is_hidden = FALSE;
}

/**
 * The main function.
 */
int
main (int argc, char **argv)
{
  GOptionContext *context;
  GError *error = NULL;

  gtk_init (&argc, &argv);

  g_print ("RFB client starting...\n");

  prepare_message_handlers ();

  initialise_extensions ();

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
      channel = g_io_channel_unix_new (following_fd);
      g_io_add_watch (channel,
                      G_IO_IN,
                      check_for_fd_input,
                      NULL);
    }
  else
    {
      g_warning ("No FD given to follow; we won't be able to do much");
    }

  create_doppelganger ();

  gtk_main ();
}

/* EOF xzibit-rfb-client.c */
