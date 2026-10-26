# $Id: Makefile,v 1.4 2008/09/27 02:06:02 laffer1 Exp $
# gtkjj: Just Journal for GTK
CC?=cc
CFLAGS= -D_DEFAULT_SOURCE -I/usr/local/include -Wall -pedantic -std=c99 -O2 `pkg-config --cflags gtk+-3.0 libcurl jansson`
LDFLAGS= `pkg-config --libs gtk+-3.0 libcurl jansson`
PREFIX=	/usr/local

all: gtkjj

gtkjj: gtkjj.c ../jj_rest.c ../jj_rest.h
	${CC} ${CFLAGS} -o gtkjj gtkjj.c ../jj_rest.c ${LDFLAGS}

install:
	install gtkjj ${DESTDIR}${PREFIX}/bin/gtkjj

clean:
	rm -f *.o gtkjj
