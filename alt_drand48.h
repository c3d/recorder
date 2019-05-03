#ifndef ALT_DRAND48_H
#define ALT_DRAND48_H
// *****************************************************************************
// alt_drand48.h                                                Recorder project
// *****************************************************************************
//
// File description:
//
//     An alternative drand48 implementation for platforms that don't have it
//     (most notably MinGW)
//      see http://pubs.opengroup.org/onlinepubs/7908799/xsh/drand48.html
//
//
//
//
//
//
// *****************************************************************************
// This software is licensed under the GNU Lesser General Public License v2+
// (C) 2019, Christophe de Dinechin <christophe@dinechin.org>
// (C) 2018, Frediano Ziglio <fziglio@redhat.com>
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

#include "config.h"

#ifndef HAVE_DRAND48
// Missing functions on MinGW
// see http://pubs.opengroup.org/onlinepubs/7908799/xsh/drand48.html
static double drand48(void)
{
    static uint64_t seed = 1;
    seed = (seed * UINT64_C(0x5DEECE66D) + 0xB) & UINT64_C(0xffffffffffff);
    return seed * (1.0 / ((double) UINT64_C(0x1000000000000)));
}
#endif // HAVE_DRAND48

#endif // ALT_DRAND48_H
