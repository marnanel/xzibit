#include "get-avatar.h"
#include <gdk-pixbuf/gdk-pixbuf.h>

#define IMAGE_FILENAME "/tmp/get-avatar.png"
#define MAX_DIMENSIONS 100

static char*
get_filename (int which)
{
  switch (which)
    {
    case 0:
      {
	const char *env = g_getenv ("XZIBIT_AVATAR");
	if (env)
	  return g_strdup (env);
	else
	  return g_strdup ("");
      }

    case 1:
      {
	const char *xdg = g_get_user_config_dir ();
	char *result = g_build_filename
	  (xdg,
	   "xzibit",
	   "avatar.png",
	   NULL);
	
	return result;
      }

    case 2:
      {
	char *result = g_build_filename
	  (g_get_home_dir (),
	   ".face",
	   NULL);

	return result;
      }

    default:
      return NULL;
    }
}

static GdkPixbuf*
scale_sensibly (GdkPixbuf *source)
{
  int width, height;

  if (!source)
    return NULL;

  width = gdk_pixbuf_get_width (source);
  height = gdk_pixbuf_get_height (source);

  if (width > MAX_DIMENSIONS ||
      height > MAX_DIMENSIONS)
    {
      /* Too big!  Scale it. */

      GdkPixbuf *temp;
      gdouble aspect =
	((gdouble)width) /
	((gdouble) height);

      if (aspect > 1.0)
	{
	  /* Landscape */
	  width = MAX_DIMENSIONS;
	  height = ((gdouble) MAX_DIMENSIONS) / aspect;
	}
      else
	{
	  /* Portrait (or square) */
	  height = MAX_DIMENSIONS;
	  width = ((gdouble) MAX_DIMENSIONS) * aspect;
	}

      temp = gdk_pixbuf_scale_simple (source,
				      width, height,
				      GDK_INTERP_BILINEAR);

      gdk_pixbuf_unref (source);
      source = temp;
    }

  return source;
}

GString*
get_avatar (void)
{
  int i=0;
  char *filename;

  do
    {
      filename = get_filename (i);

      if (filename && *filename &&
	  g_file_test (filename,
		       G_FILE_TEST_EXISTS | G_FILE_TEST_IS_REGULAR))
	{
	  GdkPixbuf *pixbuf = NULL;
	  gchar *buffer;
	  gsize buffer_size;

	  pixbuf = gdk_pixbuf_new_from_file (filename,
					     NULL);

	  if (!pixbuf)
	    {
	      g_warning ("File %s was not an image; ignoring", filename);
	      g_free (filename);
	      i++;
	      continue;
	    }

	  pixbuf = scale_sensibly (pixbuf);

	  /* FIXME: error checking?? */
	  gdk_pixbuf_save_to_buffer (pixbuf,
				     &buffer,
				     &buffer_size,
				     "png",
				     NULL, NULL);

	  g_free (filename);
	  return g_string_new_len (buffer,
				   buffer_size);
	}

      g_free (filename);
      i++;
    }
  while (filename);

  return g_string_new ("");
}


#ifdef GET_AVATAR_TEST

int
main (int argc, char **argv)
{
  GString* result;
  char* args[3];

  gtk_init (&argc, &argv);

  g_print ("get-avatar - no parameters; try setting XZIBIT_AVATAR\n");

  result = get_avatar ();

  g_file_set_contents (IMAGE_FILENAME,
		       result->str,
		       result->len,
		       NULL);

  args[0] = "eog";
  args[1] = IMAGE_FILENAME;
  args[2] = NULL;

  if (result->len!=0)
    {
      g_spawn_async ("/",
		     args,
		     NULL,
		     G_SPAWN_SEARCH_PATH,
		     NULL, NULL,
		     NULL, NULL);
    }

  g_string_free (result, TRUE);
}

#endif /* GET_AVATAR_TEST */
