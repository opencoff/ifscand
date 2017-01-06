#	$OpenBSD: Makefile,v 1.14 2016/05/03 17:52:33 jca Exp $

commonsrc= ../lib
.PATH: $(commonsrc)

libsrcs = error.c 
PROG=	ifscanctl
SRCS=	ifscanctl.c $(libsrcs)

MAN=	ifscanctl.8

MANDIR = /usr/local/man/man
BINDIR = /usr/local/bin

# Comment for release builds
#DEBUG=1

.ifdef DEBUG
OFLAGS = -g
.else
OFLAGS = -O2 -fexpensive-optimizations
.endif

INCS = -I$(commonsrc) -I..
CFLAGS = $(OFLAGS) $(INCS) \
		 -Wall -Wmissing-declarations -Wshadow \
		 -Wpointer-arith -Wsign-compare

LDADD=	-lutil
DPADD=	${LIBUTIL}


.include <bsd.prog.mk>
