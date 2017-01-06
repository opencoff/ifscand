#	$OpenBSD: Makefile,v 1.14 2016/05/03 17:52:33 jca Exp $


commonsrc= ../lib
.PATH: $(commonsrc)

libsrcs = error.c splitargs.c strtrim.c str2hex.c mkdirhier.c
PROG=	ifscand
SRCS=	ifscand.c scan.c db.c cmds.c ifcfg.c \
		$(libsrcs)

MAN=	ifscand.8

MANDIR = /usr/local/man/man
BINDIR = /usr/local/bin

INCS = -I$(commonsrc) -I..

# Comment for release builds
#DEBUG=1

.ifdef DEBUG
OFLAGS = -g
.else
OFLAGS = -O2 -fexpensive-optimizations
.endif

CFLAGS = $(OFLAGS) $(INCS) \
		 -Wall -Wmissing-declarations -Wshadow \
		 -Wpointer-arith -Wsign-compare

LDADD=	-lutil
DPADD=	${LIBUTIL}


.include <bsd.prog.mk>

afterinstall:
	cp ../etc/rc.d/ifscand /etc/rc.d/
	cat ../etc/rc.conf.local >> /etc/rc.conf.local

