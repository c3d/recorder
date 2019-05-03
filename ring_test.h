#ifndef RING_TEST_H
#define RING_TEST_H
// *****************************************************************************
// ring_test.h                                                  Recorder project
// *****************************************************************************
//
// File description:
//
//     Header for ring_test.c
//
//
//
//
//
//
//
//
// *****************************************************************************
// This software is licensed under the GNU Lesser General Public License v2+
// (C) 2017-2019, Christophe de Dinechin <christophe@dinechin.org>
// *****************************************************************************
// This file is part of Recorder
//
// Recorder is free software: you can redistribute it and/or modify
// it under the terms of the GNU Lesser General Public License as published by
// the Free Software Foundation, either version 2 of the License, or
// (at your option) any later version.
//
// Recorder is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Lesser General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public License
// along with Recorder, in a file named COPYING.
// If not, see <https://www.gnu.org/licenses/>.
// *****************************************************************************

// Place these declaration in a header to avoid clang warnings
#define RECORDER_RING(Name, Type, Size)                  \
    RECORDER_RING_DECLARE(Name, Type, Size)              \
    RECORDER_RING_DEFINE (Name, Type, Size)
RECORDER_RING(buffer, char, 1024);

#endif
