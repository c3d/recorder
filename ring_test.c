// *****************************************************************************
// ring_test.c                                                  Recorder project
// *****************************************************************************
//
// File description:
//
//     Test ring buffer with multiple concurrent writers, one reader,
//     and variable-size writes. This corresponds to the use of the ring
//     buffer as a circular print buffer, where we want messages to be
//     in order, and not mixed one with another.
//
//     The test writes messages with different length. However, the length
//     can be determined from the first letter. It then checks that messages
//     are not garbled by other threads.
//
// *****************************************************************************
// This software is licensed under the GNU Lesser General Public License v2+
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

#include <stdio.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <signal.h>


int failed = 0;



// ============================================================================
//
//    Recorders that we will use here
//
// ============================================================================

RECORDER(MAIN,       64, "Global operations in 'main()'");
RECORDER(Pauses,    256, "Pauses during blocking operations");
RECORDER(Reads,     256, "Reading from the ring");
RECORDER(Writes,    256, "Writing into the ring");
RECORDER(Special,    64, "Special operations to the recorder");
RECORDER(SpeedTest,  32, "Recorder speed test");
RECORDER(Timing,     64, "Timing information");



// ============================================================================
//
//    Ringbuffer test
//
// ============================================================================

#include "ring_test.h"


// Generate N writer threads. Each writer attempts to read if there is
// enough room for it. It writes one of a set of known strings.
// The test is to check that we only get exactly the known strings
// and not some jumble of strings from different sizes.
// Notice that there is something special about the initials

int debug = 0;

const char *testStrings[] =
{
    "All your bases are belong to us",
    "Be yourself, everyone else is already taken",
    "Can't you read?",
    "Des cubes?",
    "Extraordinary claims require extraordinary evidence",
    "Fool!",
    "Gastoooon!",
    "History has a tendency to repeat itself",
    "I see no reason to believe you exist",
    "Jealousy is all the fun you think they had",
    "Kangaroos everywhere",
    "Le pelican est avec le kangourou le seul marsupial volant a avoir "
    "une poche ventrale sous le bec",
    "Make it so",
    "Ni pour ni contre, bien au contraire",
    "Oh, des poules!",
    "Petit, mais costaud",
    "Q",
    "Rarely have mere mortals developed code of such low quality",
    "Sympa, ce sofa si soft",
    "Total verrueckt",
    "Under capitalism, man exploits man, "
    "under communism it's just the opposite",
    "Va, cours, vole et nous venge",
    "Whaaaaaat?!?",
    "Xenodocheionology is apparently a pathologic love of hotels",
    "Y a-t-il un developpeur pour sauver ce code",
    "Zero seems like an odd value here"
};


#define INFO(...)                                                       \
    do                                                                  \
    {                                                                   \
        char buf[256];                                                  \
        int len = snprintf(buf, sizeof(buf),                            \
                           "R%5zu W%5zu C%5zu L%5zu: ",                 \
                           buffer.ring.reader, buffer.ring.writer,      \
                           buffer.ring.commit,                          \
                           buffer_writable());                          \
        len += snprintf(buf + len, sizeof(buf) - len, __VA_ARGS__);     \
        puts(buf);                                                      \
    } while(0)

#define VERBOSE(...) if (debug) INFO(__VA_ARGS__)

#define FAIL(...)                                                       \
    do                                                                  \
    {                                                                   \
        char buf[256];                                                  \
        int len = snprintf(buf, sizeof(buf),                            \
                           "R%5zu W%5zu C%5zu L%5zu: "                  \
                           "FAILED: ",                                  \
                           buffer.ring.reader,                          \
                           buffer.ring.writer,                          \
                           buffer.ring.commit,                          \
                           buffer_writable());                          \
        len += snprintf(buf + len, sizeof(buf) - len, __VA_ARGS__);     \
        puts(buf);                                                      \
        failed = 1;                                                     \
        recorder_dump();                                                \
    } while(0)


unsigned count_write_blocked = 0;
unsigned count_write_spins = 0;
unsigned count_commit_blocked = 0;
unsigned count_commit_spins = 0;
unsigned count_read_blocked = 0;
unsigned count_read_spins = 0;
unsigned count_writes = 0;
unsigned count_written = 0;
unsigned count_reads = 0;
unsigned count_read = 0;
unsigned count_read_overflow = 0;

unsigned thread_id = 0;
unsigned threads_to_stop = 0;

void dawdle(unsigned minimumMs)
{
    struct timespec tm;
    tm.tv_sec = 0;
    tm.tv_nsec =  + minimumMs * (1000 * 1000 + drand48() * 2000000);
    record(Pauses, "Pausing %ld.%03dus", tm.tv_nsec / 1000, tm.tv_nsec % 1000);
    nanosleep(&tm, NULL);
}


bool writer_block(recorder_ring_t *rb, ringidx_t oldW, ringidx_t lastW)
{
    record(Writes, "Blocking write old=%u last=%u", oldW, lastW);

    recorder_ring_fetch_add(count_write_blocked, 1);

    /* Wait until reader is beyond the last item we are going to write */
    record(Writes,"Blocking write %zu-%zu", oldW, lastW);
    while ((intptr_t) (lastW - rb->reader) >= (intptr_t) (rb->size - 1))
    {
        recorder_ring_fetch_add(count_write_spins, 1);
        VERBOSE("Blocking write ahead %d %zu-%zu",
                (int) (lastW - rb->reader - rb->size),
                oldW, lastW);
        record(Pauses,"Blocking write ahead %d %zu-%zu",
               (int) (lastW - rb->reader - rb->size),
               oldW, lastW);
        dawdle(5);
    }
    VERBOSE("Unblocked write ahead %d %zu-%zu",
            (int) (lastW - rb->reader - rb->size),
            oldW, lastW);
    record(Writes, "Unblocking old=%u last=%u",
           oldW, lastW);

    /* It's now safe to keep writing */
    return true;
}


bool commit_block(recorder_ring_t *rb, ringidx_t commit, ringidx_t oldW)
{
    record(Writes, "Blocking commit current=%u need=%u", commit, oldW);

    recorder_ring_fetch_add(count_commit_blocked, 1);

    /* Wait until reader is beyond the last item we are going to write */
    record(Reads,"Blocking commit %zu-%zu", commit, oldW);
    while (rb->commit != oldW)
    {
        recorder_ring_fetch_add(count_commit_spins, 1);
        VERBOSE("Blocking commit, at %zu, need %zu", rb->commit, oldW);
        record(Pauses,"Blocking commit %zu-%zu-%zu", commit, rb->commit, oldW);
        dawdle(1);
    }
    VERBOSE("Unblocked commit was %zu, needed %zu, now %zu",
            commit, oldW, rb->commit);
    record(Writes, "Unblocking commit, was %zu, needed %zu, now %zu",
           commit, oldW, rb->commit);

    /* It's now safe to keep writing */
    return true;
}


void *writer_thread(void *data)
{
    const unsigned numberOfTests = sizeof(testStrings) / sizeof(*testStrings);
    unsigned tid = recorder_ring_fetch_add(thread_id, 1);
    record(MAIN, "Entering writer thread %u", tid);

    while (!threads_to_stop)
    {
        int index = drand48() * numberOfTests;
        const char *str = testStrings[index];
        int len = strlen(str);
        VERBOSE("Write #%02d '%s' size %u", tid, str, len);
        recorder_ring_fetch_add(count_writes, 1);
        record(Writes, "Writing '%s'", str);
        ringidx_t wr = 0;
        size_t size = buffer_block_write(str, len,
                                         writer_block, commit_block, &wr);
        record(Writes, "Wrote '%s' size %zu at index %u", str, size, wr);
        recorder_ring_fetch_add(count_written, 1);

        VERBOSE("Wrote #%02d '%s' at offset %lu-%lu size %u",
                tid, str, wr, wr + len - 1, len);
    }
    unsigned toStop = recorder_ring_fetch_add(threads_to_stop, -1U);
    record(MAIN, "Exiting thread %u, stopping %u more", tid, toStop);
    return NULL;
}


bool reader_block(recorder_ring_t *rb, ringidx_t curR, ringidx_t lastR)
{
    record(Reads, "Blocked curR=%zu lastR=%zu", curR, lastR);
    recorder_ring_fetch_add(count_read_blocked, 1);
    while ((intptr_t) (rb->commit - lastR) < 0)
    {
        recorder_ring_fetch_add(count_read_spins, 1);
        VERBOSE("Blocking read commit=%zu lastR=%zu", rb->commit, lastR);
        record(Pauses, "Blocking read commit=%zu last=%zu", rb->commit, lastR);
        dawdle(1);
    }
    record(Reads, "Unblocking commit=%zu lastR=%zu", rb->commit, lastR);
    return true; // We waited until commit caught up, so we can keep reading
}


unsigned overflow_handler_called = 0;

bool reader_overflow(recorder_ring_t *rb, ringidx_t curR, ringidx_t minR)
{
    size_t  skip = minR - curR;
    record(Reads, "Overflow currentR=%u minR=%u skip=%u", curR, minR, skip);

    recorder_ring_fetch_add(count_read_overflow, 1);
    VERBOSE("Reader overflow %zu reader %zu -> %zu, skip %zu",
            rb->overflow, rb->reader, minR, skip);
    recorder_ring_fetch_add(overflow_handler_called, 1);


    record(Reads, "End overflow minReader=%u skip=%u", minR, skip);

    // Writer actually blocks until reader catches up, so we can keep reading
    return 1;
}


void *reader_thread(void *data)
{
    char buf[256];
    unsigned tid = recorder_ring_fetch_add(thread_id, 1);
    ringidx_t rd = 0;

    record(MAIN, "Entering reader thread tid %u", tid);

    while (threads_to_stop != 1)
    {
        // Read initial byte, the capital at beginning of message
        unsigned overflow = buffer.ring.overflow;
        unsigned readable = buffer_readable();

        if (overflow)
        {
            VERBOSE("Reader overflow #%02d is %u",
                    tid, overflow);
            buffer.ring.overflow = 0;
        }

        char *ptr = buf;
        unsigned size = 0;
        if (readable)
        {
            // Reported that we can't read. Check if it's overflow
            size = buffer_block_read(buf, 1, &rd,reader_block,reader_overflow);
            if (size == 0)
            {
                FAIL("Blocking read did not get data");
            }
        }
        record(Reads, "Index %u Readable: %u, Size: %u, Overflow %u",
               rd, readable, size, overflow);
        if (size == 0)
            continue;

        if (size > 1)
        {
            FAIL("Returned initial size %u is too large", size);
            exit(-1);
        }

        char initial = ptr[0];
        if (initial < 'A' || initial > 'Z')
        {
            FAIL("First byte is '%c' (0x%x)", initial, initial);
            exit(-2);
        }
        unsigned index = initial - 'A';
        const char *test = testStrings[index];
        unsigned testLen = strlen(test);
        record(Reads, "Initial %c (%d), expecting '%s' length %u",
               initial, initial, test, testLen);

        // Read the rest of the buffer based on input length
        VERBOSE("Reading #%02d '%c' %u bytes", tid, initial, testLen);
        recorder_ring_fetch_add(count_reads, 1);
        size += buffer_block_read(buf + size, testLen - size, &rd,
                                  reader_block, reader_overflow);
        recorder_ring_fetch_add(count_read, 1);
        record(Reads, "Index %u: Read %u bytes out of %u at index %u",
               rd, size, testLen);

        if (testLen != size)
        {
            FAIL("Length for '%c' is %u, should be %u",
                 initial, size, testLen);
            exit(-3);
        }

        if (memcmp(ptr, test, testLen) != 0)
        {
            ptr[testLen] = 0;
            FAIL("Data miscompare, had %u bytes '%s' != '%s'",
                 size, ptr, test);
            exit(-4);
        }
        buffer.ring.reader = rd;

        ptr[size] = 0;
        VERBOSE("Read #%02d '%s' %u bytes", tid, ptr, testLen);
    }

    unsigned toStop = recorder_ring_fetch_add(threads_to_stop, -1);
    record(MAIN, "Exiting reader thread tid %u, %u more to stop", tid, toStop);

    return NULL;
}


int ringbuffer_test(int argc, char **argv)
{
    pthread_t tid;

    record(MAIN, "Entering ringbuffer test argc=%d", argc);
    INFO("Launching reader thread");
    pthread_create(&tid, NULL, reader_thread, NULL);

    int i, count = argc >= 2 ? atoi(argv[1]) : 16;
    if (count < 0)
    {
        debug = 1;
        count = -count;
    }

    INFO("Launching %d writer thread%s", count, count>1?"s":"");
    for (i = 0; i < count; i++)
        pthread_create(&tid, NULL, writer_thread, NULL);


    unsigned howLong = argc >= 3 ? atoi(argv[2]) : 1;
    INFO("Testing in progress, please wait about %ds", howLong);
    unsigned sleepTime = howLong;
    do { sleepTime =  sleep(sleepTime); } while (sleepTime);
    INFO("Testing completed successfully:");
    record(MAIN, "Stopping threads");
    threads_to_stop = count + 1;

    while(threads_to_stop)
    {
        record(Pauses, "Waiting for ring test threads to stop, %u remaining",
               threads_to_stop);
        dawdle(1);
    }

    printf("Test analysis:\n"
           "  Initiated Writes  = %8u (Requests to write in buffer)\n"
           "  Completed Writes  = %8u (Writes that were finished, %3d.%02d%%)\n"
           "  Blocked   Writes  = %8u (Writes that blocked, %3d.%02d%%)\n"
           "  Spinning  Writes  = %8u (Number of spins waiting to write)\n"
           "  Blocked   Commits = %8u (Commits that blocked, %3d.%02d%%)\n"
           "  Spinning  Commits = %8u (Number of spins waiting to commit)\n"
           "  Initiated Reads   = %8u (Requests to read from buffer)\n"
           "  Completed Reads   = %8u (Number of reads that finished)\n"
           "  Blocked   Reads   = %8u (Reads that blocked, %3d.%02d%%)\n"
           "  Spinning  Reads   = %8u (Number of spins waiting to read)\n"
           "  Overflow  Reads   = %8u (Number of read overflows)\n",
           count_writes, count_written,
           100 * count_written / count_writes,
           (10000 * count_written / count_writes) % 100,
           count_write_blocked,
           100 * count_write_blocked / count_writes,
           (10000 * count_write_blocked / count_writes) % 100,
           count_write_spins,
           count_commit_blocked,
           100 * count_commit_blocked / count_writes,
           (10000 * count_commit_blocked / count_writes) % 100,
           count_commit_spins,
           count_reads, count_read, count_read_blocked,
           100 * count_read_blocked / count_reads,
           (10000 * count_read_blocked / count_reads) % 100,
           count_read_spins,
           count_read_overflow);


    return 0;
}


RECORDER_RING_DECLARE(speed_test, recorder_entry, 512);
RECORDER_RING_DEFINE(speed_test, recorder_entry, 512);


static inline ringidx_t special_ring_write(recorder_ring_p ring,
                                           recorder_entry *source)
// ----------------------------------------------------------------------------
//   Optimized version
// ----------------------------------------------------------------------------
{
    const size_t     size   = 512;
    recorder_entry * data   = (recorder_entry *) (ring + 1);
    ringidx_t        writer = recorder_ring_fetch_add(ring->writer, 1);
    data[writer % size] = *source;
    return writer;
}


void compare_performance_of_common_operations(unsigned loops)
{
    unsigned       i;
    uintptr_t      start, duration;
    double         cost;
    recorder_entry entry = { 0 };
    void *         ptrs[256] = { NULL };

#define TEST(Info, Code)                                        \
    record(Timing, "Test: " Info);                              \
    start = recorder_tick();                                    \
    for (i = 0; i < loops; i++) { Code; }                       \
    duration = recorder_tick() - start;                         \
    cost = 1e9 * duration / RECORDER_HZ / loops;                \
    record(Timing, Info " cost is %.6f ns", cost);

    TEST("regular ring_write", speed_test_write(&entry, 1));
    TEST("special ring_write", special_ring_write(&speed_test.ring, &entry));
    TEST("fetch-add",
         entry.order = recorder_ring_fetch_add(recorder_order, 1);
         special_ring_write(&speed_test.ring, &entry));
    TEST("recorder_tick()",
         entry.timestamp = recorder_tick();
         special_ring_write(&speed_test.ring, &entry));
    TEST("tick + fetch-add",
         entry.order = recorder_ring_fetch_add(recorder_order, 1);
         entry.timestamp = recorder_tick();
         special_ring_write(&speed_test.ring, &entry));
    TEST("tick + fetch-add + copy",
         entry.order = recorder_ring_fetch_add(recorder_order, 1);
         entry.timestamp = recorder_tick();
         entry.args[0] = i;
         entry.args[1] = 3-i;
         entry.args[2] = i * 1081;
         entry.args[3] = i ^ 0xFE;
         special_ring_write(&speed_test.ring, &entry));
#ifdef CLOCK_MONOTONIC
    TEST("clock_gettime + copy",
         struct timespec ts;
         clock_gettime(CLOCK_MONOTONIC, &ts);
         entry.order = recorder_ring_fetch_add(recorder_order, 1);
         entry.timestamp = ts.tv_sec * 1000000L + ts.tv_nsec / 1000;
         entry.args[0] = i;
         entry.args[1] = 3-i;
         entry.args[2] = i * 1081;
         entry.args[3] = i ^ 0xFE;
         special_ring_write(&speed_test.ring, &entry));
#endif

    TEST("RECORD", record(SpeedTest, "Speed test %u", i));
    TEST("RECORD_FAST", RECORD_FAST(SpeedTest, "Speed test %u", i));

    TEST("malloc(512)",
         free(ptrs[i % 256]);
         ptrs[i % 256] = malloc(32));
    TEST("malloc(jigsaw)",
         free(ptrs[i % 256]);
         ptrs[i % 256] = malloc(512 + i % 7777 * 13));
    TEST("memcpy",
         memcpy(ptrs[i % 256], ptrs[(i+1) % 256], 512));
    TEST("gettimeofday", recorder_tick());
    TEST("snprintf", snprintf(ptrs[i % 256], 512, "Speed test %u", i));

    FILE *f = fopen("test.out", "w");
    TEST("fprintf", fprintf(f, "Speed test %u", i));
    fclose(f);

    f = fopen("test.out", "w");
    TEST("fprintf + fflush", fprintf(f, "Speed test %u", i); fflush(f));
    fclose(f);

    recorder_dump_for("Timing");
}



// ============================================================================
//
//    Main entry point
//
// ============================================================================

int main(int argc, char **argv)
{
    recorder_trace_set(".*_(warning|error)");
    recorder_dump_on_common_signals(0, 0);
    ringbuffer_test(argc, argv);
    if (failed)
        recorder_dump();        // Try to figure out what failed
    compare_performance_of_common_operations(100000);
    return failed;
}
