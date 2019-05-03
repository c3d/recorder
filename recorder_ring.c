// *****************************************************************************
// recorder_ring.c                                              Recorder project
// *****************************************************************************
//
// File description:
//
//     Implement common ring functionality
//
//
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
#include <stdlib.h>
#include <string.h>

typedef intptr_t ringdiff_t;


recorder_ring_p recorder_ring_init(recorder_ring_p ring,
                                   size_t size, size_t item_size)
// ----------------------------------------------------------------------------
//   Initialize a ring
// ----------------------------------------------------------------------------
{
    ring->size = size;
    ring->item_size = item_size;
    ring->reader = 0;
    ring->writer = 0;
    ring->commit = 0;
    ring->overflow = 0;
    return ring;
}


recorder_ring_p recorder_ring_new(size_t size, size_t item_size)
// ----------------------------------------------------------------------------
//   Create a new ring with the given name
// ----------------------------------------------------------------------------
{
    recorder_ring_p ring = malloc(sizeof(recorder_ring_t) + size * item_size);
    recorder_ring_init(ring, size, item_size);
    return ring;
}


void recorder_ring_delete(recorder_ring_p ring)
// ----------------------------------------------------------------------------
//   Delete the given ring from the list
// ----------------------------------------------------------------------------
{
    free(ring);
}


extern size_t recorder_ring_readable(recorder_ring_p ring, ringidx_t *reader)
// ----------------------------------------------------------------------------
//   Return number of elements readable in the ring
// ----------------------------------------------------------------------------
{
    if (!reader)
        reader = &ring->reader;
    size_t readable = ring->commit - *reader;
    if (readable > ring->size)
        readable = ring->size;
    return readable;
}


extern size_t recorder_ring_writable(recorder_ring_p ring)
// ----------------------------------------------------------------------------
//   Return number of elements that can be written in the ring
// ----------------------------------------------------------------------------
{
    const ringidx_t size     = ring->size;
    ringidx_t       reader   = ring->reader;
    ringidx_t       writer   = ring->writer;
    ringidx_t       written  = writer - reader;
    ringidx_t       writable = size - written - 1;

    // Check if we overflowed
    if (written >= size - 1)
        writable = 0;

    return writable;
}


void *recorder_ring_peek(recorder_ring_p ring)
// ----------------------------------------------------------------------------
//   Peek the next entry that would be read in the ring and advance by 1
// ----------------------------------------------------------------------------
{
    char         *data      = (char *) (ring + 1);
    const size_t  size      = ring->size;
    const size_t  item_size = ring->item_size;
    ringidx_t     reader    = ring->reader;
    ringidx_t     commit    = ring->commit;
    size_t        written   = commit - reader;
    if (written >= size)
    {
        ringidx_t minR = commit - size + 1;
        ringidx_t skip = minR - reader;
        recorder_ring_add_fetch(ring->overflow, skip);
        reader = recorder_ring_add_fetch(ring->reader, skip);
        written = commit - reader;
    }
    return written ? data + reader % size * item_size : NULL;
}


ringidx_t recorder_ring_read(recorder_ring_p         ring,
                             void                   *destination,
                             size_t                  count,
                             ringidx_t              *reader_ptr,
                             recorder_ring_block_fn  read_block,
                             recorder_ring_block_fn  read_overflow)
// ----------------------------------------------------------------------------
//   Ring up to 'count' elements, return number of elements read
// ----------------------------------------------------------------------------
//   If enough data is available in the ring buffer, the elements read are
//   guaranteed to be contiguous.
{
    const size_t  size      = ring->size;
    const size_t  item_size = ring->item_size;
    char         *ptr       = destination;
    char         *data      = (char *) (ring + 1);
    ringidx_t     reader, writer, commit, available, to_copy;
    ringidx_t     first_reader, next_reader;
    ringidx_t     idx, to_end;
    size_t        this_round, byte_count;

    if (!reader_ptr)
        reader_ptr = &ring->reader;

    // First commit to reading a given amount of contiguous data
    do
    {
        reader = *reader_ptr;
        commit = ring->commit;
        writer = ring->writer;
        available = commit - reader;
        to_copy = count;

        // Check if we want to copy more than available
        if (to_copy > available)
            if (!read_block || !read_block(ring, reader, reader + to_copy))
                to_copy = available;

        // Check if write may have overwritten beyond our read point
        if (writer - reader >= size)
        {
            // If so, catch up
            ringidx_t first_valid = writer - size + 1;
            if (!read_overflow || !read_overflow(ring, reader, first_valid))
            {
                ringidx_t skip = first_valid - reader;
                recorder_ring_add_fetch(ring->overflow, skip);
                recorder_ring_add_fetch(*reader_ptr, skip);
                reader = first_valid;
            }
        }

        // Then copy data in contiguous memcpy chunks (normally at most two)
        ptr = destination;
        first_reader = reader;
        next_reader = first_reader + to_copy;
        while (to_copy)
        {
            // Compute how much we can copy in one memcpy
            idx        = reader % size;
            to_end     = size - idx;
            this_round = to_copy < to_end ? to_copy : to_end;
            byte_count = this_round * item_size;

            // Copy data from buffer into destination
            memcpy(ptr, data + idx * item_size, byte_count);
            ptr += byte_count;
            to_copy -= this_round;
            reader += this_round;
        }
    } while (!recorder_ring_compare_exchange(*reader_ptr,
                                             first_reader, next_reader));

    // Return number of items effectively read
    return count - to_copy;
}


ringidx_t recorder_ring_write(recorder_ring_p         ring,
                              const void             *source,
                              size_t                  count,
                              recorder_ring_block_fn  write_block,
                              recorder_ring_block_fn  commit_block,
                              ringidx_t              *writer_ptr)
// ----------------------------------------------------------------------------
//   Write 'count' elements from 'ptr' into 'rb', return entry idx
// ----------------------------------------------------------------------------
{
    const size_t size      = ring->size;
    const size_t item_size = ring->item_size;
    const char * ptr       = source;
    char *       data      = (char *) (ring + 1);
    size_t       to_copy   = count;
    ringidx_t    reader, writer, idx, available, to_end, first_writer;
    size_t       this_round, byte_count;


    // First commit to writing a given amount of contiguous data
    do
    {
        reader = ring->reader;
        writer = ring->writer;
        available = size + reader - writer;
        to_copy = count;

        // Check if we want to copy more than can be written
        if (to_copy > available)
            if (write_block && !write_block(ring, writer, writer + to_copy))
                to_copy = available;

    } while (!recorder_ring_compare_exchange(ring->writer,
                                             writer, writer + to_copy));

    // Record first writer, to see if we will be the one committing
    first_writer = writer;
    if (writer_ptr)
        *writer_ptr = writer;

    // Then copy data in contiguous memcpy chunks (normally at most two)
    while (to_copy)
    {
        // Compute how much we can copy in one memcpy
        idx        = writer % size;
        to_end     = size - idx;
        this_round = to_copy < to_end ? to_copy : to_end;
        byte_count = this_round * item_size;

        // Copy data from buffer into destination
        memcpy(data + idx * item_size, ptr, byte_count);
        ptr += byte_count;
        to_copy -= this_round;
        writer += this_round;
    }

    // Commit buffer change, but only if commit is first_writer.
    // Otherwise, some other write is still copying its data, we must spin.
    ringidx_t expected = first_writer;
    while (!recorder_ring_compare_exchange(ring->commit, expected, writer))
    {
        if (!commit_block || !commit_block(ring, ring->commit, first_writer))
        {
            // Skip forward
            recorder_ring_fetch_add(ring->commit, writer - first_writer);
            break;
        }
        expected = first_writer;
    }

    // Return number of items effectively written
    return count - to_copy;
}
