#include "vnc.h"
#include <gtk/gtk.h>

typedef struct _VncPrivate {
  int port;
} VncPrivate;

GHashTable *servers = NULL;

static void
ensure_servers (void)
{
  if (servers)
    return;

  servers = g_hash_table_new_full (g_int_hash,
				   g_int_equal,
				   g_free,
				   g_free);
}

void
vnc_start (Window id)
{
  VncPrivate *private = NULL;
  int *key;

  ensure_servers ();

  private = g_hash_table_lookup (servers,
				 &id);

  if (private)
    return;

  g_warning ("Starting VNC server for %08x", (unsigned int) id);

  private = g_malloc (sizeof(VncPrivate));
  key = g_malloc (sizeof(int));

  *key = id;
  private->port = (random() % 60000) + 1024;

  g_warning ("(on port %d)", private->port);

  g_hash_table_insert (servers,
		       key,
		       private);
}

unsigned int
vnc_port (Window id)
{
  VncPrivate *private = NULL;

  if (!servers)
    return 0;

  private = g_hash_table_lookup (servers,
				 &id);
  
  if (private)
    return private->port;
  else
    return 0;
}

void
vnc_supply_pixmap (Window id,
		   GdkPixbuf *pixbuf)
{
  g_warning ("Pixmap supplied for %08x", (unsigned int) id);

  /* not implemented */
}

void
vnc_stop (Window id)
{
  g_warning ("Stopping VNC server for %08x", (unsigned int) id);

  /* not implemented */
}

/* eof vnc.c */

