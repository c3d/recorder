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
TESTS=  hanoi_test.c hanoi_test.cpp 			\
	recorder_cplusplus_test.cpp recorder_test.c 	\
	ring_test.c 
TEST_ARGS_hanoi_test=10 | wc

include $(BUILD)rules.mk

$(BUILD)rules.mk:
	cd .. && git submodule update --init --recursive
