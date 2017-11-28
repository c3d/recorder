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

SOURCES=recorder_ring.c recorder.c
PRODUCTS=recorder.lib
CONFIG=struct_sigaction <regex.h> <sys/mman.h>

# Need -lpthreads in some build environments
LDFLAGS_linux  = -lpthread -lm
LDFLAGS_cygwin = -lpthread -lm
LDFLAGS_mingw  = -lpthread -lm

TESTS=  hanoi_test.c recorder_test.c ring_test.c crash_test.c
TEST_ARGS_hanoi_test=20 | grep "hanoi_test.c"

include $(BUILD)rules.mk

# For the recorder, we need C++11
CXX=$(CXX11)

$(BUILD)rules.mk:
	git submodule update --init --recursive
