#******************************************************************************
# Makefile<recorder>                                          Recorder project
#******************************************************************************
#
#  File Description:
#
#     The top-level makefile for Recorder, a non-blocking flight recorder
#     for C or C++ programs
#
#
#
#
#
#
#******************************************************************************
# (C) 2015-2018 Christophe de Dinechin <christophe@dinechin.com>
#******************************************************************************

SOURCES=recorder_ring.c recorder.c
HEADERS=recorder_ring.h recorder.h
PRODUCTS=recorder.dll
PRODUCTS_VERSION=$(PACKAGE_VERSION)
CONFIG=sigaction <regex.h> <sys/mman.h> drand48 libregex

# For pkg-config generation
PACKAGE_NAME=recorder
PACKAGE_VERSION=1.0.3
PACKAGE_DESCRIPTION=A low-overhead, real-time flight recorder for C/C++
PACKAGE_URL="http://github.com/c3d/recorder"
PACKAGE_REQUIRES=
PACKAGE_BUGS=christophe@dinechin.org

LDFLAGS  = -lpthread -lm

TESTS=  hanoi_test.c recorder_test.c ring_test.c crash_test.c
TEST_ARGS_hanoi_test=20 | grep "End fast recording Hanoi with 20 iterations"

MIQ=make-it-quick/
include $(MIQ)rules.mk

$(MIQ)rules.mk:
	git submodule update --init --recursive

MAKEOVERRIDES:=
scope: scope/Makefile
scope: .ALWAYS
	cd scope && make
scope/Makefile:
	cd scope && qmake
.install: $(DO_INSTALL=scope/recorder_scope.$(DO_INSTALL)_exe)
scope/recorder_scope.$(DO_INSTALL)_exe: scope
