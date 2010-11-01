#include "pointer-filter.h"

#include <stdlib.h>

#include <stdio.h>

struct _PointerFilter {
  pointer_filter_cb callback;
  gpointer user_data;
  /**
   * Data left over from a previous read which
   * should be read before the next read.  If
   * this is NULL, there is no prefix.
   */
  gpointer prefix;
  /**
   * Length of the prefix.
   */
  unsigned int prefix_length;
  /**
   * The four bytes of the taboo cursor
   * position.
   */
  unsigned char taboo[4];
};

PointerFilter*
pointer_filter_new(pointer_filter_cb callback,
		   gpointer user_data)
{
  PointerFilter *result = g_malloc (sizeof (PointerFilter));

  result->callback = callback;
  result->user_data = user_data;
  result->prefix = NULL;
  result->prefix_length = 0;

  result->taboo[0] = 0;
  result->taboo[1] = 0;
  result->taboo[2] = 0;
  result->taboo[3] = 0;

  return result;
}

void
pointer_filter_move(PointerFilter *pf,
		    unsigned int x,
		    unsigned int y)
{
  pf->taboo[0] = (x>>8) & 0xFF;
  pf->taboo[1] = (x   ) & 0xFF;
  pf->taboo[2] = (y>>8) & 0xFF;
  pf->taboo[3] = (y   ) & 0xFF;
}

void
pointer_filter_read(PointerFilter *pf,
		    gpointer data,
		    unsigned int length)
{
  const unsigned char *chars = data;
  unsigned int state = 0;
  unsigned int i;
  unsigned int frame_start = 0;
  
  for (i=0; i<length; i++)
    {
      printf ("At %3d, state is %d, char is %02x.\n",
	      i, state, chars[i]);

      switch (state)
	{
	case 0:
	  if (chars[i]==5)
	    {
	      frame_start = i;
	      state = 1;
	    }
	  break;

	case 1:
	  /* this always matches */
	  state = 2;
	  break;

	case 2:
	case 3:
	case 4:
	case 5:
	  if (chars[i]==pf->taboo[state-2])
	    {
	      state++;

	      if (state==6)
		{
		  /* Jackpot! */
		  printf ("Jackpot!\n");
		}
	    }
	  else
	    {
	      state = 0;
	      i = frame_start+1;
	    }
	}
    }
}

void
pointer_filter_free(PointerFilter *pf)
{
  g_free (pf);
}

#ifdef POINTER_FILTER_TEST

#include <string.h>
#include <stdio.h>

static char
hex_to_number (char digit)
{
  switch (digit)
    {
    case '0': return 0;
    case '1': return 1;
    case '2': return 2;
    case '3': return 3;
    case '4': return 4;
    case '5': return 5;
    case '6': return 6;
    case '7': return 7;
    case '8': return 8;
    case '9': return 9;
    case 'a': case 'A': return 10;
    case 'b': case 'B': return 11;
    case 'c': case 'C': return 12;
    case 'd': case 'D': return 13;
    case 'e': case 'E': return 14;
    case 'f': case 'F': return 15;
    default:
      fprintf (stderr, "Unknown hex digit %c\n", digit);
      return 0;
    }
}

static void
resurrect (gpointer data,
	   unsigned int length,
	   gpointer user_data)
{
  printf ("(resurrect %d)\n", length);
}

static void
run_test (const char* name,
	  int x, int y,
	  const char *send,
	  const char *get)
{
  unsigned char *temp;
  const unsigned char *in_cursor;
  unsigned char* out_cursor;
  PointerFilter *testing;
  unsigned int temp_length;
  
  printf ("Test \"%s\":",
	  name);

  temp_length = (strlen(send)+1)/3;
  temp = g_malloc (temp_length);
  in_cursor = send;
  out_cursor = temp;
  
  while (*in_cursor!=0 &&
	 *in_cursor!='/')
    {
      if (*in_cursor==' ')
	in_cursor++;

      *out_cursor =
	hex_to_number (*in_cursor) << 4 |
	hex_to_number (*(in_cursor+1));

      in_cursor += 2;
      out_cursor++;
    }

  testing = pointer_filter_new (resurrect,
				NULL);

  pointer_filter_move (testing,
		       x, y);

  pointer_filter_read (testing,
		       temp,
		       temp_length);
}

int
main(int argc, char **argv)
{
  g_type_init ();

  run_test ("Simple removal",
	    0xAB, 0xCD,
	    "01 02 03 04 05 06 05 12 00 AB 00 CD 07 08",
	    "01 02 03 04 05 06 07 08");
}

#endif /* POINTER_FILTER_TEST */

