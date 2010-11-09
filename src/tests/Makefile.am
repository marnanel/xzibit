bin_PROGRAMS = xzibit-test-send xzibit-test-compare xzibit-arrange

xzibit_test_send_SOURCES = xzibit-test-send.c
xzibit_test_send_CPPFLAGS = @GTK_CFLAGS@ @X11_CFLAGS@
xzibit_test_send_LDADD = @GTK_LIBS@ @X11_LIBS@ -lXi -lXtst -lXext -lvncserver

xzibit_test_compare_SOURCES = xzibit-test-compare.c
xzibit_test_compare_CPPFLAGS = -g @GTK_CFLAGS@ @X11_CFLAGS@
xzibit_test_compare_LDADD = @GTK_LIBS@ @X11_LIBS@ -lXi -lXtst -lXext

xzibit_arrange_SOURCES = xzibit-arrange.c
xzibit_arrange_CPPFLAGS = @GTK_CFLAGS@ @X11_CFLAGS@
xzibit_arrange_LDADD = @GTK_LIBS@ @X11_LIBS@
