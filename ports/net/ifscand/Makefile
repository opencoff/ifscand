# $OpenBSD$

COMMENT=	Automatic WiFi configuration daemon

DISTNAME= ifscand-0.4
PKGNAME= ifscand
CATEGORIES=	net

HOMEPAGE=	https://github.com/opencoff/ifscand
MAINTAINER= Sudhi Herle <sw+ifscand@herle.net>

PERMIT_PACKAGE_CDROM=	Yes

WANTLIB += c util

MASTER_SITES=	https://github.com/opencoff/ifscand/archive/

WRKDIST = ${WRKDIR}/${PKGNAME}-${DISTNAME}

# XXX Do I copy here or not?
post-install:
	mkdir -p ${PREFIX}/etc/rc.d
	cp ${WRKDIST}/etc/rc.d/ifscand ${PREFIX}/etc/rc.d/
	cat ${WRKDIST}/etc/rc.conf.local >> ${PREFIX}/etc/rc.conf.local
	@cat ${WRKDIST}/etc/post-install.txt

.include <bsd.port.mk>

