#ifndef CONTACT_CHOOSER
#define CONTACT_CHOOSER 1

#include <gtk/gtk.h>

typedef void contact_chooser_cb (int,
				 const char*,
				 const char*);

GtkWidget *
show_contact_chooser (int window,
		      contact_chooser_cb callback);

#endif /* !CONTACT_CHOOSER */
