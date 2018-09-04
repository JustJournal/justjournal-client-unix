# Makefile for jjclient
CC?=cc
CFLAGS?= -I/usr/local/include -I${LOCALBASE}/include -Wall -pedantic -std=c99 -O2
LDFLAGS?=-L/usr/local/lib -L${LOCALBASE}/lib -lxmlrpc_client -lxmlrpc -lxmlrpc_xmlparse -lxmlrpc_xmltok -lxmlrpc_util -lcurl
PREFIX?= /usr/local

all: clean jjclient

jjclient: jj.o
	${CC} ${CFLAGS} ${LDFLAGS} -o jjclient jj.o

jj.o: jj.c
	${CC} ${CFLAGS} -c jj.c

install: install-man
	install -o root -g wheel -m 555 jjclient ${DESTDIR}${PREFIX}/bin/jjclient

install-man:
	install -o root -g wheel -m 444 jjclient.1 ${DESTDIR}${PREFIX}/man/man1/

clean:
	rm -f *.o jjclient

