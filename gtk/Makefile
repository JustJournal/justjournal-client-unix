# $Id: Makefile,v 1.4 2008/09/27 02:06:02 laffer1 Exp $
# gtkjj: Just Journal for GTK
CC=gcc
CFLAGS= -D_DEFAULT_SOURCE -I/usr/local/include -Wall -pedantic -std=c99 -O2
GTK_CFLAGS:= $(shell pkg-config --cflags gtk+-3.0 xmlrpc xmlrpc_client)
GTK_LDFLAGS:= $(shell pkg-config --libs gtk+-3.0 xmlrpc xmlrpc_client)
LDFLAGS= -lcurl
PREFIX=	/usr/local

all: clean gtkjj

gtkjj: gtkjj.c
	${CC} ${CFLAGS} ${GTK_CFLAGS} -Wl,--no-as-needed ${GTK_LDFLAGS} ${LDFLAGS} -o gtkjj gtkjj.c

install:
	install gtkjj ${DESTDIR}${PREFIX}/bin/gtkjj

clean:
	rm -f *.o gtkjj
