pkglibexec_PROGRAMS = xzibit-rfb-client
xzibit_rfb_client_SOURCES = xzibit-rfb-client.c xzibit-rfb-client.h
xzibit_rfb_client_CFLAGS = -g @CLUTTER_CFLAGS@ @GDK_CFLAGS@ @GTK_CFLAGS@ @GTK_VNC_CFLAGS@
xzibit_rfb_client_LDADD = @CLUTTER_LIBS@ @GDK_LIBS@ @GTK_LIBS@ @GTK_VNC_LIBS@

# Things to fix about this:
#  - It gets the filename wrong.  It should be xzibit.so, not libxzibit.so.
#  - It installs the .la as well (but this is less important)
mutterplugindir = $(libdir)/mutter/plugins
mutterplugin_LTLIBRARIES = libxzibit.la
libxzibit_la_SOURCES = xzibit-plugin.c xzibit-plugin.h vnc.c vnc.h
libxzibit_la_CFLAGS = -g @CLUTTER_CFLAGS@ @GDK_CFLAGS@ @GTK_CFLAGS@ @MUTTER_PLUGINS_CFLAGS@
libxzibit_la_LIBADD = @CLUTTER_LIBS@ @GDK_LIBS@ @GTK_LIBS@ @MUTTER_PLUGINS_LIBS@




