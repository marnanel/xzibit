#include <gdk/gdkx.h>
#include <gdk-pixbuf/gdk-pixdata.h>
#include <stdlib.h>
#include <string.h>

int sent_window_count = 0;
Window sent_window_id = None;
int received_window_count = 0;
Window received_window_id = None;

typedef enum _TestCode {
  TEST_COMPARE_TITLE,
  TEST_COMPARE_CONTENTS,
  TEST_LAST
} TestCode;

gboolean verbose = FALSE;
gboolean run_test[TEST_LAST];

static const GOptionEntry options[] =
{
	{
	  "verbose", 'v', 0, G_OPTION_ARG_NONE, &verbose,
	  "Print messages", NULL },
	{
	  "title", 'T', 0, G_OPTION_ARG_NONE, &(run_test[TEST_COMPARE_TITLE]),
	  "Compare the titles of the windows", NULL },
	{
	  "contents", 'C', 0, G_OPTION_ARG_NONE, &(run_test[TEST_COMPARE_CONTENTS]),
	  "Compare the contents of the windows", NULL },
	{ NULL, 0, 0, G_OPTION_ARG_NONE, NULL, NULL, 0 }
};

static int
check_for_sent_and_received_windows_tail (Window window)
{
  Window root, parent;
  Window *kids;
  Window found = 0;
  int window_is_shared;
  Atom actual_type;
  int actual_format;
  long n_items, bytes_after;
  unsigned char *prop_return;
  unsigned int n_kids, i;

  /*
   * If this window has kids, check them first.
   */

  if (XQueryTree (gdk_x11_get_default_xdisplay (),
		  window,
		  &root,
		  &parent,
		  &kids,
		  &n_kids)==0)
    {
      return 0;
    }

  for (i=0; i<n_kids; i++)
    {
      check_for_sent_and_received_windows_tail (kids[i]);
    }

  if (kids)
    {
      XFree (kids);
    }

  /*
   * Now check this window itself.
   */

  XGetWindowProperty(gdk_x11_get_default_xdisplay (),
		     window,
		     gdk_x11_get_xatom_by_name("_XZIBIT_SHARE"),
		     0, 4, False,
		     gdk_x11_get_xatom_by_name("CARDINAL"),
		     &actual_type,
		     &actual_format,
		     &n_items,
		     &bytes_after,
		     &prop_return);

  /* Default is zero. */
  window_is_shared = 0;

  if (prop_return) {
    window_is_shared = *((int*) prop_return);
    XFree (prop_return);
  }

  /* Now, what have we learned? */

  switch (window_is_shared)
    {
    case 1: /* Sent */
      sent_window_count++;
      sent_window_id = window;
      break;

    case 2: /* Received */
      received_window_count++;
      received_window_id = window;
      break;
    }
}

static int
check_for_sent_and_received_windows (void)
{
  return check_for_sent_and_received_windows_tail (gdk_x11_get_default_root_xwindow ());
}

static void
usage_message (void)
{
  g_print ("xzibit-test-compare : compares sent and received windows\n");
  g_print ("If there is one sent and one received window, we will compare those.\n");
  g_print ("If there is one sent and no received windows,\n");
  g_print ("we will wait for the received window to appear.\n");
  g_print ("Otherwise, you'll get this message.\n");
  g_print ("Use the --help option to get a list of possible points of comparison.\n");
  g_print ("If you're running tests from the commandline,\n");
  g_print ("you will usually need the -v switch.\n");

  exit (255);
}

static char*
get_window_detail (Window window,
		   int point_of_comparison)
{
  if (verbose)
    {
      g_print ("Running test on window %x for %d\n",
	       (int) window, point_of_comparison);
    }

  switch (point_of_comparison)
    {
    case TEST_COMPARE_TITLE:
      {
	Atom actual_type;
	int actual_format;
	unsigned long n_items, bytes_after;
	unsigned char *property;
	unsigned char *name_of_window = NULL;

	/*
	 * nb: this ignores the fact that WM_NAME is Latin-1
	 * whereas _NET_WM_NAME is UTF-8.  The distinction
	 * doesn't arise in the things we're testing.
	 */

	if (XGetWindowProperty (gdk_x11_get_default_xdisplay (),
				window,
				gdk_x11_get_xatom_by_name ("_NET_WM_NAME"),
				0,
				1024,
				False,
				gdk_x11_get_xatom_by_name ("UTF8_STRING"),
				&actual_type,
				&actual_format,
				&n_items,
				&bytes_after,
				&property)==Success)
	  {
	    name_of_window = property;
	  }
            
	if (!name_of_window &&
	    XGetWindowProperty(gdk_x11_get_default_xdisplay (),
			       window,
			       gdk_x11_get_xatom_by_name ("WM_NAME"),
			       0,
			       1024,
			       False,
			       gdk_x11_get_xatom_by_name ("STRING"),
			       &actual_type,
			       &actual_format,
			       &n_items,
			       &bytes_after,
			       &property)==Success)
	  {
	    name_of_window = property;
	  }

	if (name_of_window)
	  {
	    unsigned char *result = g_strdup (name_of_window);

	    XFree (name_of_window);

	    return result;
	  }
	else
	  {
	    return g_strdup("");
	  }
      }
      break;

    case TEST_COMPARE_CONTENTS:
      {
	GdkWindow *foreign = gdk_window_foreign_new (window);
	int width, height;
	GdkPixbuf *pixbuf;
	GdkPixdata pixdata;
	GString *serialised;
	gchar *digest;

	gdk_window_get_size (foreign, &width, &height);
	
	pixbuf = gdk_pixbuf_get_from_drawable (NULL,
					       foreign,
					       gdk_colormap_get_system (),
					       0, 0, 0, 0,
					       width, height);
	gdk_pixdata_from_pixbuf (&pixdata,
				 pixbuf,
				 FALSE);

	/*
	 * Let's turn it into a null-termiated ASCII string,
	 * because the formula for getting the length of the
	 * data part of a GdkPixbuf is over-complicated.
	 */
	serialised =
	  gdk_pixdata_to_csource (&pixdata,
				  "pix",
				  GDK_PIXDATA_DUMP_PIXDATA_STRUCT);

	digest = g_compute_checksum_for_data (G_CHECKSUM_MD5,
					      serialised->str,
					      serialised->len);

	gdk_pixbuf_unref (pixbuf);
	/* XXX how do you free a GdkPixdata? */
	g_string_free (serialised, TRUE);

	return digest;
      }
      break;

    default:
      g_warning ("Unknown test %d is running!",
		 point_of_comparison);
      return g_strdup ("?");
    }
}

static void
run_comparison (Window a, Window b)
{
  int test_count = 0;
  int successes = 0;
  int i;

  for (i=0; i<TEST_LAST; i++)
    {
      char *a_result, *b_result;

      if (!run_test[i])
	{
	  continue;
	}

      a_result = get_window_detail (a, i);
      b_result = get_window_detail (b, i);

      test_count++;

      if (strcmp (a_result, b_result)==0)
	{
	  if (verbose)
	    {
	      g_print ("Test %d passes\n", i);
	    }

	  successes++;
	}
      else
	{
	  /* always tell them about failures, even if !verbose */
	  g_print ("Test %d FAILS\n%x: %s\n%x: %s\n",
		   i,
		   (int) a, a_result,
		   (int) b, b_result);
	}

      g_free (a_result);
      g_free (b_result);
    }

  if (test_count==0)
    {
      g_print ("warning: no tests were run\n");
    }
  else if (verbose)
    {
      g_print ("Tests run: %d\nSuccesses: %d\n",
	       test_count, successes);
    }
}

static void
parse_options(int argc, char **argv)
{
  GOptionContext *context;
  GError *error = NULL;

  memset (run_test, 0, sizeof(run_test));

  context = g_option_context_new ("xzibit-test-compare");
  g_option_context_add_main_entries (context, options, NULL);
  g_option_context_parse (context, &argc, &argv, &error);
  if (error)
    {
      g_print ("%s: %s\n",
	       argv[0],
	       error->message);
      g_error_free (error);
      exit (1);
    }
}

int
main(int argc, char **argv)
{
  gtk_init (&argc, &argv);

  parse_options (argc, argv);

  check_for_sent_and_received_windows ();

  if (sent_window_count==1 && received_window_count==1)
    {
      /* ideal; we know what we're dealing with */
      run_comparison (sent_window_id,
		      received_window_id);
    }
  else if (sent_window_count==1 && received_window_count==0)
    {
      /*
       * a window has been sent; none has been retrieved;
       * block on it
       */
      g_warning ("Not implemented: wait for received");
    }
  else
    {
      g_print ("Count of sent windows: %d.  Count of received windows: %d.\n\n",
	       sent_window_count,
	       received_window_count);
      usage_message ();
    }

  return 0;
}

/* EOF xzibit-test-compare.c */
