# Makefile for jjclient - MidnightBSD/FreeBSD

.POSIX:

CC?=		cc
CFLAGS+=	-Wall -pedantic -O2 -std=c99
PREFIX?=	/usr/local
MANDIR?=	${PREFIX}/share/man

XMLRPC_CFLAGS!=	pkg-config --cflags xmlrpc xmlrpc_client 2>/dev/null || echo "-I/usr/local/include -I${LOCALBASE}/include"
XMLRPC_LIBS!=	pkg-config --libs xmlrpc xmlrpc_client 2>/dev/null || echo "-L/usr/local/lib -L${LOCALBASE}/lib -lxmlrpc_client -lxmlrpc -lxmlrpc_xmlparse -lxmlrpc_xmltok -lxmlrpc_util"

CFLAGS+=	${XMLRPC_CFLAGS}
LDFLAGS+=	${XMLRPC_LIBS}

.PHONY: all clean install install-man

all: clean jjclient

jjclient: jj.o
	${CC} ${CFLAGS} -o jjclient jj.o ${LDFLAGS}

jj.o: jj.c
	${CC} ${CFLAGS} -c jj.c

install: install-man
	install -o root -g wheel -m 555 jjclient ${DESTDIR}${PREFIX}/bin/jjclient

install-man:
	install -o root -g wheel -m 444 jjclient.1 ${DESTDIR}${MANDIR}/man1/

clean:
	rm -f *.o jjclient

