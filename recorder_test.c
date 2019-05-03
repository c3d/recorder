// *****************************************************************************
// recorder_test.c                                              Recorder project
// *****************************************************************************
//
// File description:
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
// *****************************************************************************
// This software is licensed under the GNU Lesser General Public License v3
// (C) 2017-2019, Christophe de Dinechin <christophe@dinechin.org>
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

#include "recorder_ring.h"
#include "recorder.h"
#include "alt_drand48.h"

#include <math.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <inttypes.h>


int failed = 0;

RECORDER(MAIN,          64, "Global operations in 'main()'");
RECORDER(Pauses,        256, "Pauses during blocking operations");
RECORDER(Special,        64, "Special operations to the recorder");
RECORDER(SpeedTest,      32, "Recorder speed test");
RECORDER(SpeedInfo,      32, "Recorder information during speed test");
RECORDER(FastSpeedTest,  32, "Fast recorder speed test");



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
        record(MAIN, __VA_ARGS__);                                      \
        char buf[256];                                                  \
        snprintf(buf, sizeof(buf), __VA_ARGS__);                        \
        puts(buf);                                                      \
    } while(0)

#define VERBOSE(...) if (debug) INFO(__VA_ARGS__)

#define FAIL(...)                                                       \
    do                                                                  \
    {                                                                   \
        record(MAIN, "FAILURE");                                        \
        record(MAIN, __VA_ARGS__);                                      \
        char buf[256];                                                  \
        snprintf(buf, sizeof(buf), __VA_ARGS__);                        \
        puts(buf);                                                      \
        failed = 1;                                                     \
    } while(0)

unsigned thread_id = 0;
unsigned threads_to_stop = 0;

void dawdle(unsigned minimumMs, unsigned deltaMs)
{
    struct timespec tm;
    tm.tv_sec  = 0;
    tm.tv_nsec = (minimumMs + drand48() * deltaMs) * 1000000;
    record(Pauses, "Pausing #%u %ld.%03dus",
           recorder_ring_fetch_add(pauses_count, 1),
           tm.tv_nsec / 1000, tm.tv_nsec % 1000);
    nanosleep(&tm, NULL);
}

// RECORDER(SpeedTest,      32, "Recorder speed test");
RECORDER_TWEAK_DEFINE(sleep_time, 0, "Sleep time between records");
RECORDER_TWEAK_DEFINE(sleep_time_delta, 0, "Variations in sleep time between records");

void *recorder_thread(void *thread)
{
    uintptr_t i = 0;
    unsigned tid = (unsigned) (uintptr_t) thread;
    uintptr_t last_time = recorder_tick();
    while (!threads_to_stop)
    {
        i++;
        uintptr_t current_time = recorder_tick();
        record(SpeedTest, "[thread %u] Recording %u, mod %u after %ld", tid, i, i % 500,
            current_time - last_time);
        last_time = current_time;
        if (RECORDER_TWEAK(sleep_time))
        {
            struct timespec tm;
            uint64_t wait_time = (uint64_t)
                (RECORDER_TWEAK(sleep_time) + drand48()*RECORDER_TWEAK(sleep_time_delta));
            tm.tv_sec  = 0;
            tm.tv_nsec = wait_time * 1000;
            nanosleep(&tm, NULL);
        }
    }
    recorder_ring_fetch_add(recorder_count, i);
    recorder_ring_fetch_add(threads_to_stop, -1);
    return NULL;
}

void *recorder_fast_thread(void *thread)
{
    uintptr_t i = 0;
    unsigned tid = (unsigned) (uintptr_t) thread;
    while (!threads_to_stop)
    {
        i++;
        RECORD_FAST(FastSpeedTest, "[thread %u] Fast recording %u mod %u",
                    tid, i, i % 700);
    }
    recorder_ring_fetch_add(recorder_count, i);
    recorder_ring_fetch_add(threads_to_stop, -1);
    return NULL;
}

typedef struct example { int x; int y; int z; } example_t;

size_t show_struct(intptr_t trace,
                   const char *format, char *buffer, size_t len, uintptr_t data)
{
    example_t *e = (example_t *) data;
    size_t s = trace
        ? snprintf(buffer, len, "example(%d, %d, %d)", e->x, e->y, e->z)
        : snprintf(buffer, len, "example(%p)", e);
    return s;
}

void flight_recorder_test(int argc, char **argv)
{
    int i, j;
    unsigned count = argc >= 2 ? atoi(argv[1]) : 16;
    unsigned howLong = argc >= 3 ? atoi(argv[2]) : 1;

    INFO("Testing recorder version %u.%02u.%02u",
         RECORDER_VERSION_MAJOR(RECORDER_CURRENT_VERSION),
         RECORDER_VERSION_MINOR(RECORDER_CURRENT_VERSION),
         RECORDER_VERSION_PATCH(RECORDER_CURRENT_VERSION));
    if (RECORDER_CURRENT_VERSION != RECORDER_VERSION(1,0,8))
        FAIL("Testing an unexpected version of the recorder, "
             "update RECORDER_CURRENT_VERSION");

    for (i = 0; i < 2; i++)
    {
        recorder_count = 0;

        INFO("Launching %u %s recorder thread%s",
             count, i ? "fast" : "normal", count>1?"s":"");
        record(MAIN, "Starting %s speed test for %us with %u threads",
               i ? "fast" : "normal", howLong, count);

        pthread_t tid;
        for (j = 0; j < count; j++)
            pthread_create(&tid, NULL,
                           i ? recorder_fast_thread : recorder_thread,
                           (void *) (intptr_t) j);

        INFO("%s recorder testing in progress, please wait about %ds",
             i ? "Fast" : "Normal", howLong);
        unsigned sleepTime = howLong;
        do { sleepTime =  sleep(sleepTime); } while (sleepTime);
        INFO("%s recorder testing completed, stopping threads",
             i ? "Fast" : "Normal");
        threads_to_stop = count;

        while(threads_to_stop)
        {
            record(Pauses, "Waiting for recorder threads to stop, %u remaining",
                   threads_to_stop);
            dawdle(1, 0);
        }
        INFO("%s test: all threads have stopped, %"PRIuPTR" iterations",
             i ? "Fast" : "Normal", recorder_count);

        recorder_count += (recorder_count == 0);
        printf("Recorder test analysis (%s):\n"
               "  Iterations            = %8"PRIuPTR"\n"
               "  Iterations / ms       = %8"PRIuPTR"\n"
               "  Duration per record   = %8uns\n"
               "  Number of threads     = %8u\n",
               i ? "Fast version" : "Normal version",
               recorder_count,
               recorder_count / (howLong * 1000),
               (unsigned) (howLong * 1000000000ULL / recorder_count),
               count);

        INFO("Recorder test complete (%s), %u threads.",
             i ? "Fast version" : "Normal version", count);
        INFO("  Iterations      = %10"PRIuPTR, recorder_count);
        INFO("  Iterations / ms = %10"PRIuPTR, recorder_count / (howLong * 1000));
        INFO("  Record cost     = %10uns",
             (unsigned) (howLong * 1000000000ULL / recorder_count));
    }

    record(Special, "Sizeof int=%u intptr_t=%u float=%u double=%u",
           sizeof(int), sizeof(intptr_t), sizeof(float), sizeof(double));

    record(Special, "Float      3.1415 = %f", 3.1415f);
    record(Special, "Float    X 3.1415 = %x", 3.1415f);
    record(Special, "Double     3.1415 = %f", 3.1415);
    record(Special, "Double   X 3.1415 = %x", 3.1415);
    record(Special, "Large %d %u %ld %lu %f %s",
           1,2u,3l,4lu,5.0,"six");
    record(Special, "Larger %d %u %ld %lu %f %s %p %g",
           1,2u,3l,4lu,5.0,"six",(void *) 7,8.0);
    record(Special, "Largest %d %u %ld %lu %f %s %p %g %x %lu %u",
           1,2u,3l,4lu,5.0,"six",(void *) 7,8.0, 9, 10, 11);
    record(Special, "Format '%8s' '%-8s' '%8.2f' '%-8.2f'",
           "abc", "def", 1.2345, 2.3456);
    record(Special, "Format '%*s' '%*.*f'",
           8, "abc", 8, 2, 1.2345);

    recorder_configure_type('E', show_struct);
    example_t x1 = { 1, 2, 3 };
    example_t x2 = { 42, -42, 42*42 };

    record(Special, "Struct dump %E then %E", &x1, &x2);

    recorder_dump_for("Special");
    recorder_dump();

    if (getenv("KEEP_RUNNING"))
    {
        uintptr_t k = 0;
        uintptr_t last_k = 0;
        uintptr_t last_tick = recorder_tick();
        while(true)
        {
            k++;
            record(FastSpeedTest, "[thread %u] Recording %u, mod %u",
                   (unsigned) (200 * sin(0.03 * k) * sin (0.000718231*k) + 200),
                   (unsigned) (k * drand48()),
                   k % 627);
            // dawdle(1, 3);
            uintptr_t tick = recorder_tick();
            if (tick - last_tick > RECORDER_HZ/1000)
            {
                record(SpeedInfo, "Iterations per millisecond: %lu (%f ns)",
                       k - last_k, 1e6 / (k - last_k));
                last_k = k;
                last_tick = tick;
            }
        }
    }
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
