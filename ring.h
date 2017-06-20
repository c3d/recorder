#ifndef RING_H
#define RING_H
// ****************************************************************************
//  ring.h                                                    Recorder project
// ****************************************************************************
//
//   File Description:
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
// ****************************************************************************
// This document is released under the GNU General Public License, with the
// following clarification and exception.
//
// Linking this library statically or dynamically with other modules is making
// a combined work based on this library. Thus, the terms and conditions of the
// GNU General Public License cover the whole combination.
//
// As a special exception, the copyright holders of this library give you
// permission to link this library with independent modules to produce an
// executable, regardless of the license terms of these independent modules,
// and to copy and distribute the resulting executable under terms of your
// choice, provided that you also meet, for each linked independent module,
// the terms and conditions of the license of that module. An independent
// module is a module which is not derived from or based on this library.
// If you modify this library, you may extend this exception to your version
// of the library, but you are not obliged to do so. If you do not wish to
// do so, delete this exception statement from your version.
//
// See http://www.gnu.org/copyleft/gpl.html and Matthew 25:22 for details
//  (C) 1992-2010 Christophe de Dinechin <christophe@taodyne.com>
//  (C) 2010 Taodyne SAS
// ****************************************************************************
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

#ifdef __cplusplus
#include "ring.hpp"
#endif // __cplusplus

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>


// ****************************************************************************
//
//    Pure C implementation
//
// ****************************************************************************


// ============================================================================
//
//    Atomic built-ins
//
// ============================================================================

#ifdef __GNUC__

// GCC-compatible compiler: use built-in atomic operations
#define ring_fetch_add(Value, Offset)                        \
    __atomic_fetch_add(&Value, Offset, __ATOMIC_ACQUIRE)

#define ring_add_fetch(Value, Offset)                        \
    __atomic_add_fetch(&Value, Offset, __ATOMIC_ACQUIRE)

#define ring_compare_exchange(Value, Expected, New)                      \
    __atomic_compare_exchange_n(&Value, &Expected, New,                 \
                                0, __ATOMIC_RELEASE, __ATOMIC_RELAXED)

#else // ! __GNUC__

#warning "Compiler not supported yet"

#endif



// ============================================================================
//
//    C-style ring buffer type
//
// ============================================================================

typedef uintptr_t ringidx_t;

typedef struct ring
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
} ring_t, *ring_p;

/* Deal with blocking situations on given ring
   - Return true if situation is handled and operation can proceed
   - Return false will abort read or write operation.
   - May be NULL to implement default non-blocking mode
   The functions take from/to as argument bys design, because the
   corresponding values may have been changed in the ring
   by the time the block-handling function get to read them. */
typedef bool (*ring_block_fn)(ring_p, ringidx_t from, ringidx_t to);

extern ring_p    ring_new(size_t size, size_t item_size);
extern void      ring_delete(ring_p ring);
extern size_t    ring_readable(ring_p ring);
extern size_t    ring_writable(ring_p ring);
extern size_t    ring_read(ring_p ring, void *data, size_t count,
                           ring_block_fn read_block,
                           ring_block_fn read_overflow,
                           ringidx_t *reader);
extern ringidx_t ring_peek(ring_p ring, void *data);
extern ringidx_t ring_write(ring_p ring, const void *data, size_t count,
                            ring_block_fn write_block,
                            ring_block_fn commit_block,
                            ringidx_t *writer);



#define RING_TYPE_DECLARE(Ring, Type)                                   \
/* ----------------------------------------------------------------*/   \
/*  Declare a ring buffer type with Size elements of given Type    */   \
/* ----------------------------------------------------------------*/   \
                                                                        \
    typedef struct Ring                                                 \
    {                                                                   \
        ring_t ring;                                                    \
        Type   data[0];                                                 \
    } Ring;                                                             \
                                                                        \
    typedef bool (*Ring##_block_fn)(Ring *,ringidx_t, ringidx_t);       \
                                                                        \
    static inline                                                       \
    Ring *Ring##_new(size_t size)                                       \
    {                                                                   \
        return (Ring *) ring_new(size, sizeof(Type));                   \
    }                                                                   \
                                                                        \
    static inline                                                       \
    void Ring##_delete(Ring *rb)                                        \
    {                                                                   \
        ring_delete(&rb->ring);                                         \
    }                                                                   \
                                                                        \
    static inline                                                       \
    ringidx_t Ring##_peek(Ring *rb, Type *ptr)                          \
    {                                                                   \
        return ring_peek(&rb->ring, ptr);                               \
    }                                                                   \
                                                                        \
    static inline                                                       \
    size_t Ring##_read(Ring *rb,                                        \
                       Type *ptr,                                       \
                       size_t count,                                    \
                       Ring##_block_fn read_block,                      \
                       Ring##_block_fn read_overflow,                   \
                       ringidx_t *reader)                               \
    {                                                                   \
        return ring_read(&rb->ring, ptr, count,                         \
                         (ring_block_fn) read_block,                    \
                         (ring_block_fn) read_overflow,                 \
                         reader);                                       \
    }                                                                   \
                                                                        \
    static inline                                                       \
    size_t Ring##_write(Ring *rb,                                       \
                        Type *ptr,                                      \
                        size_t count,                                   \
                        Ring##_block_fn write_block,                    \
                        Ring##_block_fn commit_block,                   \
                        ringidx_t *writer)                              \
    {                                                                   \
        return ring_write(&rb->ring, ptr, count,                        \
                          (ring_block_fn) write_block,                  \
                          (ring_block_fn) commit_block,                 \
                          writer);                                      \
    }                                                                   \
                                                                        \
    static inline                                                       \
    ringidx_t Ring##_readable(Ring *rb)                                 \
    {                                                                   \
        return ring_readable(&rb->ring);                                \
    }                                                                   \
                                                                        \
    static inline                                                       \
    ringidx_t Ring##_writable(Ring *rb)                                 \
    {                                                                   \
        return ring_writable(&rb->ring);                                \
    }                                                                   \



// ============================================================================
//
//    Static ring buffer allocation
//
// ============================================================================

#define RING_DECLARE(Name, Type, Size)                                  \
/* ----------------------------------------------------------------*/   \
/*  Declare a named ring buffer with helper functions to access it */   \
/* ----------------------------------------------------------------*/   \
/*   This is what you should use in headers */                          \
                                                                        \
    extern struct Name##_ring                                           \
    {                                                                   \
        ring_t ring;                                                    \
        Type   data[Size];                                              \
    } Name;                                                             \
                                                                        \
    static inline                                                       \
    size_t Name##_readable()                                            \
    {                                                                   \
        return ring_readable(&Name.ring);                               \
    }                                                                   \
                                                                        \
    static inline                                                       \
    size_t Name##_writable()                                            \
    {                                                                   \
        return ring_writable(&Name.ring);                               \
    }                                                                   \
                                                                        \
    static inline                                                       \
    ringidx_t Name##_peek(Type *ptr)                                    \
    {                                                                   \
        return ring_peek(&Name.ring, ptr);                              \
    }                                                                   \
                                                                        \
    static inline                                                       \
    size_t Name##_read(Type *ptr, ringidx_t count)                      \
    {                                                                   \
        return ring_read(&Name.ring, ptr, count, NULL, NULL, NULL);     \
    }                                                                   \
                                                                        \
    static inline                                                       \
    size_t Name##_write(Type *ptr, ringidx_t count)                     \
    {                                                                   \
        return ring_write(&Name.ring, ptr, count, NULL, NULL, NULL);    \
    }                                                                   \
                                                                        \
    static inline                                                       \
    size_t Name##_block_read(Type *ptr,                                 \
                             size_t count,                              \
                             ring_block_fn block,                       \
                             ring_block_fn overflow,                    \
                             ringidx_t *pos)                            \
    {                                                                   \
        return ring_read(&Name.ring, ptr, count, block, overflow, pos); \
    }                                                                   \
                                                                        \
    static inline                                                       \
    size_t Name##_block_write(const Type *ptr,                          \
                              size_t  count,                            \
                              ring_block_fn write_block,                \
                              ring_block_fn commit_block,               \
                              ringidx_t *pos)                           \
    {                                                                   \
        return ring_write(&Name.ring, ptr, count,                       \
                          write_block, commit_block, pos);              \
    }


#define RING_DEFINE(Name, Type, Size)                                   \
/* ----------------------------------------------------------------*/   \
/*  Define a named ring buffer                                     */   \
/* ----------------------------------------------------------------*/   \
                                                                        \
    struct Name##_ring Name =                                           \
    {                                                                   \
        { Size, sizeof(Type), 0 }                                       \
    };


#endif // RING_H
