#include <gtk/gtk.h>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <math.h>

typedef struct _Doppelganger {
  int foo;
} Doppelganger;

Doppelganger*
doppelganger_new (GdkPixbuf *pixbuf)
{
  Doppelganger *result =
    g_malloc (sizeof (Doppelganger));

  return result;
}

void
doppelganger_move (Doppelganger *dg,
		   int x, int y)
{
  g_print ("Moving to %d,%d\n", x, y);
}

void
doppelganger_free (Doppelganger *dg)
{
  g_free (dg);
}

#ifdef DOPPELGANGER_TEST

gboolean
doppelganger_turn (gpointer user_data)
{
  Doppelganger *dg =
    (Doppelganger*) user_data;
  static double place = 0.0;

  place++;
  while (place>360.0)
    {
      place -= 360.0;
    }

  doppelganger_move (dg,
		     100+100*sin(place*(M_PI/180)),
		     100+100*cos(place*(M_PI/180)));
}

int
main (int argc, char **argv)
{
  Doppelganger *dg;
  GdkPixbuf *pixbuf;

  gtk_init (&argc, &argv);

  pixbuf = gdk_pixbuf_new_from_file ("jupiter/jupiter.png",
				     NULL);

  dg = doppelganger_new (pixbuf);

  g_timeout_add (10,
		 doppelganger_turn,
		 dg);

  gtk_main ();
}

#endif /* DOPPELGANGER_TEST */

/* EOF doppelganger.c */
