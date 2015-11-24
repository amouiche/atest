ACLOCAL_AMFLAGS = -I m4

AM_CFLAGS = -include config.h  \
	@ALSA_CFLAGS@ \
	@LIBEV_CFLAGS@
	
	
AM_LDFLAGS = \
	@ALSA_LIBS@ \
	@LIBEV_LIBS@

AM_CFLAGS += -Wall -Wno-sign-compare 
AM_CFLAGS += -Wno-strict-aliasing  # to remove a lot of libev warning concerning strict aliasing

bin_PROGRAMS = atest
atest_SOURCES = atest.c \
                seq.c seq.h \
                alsa.c alsa.h


