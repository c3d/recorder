// ****************************************************************************
//  crash_test.c                                              Recorder project
// ****************************************************************************
//
//   File Description:
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
// ****************************************************************************
//  (C) 2017-2018 Christophe de Dinechin <christophe@dinechin.org>
//   This software is licensed under the GNU General Public License v3
//   See file LICENSE for details.
// ****************************************************************************

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
    RECORD(MAIN, "Signal handler for %d called", sig);
    fprintf(stderr, "Signal handler for %d called\n", sig);

    RECORD(MAIN, "Restoring default signal handler");
    signal(sig, SIG_DFL);
    exit(0);
}


int main(int argc, char **argv)
{
    RECORD(MAIN, "Starting crash test program");

    RECORD(MAIN, "Installing signal handler %p", signal_handler);
#ifdef SIGBUS
    signal(SIGBUS, signal_handler);
#endif // SIGBUS
#ifdef SIGSEGV
    signal(SIGSEGV, signal_handler);
#endif // SIGSEGV

    RECORD(MAIN, "Installing recorder default signal handlers");
    recorder_dump_on_common_signals(0, 0);

    RECORD(MAIN, "Dereferencing a NULL pointer, ptr=%p", ptr);
    *ptr = 0;

    RECORD(MAIN, "Checking results, ptr=%p failed=%d", ptr, failed);
    if (failed)
        fprintf(stderr, "The test failed (signal handler not invoked");
    else
        fprintf(stderr, "The test succeeded (signal handler was invoked");

    recorder_dump();

    return failed;
}
