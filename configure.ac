AC_INIT([atest], [1.0])
AC_CONFIG_MACRO_DIR([m4])

AM_INIT_AUTOMAKE([foreign subdir-objects -Wall -Werror])


AC_PROG_CC

LT_INIT

PKG_CHECK_MODULES([ALSA], [alsa >= 1.0.23])


AC_CONFIG_HEADERS([config.h])
AC_CONFIG_FILES([Makefile])

AC_OUTPUT
