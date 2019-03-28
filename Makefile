# ******************************************************************************
# Makefile                                                      Recorder project
# ******************************************************************************
#
# File description:
#
#     The top-level makefile for Recorder, a non-blocking flight recorder
#     for C or C++ programs
#
#
#
#
#
#
# ******************************************************************************
# This software is licensed under the GNU General Public License v3
# (C) 2017-2019, Christophe de Dinechin <christophe@dinechin.org>
# ******************************************************************************
# This file is part of Recorder
#
# Recorder is free software: you can r redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# Recorder is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with Recorder, in a file named COPYING.
# If not, see <https://www.gnu.org/licenses/>.
# ******************************************************************************

SOURCES=recorder_ring.c recorder.c
HEADERS=recorder_ring.h recorder.h
PRODUCTS=recorder.dll
PRODUCTS_VERSION=$(PACKAGE_VERSION)
CONFIG=sigaction <regex.h> <sys/mman.h> drand48 libregex setlinebuf
MANPAGES=$(wildcard man/man3/*.3 man/man1/*.1)

# For pkg-config generation
PACKAGE_NAME=recorder
PACKAGE_VERSION=1.0.5
PACKAGE_DESCRIPTION=A low-overhead, real-time flight recorder for C/C++
PACKAGE_URL="http://github.com/c3d/recorder"
PACKAGE_REQUIRES=
PACKAGE_BUGS=christophe@dinechin.org

LDFLAGS  = -lpthread -lm

TESTS=  hanoi_test.c recorder_test.c ring_test.c crash_test.c
TEST_ARGS_hanoi_test=20 2>&1| grep "End fast recording Hanoi with 20 iterations"

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
