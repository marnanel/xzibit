#include "messagebox.h"

#include <gtk/gtk.h>

void
show_messagebox(const char* message)
{
  g_warning ("This would be a message box: %s",
	     message);
}

/* EOF messagebox.c */

