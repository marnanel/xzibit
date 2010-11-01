#ifndef POINTER_FILTER_H
#define POINTER_FILTER_H 1

#include <glib.h>

typedef void (*pointer_filter_cb) (gpointer,
				   unsigned int,
				   gpointer);

typedef struct _PointerFilter PointerFilter;

PointerFilter *pointer_filter_new(pointer_filter_cb callback,
				  gpointer user_data);

void pointer_filter_move(PointerFilter *pf,
			 unsigned int x,
			 unsigned int y);

void pointer_filter_read(PointerFilter *pf,
			 gpointer data,
			 unsigned int length);

void pointer_filter_free(PointerFilter *pf);			 

#endif /* !POINTER_FILTER_H */
