// ****************************************************************************
//  recorder_test.c                                           Recorder project 
// ****************************************************************************
// 
//   File Description:
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



// ============================================================================
//
//    Ringbuffer test
//
// ============================================================================

#define RING(Type, Name, Size)                  \
    RING_DECLARE(Type, Name, Size)              \
    RING_DEFINE (Type, Name, Size)
RING(char, buffer, 1024);


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
        int len = snprintf(buf, sizeof(buf), "R%5u W%5u C%5u L%5u: ",   \
                           buffer.reader, buffer.writer, buffer.commit, \
                           buffer_writable());                          \
        len += snprintf(buf + len, sizeof(buf) - len, __VA_ARGS__);     \
        puts(buf);                                                      \
    } while(0)

#define VERBOSE(...) if (debug) INFO(__VA_ARGS__)

#define FAIL(...)                                                       \
    do                                                                  \
    {                                                                   \
        char buf[256];                                                  \
        int len = snprintf(buf, sizeof(buf), "R%5u W%5u C%5u L%5u: "    \
                           "FAILED: ",                                  \
                           buffer.reader, buffer.writer, buffer.commit, \
                           buffer_writable());                          \
        len += snprintf(buf + len, sizeof(buf) - len, __VA_ARGS__);     \
        puts(buf);                                                      \
        failed = 1;                                                     \
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
    tm.tv_nsec =  + minimumMs * (1000 * 1000 + lrand48() % 2000000);
    RECORD(Pauses, "Pausing %ld.%03dus", tm.tv_nsec / 1000, tm.tv_nsec % 1000);
    nanosleep(&tm, NULL);
}


int writer_block(buffer_ring *rb, unsigned oldW, unsigned lastW)
{
    RECORD(Writes, "Blocking write old=%u last=%u", oldW, lastW);

    ring_fetch_add(count_write_blocked, 1);

    /* Wait until reader is beyond the last item we are going to write */
    while (lastW - rb->reader >= buffer_ring_size - 1)
    {
        ring_fetch_add(count_write_spins, 1);
        VERBOSE("Blocking write ahead %d %u-%u",
                (int) (lastW - rb->reader - buffer_ring_size),
                oldW, lastW);
        dawdle(5);
    }
    VERBOSE("Unblocked write ahead %d %u-%u",
            (int) (lastW - rb->reader - buffer_ring_size),
            oldW, lastW);
    RECORD(Writes, "Unblocking old=%u last=%u",
           oldW, lastW);

    /* It's now safe to keep writing */
    return 1;
}


int commit_block(buffer_ring *rb, unsigned commit, unsigned oldW)
{
    RECORD(Writes, "Blocking commit current=%u need=%u", commit, oldW);

    ring_fetch_add(count_commit_blocked, 1);

    /* Wait until reader is beyond the last item we are going to write */
    while (rb->commit != oldW)
    {
        ring_fetch_add(count_commit_spins, 1);
        VERBOSE("Blocking commit, at %u, need %u", rb->commit, oldW);
        dawdle(1);
    }
    VERBOSE("Unblocked commit was %u, needed %u, now %u",
            commit, oldW, rb->commit);
    RECORD(Writes, "Unblocking commit, was %u, needed %u, now %u",
            commit, oldW, rb->commit);

    /* It's now safe to keep writing */
    return 1;
}


void *writer_thread(void *data)
{
    const unsigned numberOfTests = sizeof(testStrings) / sizeof(*testStrings);
    unsigned tid = ring_fetch_add(thread_id, 1);
    RECORD(Main, "Entering writer thread %u", tid);

    while (!threads_to_stop)
    {
        int index = lrand48() % numberOfTests;
        const char *str = testStrings[index];
        int len = strlen(str);
        VERBOSE("Write #%02d '%s' size %u", tid, str, len);
        ring_fetch_add(count_writes, 1);
        RECORD(Writes, "Writing '%s'", str);
        unsigned wr = buffer_block_write(str, len, writer_block, commit_block);
        RECORD(Writes, "Wrote '%s' at index %u", str, wr);
        ring_fetch_add(count_written, 1);

        VERBOSE("Wrote #%02d '%s' at offset %u-%u size %u",
                tid, str, wr, wr + len - 1, len);
    }
    unsigned toStop = ring_fetch_add(threads_to_stop, -1U);
    RECORD(Main, "Exiting thread %u, stopping %u more", tid, toStop);
    return NULL;
}


int reader_block(buffer_ring *rb, unsigned curR, unsigned lastR)
{
    RECORD(Reads, "Blocked curR=%u lastR=%u", curR, lastR);
    ring_fetch_add(count_read_blocked, 1);
    while ((int) (rb->commit - lastR) < 0)
    {
        ring_fetch_add(count_read_spins, 1);
        VERBOSE("Blocking read commit=%u lastR=%u", rb->commit, lastR);
        dawdle(1);
    }
    RECORD(Reads, "Unblocking commit=%u lastR=%u", rb->commit, lastR);
    return 1; // We waited until commit caught up, so we can keep reading
}


unsigned overflow_handler_called = 0;

int reader_overflow(buffer_ring *rb, unsigned curR, unsigned minR)
{
    unsigned skip = minR - curR;
    RECORD(Reads, "Overflow currentR=%u minR=%u skip=%u", curR, minR, skip);

    ring_fetch_add(count_read_overflow, 1);
    VERBOSE("Reader overflow %u reader %u -> %u, skip %u",
         rb->overflow, rb->reader, minR, skip);
    ring_fetch_add(overflow_handler_called, 1);

    RECORD(Reads, "End overflow minReader=%u skip=%u", minR, skip);

    // Writer actually blocks until reader catches up, so we can keep reading
    return 1;
}


void *reader_thread(void *data)
{
    char buf[256];
    unsigned tid = ring_fetch_add(thread_id, 1);

    RECORD(Main, "Entering reader thread tid %u", tid);

    while (threads_to_stop != 1)
    {
        // Read initial byte, the capital at beginning of message
        unsigned reader = buffer.reader;
        unsigned writer = buffer.writer;
        unsigned commit = buffer.commit;
        unsigned overflow = buffer.overflow;
        unsigned writable = buffer_writable();
        unsigned readable = buffer_readable();

        if (overflow)
        {
            VERBOSE("Reader overflow #%02d is %u",
                   tid, overflow);
            buffer.overflow = 0;
        }

        char *ptr = buf;
        unsigned size = 0;
        if (readable)
        {
            // Reported that we can't read. Check if it's overflow
            size = buffer_block_read(buf, 1, reader_block, reader_overflow);
            if (size == 0)
            {
                FAIL("Blocking read did not get data");
            }
        }
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

        // Read the rest of the buffer based on input length
        VERBOSE("Reading #%02d '%c' %u bytes", tid, initial, testLen);
        ring_fetch_add(count_reads, 1);
        size += buffer_block_read(buf + size, testLen - size,
                                  reader_block, reader_overflow);
        ring_fetch_add(count_read, 1);

        reader = buffer.reader;
        writer = buffer.writer;
        commit = buffer.commit;
        writable = buffer_writable();
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

        ptr[size] = 0;
        VERBOSE("Read #%02d '%s' %u bytes", tid, ptr, testLen);
    }

    unsigned toStop = ring_fetch_add(threads_to_stop, -1);
    RECORD(Main, "Exiting reader thread tid %u, %u more to stop", tid, toStop);

    return NULL;
}


int ringbuffer_test(int argc, char **argv)
{
    pthread_t tid;

    RECORD(Main, "Entering ringbuffer test argc=%d", argc);
    INFO("Launching reader thread");
    pthread_create(&tid, NULL, reader_thread, NULL);

    int count = argc >= 2 ? atoi(argv[1]) : 16;
    if (count < 0)
    {
        debug = 1;
        count = -count;
    }

    INFO("Launching %d writer thread%s", count, count>1?"s":"");
    for (unsigned i = 0; i < count; i++)
        pthread_create(&tid, NULL, writer_thread, NULL);


    unsigned howLong = argc >= 3 ? atoi(argv[2]) : 10;
    INFO("Testing in progress, please wait about %ds", howLong);
    unsigned sleepTime = howLong;
    do { sleepTime =  sleep(sleepTime); } while (sleepTime);
    INFO("Testing completed successfully:");
    RECORD(Main, "Stopping threads");
    threads_to_stop = count + 1;

    while(threads_to_stop)
    {
        RECORD(Pauses, "Waiting for ring test threads to stop, %u remaining",
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


// ============================================================================
//
//    Main entry point
//
// ============================================================================

int main(int argc, char **argv)
{
#ifdef SIGINFO
    recorder_dump_on_signal(SIGINFO);
#endif
    recorder_dump_on_signal(SIGUSR1);
    ringbuffer_test(argc, argv);
    if (failed)
        recorder_dump();        // Try to figure out what failed
    return failed;
}
