# $Id: Makefile,v 1.4 2008/09/27 02:06:02 laffer1 Exp $
# gtkjj: Just Journal for GTK
CC?=cc
CFLAGS= -D_DEFAULT_SOURCE -I/usr/include -I/usr/local/include -I/usr/include/gtk-3.0 -I/usr/include/glib-2.0 -I/usr/lib/glib-2.0/include -I/usr/include/pango-1.0 -I/usr/include/cairo -I/usr/include/harfbuzz -I/usr/include/gdk-pixbuf-2.0 -I/usr/include/atk-1.0 -Wall -pedantic -std=c99 -O2
GTK_CFLAGS:= $(shell pkg-config --cflags gtk+-3.0 2>/dev/null)
GTK_LIBS= -lgtk-3 -lgdk-3 -lpangocairo-1.0 -lcairo-gobject -lgdk_pixbuf-2.0 -latk-1.0 -lpango-1.0 -lcairo -lharfbuzz -lgio-2.0 -lgobject-2.0 -lglib-2.0 -lz
GTK_LDFLAGS:= $(shell pkg-config --libs gtk+-3.0 2>/dev/null)
LDFLAGS= -L/usr/lib -L/usr/local/lib -lxmlrpc_client -lxmlrpc -lxmlrpc_util -lcurl
PREFIX=	/usr/local

all: clean gtkjj

gtkjj: gtkjj.c
	${CC} ${CFLAGS} ${GTK_CFLAGS} ${GTK_LIBS} ${GTK_LDFLAGS} ${LDFLAGS} -o gtkjj gtkjj.c

install:
	install gtkjj ${DESTDIR}${PREFIX}/bin/gtkjj

clean:
	rm -f *.o gtkjj
