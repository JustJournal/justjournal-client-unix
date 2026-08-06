# Makefile for jjclient

.POSIX:

CC?=		cc

XMLRPC_C_FLAGS := $(shell pkg-config --cflags xmlrpc xmlrpc_client 2>/dev/null)
XMLRPC_C_LIBS := $(shell pkg-config --libs xmlrpc xmlrpc_client 2>/dev/null)

ifeq ($(XMLRPC_C_FLAGS),)
CFLAGS+=	-I/usr/local/include -I${LOCALBASE}/include -I/usr/include
LDFLAGS+=	-L/usr/local/lib -L${LOCALBASE}/lib -L/usr/lib/x86_64-linux-gnu
LIBS+=		-lxmlrpc_client -lxmlrpc -lxmlrpc_util
else
CFLAGS+=	$(XMLRPC_C_FLAGS)
LIBS+=		$(XMLRPC_C_LIBS)
endif

PREFIX?=	/usr/local
MANDIR?=	${PREFIX}/share/man

.PHONY: all clean install install-man

all: clean jjclient

jjclient: jj.o
	${CC} ${CFLAGS} -o jjclient jj.o ${LDFLAGS} ${LIBS}

jj.o: jj.c
	${CC} ${CFLAGS} -c jj.c

install: install-man
	install -o root -g wheel -m 555 jjclient ${DESTDIR}${PREFIX}/bin/jjclient

install-man:
	install -o root -g wheel -m 444 jjclient.1 ${DESTDIR}${MANDIR}/man1/

clean:
	rm -f *.o jjclient

