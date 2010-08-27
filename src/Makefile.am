bin_PROGRAMS = xzibit-toggle
xzibit_toggle_SOURCES = xzibit-toggle.c
xzibit_toggle_CPPFLAGS = @X11_CFLAGS@
xzibit_toggle_LDADD = @X11_LIBS@

pkglibexec_PROGRAMS = xzibit-rfb-client
xzibit_rfb_client_SOURCES = xzibit-rfb-client.c xzibit-rfb-client.h
xzibit_rfb_client_CPPFLAGS = -g @CLUTTER_CFLAGS@ @GDK_CFLAGS@ @GTK_CFLAGS@ @GTK_VNC_CFLAGS@
xzibit_rfb_client_LDADD = @CLUTTER_LIBS@ @GDK_LIBS@ @GTK_LIBS@ @GTK_VNC_LIBS@

mutterplugindir = $(libdir)/mutter/plugins
mutterplugin_LTLIBRARIES = libxzibit.la
libxzibit_la_SOURCES = xzibit-plugin.c xzibit-plugin.h vnc.c vnc.h
libxzibit_la_CPPFLAGS = -g @CLUTTER_CFLAGS@ @GDK_CFLAGS@ @GTK_CFLAGS@ @MUTTER_PLUGINS_CFLAGS@
libxzibit_la_LIBADD = @CLUTTER_LIBS@ @GDK_LIBS@ @GTK_LIBS@ @MUTTER_PLUGINS_LIBS@

## automake builds xzibit as "libxzibit.so" and installs it along with the
## statically-linked version.  So we rename it and remove the extra copies.
install-data-hook:
	mv $(mutterplugindir)/libxzibit.so.0.0.0 $(mutterplugindir)/xzibit.so
	rm $(mutterplugindir)/libxzibit.la $(mutterplugindir)/libxzibit.so.0 $(mutterplugindir)/libxzibit.so
