#ifndef RECORDER_RING_H
#define RECORDER_RING_H
// *****************************************************************************
// recorder_ring.h                                              Recorder project
// *****************************************************************************
//
// File description:
//
//    A ring (circular buffer) with multiple writers, generally single reader.
//
//    This implementation is supposed to work in multi-CPU configurations
//    without using locks, only using atomic primitives
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
/*
 * Implementation of a thread-safe, non-blocking ring buffer.
 * This ring buffer can work in multi-processor scenarios or multi-threaded
 * systems, and can be used to store records of any type. It can also safely
 * manage sequences of consecutive records. A typical use case is strings
 * in a console print buffer, which have variable length.
 *
 * How this works:
 *   Each buffer is represented by
 *   - an array A of N items (for performance, N should be a power of 2),
 *   - a reader index R,
 *   - a writer index W
 *   - a commit index C
 *   - an overflow counter O
 *
 *   The core invariants of the structure are (ignoring integer overflow)
 *   1.  R <= C <= W
 *   2.  overflowed = W - R >= N
 *
 *   Reading entries from the buffer consist in the following steps:
 *   1. If the buffer overflowed, "catch up":
 *   1a. Set R to W-N+1
 *   2b. Increase O to record the overflow
 *   2. There is readable data iff R < C. If so:
 *   2a. Read A[R % N] (possibly blocking)
 *   2b. Atomically increase R
 *
 *   Writing E entries in the buffer consists in the following steps:
 *   1. Atomically increase W, fetching the old W (fetch_and_add)
 *   2. Copy the entries in A[oldW % N] (possibly blocking)
 *   3. Wait until C == oldW, and Atomically set C to W (possibly blocking)
 *
 * Step 2 can block if the reader has not caught up yet.
 * Step 3 can block if another writer has still not updated C
 *
 * Important notes:
 *   The code as written is safe for use with C++ objects. Do not attempt
 *   to replace data copies with memcpy(), as it would most likely not
 *   improve speed (because of the "modulo" to perform) and would break C++.
 *
 *   In theory, if you use the buffer long enough, all indexes will ultimately
 *   wrap around. This is why all comparisons are done with something like
 *       (int) (writer - reader) >= size
 *   instead of a solution that would fail when writer wraps around
 *       writer >= reader + size
 *   It is therefore assumed that you will never create a buffer of a
 *   size larger than 2^31 on a 32-bit machine. Probably OK.
 *
 */

// ****************************************************************************
//
//   If .h included from a C++ program, select the C++ version
//
// ****************************************************************************

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>


#ifdef __cplusplus
extern "C" {
#endif  // __cplusplus


// ============================================================================
//
//    Atomic built-ins
//
// ============================================================================

#ifdef __GNUC__

// GCC-compatible compiler: use built-in atomic operations
#define recorder_ring_fetch_add(Value, Offset)                  \
    __atomic_fetch_add(&Value, Offset, __ATOMIC_ACQUIRE)

#define recorder_ring_add_fetch(Value, Offset)                  \
    __atomic_add_fetch(&Value, Offset, __ATOMIC_ACQUIRE)

#define recorder_ring_compare_exchange(Value, Expected, New)            \
    __atomic_compare_exchange_n(&Value, &Expected, New,                 \
                                0, __ATOMIC_RELEASE, __ATOMIC_RELAXED)

#define RECORDER_RING_MAYBE_UNUSED   __attribute__((unused))

#else // ! __GNUC__

#warning "Compiler not supported yet"
#define recorder_ring_fetch_add(Value, Offset)   (Value += Offset)
#define recorder_ring_add_fetch(Value, Offset)   ((Value += Offset), Value)
#define recorder_ring_compare_exchange(Val, Exp, New) ((Val = New), true)

#define RECORDER_RING_MAYBE_UNUSED

#endif



// ============================================================================
//
//    C-style ring buffer type
//
// ============================================================================

typedef uintptr_t ringidx_t;

typedef struct recorder_ring
// ----------------------------------------------------------------------------
//   Header for ring buffers
// ----------------------------------------------------------------------------
{
    size_t      size;           // Number of elements in data array
    size_t      item_size;      // Size of the elements
    ringidx_t   reader;         // Reader index
    ringidx_t   writer;         // Writer index
    ringidx_t   commit;         // Last commited write
    ringidx_t   overflow;       // Overflowed writes
} recorder_ring_t, *recorder_ring_p;

/* Deal with blocking situations on given ring
   - Return true if situation is handled and operation can proceed
   - Return false will abort read or write operation.
   - May be NULL to implement default non-blocking mode
   The functions take from/to as argument bys design, because the
   corresponding values may have been changed in the ring
   by the time the block-handling function get to read them. */
typedef bool (*recorder_ring_block_fn)(recorder_ring_p,
                                       ringidx_t from, ringidx_t to);

extern recorder_ring_p  recorder_ring_init(recorder_ring_p ring,
                                           size_t size, size_t item_size);
extern recorder_ring_p  recorder_ring_new(size_t size, size_t item_size);
extern void             recorder_ring_delete(recorder_ring_p ring);
extern size_t           recorder_ring_readable(recorder_ring_p ring, ringidx_t *reader);
extern size_t           recorder_ring_writable(recorder_ring_p ring);
extern size_t           recorder_ring_read(recorder_ring_p ring,
                                           void *data, size_t count,
                                           ringidx_t *reader,
                                           recorder_ring_block_fn read_block,
                                           recorder_ring_block_fn read_overflow);
extern void *           recorder_ring_peek(recorder_ring_p ring);
extern ringidx_t        recorder_ring_write(recorder_ring_p ring,
                                            const void *data, size_t count,
                                            recorder_ring_block_fn write_block,
                                            recorder_ring_block_fn commit_block,
                                            ringidx_t *writer);



#define RECORDER_RING_TYPE_DECLARE(Ring, Type)                          \
/* ----------------------------------------------------------------*/   \
/*  Declare a ring buffer type with Size elements of given Type    */   \
/* ----------------------------------------------------------------*/   \
                                                                        \
    typedef struct Ring                                                 \
    {                                                                   \
        recorder_ring_t ring;                                           \
        Type            data[0];                                        \
    } Ring;                                                             \
                                                                        \
    typedef bool (*Ring##_block_fn)(Ring *,ringidx_t, ringidx_t);       \
                                                                        \
    static inline RECORDER_RING_MAYBE_UNUSED                            \
    Ring *Ring##_new(size_t size)                                       \
    {                                                                   \
        return (Ring *) recorder_ring_new(size, sizeof(Type));          \
    }                                                                   \
                                                                        \
    static inline RECORDER_RING_MAYBE_UNUSED                            \
    void Ring##_delete(Ring *rb)                                        \
    {                                                                   \
        recorder_ring_delete(&rb->ring);                                \
    }                                                                   \
                                                                        \
    static inline RECORDER_RING_MAYBE_UNUSED                            \
    Type * Ring##_peek(Ring *rb)                                        \
    {                                                                   \
        return (Type *) recorder_ring_peek(&rb->ring);                  \
    }                                                                   \
                                                                        \
    static inline RECORDER_RING_MAYBE_UNUSED                            \
    size_t Ring##_read(Ring *rb,                                        \
                       Type *ptr,                                       \
                       size_t count,                                    \
                       ringidx_t *reader,                               \
                       Ring##_block_fn block,                           \
                       Ring##_block_fn overflow)                        \
    {                                                                   \
        return recorder_ring_read(&rb->ring, ptr, count, reader,        \
                                  (recorder_ring_block_fn) block,       \
                                  (recorder_ring_block_fn) overflow);   \
    }                                                                   \
                                                                        \
    static inline RECORDER_RING_MAYBE_UNUSED                            \
    size_t Ring##_write(Ring *rb,                                       \
                        Type *ptr,                                      \
                        size_t count,                                   \
                        Ring##_block_fn wrblk,                          \
                        Ring##_block_fn cmblk,                          \
                        ringidx_t *writer)                              \
    {                                                                   \
        return recorder_ring_write(&rb->ring, ptr, count,               \
                                   (recorder_ring_block_fn) wrblk,      \
                                   (recorder_ring_block_fn) cmblk,      \
                                   writer);                             \
    }                                                                   \
                                                                        \
    static inline RECORDER_RING_MAYBE_UNUSED                            \
    ringidx_t Ring##_readable(Ring *rb)                                 \
    {                                                                   \
        return recorder_ring_readable(&rb->ring);                       \
    }                                                                   \
                                                                        \
    static inline RECORDER_RING_MAYBE_UNUSED                            \
    ringidx_t Ring##_writable(Ring *rb)                                 \
    {                                                                   \
        return recorder_ring_writable(&rb->ring);                       \
    }



// ============================================================================
//
//    Static ring buffer allocation
//
// ============================================================================

#define RECORDER_RING_DECLARE(Name, Type, Size)                         \
/* ----------------------------------------------------------------*/   \
/*  Declare a named ring buffer with helper functions to access it */   \
/* ----------------------------------------------------------------*/   \
/*   This is what you should use in headers */                          \
                                                                        \
    extern struct Name##_ring                                           \
    {                                                                   \
        recorder_ring_t ring;                                           \
        Type            data[Size];                                     \
    } Name;                                                             \
                                                                        \
    static inline RECORDER_RING_MAYBE_UNUSED                            \
    size_t Name##_readable()                                            \
    {                                                                   \
        return recorder_ring_readable(&Name.ring, NULL);                \
    }                                                                   \
                                                                        \
    static inline RECORDER_RING_MAYBE_UNUSED                            \
    size_t Name##_writable()                                            \
    {                                                                   \
        return recorder_ring_writable(&Name.ring);                      \
    }                                                                   \
                                                                        \
    static inline RECORDER_RING_MAYBE_UNUSED                            \
    Type * Name##_peek()                                                \
    {                                                                   \
        return (Type *) recorder_ring_peek(&Name.ring);                 \
    }                                                                   \
                                                                        \
    static inline RECORDER_RING_MAYBE_UNUSED                            \
    size_t Name##_read(Type *ptr, ringidx_t count)                      \
    {                                                                   \
        return recorder_ring_read(&Name.ring, ptr, count,               \
                                  NULL, NULL, NULL);                    \
    }                                                                   \
                                                                        \
    static inline RECORDER_RING_MAYBE_UNUSED                            \
    size_t Name##_write(Type *ptr, ringidx_t count)                     \
    {                                                                   \
        return recorder_ring_write(&Name.ring, ptr, count,              \
                                   NULL, NULL, NULL);                   \
    }                                                                   \
                                                                        \
    static inline RECORDER_RING_MAYBE_UNUSED                            \
    size_t Name##_block_read(Type *ptr,                                 \
                             size_t count,                              \
                             ringidx_t *reader,                         \
                             recorder_ring_block_fn block,              \
                             recorder_ring_block_fn overflow)           \
    {                                                                   \
        return recorder_ring_read(&Name.ring, ptr, count,               \
                                  reader, block, overflow);             \
    }                                                                   \
                                                                        \
    static inline RECORDER_RING_MAYBE_UNUSED                            \
    size_t Name##_block_write(const Type *ptr,                          \
                              size_t  count,                            \
                              recorder_ring_block_fn write_block,       \
                              recorder_ring_block_fn commit_block,      \
                              ringidx_t *pos)                           \
    {                                                                   \
        return recorder_ring_write(&Name.ring, ptr, count,              \
                          write_block, commit_block, pos);              \
    }


#define RECORDER_RING_DEFINE(Name, Type, Size)                          \
/* ----------------------------------------------------------------*/   \
/*  Define a named ring buffer                                     */   \
/* ----------------------------------------------------------------*/   \
                                                                        \
    struct Name##_ring Name =                                           \
    {                                                                   \
        { Size, sizeof(Type), 0, 0, 0, 0 }                              \
    };


#ifdef __cplusplus
}
#endif // __cplusplus

#endif // RECORDER_RING_H
