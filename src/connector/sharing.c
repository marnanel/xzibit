#include "sharing.h"
#include <gtk/gtk.h>
#include <gdk/gdkx.h>
#include <string.h>

#include "messagebox.h"

#define _(x) (x)

guint
window_get_sharing (Window window)
{
  Atom actual_type;
  int actual_format;
  long n_items, bytes_after;
  unsigned char *prop_return;
  guint result = 0;

  XGetWindowProperty (gdk_x11_get_default_xdisplay (),
		      window,
		      gdk_x11_get_xatom_by_name ("_XZIBIT_SHARE"),
		      0, 4, False,
		      gdk_x11_get_xatom_by_name ("CARDINAL"),
		      &actual_type,
		      &actual_format,
		      &n_items,
		      &bytes_after,
		      &prop_return);

  if (prop_return)
    {
      result = *((int*) prop_return);
      XFree (prop_return);
    }

  return result;
}

typedef struct _FilterContext {
  Atom result_atom;
  gboolean ever_heard_back;
  guint timeout_id;
  GdkWindow *gdk_window;
} FilterContext;

static gboolean
event_filter_timeout (gpointer user_data)
{
  FilterContext *context =
    (FilterContext*) user_data;

  if (context->ever_heard_back)
    {
      show_messagebox (_("Xzibit is not responding."));
    }
  else
    {
      show_messagebox (_("Xzibit does not seem to be installed. "
			 "For information on how to install it, "
			 "please see http://telepathy.freedesktop.org/wiki/Xzibit ."));
    }

  context->timeout_id = 0;
  return FALSE; /* don't go round again */
}

static void
start_stop_event_filter_timeout (FilterContext *context,
				 gboolean create_new_timeout)
{
  if (context->timeout_id)
    {
      /* Remove the old timeout. */

      GSource *source =
	g_main_context_find_source_by_id (NULL,
					  context->timeout_id);

      if (source)
	{
	  g_source_destroy (source);
	}
    }

  if (create_new_timeout)
    {
      context->timeout_id =
	g_timeout_add (1000,
		       event_filter_timeout,
		       context);
    }
}

static GdkFilterReturn
event_filter (GdkXEvent *xevent,
	      GdkEvent *event,
	      gpointer user_data)
{
  FilterContext *context =
    (FilterContext*) user_data;
  XEvent *ev = (XEvent*) xevent;

  if (ev->type = PropertyNotify)
    {
      XPropertyEvent *propev =
	(XPropertyEvent*) xevent;

      if (propev->atom == context->result_atom &&
	  propev->state == PropertyNewValue)
	{
	  Atom actual_type;
	  int actual_format;
	  long n_items, bytes_after;
	  unsigned char *prop_return;
	  int result_value = 0;
	  char *message = NULL;
	  gboolean go_round_again = FALSE;

	  context->ever_heard_back = TRUE;

	  XGetWindowProperty (gdk_x11_get_default_xdisplay (),
			      propev->window,
			      context->result_atom,
			      0, 4, False,
			      gdk_x11_get_xatom_by_name ("INTEGER"),
			      &actual_type,
			      &actual_format,
			      &n_items,
			      &bytes_after,
			      &prop_return);

	  if (prop_return)
	    {
	      result_value = *((int*) prop_return);
	      XFree (prop_return);
	    }

	  switch (result_value)
	    {
	    case 100:
	      message =
		g_strdup_printf (_("Window successfully unshared."));
	      break;

	    case 101:
	      message =
		g_strdup_printf (_("Window successfully shared."));
	      break;

	    case 103:
	      /*
	       * Window is being made unshareable; there's no
	       * need to tell the user about it.  We did need
	       * to monitor, though, because it's the first thing
	       * we do, and if xzibit isn't installed we'll know
	       * about it very quickly.
	       */
	      break;

	    case 200:
	      message =
		g_strdup_printf (_("Connecting; please wait."));
	      /*
	       * FIXME: Probably should pop up something with
	       * one of those non-percentage progress bars here.
	       */
	      break;

	    case 301:
	      message =
		g_strdup_printf (_("Can't share window: you are "
				   "not logged in from the correct "
				   "account."));
	      break;

	    case 302:
	      message =
		g_strdup_printf (_("Can't share window: the person "
				   "you selected is not one of "
				   "your contacts."));
	      break;

	    case 312:
	      message =
		g_strdup_printf (_("Can't share window: the contact "
				   "you selected is not running "
				   "xzibit."));
	      break;

	    case 322:
	      message =
		g_strdup_printf (_("Can't share window: the contact "
				   "you selected has rejected the "
				   "connection."));
	      break;

	    default:
	      message =
		g_strdup_printf (_("Xzibit sent a code I didn't understand: %d"),
				 result_value);
	    }
	  
	  if (message)
	    {
	      show_messagebox (message);
	      g_free (message);
	    }

	  if (result_value/100 == 2)
	    {
	      go_round_again = TRUE;
	    }

	  start_stop_event_filter_timeout (context,
					   go_round_again);

	  if (!go_round_again)
	    {
	      gdk_window_remove_filter (context->gdk_window,
					event_filter,
					context);

	      g_free (context);
	    }
	}
    }

  return GDK_FILTER_CONTINUE;
}

static void
monitor_window (Window window)
{
  GdkWindow *foreign =
    gdk_window_foreign_new (window);
  FilterContext *context;

  if (!foreign)
    {
      g_warning ("Window %x was destroyed; can't monitor it",
		 (int) window);
      return;
    }

  context = g_malloc (sizeof (FilterContext));
  context->result_atom =
    gdk_x11_get_xatom_by_name("_XZIBIT_RESULT");
  context->ever_heard_back = FALSE;
  context->timeout_id = 0;
  context->gdk_window = foreign;

  start_stop_event_filter_timeout (context, TRUE);

  gdk_window_add_filter (foreign,
			 event_filter,
			 context);
}

void
window_set_sharing (Window window,
		    guint sharing,
		    const char* source,
		    const char* target,
		    gboolean follow_up)
{
  if (sharing>3)
    {
      g_error ("Attempted to set sharing to %d on %x, "
	       "which is out of range.",
	       sharing, (int) window);
    }

  if (sharing==1)
    {
      if (!source && !target)
	{
	  g_error ("Must set source and target when "
		   "sharing a window.");
	}
    }
  else
    {
      if (source || target)
	{
	  g_error ("Must not supply source or target "
		   "unless sharing a window.");
	}
    }

  if (follow_up)
    {
      monitor_window (window);
    }

  if (source)
    {
      XChangeProperty (gdk_x11_get_default_xdisplay (),
		       window,
		       gdk_x11_get_xatom_by_name("_XZIBIT_SOURCE"),
		       gdk_x11_get_xatom_by_name("UTF8_STRING"),
		       8,
		       PropModeReplace,
		       (const unsigned char*) source,
		       strlen(source));

    }

  if (target)
    {
      XChangeProperty (gdk_x11_get_default_xdisplay (),
		       window,
		       gdk_x11_get_xatom_by_name("_XZIBIT_TARGET"),
		       gdk_x11_get_xatom_by_name("UTF8_STRING"),
		       8,
		       PropModeReplace,
		       (const unsigned char*) target,
		       strlen(source));

    }

  XChangeProperty (gdk_x11_get_default_xdisplay (),
		   window,
		   gdk_x11_get_xatom_by_name ("_XZIBIT_SHARE"),
		   gdk_x11_get_xatom_by_name ("CARDINAL"),
		   32,
		   PropModeReplace,
		   (const unsigned char*) &sharing,
		   1);
}

#ifdef SHARING_TEST

static gboolean
timeout (gpointer user_data)
{
  static int stage = 0;

  GtkWidget *window =
    (GtkWidget*) user_data;
  Window id =
    GDK_WINDOW_XID (window->window);

  switch (++stage)
    {
    case 1:
    case 3:
      g_print ("State of window %x is %d\n",
	       (int) id, window_get_sharing (id));
      break;

    case 2:
      g_print ("Sharing the window.\n");
      window_set_sharing (id, 1,
			  "source@example.com",
			  "target@example.com",
			  1);
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
