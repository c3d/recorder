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



// ****************************************************************************
// 
//    Pure C implementation
// 
// ****************************************************************************

#include <stddef.h>



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

#define RING_TYPE_DECLARE(Ring, Type, Size)                             \
/* ----------------------------------------------------------------*/   \
/*  Declare a ring buffer type with Size elements of given Type    */   \
/* ----------------------------------------------------------------*/   \
                                                                        \
typedef struct Ring##_s                                                 \
/* ----------------------------------------------------------------*/   \
/*  The ring buffer type itself                                    */   \
/* ----------------------------------------------------------------*/   \
{                                                                       \
    const char *    name;          /* Name for debugging purpose */     \
    unsigned        reader;        /* Reader index */                   \
    unsigned        writer;        /* Writer index */                   \
    unsigned        commit;        /* Last commited write */            \
    unsigned        overflow;      /* Overflowed writes */              \
    Type            data[Size];    /* Circular buffer itself */         \
} Ring;                                                                 \
                                                                        \
                                                                        \
typedef Type Ring##_data;                                               \
enum { Ring##_size = Size };                                            \
                                                                        \
/* Deal with blocking situations on given ring                          \
   - Return true if situation is handled and operation can proceed      \
   - Return false will abort read or write operation.                   \
   - May be NULL to implement default non-blocking mode                 \
   The functions take from/to as argument bys design, because the       \
   corresponding values may have been changed in the ring               \
   by the time the block-handling function get to read them. */         \
typedef int (*Ring##_block_fn)(Ring *, unsigned from, unsigned to);     \
                                                                        \
                                                                        \
extern unsigned Ring##_read(Ring *rb,                                   \
                            Ring##_data *ptr,                           \
                            unsigned count,                             \
                            Ring##_block_fn read_block,                 \
                            Ring##_block_fn read_overflow);             \
                                                                        \
                                                                        \
extern unsigned Ring##_peek(Ring *rb,                                   \
                            Ring##_data *ptr);                          \
                                                                        \
                                                                        \
extern unsigned Ring##_write(Ring *rb,                                  \
                             const Ring##_data *ptr,                    \
                             unsigned count,                            \
                             Ring##_block_fn write_block,               \
                             Ring##_block_fn commit_block);             \
                                                                        \
                                                                        \
static inline unsigned Ring##_readable(Ring *rb)                        \
/* ----------------------------------------------------------------*/   \
/*  The number of readable elements                                */   \
/* ----------------------------------------------------------------*/   \
{                                                                       \
    return rb->commit - rb->reader;                                     \
}                                                                       \
                                                                        \
                                                                        \
static inline unsigned Ring##_writable(Ring *rb)                        \
/* ----------------------------------------------------------------*/   \
/*  The number of writable elements                                */   \
/* ----------------------------------------------------------------*/   \
{                                                                       \
    const unsigned size = Ring##_size;                                  \
    unsigned reader = rb->reader;                                       \
    unsigned writer = rb->writer;                                       \
    unsigned written = writer - reader;                                 \
    unsigned writable = size - written - 1;                             \
    if (written >= size - 1)                                            \
        writable = 0;           /* Check if we overflowed */            \
    return writable;                                                    \
}


#define RING_TYPE_DEFINE(Ring, Type, Size)                              \
/* ----------------------------------------------------------------*/   \
/*  Define a ring buffer type with Size elements of given Type     */   \
/* ----------------------------------------------------------------*/   \
                                                                        \
static inline unsigned Ring##_readable(Ring *rb);                       \
static inline unsigned Ring##_writable(Ring *rb);                       \
                                                                        \
                                                                        \
unsigned Ring##_peek(Ring *rb,                                          \
                     Ring##_data *ptr)                                  \
/* ----------------------------------------------------------------*/   \
/*  Peek the first element in the ring buffer, returns read index  */   \
/* ----------------------------------------------------------------*/   \
{                                                                       \
    const unsigned size = Ring##_size;                                  \
    unsigned reader = rb->reader;                                       \
    *ptr = rb->data[reader % size];                                     \
    return reader;                                                      \
}                                                                       \
                                                                        \
                                                                        \
unsigned Ring##_read(Ring *rb,                                          \
                     Ring##_data *ptr,                                  \
                     unsigned count,                                    \
                     Ring##_block_fn read_block,                        \
                     Ring##_block_fn read_overflow)                     \
/* ----------------------------------------------------------------*/   \
/*  Read up to 'count' elements from 'rb' into 'ptr'               */   \
/* ----------------------------------------------------------------*/   \
{                                                                       \
    const unsigned size = Ring##_size;                                  \
    Ring##_data *begin = ptr;                                           \
    Ring##_data *end = ptr + count;                                     \
    while (ptr < end)                                                   \
    {                                                                   \
        unsigned reader = rb->reader;                                   \
        unsigned writer = rb->writer;                                   \
        if (writer - reader >= size)                                    \
        {                                                               \
            /* Writer went beyond what we are about to read             \
               Check if overflow handler can cope (e.g. catch up),      \
               Otherwise record overflow and abort read here */         \
            unsigned minR = writer - size + 1;                          \
            if (!read_overflow || !read_overflow(rb, reader, minR))     \
            {                                                           \
                unsigned skip = minR - reader;                          \
                ring_add_fetch(rb->overflow, skip);                     \
                ring_add_fetch(rb->reader, skip);                       \
                break;                                                  \
            }                                                           \
        }                                                               \
                                                                        \
        /* Check if we want to read beyond what was committed.          \
           If so, we can either block until there is enough data,       \
           or abort the read here and return amount of data read. */    \
        unsigned commit = rb->commit;                                   \
        if ((int) (reader - commit) >= 0)                               \
            if (!read_block || !read_block(rb,reader,reader+(end-ptr))) \
                break;  /* We read everything there is to read */       \
                                                                        \
        *ptr = rb->data[reader % size];                                 \
        if (reader == ring_fetch_add(rb->reader, 1))                    \
            ptr++;                                                      \
    }                                                                   \
    return ptr - begin;                                                 \
}                                                                       \
                                                                        \
                                                                        \
unsigned Ring##_write(Ring *rb,                                         \
                      const Ring##_data *ptr,                           \
                      unsigned count,                                   \
                      Ring##_block_fn write_block,                      \
                      Ring##_block_fn commit_block)                     \
/* ----------------------------------------------------------------*/   \
/*  Write 'count' elements from 'ptr' into 'rb', return entry idx  */   \
/* ----------------------------------------------------------------*/   \
{                                                                       \
    const unsigned size = Ring##_size;                                  \
    unsigned oldW = ring_fetch_add(rb->writer, count);                  \
    unsigned lastW = oldW + count;                                      \
    unsigned reader = rb->reader;                                       \
    unsigned lastSafeW = reader + size - 1;                             \
                                                                        \
    /* Optimize writes that can't possibly overwrite reader */          \
    if (!write_block || (int) (lastSafeW - lastW) > 0)                  \
        lastSafeW = lastW;                                              \
                                                                        \
    /* Write everything that is not at risk to overwrite reader */      \
    unsigned w = oldW;                                                  \
    while ((int) (w - lastSafeW) < 0)                                   \
        rb->data[w++ % size] = *ptr++;                                  \
                                                                        \
    /* Slower write for things that may require us to block */          \
    while ((int) (w - lastW) < 0)                                       \
    {                                                                   \
        /* If we are overwriting reader data, block or abort write */   \
        if (w - rb->reader >= size-1 && !write_block(rb, oldW, lastW))  \
            break;                                                      \
        rb->data[w++ % size] = *ptr++;                                  \
    }                                                                   \
                                                                        \
    /* We are done. Commit buffer change, but only if commit is oldW    \
       Otherwise, some other guy before us is still copying data,       \
       so we need to wait. This is the only active spin. */             \
    unsigned expected = oldW;                                           \
    while (!ring_compare_exchange(rb->commit, expected, lastW))         \
    {                                                                   \
        unsigned current = rb->commit;                                  \
        expected = oldW;                                                \
        if (!commit_block || !commit_block(rb, current, expected))      \
        {                                                               \
            ring_fetch_add(rb->commit, count);                          \
            break;                                                      \
        }                                                               \
    }                                                                   \
    return oldW;                                                        \
}


// ============================================================================
//
//    Ring buffer static allocation
//
// ============================================================================

#define RING_STORAGE_DECLARE(Ring, Name, Size)                          \
/* ----------------------------------------------------------------*/   \
/*  Declare a named ring buffer with helper functions to access it */   \
/* ----------------------------------------------------------------*/   \
/*   This is what you should use in headers */                          \
                                                                        \
extern Ring Name;                                                       \
                                                                        \
                                                                        \
static inline unsigned Name##_readable()                                \
/* ----------------------------------------------------------------*/   \
/*  The number of elements that can be read from buffer            */   \
/* ----------------------------------------------------------------*/   \
{                                                                       \
    return Ring##_readable(&Name);                                      \
}                                                                       \
                                                                        \
                                                                        \
static inline unsigned Name##_writable()                                \
/* ----------------------------------------------------------------*/   \
/*  The number of elements that can be written without overwrite   */   \
/* ----------------------------------------------------------------*/   \
{                                                                       \
    return Ring##_writable(&Name);                                      \
}                                                                       \
                                                                        \
                                                                        \
static inline unsigned Name##_peek(Ring##_data *ptr)                    \
/* ----------------------------------------------------------------*/   \
/*  Peek the first element without moving read index, return rdidx */   \
/* ----------------------------------------------------------------*/   \
{                                                                       \
    return Ring##_peek(&Name, ptr);                                     \
}                                                                       \
                                                                        \
                                                                        \
static inline unsigned Name##_read(Ring##_data *ptr,                    \
                                   unsigned count)                      \
/* ----------------------------------------------------------------*/   \
/*  Read up to 'count' elements from buffer, non-blocking          */   \
/* ----------------------------------------------------------------*/   \
{                                                                       \
    return Ring##_read(&Name, ptr, count, NULL, NULL);                  \
}                                                                       \
                                                                        \
                                                                        \
static inline unsigned Name##_write(const Ring##_data *ptr,             \
                                    unsigned count)                     \
/* ----------------------------------------------------------------*/   \
/*  Write 'count' elements in buffer, non-blocking                 */   \
/* ----------------------------------------------------------------*/   \
/*  Returns the write index for first element written */                \
{                                                                       \
    return Ring##_write(&Name, ptr, count, NULL, NULL);                 \
}                                                                       \
                                                                        \
                                                                        \
static inline unsigned Name##_block_read(Ring##_data *ptr,              \
                                         unsigned count,                \
                                         Ring##_block_fn block,         \
                                         Ring##_block_fn overflow)      \
/* ----------------------------------------------------------------*/   \
/*  Read up to count elements with actions on block / overflow     */   \
/* ----------------------------------------------------------------*/   \
{                                                                       \
    return Ring##_read(&Name, ptr, count, block, overflow);             \
}                                                                       \
                                                                        \
                                                                        \
static inline unsigned Name##_block_write(const Ring##_data *ptr,       \
                                          unsigned count,               \
                                          Ring##_block_fn write_block,  \
                                          Ring##_block_fn commit_block) \
/* ----------------------------------------------------------------*/   \
/*  Write elements with actions if write or commit must block      */   \
/* ----------------------------------------------------------------*/   \
{                                                                       \
    return Ring##_write(&Name, ptr, count, write_block, commit_block);  \
}


#define RING_STORAGE_DEFINE(Ring, Name, Size)                           \
/* ----------------------------------------------------------------*/   \
/*  Define a named ring buffer with helper functions to access it  */   \
/* ----------------------------------------------------------------*/   \
                                                                        \
Ring Name = { #Name, 0 };


// ============================================================================
//
//   Ring declarations and definition
//
// ============================================================================

#define RING_DECLARE(Type, Name, Size)                                  \
/* ----------------------------------------------------------------*/   \
/*  Shortcut that declares a ring buffer type and one instance     */   \
/* ----------------------------------------------------------------*/   \
    RING_TYPE_DECLARE(Name##_ring, Type, Size)                          \
    RING_STORAGE_DECLARE(Name##_ring, Name, Size)


#define RING_DEFINE(Type, Name, Size)                                   \
/* ----------------------------------------------------------------*/   \
/*  Shortcut that declares a ring buffer type and one instance     */   \
/* ----------------------------------------------------------------*/   \
    RING_TYPE_DEFINE(Name##_ring, Type, Size)                           \
    RING_STORAGE_DEFINE(Name##_ring, Name, Size)


#endif // RING_H
