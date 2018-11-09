#ifndef ALT_DRAND48_H
#define ALT_DRAND48_H
// ****************************************************************************
//  alt_drand48.h                                            Recorder project
// ****************************************************************************
//
//   File Description:
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
// ****************************************************************************
//  (C) 2018 Frediano Ziglio <fziglio@redhat.com>
//  (C) 2018 Christophe de Dinechin <christophe@dinechin.org>
//   This software is licensed under the GNU Lesser General Public License v3
//   See LICENSE file for details.
// ****************************************************************************

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
