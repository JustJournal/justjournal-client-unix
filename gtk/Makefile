# Compatibility wrapper for building and installing only the GTK client.
CMAKE?=cmake
CC?=cc
BUILD_DIR?=build
PREFIX?=/usr/local
CMAKE_ARGS?=

.PHONY: all configure gtkjj install clean

all: gtkjj

configure:
	${CMAKE} -S .. -B "${BUILD_DIR}" -DCMAKE_BUILD_TYPE=Release \
	    -DCMAKE_C_COMPILER="${CC}" -DBUILD_GTK=ON \
	    -DCMAKE_RUNTIME_OUTPUT_DIRECTORY="$$(pwd)" \
	    -DCMAKE_INSTALL_PREFIX="${PREFIX}" ${CMAKE_ARGS}

gtkjj: configure
	${CMAKE} --build "${BUILD_DIR}" --target gtkjj

install: gtkjj
	DESTDIR="${DESTDIR}" ${CMAKE} --install "${BUILD_DIR}" --component gtk

clean:
	if test -f "${BUILD_DIR}/CMakeCache.txt"; then ${CMAKE} --build "${BUILD_DIR}" --target clean; fi
