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
# (C) 2015 Christophe de Dinechin <christophe@taodyne.com>
#******************************************************************************

BUILD=build/

SOURCES=ring.c recorder.c
PRODUCTS=librecorder.lib
CONFIG=struct_sigaction

# Need -lpthreads in some build environments
LDFLAGS_linux  = -lpthread
LDFLAGS_cygwin = -lpthread
LDFLAGS_mingw  = -lpthread

TESTS=  hanoi_test.c recorder_test.c ring_test.c crash_test.c
TEST_ARGS_hanoi_test=20 | wc

include $(BUILD)rules.mk

# For the recorder, we need C++11
CXX=$(CXX11)

$(BUILD)rules.mk:
	git submodule update --init --recursive
