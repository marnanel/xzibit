#include "main-window.h"
#include "contact-chooser.h"
#include "sharing.h"

typedef struct _XzibitContext {
  int window;
} XzibitContext;

static void
chosen_callback (const char *source,
		 const char *target,
		 gpointer user_data)
{
  XzibitContext *context =
    (XzibitContext *) user_data;

  window_set_sharing ((Window) context->window,
		      SHARING_SENT,
		      source,
		      target,
		      TRUE);
}

static void
chosen_window (int window,
	       gpointer user_data)
{
  XzibitContext *context =
    (XzibitContext *) user_data;

  context->window = window;

  show_contact_chooser (chosen_callback,
			user_data);
}

int
main (int argc, char **argv)
{
  XzibitContext context;
  GtkWidget *window;

  gtk_init (&argc, &argv);

  context.window = 0;

  window = show_main_window (chosen_window,
			     &context);

  g_signal_connect (window,
		    "delete-event",
		    G_CALLBACK (gtk_main_quit),
		    NULL);

  gtk_main ();
}
