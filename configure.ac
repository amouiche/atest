AC_INIT([atest], [1.0])
AC_CONFIG_MACRO_DIR([m4])

AM_INIT_AUTOMAKE([foreign subdir-objects -Wall -Werror])


AC_PROG_CC

LT_INIT

PKG_CHECK_MODULES([ALSA], [alsa >= 1.0.23])


# stollen from lighttpd's configure.ac
AC_MSG_CHECKING([for libev support])
AC_ARG_WITH([libev],
    [AS_HELP_STRING([--with-libev@<:@=PATH@:>@],[Search for libev in PATH/include and PATH/lib])],
    [WITH_LIBEV=$withval],[WITH_LIBEV=yes])

LIBEV_CFLAGS=""
LIBEV_LIBS=""

PKG_CHECK_MODULES([LIBEV], [libev], [], [
	# no pkg-config for libev, searching manually:
	if test "$WITH_LIBEV" != "yes"; then
		LIBEV_CFLAGS="-I$WITH_LIBEV/include"
		LIBEV_LIBS="-L$WITH_LIBEV/lib -lev"
	else
		AC_CHECK_HEADERS([ev.h],[
			AC_CHECK_LIB([ev], [ev_time], [
					LIBEV_LIBS="-lev"
				],[
					AC_MSG_ERROR([libev not found])
				]
			)],[
				AC_MSG_ERROR([libev not found])
			]
		)
	fi
])

AC_SUBST([LIBEV_CFLAGS])
AC_SUBST([LIBEV_LIBS])



AC_CONFIG_HEADERS([config.h])
AC_CONFIG_FILES([Makefile])

AC_OUTPUT
