ACLOCAL_AMFLAGS = -I m4

AM_CFLAGS = -include config.h  \
	@ALSA_CFLAGS@
	
	
AM_LDFLAGS = \
	@ALSA_LIBS@ \
	-lpthread

AM_CFLAGS +=  -Wall -Wno-sign-compare


bin_PROGRAMS = atest
atest_SOURCES = atest.c \
                seq.c seq.h


