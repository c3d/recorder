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

unsigned recorder_count = 0;
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
    while (!threads_to_stop)
        RECORD(SpeedTest, "Recording %u", ring_fetch_add(recorder_count, 1));
    ring_fetch_add(threads_to_stop, -1);
    return NULL;
}

void flight_recorder_test(int argc, char **argv)
{
    int i, count = argc >= 2 ? atoi(argv[1]) : 16;
    if (count < 0)
        count = -count;
    unsigned howLong = argc >= 3 ? atoi(argv[2]) : 10;

    INFO("Launching %d recorder thread%s", count, count>1?"s":"");
    RECORD(MAIN, "Starting speed test for %us with %u threads", howLong, count);
    pthread_t tid;
    for (i = 0; i < count; i++)
        pthread_create(&tid, NULL, recorder_thread, NULL);

    INFO("Recorder testing in progress, please wait about %ds", howLong);
    unsigned sleepTime = howLong;
    do { sleepTime =  sleep(sleepTime); } while (sleepTime);
    INFO("Recorder testing completed, %u iterations", recorder_count);
    threads_to_stop = count;

    while(threads_to_stop)
    {
        RECORD(Pauses, "Waiting for recorder threads to stop, %u remaining",
               threads_to_stop);
        dawdle(1);
    }
    INFO("All threads have stopped.");

    printf("Recorder test analysis:\n"
           "  Iterations            = %8u\n"
           "  Iterations / ms       = %8u\n"
           "  Duration per record   = %8uns\n"
           "  Number of threads     = %8u\n",
           recorder_count,
           recorder_count / (howLong * 1000),
           (unsigned) (howLong * 1000000000ULL / recorder_count),
           count);

    INFO("Recorder test complete, %u threads.", count);
    INFO("  Iterations           = %10u", recorder_count);
    INFO("  Iterations / ms      = %10u", recorder_count / (howLong * 1000));
    INFO("  Duration per record  = %10uns",
         (unsigned) (howLong * 1000000000ULL / recorder_count));

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
