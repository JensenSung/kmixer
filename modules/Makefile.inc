CPPFLAGS+=	-I${NETBSDSRCDIR}/common/include
USE_FORT=	no
WARNS?=		3

.if !empty(IOCONF)
_BSD_IOCONF_MK_USER_=1
.include <bsd.ioconf.mk>
.endif

.include <bsd.own.mk>
