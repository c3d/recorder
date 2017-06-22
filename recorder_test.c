// ****************************************************************************
//  recorder_test.c                                           Recorder project
// ****************************************************************************
//
//   File Description:
//
//     Test for the flight recorder
//
//     This tests that we can record things and dump them.
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

#include "ring.h"
#include "recorder.h"

#include <stdio.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <signal.h>


int failed = 0;

RECORDER(MAIN,       64, "Global operations in 'main()'");
RECORDER(Pauses,    256, "Pauses during blocking operations");
RECORDER(Special,    64, "Special operations to the recorder");
RECORDER(SpeedTest,  32, "Recorder speed test");



// ============================================================================
//
//    Flight recorder testing
//
// ============================================================================

uintptr_t recorder_count = 0;
unsigned pauses_count = 0;

#define INFO(...)                                                       \
    do                                                                  \
    {                                                                   \
        RECORD(MAIN, __VA_ARGS__);                                      \
        char buf[256];                                                  \
        snprintf(buf, sizeof(buf), __VA_ARGS__);                        \
        puts(buf);                                                      \
    } while(0)

#define VERBOSE(...) if (debug) INFO(__VA_ARGS__)

#define FAIL(...)                                                       \
    do                                                                  \
    {                                                                   \
        RECORD(MAIN, "FAILURE");                                        \
        RECORD(MAIN, __VA_ARGS__);                                      \
        char buf[256];                                                  \
        snprintf(buf, sizeof(buf), __VA_ARGS__);                        \
        puts(buf);                                                      \
        failed = 1;                                                     \
    } while(0)

unsigned thread_id = 0;
unsigned threads_to_stop = 0;

#ifdef CONFIG_MINGW
#define lrand48() rand()
#endif // CONFIG_MINGW

void dawdle(unsigned minimumMs)
{
    struct timespec tm;
    tm.tv_sec = 0;
    tm.tv_nsec =  + minimumMs * (1000 * 1000 + lrand48() % 2000000);
    RECORD(Pauses, "Pausing #%u %ld.%03dus",
           ring_fetch_add(pauses_count, 1),
           tm.tv_nsec / 1000, tm.tv_nsec % 1000);
    nanosleep(&tm, NULL);
}

void *recorder_thread(void *unused)
{
    uintptr_t i = 0;
    while (!threads_to_stop)
        RECORD(SpeedTest, "Recording %u", i++);
    ring_fetch_add(recorder_count, i);
    ring_fetch_add(threads_to_stop, -1);
    return NULL;
}

void *recorder_fast_thread(void *unused)
{
    uintptr_t i = 0;
    while (!threads_to_stop)
        RECORD_FAST(SpeedTest, "Fast recording %u", i++);
    ring_fetch_add(recorder_count, i);
    ring_fetch_add(threads_to_stop, -1);
    return NULL;
}

void flight_recorder_test(int argc, char **argv)
{
    int i, j;
    uintptr_t count = argc >= 2 ? atoi(argv[1]) : 16;
    unsigned howLong = argc >= 3 ? atoi(argv[2]) : 10;

    for (i = 0; i < 2; i++)
    {
        recorder_count = 0;

        INFO("Launching %lu %s recorder thread%s",
             count, i ? "fast" : "normal", count>1?"s":"");
        RECORD(MAIN, "Starting %s speed test for %us with %u threads",
               i ? "fast" : "normal", howLong, count);

        pthread_t tid;
        for (j = 0; j < count; j++)
            pthread_create(&tid, NULL,
                           i ? recorder_fast_thread : recorder_thread,
                           NULL);

        INFO("%s recorder testing in progress, please wait about %ds",
             i ? "Fast" : "Normal", howLong);
        unsigned sleepTime = howLong;
        do { sleepTime =  sleep(sleepTime); } while (sleepTime);
        INFO("%s recorder testing completed, stopping threads",
             i ? "Fast" : "Normal");
        threads_to_stop = count;

        while(threads_to_stop)
        {
            RECORD(Pauses, "Waiting for recorder threads to stop, %u remaining",
                   threads_to_stop);
            dawdle(1);
        }
        INFO("%s test: all threads have stopped, %lu iterations",
             i ? "Fast" : "Normal", recorder_count);

        printf("Recorder test analysis (%s):\n"
               "  Iterations            = %8lu\n"
               "  Iterations / ms       = %8lu\n"
               "  Duration per record   = %8uns\n"
               "  Number of threads     = %8lu\n",
               i ? "Fast version" : "Normal version",
               recorder_count,
               recorder_count / (howLong * 1000),
               (unsigned) (howLong * 1000000000ULL / recorder_count),
               count);

        INFO("Recorder test complete (%s), %lu threads.",
             i ? "Fast version" : "Normal version", count);
        INFO("  Iterations      = %10lu", recorder_count);
        INFO("  Iterations / ms = %10lu", recorder_count / (howLong * 1000));
        INFO("  Record cost     = %10uns",
             (unsigned) (howLong * 1000000000ULL / recorder_count));
    }

    RECORD(Special, "Sizeof int=%u intptr_t=%u float=%u double=%u",
           sizeof(int), sizeof(intptr_t), sizeof(float), sizeof(double));

    RECORD(Special, "Float      3.1415 = %f", 3.1415f);
    RECORD(Special, "Float    X 3.1415 = %x", 3.1415f);
    RECORD(Special, "Double     3.1415 = %f", 3.1415);
    RECORD(Special, "Double   X 3.1415 = %x", 3.1415);

    recorder_dump_for("Special");
    recorder_dump();
}



// ============================================================================
//
//    Main entry point
//
// ============================================================================

int main(int argc, char **argv)
{
    recorder_dump_on_common_signals(0, 0);
    flight_recorder_test(argc, argv);
    return failed;
}
