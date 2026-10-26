# Makefile for jjclient

CC?=		cc

CFLAGS+= -Wall -pedantic -O2 -std=c99
REST_CFLAGS := $(shell pkg-config --cflags libcurl jansson)
REST_LIBS := $(shell pkg-config --libs libcurl jansson)
CFLAGS+= $(REST_CFLAGS)
LDLIBS+= $(REST_LIBS)

PREFIX?=	/usr/local
MANDIR?=	${PREFIX}/share/man

.PHONY: all clean check install install-man

all: jjclient

jjclient: jj.o jj_rest.o
	${CC} ${CFLAGS} -o jjclient jj.o jj_rest.o ${LDFLAGS} ${LDLIBS}

jj.o: jj.c jj_rest.h
	${CC} ${CFLAGS} -c jj.c

jj_rest.o: jj_rest.c jj_rest.h
	${CC} ${CFLAGS} -c jj_rest.c

check:
	python3 tests/test_rest.py

install: install-man
	install -m 555 jjclient ${DESTDIR}${PREFIX}/bin/jjclient

install-man:
	install -m 444 jjclient.1 ${DESTDIR}${MANDIR}/man1/

clean:
	rm -f *.o jjclient
