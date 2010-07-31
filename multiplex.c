#include <glib/glist.h>

typedef struct {
    int current_channel;
    void (*target) (unsigned int,
            const unsigned char*,
            unsigned int);
} XzibitMultiplex;

static void
submit (unsigned int channel,
        const unsigned char *buffer,
        unsigned int size);

XzibitMultiplex*
xzibit_multiplex_new (void)
{
    XzibitMultiplex *result =
        g_malloc (sizeof (XzibitMultiplex));

    result->current_channel = 0;
    result->target = submit;

    return result;
}

void
xzibit_multiplex_free (XzibitMultiplex *self)
{
    g_free (self);
}

static void
submit (unsigned int channel,
        const unsigned char *buffer,
        unsigned int size)
{
    int i;

    for (i=0; i<size; i++)
      {
        g_print ("(%d, %d)\n", channel,
                buffer[i]);
      }
}

void
xzibit_multiplex_receive (XzibitMultiplex *self,
        const unsigned char *buffer,
        unsigned int size)
{
    int i=0;
    int startpos=0;
    const char *start = buffer;

    while (i<size)
      {
        if ((buffer[i] & 0xFE)==0xFE)
          {
            /* a control code */

            self->target (self->current_channel,
                    start,
                    i-startpos);

            switch (buffer[i])
              {
                case 0xFE:
                    self->target (self->current_channel,
                            buffer+i+1,
                            1);
                    i+=2;
                    startpos = i;
                    start = buffer+i;
                    break;

                case 0xFF:
                    self->current_channel =
                            buffer[i+2]*256+buffer[i+1];
                    i+=3;
                    startpos = i;
                    start = buffer+i;
                    break;
              }

          }
        else
          {
            i++;
          }
      }

    self->target (self->current_channel,
        start,
        size-startpos);
}

void
xzibit_multiplex_send (XzibitMultiplex *self,
        unsigned int target_channel,
        const unsigned char *buffer,
        unsigned int size)
{
    int i;
    unsigned int cursor;

    if (target_channel != self->current_channel)
      {
        const unsigned char pair[3] =
          { 255, target_channel % 256, target_channel / 256 };

        self->target (0 /* unused */,
                pair, 3);

        self->current_channel = target_channel;
      }

    cursor = 0;
    for (i=0; i<size; i++)
      {
        if ((buffer[i] & 0xFE)==0xFE)
          {
            unsigned char quoted = 0xFE;

            /* a control code; quote it */
            self->target (0 /* unused */,
                    buffer+cursor, i-cursor);

            self->target (0,
                    &quoted, 1);

            cursor = i;
          }
      }
    self->target (0 /* unused */,
            buffer+cursor, size-cursor);
}

#ifdef TEST

/* This is a list of (channel, data) pairs. */
unsigned char bytes_for_channels[][2] = {
      { 0, 1 },
      { 0, 0 },
      { 0, 254 },
      { 0, 255 },
      { 0, 3 },
      { 1, 15 },
      { 1, 177 },
      { 1, 255 },
      { 1, 153 },
      { 1, 254 },
      { 0, 3 }};

/* This is a sequence of characters which shoul
   generate the list above. */

unsigned char source[] = {
    1, 0, 254, 254, 254, 255, 3,
    255, 1, 0, 15, 177, 254, 255, 153,
    254, 254, 255, 0, 0, 3
};

static void
check_receive (unsigned int channel,
        const unsigned char *buffer,
        unsigned int size)
{
   static int cursor = 0;
   int i;

   for (i=0; i<size; i++)
     {
       if (bytes_for_channels[cursor][0]==channel &&
               bytes_for_channels[cursor][1]==buffer[i])
         {
           g_print ("PASS at offset %d: "
                   "wanted (%d, %d) and got it\n",
                   cursor,
                   channel, buffer[i]);
         }
       else
         {
           g_print ("FAIL at offset %d\a: "
                   "wanted (%d,%d), got (%d,%d)\n",
                   cursor,
                   channel, buffer[i],
                   bytes_for_channels[cursor][0],
                   bytes_for_channels[cursor][1]);
         }

       cursor++;
     }
}

static void
check_send (unsigned int channel,
        const unsigned char *buffer,
        unsigned int size)
{
  static int cursor = 0;
  int i;

  for (i=0; i<size; i++)
    {
      if (source[cursor]==buffer[i])
        {
          g_print ("PASS: wanted %d and got it\n", buffer[i]);
        }
      else
        {
          g_print ("FAIL\a: wanted %d but got %d\n",
                  source[cursor],
                  buffer[i]);
        }
      cursor++;
    }
}
    
int
main (int argc, char **argv)
{
    char buffer[256];
    XzibitMultiplex *multiplex = NULL;
    int i, cursor;
    int current_channel = 0;

    /****************************/

    multiplex = xzibit_multiplex_new ();
    multiplex->target = check_receive;

    xzibit_multiplex_receive (multiplex,
            source,
            G_N_ELEMENTS(source));

    xzibit_multiplex_free (multiplex);

    /****************************/

    multiplex = xzibit_multiplex_new ();
    multiplex->target = check_send;

    /* for this, we have to do some
       concatenation.
       */
    cursor = 0;
    for (i=0; i<G_N_ELEMENTS (bytes_for_channels); i++)
      {
        if (bytes_for_channels[i][0]!=current_channel)
          {
            xzibit_multiplex_send (multiplex,
                    current_channel,
                    buffer,
                    cursor);
            current_channel = bytes_for_channels[i][0];
            cursor = 0;
          }

        buffer[cursor++] = bytes_for_channels[i][1];
      }
    xzibit_multiplex_send (multiplex,
            current_channel,
            buffer,
            cursor);

    /*
    xzibit_multiplex_send (multiplex,
            bytes_for_channels,
            G_N_ELEMENTS(source));
*/
    xzibit_multiplex_free (multiplex);

}

#endif

