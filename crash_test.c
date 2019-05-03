// *****************************************************************************
// crash_test.c                                                 Recorder project
// *****************************************************************************
//
// File description:
//
//     Test for the flight recorder
//
//     This tests that we can dump something at crash time
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

#include "recorder_ring.h"
#include "recorder.h"

#include <stdio.h>
#include <signal.h>
#include <stdlib.h>


int failed = 1;
int *ptr = NULL;


// ============================================================================
//
//    Main entry point
//
// ============================================================================

RECORDER(MAIN, 64, "Primary recorder for crash_test.c");

static void signal_handler(int sig)
{
    record(MAIN, "Signal handler for %d called", sig);
    printf("Signal handler for %d called\n", sig);

    record(MAIN, "Restoring default signal handler");
    signal(sig, SIG_DFL);
    exit(0);
}


int main(int argc, char **argv)
{
    record(MAIN, "Starting crash test program");

    record(MAIN, "Installing signal handler %p", signal_handler);
#ifdef SIGBUS
    signal(SIGBUS, signal_handler);
#endif // SIGBUS
#ifdef SIGSEGV
    signal(SIGSEGV, signal_handler);
#endif // SIGSEGV

    record(MAIN, "Installing recorder default signal handlers");
    recorder_dump_on_common_signals(0, 0);

    record(MAIN, "Dereferencing a NULL pointer, ptr=%p", ptr);
    *ptr = 0;

    record(MAIN, "Checking results, ptr=%p failed=%d", ptr, failed);
    if (failed)
        fprintf(stderr, "The test failed (signal handler not invoked");
    else
        fprintf(stderr, "The test succeeded (signal handler was invoked");

    recorder_dump();

    return failed;
}
