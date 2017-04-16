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
# (C) 2015 Taodyne SAS
#******************************************************************************

BUILD=build/

SOURCES=recorder.cpp
PRODUCTS=librecorder.lib

# Need -lpthreads in some build environments
LDFLAGS_linux  = -lpthread
LDFLAGS_cygwin = -lpthread
LDFLAGS_mingw  = -lpthread

TESTS=  hanoi_test.c hanoi_test.cpp 			\
	recorder_cplusplus_test.cpp recorder_test.c 	\
	ring_test.c 
TEST_ARGS_hanoi_test=10 | wc


include $(BUILD)rules.mk

# For the recorder, we need C++11
CXX=$(CXX11)

$(BUILD)rules.mk:
	cd .. && git submodule update --init --recursive
