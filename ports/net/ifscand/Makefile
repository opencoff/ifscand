# $OpenBSD$

COMMENT=	Automatic WiFi configuration daemon

DISTNAME=	ifscand-0.3
CATEGORIES=	net

HOMEPAGE=	https://github.com/opencoff/ifscand
MAINTAINER= Sudhi Herle <sw+ifscand@herle.net>

PERMIT_PACKAGE_CDROM=	Yes

WANTLIB += c util

MASTER_SITES=	https://github.com/opencoff/ifscand/archive/0.3.tar.gz

# XXX Do I copy here or not?
post-install:
	mv ${WRKINST}/etc/rc.d/ifscand /etc/rc.d/
	cat ${WRKINST}/etc/rc.conf >> /etc/rc.conf.local

.include <bsd.port.mk>

