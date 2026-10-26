# Compatibility wrapper for BSD make and GNU make. Build rules live in CMake.
CMAKE?=cmake
CC?=cc
CTEST?=ctest
BUILD_DIR?=build
PREFIX?=/usr/local
MANDIR?=${PREFIX}/share/man
CMAKE_ARGS?=

.PHONY: all configure jjclient check install install-man clean

all: jjclient

configure:
	${CMAKE} -S . -B "${BUILD_DIR}" -DCMAKE_BUILD_TYPE=Release \
	    -DCMAKE_C_COMPILER="${CC}" -DBUILD_GTK=OFF \
	    -DCMAKE_RUNTIME_OUTPUT_DIRECTORY="$$(pwd)" \
	    -DCMAKE_INSTALL_PREFIX="${PREFIX}" -DCMAKE_INSTALL_MANDIR="${MANDIR}" ${CMAKE_ARGS}

jjclient: configure
	${CMAKE} --build "${BUILD_DIR}" --target jjclient

check: configure
	${CMAKE} -S . -B "${BUILD_DIR}" -DBUILD_TESTING=ON ${CMAKE_ARGS}
	${CMAKE} --build "${BUILD_DIR}"
	cd "${BUILD_DIR}" && ${CTEST} --output-on-failure

install: jjclient install-man
	DESTDIR="${DESTDIR}" ${CMAKE} --install "${BUILD_DIR}" --component cli

install-man: configure
	DESTDIR="${DESTDIR}" ${CMAKE} --install "${BUILD_DIR}" --component man

clean:
	if test -f "${BUILD_DIR}/CMakeCache.txt"; then ${CMAKE} --build "${BUILD_DIR}" --target clean; fi
