#ifndef RING_TEST_H
#define RING_TEST_H
// ****************************************************************************
//  ring_test.h                                               Recorder project
// ****************************************************************************
//
//   File Description:
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
// ****************************************************************************
//  (C) 2017 Christophe de Dinechin <christophe@dinechin.org>
//   This software is licensed under the GNU General Public License v3
//   See file LICENSE for details.
// ****************************************************************************

// Place these declaration in a header to avoid clang warnings
#define RING(Type, Name, Size)                  \
    RING_DECLARE(Type, Name, Size)              \
    RING_DEFINE (Type, Name, Size)
RING(char, buffer, 1024);

#endif
