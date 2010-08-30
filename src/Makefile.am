bin_SCRIPTS = xzibit-run xzibit-demo

bin_PROGRAMS = xzibit-toggle xzibit-autoshare
xzibit_toggle_SOURCES = xzibit-toggle.c
xzibit_toggle_CPPFLAGS = @X11_CFLAGS@
xzibit_toggle_LDADD = @X11_LIBS@

xzibit_autoshare_SOURCES = xzibit-autoshare.c
xzibit_autoshare_CPPFLAGS = -g @GDK_CFLAGS@ @GTK_CFLAGS@
xzibit_autoshare_LDADD = @GDK_LIBS@ @GTK_LIBS@

pkglibexec_PROGRAMS = xzibit-rfb-client
xzibit_rfb_client_SOURCES = xzibit-rfb-client.c
xzibit_rfb_client_CPPFLAGS = -g @CLUTTER_CFLAGS@ @GDK_CFLAGS@ @GTK_CFLAGS@ @GTK_VNC_CFLAGS@
xzibit_rfb_client_LDADD = @CLUTTER_LIBS@ @GDK_LIBS@ @GTK_LIBS@ @GTK_VNC_LIBS@

mutterplugindir = $(libdir)/mutter/plugins
mutterplugin_LTLIBRARIES = libxzibit.la
libxzibit_la_SOURCES = xzibit-plugin.c vnc.c vnc.h
libxzibit_la_CPPFLAGS = -g @CLUTTER_CFLAGS@ @GDK_CFLAGS@ @GTK_CFLAGS@ @MUTTER_PLUGINS_CFLAGS@
libxzibit_la_LIBADD = @CLUTTER_LIBS@ @GDK_LIBS@ @GTK_LIBS@ @MUTTER_PLUGINS_LIBS@ -lXi -lXtst -lXext -lvncserver

## automake builds xzibit as "libxzibit.so" and installs it along with the
## statically-linked version.  So we rename it and remove the extra copies.
install-data-hook:
	mv $(mutterplugindir)/libxzibit.so.0.0.0 $(mutterplugindir)/xzibit.so
	rm $(mutterplugindir)/libxzibit.la $(mutterplugindir)/libxzibit.so.0 $(mutterplugindir)/libxzibit.so
