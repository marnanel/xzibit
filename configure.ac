dnl Process this file with autoconf to produce a configure script.
AC_INIT(xzibit, 0.01, thomas.thurman@collabora.co.uk)
AC_CONFIG_AUX_DIR(config)
AC_CONFIG_SRCDIR(src/xzibit-plugin.c)

AC_CONFIG_MACRO_DIR([m4])

AM_INIT_AUTOMAKE

dnl We don't want any static libraries.
LT_INIT([disable-static])

PKG_CHECK_MODULES([CLUTTER], [clutter-1.0])
AC_SUBST([CLUTTER_CFLAGS])
AC_SUBST([CLUTTER_LIBS])

PKG_CHECK_MODULES([GDK], [gdk-2.0])
AC_SUBST([GDK_CFLAGS])
AC_SUBST([GDK_LIBS])

PKG_CHECK_MODULES([GTK], [gtk+-2.0])
AC_SUBST([GTK_CFLAGS])
AC_SUBST([GTK_LIBS])

PKG_CHECK_MODULES([MUTTER_PLUGINS], [mutter-plugins])
AC_SUBST([MUTTER_PLUGINS_CFLAGS])
AC_SUBST([MUTTER_PLUGINS_LIBS])

AC_OUTPUT(Makefile src/Makefile)

