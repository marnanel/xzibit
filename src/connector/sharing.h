#ifndef SHARING_H
#define SHARING_H

#include <glib.h>
#include <X11/Xlib.h>

guint window_get_sharing (Window window);

void window_set_sharing (Window window,
			 guint sharing,
			 const char* source,
			 const char* target,
			 gboolean follow_up);

#endif /* !SHARING_H */
