# Makefile for jjclient

.POSIX:

CC?=		cc
CFLAGS+=	-I/usr/local/include -I${LOCALBASE}/include -Wall -pedantic -O2 -std=c99
LDFLAGS+=	-L/usr/local/lib -L${LOCALBASE}/lib -lxmlrpc_client -lxmlrpc -lxmlrpc_xmlparse -lxmlrpc_xmltok -lxmlrpc_util -lcurl
PREFIX?=	/usr/local
MANDIR?=	${PREFIX}/share/man

.PHONY: all clean install install-man

all: clean jjclient

jjclient: jj.o
	${CC} ${CFLAGS} ${LDFLAGS} -o jjclient jj.o

jj.o: jj.c
	${CC} ${CFLAGS} -c jj.c

install: install-man
	install -o root -g wheel -m 555 jjclient ${DESTDIR}${PREFIX}/bin/jjclient

install-man:
	install -o root -g wheel -m 444 jjclient.1 ${DESTDIR}${MANDIR}/man1/

clean:
	rm -f *.o jjclient

