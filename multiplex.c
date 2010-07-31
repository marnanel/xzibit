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

int
main (int argc, char **argv)
{
    XzibitMultiplex *multiplex = NULL;

    multiplex = xzibit_multiplex_new ();
    multiplex->target = check_receive;

    xzibit_multiplex_receive (multiplex,
            source,
            G_N_ELEMENTS(source));

    xzibit_multiplex_free (multiplex);

}

#endif

