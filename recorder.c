// *****************************************************************************
// recorder.c                                                   Recorder project
// *****************************************************************************
//
// File description:
//
//     Implementation of a non-blocking flight recorder
//
//
//
//
//
//
//
//
// *****************************************************************************
// This software is licensed under the GNU General Public License v3
// (C) 2017-2019, Christophe de Dinechin <christophe@dinechin.org>
// (C) 2018-2019, Frediano Ziglio <fziglio@redhat.com>
// *****************************************************************************
// This file is part of Recorder
//
// Recorder is free software: you can r redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// Recorder is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with Recorder, in a file named COPYING.
// If not, see <https://www.gnu.org/licenses/>.
// *****************************************************************************

#include "recorder.h"
#include "config.h"

#include <ctype.h>
#include <fcntl.h>
#include <pthread.h>
#if HAVE_REGEX_H
#include <regex.h>
#endif
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <inttypes.h>
#include <sys/time.h>
#if HAVE_SYS_MMAN_H
#include <sys/mman.h>
#endif // HAVE_SYS_MMAN_H
#include <sys/stat.h>



// ============================================================================
//
//   Available recorders exposed by recorder library
//
// ============================================================================

RECORDER(recorder,              32, "Recorder operations and configuration");
RECORDER(recorder_warning,       8, "Recorder warnings");
RECORDER(recorder_error,         8, "Recorder errors");
RECORDER(recorder_signals,      32, "Recorder signal handling");
RECORDER(recorder_traces,       64, "Recorder traces");


enum
{
    RECORDER_SIGNALS_MASK = 0
#ifdef SIGQUIT
    | (1U << SIGQUIT)
#endif // SIGQUIT
#ifdef SIGILL
    | (1U << SIGILL)
#endif // SIGILL
#ifdef SIGABRT
    | (1U << SIGABRT)
#endif // SIGABRT
#ifdef SIGBUS
    | (1U << SIGBUS)
#endif // SIGBUS
#ifdef SIGSEGV
    | (1U << SIGSEGV)
#endif // SIGSEGV
#ifdef SIGSYS
    | (1U << SIGSYS)
#endif // SIGSYS
#ifdef SIGXCPU
    | (1U << SIGXCPU)
#endif // SIGXCPU
#ifdef SIGXFSZ
    | (1U << SIGXFSZ)
#endif // SIGXFSZ
#ifdef SIGINFO
    | (1U << SIGINFO)
#endif // SIGINFO
#ifdef SIGUSR1
    | (1U << SIGUSR1)
#endif // SIGUSR1
#ifdef SIGUSR2
    | (1U << SIGUSR2)
#endif // SIGUSR2
#ifdef SIGSTKFLT
    | (1U << SIGSTKFLT)
#endif // SIGSTKFLT
#ifdef SIGPWR
    | (1U << SIGPWR)
#endif // SIGPWR
};

RECORDER_TWEAK_DEFINE(recorder_signals_mask,
                      RECORDER_SIGNALS_MASK,
                      "Recorder default mask for signals to catch");



// ============================================================================
//
//   Utility functions wrapping pattern maching irrespective of regex.h
//
// ============================================================================

#if HAVE_REGEX_H

// In the regexp.h case, we use real regular expressions
typedef regex_t pattern_t;
#define pattern_comp(re, what)  regcomp(re, what, REG_EXTENDED|REG_ICASE)
#define pattern_free(re)        regfree(re)

static inline bool pattern_match(regex_t *re, const char *s)
// ----------------------------------------------------------------------------
//   Match input string against a given "true" regexp
// ----------------------------------------------------------------------------
{
    regmatch_t rm;
    return regexec(re, s, 1, &rm, 0) == 0 &&
        rm.rm_so == 0 && s[rm.rm_eo] == 0;
}

#else // !HAVE_REGEX_H

// In the non regexp.h case, we degrade to string comparisons
typedef const char *pattern_t;

static inline int pattern_comp(pattern_t *re, const char *what)
// ----------------------------------------------------------------------------
//   No real regexp compilation in that case
// ----------------------------------------------------------------------------
{
    *re = what;
    return 0;
}

static inline bool pattern_match(pattern_t *re, const char *s)
// ----------------------------------------------------------------------------
//   No regexp matching, replace with string search
// ----------------------------------------------------------------------------
{
    return strstr(*re, s) != NULL;
}

static inline void pattern_free(pattern_t *re)
// ----------------------------------------------------------------------------
//   No need to free input string
// ----------------------------------------------------------------------------
{
}
#endif

#define array_size(a)   (sizeof(a) / sizeof(a[0]))

// ============================================================================
//
//    Local prototypes (in case -Wmissing-prototypes is enabled)
//
// ============================================================================

size_t    recorder_chan_write(recorder_chan_p chan, const void *ptr, size_t cnt);
size_t    recorder_chan_writable(recorder_chan_p chan);
ringidx_t recorder_chan_writer(recorder_chan_p chan);
ringidx_t recorder_chan_reader(recorder_chan_p chan);
size_t    recorder_chan_item_size(recorder_chan_p chan);


// ============================================================================
//
//    User-configurable parameters
//
// ============================================================================

static unsigned recorder_print(const char *ptr, size_t len, void *file_arg);
static void recorder_format_entry(recorder_show_fn show,
                                  void *output,
                                  const char *label,
                                  const char *location,
                                  uintptr_t order,
                                  uintptr_t timestamp,
                                  const char *message);

static void * recorder_output = NULL;
static recorder_show_fn recorder_show = recorder_print;
static recorder_format_fn recorder_format = recorder_format_entry;
static recorder_type_fn recorder_types[256];



// ============================================================================
//
//   Recording data
//
// ============================================================================

ringidx_t recorder_append(recorder_info *rec,
                          const char *where,
                          const char *format,
                          uintptr_t a0,
                          uintptr_t a1,
                          uintptr_t a2,
                          uintptr_t a3)
// ----------------------------------------------------------------------------
//  Enter a record entry in ring buffer with given set of args
// ----------------------------------------------------------------------------
{
    recorder_ring_p ring   = &rec->ring;
    recorder_entry *data   = rec->data;
    ringidx_t       writer = recorder_ring_fetch_add(ring->writer, 1);
    size_t          size   = ring->size;
    recorder_entry *entry  = &data[writer % size];
    entry->format = format;
    entry->order = recorder_ring_fetch_add(recorder_order, 1);
    entry->timestamp = recorder_tick();
    entry->where = where;
    entry->args[0] = a0;
    entry->args[1] = a1;
    entry->args[2] = a2;
    entry->args[3] = a3;
    recorder_ring_fetch_add(ring->commit, 1);
    if (rec->trace)
        recorder_trace_entry(rec, entry);
    return writer;
}


ringidx_t recorder_append2(recorder_info *rec,
                           const char *where,
                           const char *format,
                           uintptr_t a0,
                           uintptr_t a1,
                           uintptr_t a2,
                           uintptr_t a3,
                           uintptr_t a4,
                           uintptr_t a5,
                           uintptr_t a6,
                           uintptr_t a7)
// ----------------------------------------------------------------------------
//   Enter a double record (up to 8 args)
// ----------------------------------------------------------------------------
{
    recorder_ring_p ring   = &rec->ring;
    recorder_entry *data   = rec->data;
    ringidx_t       writer = recorder_ring_fetch_add(ring->writer, 2);
    size_t          size   = ring->size;
    recorder_entry *entry  = &data[writer % size];
    entry->format = format;
    entry->order = recorder_ring_fetch_add(recorder_order, 1);
    entry->timestamp = recorder_tick();
    entry->where = where;
    entry->args[0] = a0;
    entry->args[1] = a1;
    entry->args[2] = a2;
    entry->args[3] = a3;
    recorder_entry *entry2 = &data[(writer+1) % size];
    entry2->format = NULL;
    entry2->order = entry->order;
    entry2->timestamp = entry->timestamp;
    entry2->where = where;
    entry2->args[0] = a4;
    entry2->args[1] = a5;
    entry2->args[2] = a6;
    entry2->args[3] = a7;
    recorder_ring_fetch_add(ring->commit, 2);
    if (rec->trace)
        recorder_trace_entry(rec, entry);
    return writer;
}


ringidx_t recorder_append3(recorder_info *rec,
                           const char *where,
                           const char *format,
                           uintptr_t a0,
                           uintptr_t a1,
                           uintptr_t a2,
                           uintptr_t a3,
                           uintptr_t a4,
                           uintptr_t a5,
                           uintptr_t a6,
                           uintptr_t a7,
                           uintptr_t a8,
                           uintptr_t a9,
                           uintptr_t a10,
                           uintptr_t a11)
// ----------------------------------------------------------------------------
//   Record a triple entry (up to 12 args)
// ----------------------------------------------------------------------------
{
    recorder_ring_p ring   = &rec->ring;
    recorder_entry *data   = rec->data;
    ringidx_t       writer = recorder_ring_fetch_add(ring->writer, 3);
    size_t          size   = ring->size;
    recorder_entry *entry  = &data[writer % size];
    entry->format = format;
    entry->order = recorder_ring_fetch_add(recorder_order, 1);
    entry->timestamp = recorder_tick();
    entry->where = where;
    entry->args[0] = a0;
    entry->args[1] = a1;
    entry->args[2] = a2;
    entry->args[3] = a3;
    recorder_entry *entry2 = &data[(writer+1) % size];
    entry2->format = NULL;
    entry2->order = entry->order;
    entry2->timestamp = entry->timestamp;
    entry2->where = where;
    entry2->args[0] = a4;
    entry2->args[1] = a5;
    entry2->args[2] = a6;
    entry2->args[3] = a7;
    recorder_entry *entry3 = &data[(writer+2) % size];
    entry3->format = NULL;
    entry3->order = entry->order;
    entry3->timestamp = entry->timestamp;
    entry3->where = where;
    entry3->args[0] = a8;
    entry3->args[1] = a9;
    entry3->args[2] = a10;
    entry3->args[3] = a11;
    recorder_ring_fetch_add(ring->commit, 3);
    if (rec->trace)
        recorder_trace_entry(rec, entry);
    return writer;
}


ringidx_t recorder_append_fast(recorder_info *rec,
                               const char *where,
                               const char *format,
                               uintptr_t a0,
                               uintptr_t a1,
                               uintptr_t a2,
                               uintptr_t a3)
// ----------------------------------------------------------------------------
//  Enter a record entry in ring buffer with given set of args
// ----------------------------------------------------------------------------
{
    recorder_ring_p ring   = &rec->ring;
    recorder_entry *data   = rec->data;
    ringidx_t       writer = recorder_ring_fetch_add(ring->writer, 1);
    size_t          size   = ring->size;
    recorder_entry *entry  = &data[writer % size];
    entry->format = format;
    entry->order = recorder_ring_fetch_add(recorder_order, 1);
    entry->timestamp = data[(writer - 1) % size].timestamp;
    entry->where = where;
    entry->args[0] = a0;
    entry->args[1] = a1;
    entry->args[2] = a2;
    entry->args[3] = a3;
    recorder_ring_fetch_add(ring->commit, 1);
    if (rec->trace)
        recorder_trace_entry(rec, entry);
    return writer;
}


ringidx_t recorder_append_fast2(recorder_info *rec,
                                const char *where,
                                const char *format,
                                uintptr_t a0,
                                uintptr_t a1,
                                uintptr_t a2,
                                uintptr_t a3,
                                uintptr_t a4,
                                uintptr_t a5,
                                uintptr_t a6,
                                uintptr_t a7)
// ----------------------------------------------------------------------------
//   Enter a double record (up to 8 args)
// ----------------------------------------------------------------------------
{
    recorder_ring_p ring   = &rec->ring;
    recorder_entry *data   = rec->data;
    ringidx_t       writer = recorder_ring_fetch_add(ring->writer, 2);
    size_t          size   = ring->size;
    recorder_entry *entry  = &data[writer % size];
    entry->format = format;
    entry->order = recorder_ring_fetch_add(recorder_order, 1);
    entry->timestamp = data[(writer - 1) % size].timestamp;
    entry->where = where;
    entry->args[0] = a0;
    entry->args[1] = a1;
    entry->args[2] = a2;
    entry->args[3] = a3;
    recorder_entry *entry2 = &data[(writer+1) % size];
    entry2->format = NULL;
    entry2->order = entry->order;
    entry2->timestamp = entry->timestamp;
    entry2->where = where;
    entry2->args[0] = a4;
    entry2->args[1] = a5;
    entry2->args[2] = a6;
    entry2->args[3] = a7;
    recorder_ring_fetch_add(ring->commit, 2);
    if (rec->trace)
        recorder_trace_entry(rec, entry);
    return writer;
}


ringidx_t recorder_append_fast3(recorder_info *rec,
                                const char *where,
                                const char *format,
                                uintptr_t a0,
                                uintptr_t a1,
                                uintptr_t a2,
                                uintptr_t a3,
                                uintptr_t a4,
                                uintptr_t a5,
                                uintptr_t a6,
                                uintptr_t a7,
                                uintptr_t a8,
                                uintptr_t a9,
                                uintptr_t a10,
                                uintptr_t a11)
// ----------------------------------------------------------------------------
//   Record a triple entry (up to 12 args)
// ----------------------------------------------------------------------------
{
    recorder_ring_p ring   = &rec->ring;
    recorder_entry *data   = rec->data;
    ringidx_t       writer = recorder_ring_fetch_add(ring->writer, 3);
    size_t          size   = ring->size;
    recorder_entry *entry  = &data[writer % size];
    entry->format = format;
    entry->order = recorder_ring_fetch_add(recorder_order, 1);
    entry->timestamp = data[(writer - 1) % size].timestamp;
    entry->where = where;
    entry->args[0] = a0;
    entry->args[1] = a1;
    entry->args[2] = a2;
    entry->args[3] = a3;
    recorder_entry *entry2 = &data[(writer+1) % size];
    entry2->format = NULL;
    entry2->order = entry->order;
    entry2->timestamp = entry->timestamp;
    entry2->where = where;
    entry2->args[0] = a4;
    entry2->args[1] = a5;
    entry2->args[2] = a6;
    entry2->args[3] = a7;
    recorder_entry *entry3 = &data[(writer+2) % size];
    entry3->format = NULL;
    entry3->order = entry->order;
    entry3->timestamp = entry->timestamp;
    entry3->where = where;
    entry3->args[0] = a8;
    entry3->args[1] = a9;
    entry3->args[2] = a10;
    entry3->args[3] = a11;
    recorder_ring_fetch_add(ring->commit, 3);
    if (rec->trace)
        recorder_trace_entry(rec, entry);
    return writer;
}



// ============================================================================
//
//    Recorder dump utility
//
// ============================================================================

/// Global counter indicating the order of entries across recorders.
uintptr_t       recorder_order   = 0;

unsigned        recorder_dumping = 0;

/// List of the currently active flight recorders (ring buffers)
static recorder_info * recorders        = NULL;

/// List of the currently active tweaks
recorder_tweak *tweaks           = NULL;


static void recorder_dump_entry(recorder_info      *rec,
                                recorder_entry     *entry,
                                recorder_format_fn  format,
                                recorder_show_fn    show,
                                void               *output)
// ----------------------------------------------------------------------------
//  Dump a recorder entry in a buffer between dst and dst_end, return last pos
// ----------------------------------------------------------------------------
{
    char buffer[256];
    char format_buffer[32];

    const char     *label         = rec->name;
    char           *dst           = buffer;
    char           *dst_end       = buffer + sizeof buffer - 1;
    const char     *fmt           = entry->format;
    unsigned        arg_index     = 0;
    const unsigned  max_arg_index = array_size(entry->args);

    // Exit if we get there for a long-format second entry
    if (!fmt)
        return;

    // Apply formatting. This complicated loop is because
    // we need to detect floating-point values, which are passed
    // differently on many architectures such as x86 or ARM
    // (passed in different registers). So we detect them from the format,
    // convert intptr_t to float or double depending on its size,
    // and call the variadic snprintf passing a double value that will
    // naturally go in the right register. A bit ugly.
    bool finished_in_newline = false;
    while (dst < dst_end)
    {
        char c = *fmt++;
        if (c != '%')
        {
            *dst = c;
            if (!c)
                break;
            dst++;
        }
        else
        {
            char     *fmt_copy       = format_buffer;
            char     *fmt_end        = format_buffer + sizeof format_buffer - 1;
            bool      floating_point = false;
            recorder_type_fn special = NULL;
            bool      done           = false;
            bool      unsupported    = false;
            bool      safe_pointer   = false;
            int       fields[2]      = { 0 };
            unsigned  field_cnt      = 0;
            *fmt_copy++ = c;
            while (!done && fmt_copy < fmt_end)
            {
                c = *fmt++;
                *fmt_copy++ = c;

                special = recorder_types[(uint8_t) c];
                if (special)
                    break;

                switch(c)
                {
                case 'f': case 'F':  // Floating point formatting
                case 'g': case 'G':
                case 'e': case 'E':
                case 'a': case 'A':
                    floating_point = true;
                    /* Falls through */
                case 'b':           // Integer formatting
                case 'c': case 'C':
                case 's': case 'S':
                case 'd': case 'D':
                case 'i':
                case 'o': case 'O':
                case 'u': case 'U':
                case 'x':
                case 'X':
                case 'p':
                case '%':
                case 0:             // End of string
                    done = true;
                    break;

                    // GCC: case '0' ... '9', not supported on IAR
                case '0': case '1': case '2': case '3': case '4':
                case '5': case '6': case '7': case '8': case '9':
                case '.':
                case '-':
                case 'l': case 'L':
                case 'h':
                case 'j':
                case 't':
                case 'z':
                case 'q':
                case 'v':
#ifdef _WIN32
                    // On Windows size specifiers "I", "I32", "I64"
                case 'I':
#endif
                    break;
                case '+':
                    safe_pointer = true;
                    break;
                case 'n':           // Expect two args
                case '*':
                    // Check for long entry, need to skip to next entry
                    if (arg_index >= max_arg_index)
                    {
                        recorder_ring_p ring = &rec->ring;
                        recorder_entry *base = (recorder_entry *) (ring + 1);
                        ringidx_t idx = entry - base;
                        entry = &base[(idx + 1) % ring->size];
                        arg_index = 0;
                    }
                    unsupported = field_cnt >= 2;
                    if (!unsupported)
                        fields[field_cnt++] = (int) entry->args[arg_index++];
                    break;

                default:
                    unsupported = true;
                    break;
                }
            }
            if (!c || unsupported)
                break;
            bool is_string = (c == 's' || c == 'S');
            if (is_string && !safe_pointer && recorder_dumping)
                fmt_copy[-1] = 'p'; // Replace with a pointer if not tracing
            *fmt_copy++ = 0;

            // Check for long entry, need to skip to next entry
            if (arg_index >= max_arg_index)
            {
                recorder_ring_p ring = &rec->ring;
                recorder_entry *base = (recorder_entry *) (ring + 1);
                ringidx_t idx = entry - base;
                entry = &base[(idx + 1) % ring->size];
                arg_index = 0;
            }

            // Warning: It is important for correctness that only
            // one call to snprintf happens per loop, since snprintf
            // return value can be larger than what is actually written.
            if (special)
            {
                uintptr_t arg = entry->args[arg_index++];
                intptr_t tracing = recorder_dumping ? 0 : rec->trace;
                dst += special(safe_pointer | tracing,
                               format_buffer, dst, dst_end - dst, arg);
            }
            else if (floating_point)
            {
                double arg;
                if (sizeof(intptr_t) == sizeof(float))
                {
                    union { float f; intptr_t i; } u;
                    u.i = entry->args[arg_index++];
                    arg = (double) u.f;
                }
                else
                {
                    union { double d; intptr_t i; } u;
                    u.i = entry->args[arg_index++];
                    arg = u.d;
                }
                switch(field_cnt)
                {
                case 0:
                    dst += snprintf(dst, dst_end - dst,
                                    format_buffer, arg);
                    break;
                case 1:
                    dst += snprintf(dst, dst_end - dst,
                                    format_buffer, fields[0], arg);
                    break;
                case 2:
                    dst += snprintf(dst, dst_end - dst,
                                    format_buffer, fields[0], fields[1], arg);
                    break;
                }
            }
            else
            {
                intptr_t arg = entry->args[arg_index++];
                if (is_string && arg == 0)
                    arg = (intptr_t) "<NULL>";
                switch (field_cnt)
                {
                case 0:
                    dst += snprintf(dst, dst_end - dst,
                                    format_buffer, arg);
                    break;
                case 1:
                    dst += snprintf(dst, dst_end - dst,
                                    format_buffer, fields[0], arg);
                    break;
                case 2:
                    dst += snprintf(dst, dst_end - dst,
                                    format_buffer, fields[0], fields[1], arg);
                    break;
                }
            }
        }
        finished_in_newline = c == '\n';
    }
    // Check if snprintf returned a value beyond the buffer
    if (dst > dst_end)
        dst = dst_end;
    if (finished_in_newline)
        dst--;
    *dst++ = 0;

    format(show, output, label,
           entry->where, entry->order, entry->timestamp, buffer);
}



// ============================================================================
//
//    Default output prints things to stderr
//
// ============================================================================

void *recorder_configure_output(void *output)
// ----------------------------------------------------------------------------
//   Configure the output stream
// ----------------------------------------------------------------------------
{
    record(recorder, "Configure output %p from %p", output, recorder_output);
    void *previous = recorder_output;
    recorder_output = output;
    return previous;
}


static unsigned recorder_print(const char *ptr, size_t len, void *file_arg)
// ----------------------------------------------------------------------------
//   The default printing function - prints to stderr
// ----------------------------------------------------------------------------
{
    FILE *file = file_arg ? file_arg : stderr;
    return (unsigned) fprintf(file, "%.*s\n", (int) len, ptr);
}


recorder_show_fn  recorder_configure_show(recorder_show_fn show)
// ----------------------------------------------------------------------------
//   Configure the function used to output data to the stream
// ----------------------------------------------------------------------------
{
    record(recorder, "Configure show %p from %p", show, recorder_show);
    recorder_show_fn previous = recorder_show;
    recorder_show = show;
    return previous;
}



// ============================================================================
//
//    Default format for recorder entries
//
// ============================================================================

// Truly shocking that Visual Studio before 2015 does not have a working
// snprintf or vsnprintf. Note that the proposed replacements are not accurate
// since they return -1 on overflow instead of the length that would have
// been written as the standard mandates.
// http://stackoverflow.com/questions/2915672/snprintf-and-visual-studio-2010.
#if defined(_MSC_VER) && _MSC_VER < 1900
#  define snprintf  _snprintf
#endif

RECORDER_TWEAK_DEFINE(recorder_location, 0,
                      "Set to show location in recorder dumps");
RECORDER_TWEAK_DEFINE(recorder_function, 0,
                      "Set to show function in recorder dumps");

static void recorder_format_entry(recorder_show_fn show,
                                  void *output,
                                  const char *label,
                                  const char *function_name,
                                  uintptr_t order,
                                  uintptr_t timestamp,
                                  const char *message)
// ----------------------------------------------------------------------------
//   Default formatting for the entries
// ----------------------------------------------------------------------------
{
    char buffer[256];
    char *dst = buffer;
    char *dst_end = buffer + sizeof buffer;

    // Look for file:line: in the input message
    const char *end_of_fileline = message;
    for (int colon = 0; colon < 2; colon++)
        while (*end_of_fileline && *end_of_fileline++ != ':')
            /* Empty */;
    if (*end_of_fileline == 0)       // Play it ultra-safe
        end_of_fileline = message;

    int size = (int) RECORDER_TWEAK(recorder_location);
    if (size)
    {
        int fileline_size = (int) (end_of_fileline - message);
        if (size != 1)
            dst += snprintf(dst, dst_end - dst,
                            "%*.*s", size, fileline_size, message);
        else
            dst += snprintf(dst, dst_end - dst,
                            "%.*s", fileline_size, message);
        if (dst > dst_end)
            dst = dst_end;
    }
    message = end_of_fileline;

    size = (int) RECORDER_TWEAK(recorder_function);
    if (size)
    {
        if (size != 1)
            dst += snprintf(dst, dst_end - dst, "%*s:", size, function_name);
        else
            dst += snprintf(dst, dst_end - dst, "%s:", function_name);
        if (dst > dst_end)
            dst = dst_end;
    }

    if (RECORDER_64BIT) // Static if to detect how to display time
    {
        // Time stamp in us, show in seconds
        dst += snprintf(dst, dst_end - dst,
                        "[%lu %.6f] %s: %s",
                        (unsigned long) order,
                        (double) timestamp / RECORDER_HZ,
                        label, message);
    }
    else
    {
        // Time stamp  in ms, show in seconds
        dst += snprintf(dst, dst_end - dst,
                        "[%lu %.3f] %s: %s",
                        (unsigned long) order,
                        (double) timestamp / RECORDER_HZ,
                        label, message);
    }
    // In case snprintf overflowed
    if (dst > dst_end)
        dst = dst_end;

    show(buffer, dst - buffer, output);
}


recorder_format_fn recorder_configure_format(recorder_format_fn format)
// ----------------------------------------------------------------------------
//   Configure the function used to format entries
// ----------------------------------------------------------------------------
{
    record(recorder, "Configure format %p from %p", format, recorder_format);
    recorder_format_fn previous = recorder_format;
    recorder_format = format;
    return previous;
}


recorder_type_fn recorder_configure_type(uint8_t id,
                                         recorder_type_fn type)
// ----------------------------------------------------------------------------
//    Register a formatting function for specific data types
// ----------------------------------------------------------------------------
{
    recorder_type_fn previous = recorder_types[id];
    record(recorder, "Configure type '%c' to %p from %p", id, type, previous);
    recorder_types[id] = type;
    return previous;
}


static recorder_entry *recorder_peek(recorder_ring_p ring)
// ----------------------------------------------------------------------------
//   Peek the next entry that would be read in the ring and advance by 1
// ----------------------------------------------------------------------------
{
    recorder_entry *data      = (recorder_entry *) (ring + 1);
    const size_t    size      = ring->size;
    ringidx_t       reader    = ring->reader;
    ringidx_t       commit    = ring->commit;
    size_t          written   = commit - reader;
    if (written >= size)
    {
        ringidx_t minR = commit - size + 1;
        ringidx_t skip = minR - reader;
        recorder_ring_add_fetch(ring->overflow, skip);
        reader = recorder_ring_add_fetch(ring->reader, skip);
        written = commit - reader;
    }
    return written ? data + reader % size : NULL;
}


unsigned recorder_sort(const char *what,
                       recorder_format_fn format,
                       recorder_show_fn show, void *output)
// ----------------------------------------------------------------------------
//   Dump all entries, sorted by their global 'order' field
// ----------------------------------------------------------------------------
{
    recorder_entry *entry;
    unsigned        dumped = 0;

    pattern_t re;
    int status = pattern_comp(&re, what);

    recorder_ring_fetch_add(recorder_dumping, 1);
    while (status == 0)
    {
        uintptr_t       lowest_order = ~0UL;
        recorder_entry *lowest_entry = NULL;
        recorder_info  *lowest_rec   = NULL;
        recorder_info  *rec;

        for (rec = recorders; rec; rec = rec->next)
        {
            // Skip recorders that don't match the pattern
            if (!pattern_match(&re, rec->name))
                continue;

            // Loop while this recorder is readable and we can find next order
            entry = recorder_peek(&rec->ring);
            if (entry)
            {
                uintptr_t order = entry->order;
                if (order < lowest_order)
                {
                    lowest_rec = rec;
                    lowest_order = order;
                    lowest_entry = entry;
                }
            }
        }

        if (!lowest_rec)
            break;

        recorder_ring_fetch_add(lowest_rec->ring.reader, 1);
        recorder_dump_entry(lowest_rec, lowest_entry, format, show, output);
        dumped++;
    }
    recorder_ring_fetch_add(recorder_dumping, -1);

    pattern_free(&re);

    return dumped;
}


unsigned recorder_dump(void)
// ----------------------------------------------------------------------------
//   Dump all entries, sorted by their global 'order' field
// ----------------------------------------------------------------------------
{
    record(recorder, "Recorder dump");
    return recorder_sort(".*", recorder_format,recorder_show,recorder_output);
}


unsigned recorder_dump_for(const char *what)
// ----------------------------------------------------------------------------
//   Dump all entries for recorder with names matching 'what'
// ----------------------------------------------------------------------------
{
    record(recorder, "Recorder dump for %+s", what);
    return recorder_sort(what, recorder_format,recorder_show,recorder_output);
}


recorder_info *recorder_list(void)
// ----------------------------------------------------------------------------
//   Return the list of recorders
// ----------------------------------------------------------------------------
{
    return recorders;
}



// ============================================================================
//
//    Implementation of recorder shared memory structures
//
// ============================================================================

#define RECORDER_CMD_LEN  1024

typedef struct recorder_shans
// ----------------------------------------------------------------------------
//   Shared-memory information about recorder_chans
// ----------------------------------------------------------------------------
{
    uint32_t        magic;      // Magic number to check structure type
    uint32_t        version;    // Version number for shared memory format
    uint32_t        serial;     // Serial ID for the current channels
    off_t           head;       // First recorder_chan in linked list
    off_t           free_list;  // Free list
    off_t           offset;     // Current offset for new recorder_chans
    recorder_ring_t commands;   // Incoming configuration commands
    char            commands_buffer[RECORDER_CMD_LEN];
} recorder_shans, *recorder_shans_p;


typedef struct recorder_shan
// ----------------------------------------------------------------------------
//   A named data recorder_chan in shared memory
// ----------------------------------------------------------------------------
{
    recorder_type   type;       // Data type stored in recorder_chan
    off_t           next;       // Offset to next recorder_chan in linked list
    off_t           name;       // Offset of name in recorder_shan
    off_t           description; // Offset of description
    off_t           unit;       // Offset of measurement unit
    recorder_data   min;        // Minimum value
    recorder_data   max;        // Maximum value
    recorder_ring_t ring;       // Ring data
} recorder_shan, *recorder_shan_p;


typedef struct recorder_chans
// ----------------------------------------------------------------------------
//   Information about mapping of shared recorder_chans
// ----------------------------------------------------------------------------
{
    int             fd;         // File descriptor for mmap
    uint32_t        serial;     // Serial ID to check if still valid
    void *          map_addr;   // Address in memory for mmap
    size_t          map_size;   // Size allocated for mmap
    recorder_chan_p head;       // First recorder_chan in list
} recorder_chans_t, *recorder_chans_p;


typedef struct recorder_chan
// ----------------------------------------------------------------------------
//   Accessing a shared memory recorder_chan
// ----------------------------------------------------------------------------
{
    recorder_chans_p chans;
    off_t            offset;
    recorder_chan_p  next;
} recorder_chan_t, *recorder_chan_p;


// Map memory in 4K chunks (one page)
#define MAP_SIZE        4096


static inline recorder_shan_p recorder_shared(recorder_chan_p chan)
// ----------------------------------------------------------------------------
//  Return the recorder_chan shared address
// ----------------------------------------------------------------------------
{
    recorder_chans_p  chans    = chan->chans;
    char             *map_addr = chans->map_addr;
    recorder_shan_p   shan     = (recorder_shan_p) (map_addr + chan->offset);
    return shan;
}



// ============================================================================
//
//    Interface for the local process
//
// ============================================================================

#ifdef HAVE_SYS_MMAN_H
static bool recorder_shans_file_extend(int fd, off_t new_size)
// ----------------------------------------------------------------------------
//    Extend a file to the given size
// ----------------------------------------------------------------------------
{
    return
        lseek(fd, new_size-1, SEEK_SET) != -1 &&
        write(fd, "", 1) == 1;
}
#endif // HAVE_SYS_MMAN_H


recorder_chans_p recorder_chans_new(const char *file)
// ----------------------------------------------------------------------------
//   Create a new mmap'd file
// ----------------------------------------------------------------------------
{
#ifndef HAVE_SYS_MMAN_H
    // Some version of MinGW do not have <sys/mman.h>
    record(recorder_error,
           "Cannot create export channels %s on system without mmap",
           file);
    return NULL;
#else // HAVE_SYS_MMAN_H
    record(recorder, "Create export channels %s", file);
    printf("Creating new %s\n", file);
    if (!file)
    {
        record(recorder_error, "NULL export file");
        return NULL;
    }

    // Open the file
    int fd = open(file, O_RDWR|O_CREAT|O_TRUNC, (mode_t) 0600);
    if (fd == -1)
    {
        record(recorder_error,
               "Unable to create exports file %s: %s (%d)",
               file, strerror(errno), errno);
        return NULL;
    }

    // Make sure we have enough space for the data
    size_t map_size = MAP_SIZE;
    if (!recorder_shans_file_extend(fd, map_size))
    {
        record(recorder_error,
               "Unable to create initial mapping for exports file %s: %s (%d)",
               file, strerror(errno), errno);
        close(fd);
        return NULL;
    }

    // Map space for the recorder_chans
    off_t  offset   = 0;
    void  *map_addr = mmap(NULL, map_size,
                           PROT_READ | PROT_WRITE,
                           MAP_FILE | MAP_SHARED,
                           fd, offset);
    if (map_addr == MAP_FAILED)
    {
        record(recorder_error, "Unable to mmap %s: %s (%d)",
               file, strerror(errno), errno);
        close(fd);
        return NULL;
    }

    // Successful: Initialize in-memory recorder_chans list
    recorder_chans_p chans = malloc(sizeof(recorder_chans_t));
    chans->fd = fd;
    chans->map_addr = map_addr;
    chans->map_size = map_size;
    chans->head = NULL;

    // Initialize shared-memory data
    recorder_shans_p shans = map_addr;
    shans->magic = RECORDER_CHAN_MAGIC;
    shans->version = RECORDER_CHAN_VERSION;
    struct timeval t;
    gettimeofday(&t, NULL);
    shans->serial = t.tv_usec;
    shans->head = 0;
    shans->free_list = 0;
    shans->offset = sizeof(recorder_shans);
    recorder_ring_init(&shans->commands,
                       sizeof(shans->commands_buffer),
                       sizeof(shans->commands_buffer[0]));

    return chans;
#endif // HAVE_SYS_MMAN_H
}


void recorder_chans_delete(recorder_chans_p chans)
// ----------------------------------------------------------------------------
//   Delete the list of exported items from the shared memory area
// ----------------------------------------------------------------------------
{
    int i;
    recorder_info *rec;
    for (rec = recorders; rec; rec = rec->next)
    {
        if (rec->trace == RECORDER_CHAN_MAGIC)
            rec->trace = 0;
        for (i = 0; i < (int) array_size(rec->exported); i++)
            rec->exported[i] = NULL;
    }

    recorder_chan_p next = NULL;
    recorder_chan_p chan;
    for (chan = chans->head; chan; chan = next)
    {
        next = chan->next;
        recorder_chan_delete(chan);
    }

#ifdef HAVE_SYS_MMAN_H
    munmap(chans->map_addr, chans->map_size);
#endif // HAVE_SYS_MMAN_H
    close(chans->fd);
    free(chans);
}


recorder_chan_p recorder_chan_new(recorder_chans_p chans,
                                  recorder_type    type,
                                  size_t           size,
                                  const char *     name,
                                  const char *     description,
                                  const char *     unit,
                                  recorder_data    min,
                                  recorder_data    max)
// ----------------------------------------------------------------------------
//    Allocate and create a new recorder_chan
// ----------------------------------------------------------------------------
{
#ifndef HAVE_SYS_MMAN_H
    record(recorder_error, "recorder_chan_new called on system without mmap");
    return NULL;
#else // HAVE_SYS_MMAN_H
    recorder_shans_p   shans       = chans->map_addr;
    size_t             offset      = shans->offset;
    size_t             item_size   = 2 * sizeof(recorder_data);

    size_t             name_len    = strlen(name);
    size_t             descr_len   = strlen(description);
    size_t             unit_len    = strlen(unit);

    size_t             name_offs   = sizeof(recorder_shan) + size*item_size;
    size_t             descr_offs  = name_offs + name_len + 1;
    size_t             unit_offs   = descr_offs + descr_len + 1;

    size_t             alloc       = unit_offs + unit_len + 1;

    // TODO: Find one from the free list if there is one

    size_t align = sizeof(long double);
    size_t new_offset = (offset + alloc + align-1) & ~(align-1);
    if (new_offset >= chans->map_size)
    {
        size_t map_size = (new_offset / MAP_SIZE + 1) * MAP_SIZE;
        if (!recorder_shans_file_extend(chans->fd, map_size))
        {
            record(recorder_error,
                   "Could not extend mapping to %zu bytes: %s (%d)",
                   map_size, strerror(errno), errno);
            return NULL;
        }
        void *map_addr = mmap(chans->map_addr, map_size,
                              PROT_READ | PROT_WRITE,
                              MAP_FILE | MAP_SHARED,
                              chans->fd, 0);
        if (map_addr == MAP_FAILED)
        {
            record(recorder_error,
                   "Unable to extend mmap to %zu bytes, errno=%d (%s)",
                   map_size, errno, strerror(errno));
            return NULL;
        }

        // Note that if the new mapping address is different,
        // all recorder_chan_p become invalid
        chans->map_size = map_size;
        chans->map_addr = map_addr;
    }
    shans->offset = new_offset;

    // Initialize recorder_chan fields
    recorder_shan_p shan = (recorder_shan_p) ((char *) chans->map_addr+offset);
    char *base = (char *) shan;
    shan->type = type;
    shan->next = shans->head;
    shan->name = name_offs;
    shan->description = descr_offs;
    shan->unit = unit_offs;
    shan->min = min;
    shan->max = max;
    memcpy(base + name_offs, name, name_len + 1);
    memcpy(base + descr_offs, description, descr_len + 1);
    memcpy(base + unit_offs, unit, unit_len + 1);

    // Initialize ring fields
    recorder_ring_p ring = &shan->ring;
    ring->size = size;
    ring->item_size = item_size;
    ring->reader = 0;
    ring->writer = 0;
    ring->commit = 0;
    ring->overflow = 0;

    // Link recorder_chan in recorder_chans list
    shans->head = offset;

    // Create recorder_chan access
    recorder_chan_p chan = malloc(sizeof(recorder_chan_t));
    chan->chans = chans;
    chan->offset = offset;
    chan->next = chans->head;
    chans->head = chan;

    return chan;
#endif // HAVE_SYS_MMAN_H
}


void recorder_chan_delete(recorder_chan_p chan)
// ----------------------------------------------------------------------------
//   Delete a recorder_chan
// ----------------------------------------------------------------------------
{
    recorder_chans_p  chans       = chan->chans;
    char             *map_addr    = chans->map_addr;
    intptr_t          chan_offset = chan->offset;
    recorder_shans_p  shans       = (recorder_shans_p) map_addr;
    off_t            *last        = &shans->head;
    off_t             offset;

    for (offset = *last; offset; offset = *last)
    {
        recorder_shan_p shan = (recorder_shan_p) (map_addr + offset);
        if (*last == chan_offset)
        {
            *last = shan->next;
            shan->next = shans->free_list;
            shans->free_list = chan_offset;
            break;
        }
        last = &shan->next;
    }

    recorder_chan_p * last_chan;
    for (last_chan = &chans->head; *last_chan; last_chan = &(*last_chan)->next)
    {
        if (*last_chan == chan)
        {
            *last_chan = chan->next;
            break;
        }
    }

    free(chan);
}


size_t recorder_chan_write(recorder_chan_p chan, const void *ptr, size_t count)
// ----------------------------------------------------------------------------
//   Write some data in the recorder_chan
// ----------------------------------------------------------------------------
{
    recorder_shan_p shan = recorder_shared(chan);
    return recorder_ring_write(&shan->ring, ptr, count, NULL, NULL, NULL);
}


size_t recorder_chan_writable(recorder_chan_p chan)
// ----------------------------------------------------------------------------
//    Return number of items that can be written in ring
// ----------------------------------------------------------------------------
{
    recorder_shan_p shan = recorder_shared(chan);
    return recorder_ring_writable(&shan->ring);
}


ringidx_t recorder_chan_writer(recorder_chan_p chan)
// ----------------------------------------------------------------------------
//    Return current writer index
// ----------------------------------------------------------------------------
{
    recorder_shan_p shan = recorder_shared(chan);
    return shan->ring.writer;
}



// ============================================================================
//
//    Subscribing to recorder_chans in a remote process
//
// ============================================================================

recorder_chans_p recorder_chans_open(const char *file)
// ----------------------------------------------------------------------------
//    Map the file in memory, and scan its structure
// ----------------------------------------------------------------------------
{
#ifndef HAVE_SYS_MMAN_H
    record(recorder_error,
           "Cannot open export channels %s on system without mmap",
           file);
    return NULL;
#else // HAVE_SYS_MMAN_H
    record(recorder, "Open export channels %s", file);
    int fd = open(file, O_RDWR);
    if (fd == -1)
    {
        record(recorder_error,
               "Unable to open %s for reading: %s (%d)",
               file, strerror(errno), errno);
        return NULL;
    }
    struct stat stat;
    if (fstat(fd, &stat) != 0)
    {
        record(recorder_error,
               "Unable to stat %s: %s (%d)",
               file, strerror(errno), errno);
        return NULL;
    }

    // Map space for the recorder_chans
    size_t  map_size = stat.st_size;
    off_t   offset   = 0;
    void   *map_addr = mmap(NULL, map_size,
                            PROT_READ|PROT_WRITE,
                            MAP_FILE | MAP_SHARED,
                            fd, offset);
    recorder_shans_p shans = map_addr;
    if (map_addr == MAP_FAILED                  ||
        shans->magic != RECORDER_CHAN_MAGIC     ||
        shans->version != RECORDER_CHAN_VERSION)
    {
        if (map_addr == MAP_FAILED)
            record(recorder_error,
                   "Unable to map %s file for reading: %s (%d)",
                   file, strerror(errno), errno);
        if (shans->magic == (RECORDER_CHAN_MAGIC ^ RECORDER_64BIT))
            record(recorder_error,
                   "Mismatch between 32-bit and 64-bit recorder data");
        else if (shans->magic != RECORDER_CHAN_MAGIC)
            record(recorder_error,
                   "Wrong magic number, got %x instead of %x",
                   shans->magic, RECORDER_CHAN_MAGIC);
        if (shans->version != RECORDER_CHAN_VERSION)
            record(recorder_error,
                   "Wrong exports file version, got %x instead of %x",
                   shans->version, RECORDER_CHAN_VERSION);

        close(fd);
        return NULL;
    }

    int retries = 0;
    while (retries < 3)
    {
        // Successful: Initialize with recorder_chan descriptor
        recorder_chans_p chans = malloc(sizeof(recorder_chans_t));
        chans->fd = fd;
        chans->map_addr = map_addr;
        chans->map_size = map_size;
        chans->serial = shans->serial;
        chans->head = NULL;

        // Create recorder_chans for all recorder_chans in shared memory
        recorder_shan_p shan;
        off_t           off;
        for (off = shans->head; off; off = shan->next)
        {
            shan = (recorder_shan_p) ((char *) map_addr + off);
            recorder_chan_p chan = malloc(sizeof(recorder_chan_t));
            chan->chans = chans;
            chan->offset = off;
            chan->next = chans->head;
            chans->head = chan;
        }

        if (recorder_chans_valid(chans))
            return chans;

        // The serial number changed - Program restarted at wrong time?
        record(recorder_warning,
               "Export channels serial changed, retry #%d", retries);
        recorder_chans_close(chans);
        retries++;
    }

    record(recorder_error, "Too many retries mapping %s, giving up", file);
    return NULL;
#endif // HAVE_SYS_MMAN_H
}


void recorder_chans_close(recorder_chans_p chans)
// ----------------------------------------------------------------------------
//   Close shared memory recorder_chans
// ----------------------------------------------------------------------------
{
    recorder_chan_p chan, next;
    for (chan = chans->head; chan; chan = next)
    {
        next = chan->next;
        free(chan);
    }
    free (chans);
}


bool recorder_chans_valid(recorder_chans_p chans)
// ----------------------------------------------------------------------------
//   Return true if the open chans is still valid
// ----------------------------------------------------------------------------
{
    recorder_shans_p shans = (recorder_shans_p) chans->map_addr;
    return chans->serial == shans->serial;
}


bool recorder_chans_configure(recorder_chans_p chans,
                              const char *message)
// ----------------------------------------------------------------------------
//   Send a configuration message over the shared memory buffer
// ----------------------------------------------------------------------------
{
    recorder_shans_p shans = chans->map_addr;
    recorder_ring_p  cmds  = &shans->commands;
    size_t           len   = strlen(message);
    size_t           avail = recorder_ring_writable(cmds);
    if (avail < len)
    {
        record(recorder_warning,
               "Insufficient space in command buffer, %u < %u", avail, len);
        return false;
    }
    recorder_ring_write(cmds, message, len, NULL, NULL, NULL);
    return true;
}


recorder_chan_p recorder_chan_find(recorder_chans_p  chans,
                                   const char       *pattern,
                                   recorder_chan_p   after)
// ----------------------------------------------------------------------------
//   Find a recorder_chan with the given name in the recorder_chan list
// ----------------------------------------------------------------------------
{
    pattern_t re;
    int             status = pattern_comp(&re, pattern);
    recorder_chan_p first  = after ? after->next : chans->head;
    recorder_chan_p chan   = NULL;;

    if (status == 0)
    {
        for (chan = first; chan; chan = chan->next)
        {
            const char *name = recorder_chan_name(chan);
            if (pattern_match(&re, name))
                break;
        }
    }
    pattern_free(&re);
    return chan;
}


const char *recorder_chan_name(recorder_chan_p chan)
// ----------------------------------------------------------------------------
//   Return the name for a given recorder_chan
// ----------------------------------------------------------------------------
{
    recorder_shan_p shan = recorder_shared(chan);
    return (const char *) shan + shan->name;
}


const char *recorder_chan_description(recorder_chan_p chan)
// ----------------------------------------------------------------------------
//   Return the description for a given recorder_chan
// ----------------------------------------------------------------------------
{
    recorder_shan_p shan = recorder_shared(chan);
    return (const char *) shan + shan->description;
}


const char *recorder_chan_unit(recorder_chan_p chan)
// ----------------------------------------------------------------------------
//   Return the measurement unit for a given recorder_chan
// ----------------------------------------------------------------------------
{
    recorder_shan_p shan = recorder_shared(chan);
    return (const char *) shan + shan->unit;
}


recorder_data recorder_chan_min(recorder_chan_p chan)
// ----------------------------------------------------------------------------
//   Return the min value specified for the given channel
// ----------------------------------------------------------------------------
{
    recorder_shan_p shan = recorder_shared(chan);
    return shan->min;
}


recorder_data recorder_chan_max(recorder_chan_p chan)
// ----------------------------------------------------------------------------
//   Return the max value specified for the given channel
// ----------------------------------------------------------------------------
{
    recorder_shan_p shan = recorder_shared(chan);
    return shan->max;
}


recorder_type recorder_chan_type(recorder_chan_p chan)
// ----------------------------------------------------------------------------
//   Return the element type for a given recorder_chan
// ----------------------------------------------------------------------------
{
    recorder_shan_p shan = recorder_shared(chan);
    return shan->type;
}


size_t recorder_chan_size(recorder_chan_p chan)
// ----------------------------------------------------------------------------
//   Return the ring size for a given recorder_chan
// ----------------------------------------------------------------------------
{
    recorder_shan_p shan = recorder_shared(chan);
    return shan->ring.size;
}


size_t recorder_chan_item_size(recorder_chan_p chan)
// ----------------------------------------------------------------------------
//   Return the ring item size for a given recorder_chan
// ----------------------------------------------------------------------------
{
    recorder_shan_p shan = recorder_shared(chan);
    return shan->ring.item_size;
}


size_t recorder_chan_readable(recorder_chan_p chan, ringidx_t *reader)
// ----------------------------------------------------------------------------
//   Return number of readable elements in ring
// ----------------------------------------------------------------------------
{
    if (!recorder_chans_valid(chan->chans))
        return 0;
    recorder_shan_p shan = recorder_shared(chan);
    return recorder_ring_readable(&shan->ring, reader);
}


size_t recorder_chan_read(recorder_chan_p chan,
                          recorder_data *ptr, size_t count,
                          ringidx_t *reader)
// ----------------------------------------------------------------------------
//   Read data from the ring
// ----------------------------------------------------------------------------
{
    if (!recorder_chans_valid(chan->chans))
        return 0;
    recorder_shan_p shan = recorder_shared(chan);
    return recorder_ring_read(&shan->ring, ptr, count, reader, NULL, NULL);
}


ringidx_t recorder_chan_reader(recorder_chan_p chan)
// ----------------------------------------------------------------------------
//   Return current reader index for recorder_chan
// ----------------------------------------------------------------------------
{
    if (!recorder_chans_valid(chan->chans))
        return 0;
    recorder_shan_p shan = recorder_shared(chan);
    return shan->ring.reader;
}


static recorder_type recorder_type_from_format(const char *format,
                                               unsigned index)
// ----------------------------------------------------------------------------
//   Analyze format string to figure out the type of export
// ----------------------------------------------------------------------------
{
    char           c;
    bool           in_format    = false;
    recorder_type  result       = RECORDER_NONE;
    unsigned       start_index  = index;
    const char    *start_format = format;

    for (c = *format++; c; c = *format++)
    {
        if (c == '%')
        {
            in_format = !in_format;
            continue;
        }
        if (!in_format)
            continue;
        switch (c)
        {
        case 'f': case 'F':  // Floating point formatting
        case 'g': case 'G':
        case 'e': case 'E':
        case 'a': case 'A':
            result = RECORDER_REAL;
            break;

        case 'b':           // Integer formatting
        case 'd': case 'D':
        case 'i':
            result = RECORDER_SIGNED;
            break;

        case 'c': case 'C':
        case 's': case 'S':
        case 'o': case 'O':
        case 'u': case 'U':
        case 'x':
        case 'X':
        case 'p':
            result = RECORDER_UNSIGNED;
            break;

            // GCC: case '0' ... '9', not supported on IAR
        case '0': case '1': case '2': case '3': case '4':
        case '5': case '6': case '7': case '8': case '9':
        case '.':
        case '+':
        case '-':
        case 'l': case 'L':
        case 'h':
        case 'j':
        case 't':
        case 'z':
        case 'q':
        case 'v':
            break;
        case 'n':           // Expect two args
        case '*':
        default:
            result = RECORDER_INVALID;
            break;
        }

        if (result != RECORDER_NONE)
        {
            if (!index)
            {
                record(recorder, "Export type at index %u in %s is %u",
                       start_index, start_format, result);
                return result;
            }
            index--;
            result = RECORDER_NONE;
            in_format = false;
        }
    }

    record(recorder_warning,
           "Unknown format directive at index %u in %s",
           start_index, start_format);
    return RECORDER_INVALID;
}



// ============================================================================
//
//   Background dump
//
// ============================================================================

RECORDER_TWEAK_DEFINE(recorder_dump_sleep, 100,
                      "Sleep time between background dumps (ms)");

static bool background_dump_running = false;


static void *background_dump(void *pattern)
// ----------------------------------------------------------------------------
//    Dump the recorder (background thread)
// ----------------------------------------------------------------------------
{
    const char *what = pattern;
    while (background_dump_running)
    {
        unsigned dumped = recorder_sort(what, recorder_format,
                                        recorder_show, recorder_output);
        if (dumped == 0)
        {
            struct timespec tm;
            tm.tv_sec  = 0;
            tm.tv_nsec = 1000000 * RECORDER_TWEAK(recorder_dump_sleep);
            nanosleep(&tm, NULL);
        }
    }
    return pattern;
}


void recorder_background_dump(const char *what)
// ----------------------------------------------------------------------------
//   Dump the selected recorders, sleeping sleep_ms if nothing to dump
// ----------------------------------------------------------------------------
{
    pthread_t tid;
    background_dump_running = true;
    if (strcmp(what, "all") == 0)
        what = ".*";
    pthread_create(&tid, NULL, background_dump, (void *) what);
    record(recorder, "Started background dump thread for %s, thread %p",
           what, (void *) tid);
}


void recorder_background_dump_stop(void)
// ----------------------------------------------------------------------------
//   Stop the background dump task
// ----------------------------------------------------------------------------
{
    background_dump_running = false;
}



// ============================================================================
//
//    Signal handling
//
// ============================================================================

// Saved old actions
#if HAVE_SIGACTION
static struct sigaction old_action[NSIG] = { };

static void signal_handler(int sig, siginfo_t *info, void *ucontext)
// ----------------------------------------------------------------------------
//    Dump the recorder when receiving a signal
// ----------------------------------------------------------------------------
{
    record(recorder_signals,
           "Received signal %+s (%d) si_addr=%p, ucontext %p, dumping recorder",
           strsignal(sig), sig, info->si_addr, ucontext);
    fprintf(stderr, "Received signal %s (%d), dumping recorder\n",
            strsignal(sig), sig);

    // Restore previous handler in case we crash during the dump
    sigaction(sig, &old_action[sig], NULL);
    recorder_dump();
}


void recorder_dump_on_signal(int sig)
// ----------------------------------------------------------------------------
//    C interface for Recorder::DumpOnSignal
// ----------------------------------------------------------------------------
{
    if (sig < 0 || sig >= NSIG)
        return;

    struct sigaction action;

    // Already set?
    sigaction(sig, NULL, &action);
    if ((action.sa_flags & SA_SIGINFO) != 0 && action.sa_sigaction == signal_handler)
        return;
    old_action[sig] = action;

    action.sa_sigaction = signal_handler;
    sigemptyset(&action.sa_mask);
    action.sa_flags = SA_SIGINFO;
    sigaction(sig, &action, NULL);
    record(recorder_signals,
           "Recorder dump handler %p for signal %u, old action=%p",
           signal_handler, sig, old_action[sig].sa_sigaction);
}

#else // !HAVE_SIGACTION

/* For MinGW, there is no struct sigaction */
typedef void (*sig_fn)(int);
static sig_fn old_handler[NSIG] = { };

static void signal_handler(int sig)
// ----------------------------------------------------------------------------
//   Dump the recorder when receiving the given signal
// ----------------------------------------------------------------------------
{
    record(recorder_signals, "Received signal %d, dumping recorder", sig);
    fprintf(stderr, "Received signal %d, dumping recorder\n", sig);

    // Restore previous handler
    sig_fn next = signal(sig, old_handler[sig]);
    recorder_dump();

    // If there is a 'next' handler, call it
    if (next != SIG_DFL && next != SIG_IGN)
        next(sig);
}


void recorder_dump_on_signal(int sig)
// ----------------------------------------------------------------------------
//    C interface for Recorder::DumpOnSignal
// ----------------------------------------------------------------------------
{
    if (sig < 0 || sig >= NSIG)
        return;
    old_handler[sig] = signal(sig, signal_handler);
    record(recorder_signals,
           "Recorder dump handler %p for signal %u, old handler=%p",
           signal_handler, sig, old_handler[sig]);
    if (old_handler[sig] == signal_handler)
        old_handler[sig] = (sig_fn) SIG_DFL;
}

#endif // HAVE_SIGACTION


void recorder_dump_on_common_signals(unsigned add, unsigned remove)
// ----------------------------------------------------------------------------
//    Easy interface to dump on the most common signals
// ----------------------------------------------------------------------------
{
    // Normally, this is called after constructors have run, so this is
    // a good time to check environment settings
    recorder_trace_set(getenv("RECORDER_TRACES"));
    recorder_trace_set(getenv("RECORDER_TWEAKS"));

    const char *dump_pattern = getenv("RECORDER_DUMP");
    if (dump_pattern)
        recorder_background_dump(dump_pattern);

    unsigned sig;
    unsigned signals = (add | RECORDER_TWEAK(recorder_signals_mask)) & ~remove;

    record(recorder_signals,
           "Activating dump for signal mask 0x%X", signals);
    for (sig = 0; signals; sig++)
    {
        unsigned mask = 1U << sig;
        if (signals & mask)
            recorder_dump_on_signal(sig);
        signals &= ~mask;
    }
}



// ============================================================================
//
//    Support functions
//
// ============================================================================

#ifndef recorder_tick
uintptr_t recorder_tick(void)
// ----------------------------------------------------------------------------
//   Return the "ticks" as stored in the recorder
// ----------------------------------------------------------------------------
{
    static uintptr_t initialTick = 0;
    struct timeval t;
    gettimeofday(&t, NULL);
#if RECORDER_64BIT
    uintptr_t tick = t.tv_sec * 1000000ULL + t.tv_usec;
#else
    uintptr_t tick = t.tv_sec * 1000ULL + t.tv_usec / 1000;
#endif
    if (!initialTick)
        initialTick = tick;
    return tick - initialTick;
}
#endif // recorder_tick


void recorder_activate (recorder_info *recorder)
// ----------------------------------------------------------------------------
//   Activate the given recorder by putting it in linked list
// ----------------------------------------------------------------------------
{
    if (recorder->next)
    {
        record(recorder_error, "Re-activating %+s (%p)",
               recorder->name, recorder);
        return;
    }
    record(recorder, "Activating '%+s' (%p)", recorder->name, recorder);

    // Lock-free insertion. Note that compare_exchange updates head if it fails
    recorder_info  *head = recorders;
    do { recorder->next = head; }
    while (!recorder_ring_compare_exchange(recorders, head, recorder));
}


void recorder_tweak_activate (recorder_tweak *tweak)
// ----------------------------------------------------------------------------
//   Activate the given recorder by putting it in linked list
// ----------------------------------------------------------------------------
{
    if (tweak->next)
    {
        record(recorder_error, "Re-activating tweak %+s (%p)",
               tweak->name, tweak);
        return;
    }
    record(recorder, "Activating tweak '%+s' (%p)", tweak->name, tweak);

    // Lock-free insertion. Note that compare_exchange updates head if it fails
    recorder_tweak  *head = tweaks;
    do { tweak->next = head; }
    while (!recorder_ring_compare_exchange(tweaks, head, tweak));
}




// ============================================================================
//
//    Recorder sharing
//
// ============================================================================

RECORDER_TWEAK_DEFINE(recorder_export_size, 2048,
                      "Number of samples stored when exporting records");
static recorder_chans_p chans = NULL;

static const char *recorder_type_name[] =
// ----------------------------------------------------------------------------
//   Name for each of the types in the recorder exported channels
// ----------------------------------------------------------------------------
{
    "NONE",              // Nothing exported (pending format)
    "INVALID",           // Invalid data
    "SIGNED",            // Signed value
    "UNSIGNED",          // Unsigned value
    "REAL"               // Real number
};


void recorder_trace_entry(recorder_info *info, recorder_entry *entry)
// ----------------------------------------------------------------------------
//   Show a recorder entry when a trace is enabled
// ----------------------------------------------------------------------------
{
    unsigned i;

    // Dump entry if it's not just exported to shared memory
    if (info->trace != RECORDER_CHAN_MAGIC)
        recorder_dump_entry(info, entry,
                            recorder_format, recorder_show, recorder_output);

    // Export channels to shared memory
    for (i = 0; i < array_size(info->exported); i++)
    {
        recorder_chan_p exported = info->exported[i];
        if (exported)
        {
            recorder_shan_p  shan   = recorder_shared(exported);
            recorder_ring_p  ring   = &shan->ring;
            ringidx_t        writer = recorder_ring_fetch_add(ring->writer, 1);
            recorder_data   *data   = (recorder_data *) (ring + 1);
            size_t           size   = ring->size;
            recorder_type    none   = RECORDER_NONE;

            record(recorder, "Channel #%u '%+s' type %u %+s",
                   i,
                   (const char *) shan + shan->name,
                   shan->type,
                   shan->type < array_size(recorder_type_name)
                   ? recorder_type_name[shan->type]
                   : "UNGOOD");

            if (recorder_ring_compare_exchange(shan->type, none,
                                               RECORDER_INVALID))
                shan->type = recorder_type_from_format(entry->format, i);

            data += 2 * (writer % size);
            data[0].unsigned_value = entry->timestamp;
            data[1].unsigned_value = entry->args[i];
            recorder_ring_fetch_add(ring->commit, 1);
        }
    }
}


const char *recorder_export_file(void)
// ----------------------------------------------------------------------------
//    Return the name of the file used for sharing data across processes
// ----------------------------------------------------------------------------
{
    const char *result = getenv("RECORDER_SHARE");
    if (!result)
        result = "/tmp/recorder_share";
    return result;
}


static void recorder_atexit_cleanup(void)
// ----------------------------------------------------------------------------
//   Cleanup when exiting the program
// ----------------------------------------------------------------------------
{
    recorder_chans_delete(chans);
}


RECORDER_TWEAK_DEFINE(recorder_configuration_sleep, 100,
                      "Sleep time between configuration checks (ms)");
static void *background_configuration_check(void *ignored)
// ----------------------------------------------------------------------------
//    Check if there is a configuration command and apply it
// ----------------------------------------------------------------------------
{
    char buffer[RECORDER_CMD_LEN];
    while (chans)
    {
        recorder_shans *shans = chans->map_addr;
        size_t cmdlen = recorder_ring_readable(&shans->commands, NULL);
        if (cmdlen)
        {
            record(recorder, "Got shared-memory command len %zu", cmdlen);
            recorder_ring_read(&shans->commands,buffer,cmdlen,NULL,NULL,NULL);
            buffer[cmdlen] = 0;
            recorder_trace_set(buffer);
        }
        else
        {
            struct timespec tm;
            tm.tv_sec  = 0;
            tm.tv_nsec = 1000000 * RECORDER_TWEAK(recorder_configuration_sleep);
            nanosleep(&tm, NULL);
        }
    }
    return ignored ? NULL : NULL;
}


static void recorder_share(const char *path)
// ----------------------------------------------------------------------------
//   Share to the given name
// ----------------------------------------------------------------------------
{
    bool had_chans = chans != NULL;
    if (chans)
        recorder_chans_delete(chans);
    chans = recorder_chans_new(path);
    if (!had_chans && chans)
    {
        pthread_t tid;
        atexit(recorder_atexit_cleanup);
        pthread_create(&tid, NULL, background_configuration_check, NULL);
        record(recorder, "Started background configuration thread\n");
    }
}


static void recorder_export(recorder_info *rec, const char *value, bool multi)
// ----------------------------------------------------------------------------
//   Export channels in the given recorder with the given names
// ----------------------------------------------------------------------------
{
    if (!chans)
    {
        recorder_share(recorder_export_file());
        if (!chans)
            return;
    }

    char *names = strdup(value);
    char *next  = names;
    int   t;
    for (t = 0; next && t < (int) array_size(rec->exported); t++)
    {
        char *name = next;
        next = strchr(next, ',');
        if (next)
        {
            *next = 0;
            next++;
        }

        recorder_chan_p chan = rec->exported[t];
        size_t size = RECORDER_TWEAK(recorder_export_size);
        recorder_data min, max;
        min.signed_value = 0;
        max.signed_value = 0;

        char *chan_name = name;
        if (multi)
        {
            chan_name = malloc(strlen(rec->name) + strlen(name) + 2);
            sprintf(chan_name, "%s/%s", rec->name, name);
        }

        record(recorder, "Exporting channel %+s for index %u in %+s\n",
               name, t, rec->name);
        if (!chan || strcmp(recorder_chan_name(chan), name) != 0)
        {
            if (chan)
                recorder_chan_delete(chan);
            chan = recorder_chan_new(chans, RECORDER_NONE, size,
                                     chan_name, rec->description, "", min, max);
        }
        rec->exported[t] = chan;
        if (multi)
            free(chan_name);

        if (rec->trace == 0)
            rec->trace = RECORDER_CHAN_MAGIC;
    }

    free(names);
}


int recorder_trace_set(const char *param_spec)
// ----------------------------------------------------------------------------
//   Activate given traces
// ----------------------------------------------------------------------------
{
    const char     *next = param_spec;
    char            buffer[128];
    int             rc   = RECORDER_TRACE_OK;
    recorder_info  *rec;
    recorder_tweak *tweak;

    // Facilitate usage such as: recorder_trace_set(getenv("RECORDER_TRACES"))
    if (!param_spec)
        return 0;

    record(recorder_traces, "Setting traces to %s", param_spec);

    // Loop splitting input at ':' and ' ' boundaries
    do
    {
        // Default value is 1 if not specified
        intptr_t    value     = 1;
        char       *param     = (char *) next;
        const char *original  = param;
        char       *value_ptr = NULL;
        char       *alloc     = NULL;
        char       *end       = NULL;
        bool        numerical = true;
        size_t      len       = 0;

        // Split foo:bar:baz so that we consider only foo in this loop
        next = strpbrk(param, ": ");
        if (next)
        {
            len = next - param;
            if (len < sizeof(buffer)-1U)
            {
                memcpy(buffer, param, len);
                param = buffer;
            }
            else
            {
                alloc = malloc(len + 1);
                memcpy(alloc, param, len);
                param = alloc;
            }
            param[len] = 0;
            next++;
        }
        else
        {
            len = strlen(param);
        }

        // Check if we have an explicit value (foo=1), otherwise use default
        value_ptr = strchr(param, '=');
        if (value_ptr)
        {
            size_t offset = value_ptr - param;
            if (!alloc && param != buffer)
            {
                if (len < sizeof(buffer)-1U)
                {
                    memcpy(buffer, param, len);
                    param = buffer;
                }
                else
                {
                    alloc = malloc(len + 1);
                    memcpy(alloc, param, len);
                    param = alloc;
                }
                param[len] = 0;
            }
            value_ptr = param + offset;
            *value_ptr++ = 0;
            numerical = isdigit(*value_ptr) || *value_ptr == '-';
            if (numerical)
            {
                value = (intptr_t) strtol(value_ptr, &end, 0);
                if (*end != 0)
                {
                    rc = RECORDER_TRACE_INVALID_VALUE;
                    record(recorder_traces,
                           "Invalid numerical value %s (ends with %c)",
                           original + (value_ptr - param),
                           *end);
                }
            }
        }

        // Check recorders that match the given regexp.
        // We start with recorder names, so that if a recorder named
        // 'help' exists, it is considered instead of 'help command.
        // To force the command to be considered, you need @help
        unsigned matches = 0;
        if (*param != '@')
        {
            if (strcmp(param, "all") == 0)
                param = (char *) ".*";

            pattern_t re;
            int status = pattern_comp(&re, param);
            if (status == 0)
            {
                if (numerical)
                {
                    // Numerical value: set the corresponding trace
                    for (rec = recorders; rec; rec = rec->next)
                    {
                        bool re_result = pattern_match(&re, rec->name);
                        record(recorder_traces, "Numerical testing %+s = %+s",
                               rec->name, re_result ? "YES" : "NO");
                        if (re_result)
                        {
                            record(recorder_traces,
                                   "Set %+s from %ld to %ld",
                                   rec->name, rec->trace, value);
                            rec->trace = value;
                            matches++;
                        }
                    }
                    for (tweak = tweaks; tweak; tweak = tweak->next)
                    {
                        bool re_result = pattern_match(&re, tweak->name);
                        if (re_result)
                        {
                            record(recorder_traces,
                                   "Set tweak %+s from %ld to %ld",
                                   tweak->name, tweak->trace, value);
                            tweak->trace = value;
                            matches++;
                        }
                    }
                }
                else
                {
                    // Non-numerical: Activate corresponding exports
                    for (rec = recorders; rec; rec = rec->next)
                        if (pattern_match(&re, rec->name))
                            matches++;

                    for (rec = recorders; rec; rec = rec->next)
                    {
                        bool re_result = pattern_match(&re, rec->name);
                        record(recorder_traces, "Textual testing %+s = %+s",
                               rec->name, re_result ? "YES" : "NO");
                        if (re_result)
                        {
                            record(recorder_traces,
                                   "Share %+s under name %s",
                                   rec->name, value_ptr);
                            recorder_export(rec, value_ptr, matches > 1);
                        }
                    }
                }
            }
            else
            {
                rc = RECORDER_TRACE_INVALID_NAME;
#if HAVE_REGEX_H
                static char     error[128];
                regerror(status, &re, error, sizeof(error));
                record(recorder_traces, "regcomp returned %d: %s",
                       status, error);
#endif // HAVE_REGEX_H
            }
            pattern_free(&re);
        }
        else
        {
            // We got @ at beginning of parameter: this must be a command
            param++;
        }

        // Check if we already matched a parameter, otherwise check commands
        if (matches > 0)
        {
            // We found matching parameters, don't process command
            record(recorder_traces, "%u %+s impacted", matches,
                   matches > 1 ? "traces were" : "trace was");
        }
        else if (strcmp(param, "help") == 0 || strcmp(param, "list") == 0)
        {
            fprintf(stderr, "List of available recorders:\n");
            for (rec = recorders; rec; rec = rec->next)
                if (rec->trace <= 1)
                    fprintf(stderr, "%20s%s: %s\n",
                           rec->name, rec->trace ? "*" : " ",
                           rec->description);
                else
                    fprintf(stderr, "%20s : %s = %"PRIdPTR" (0x%"PRIXPTR")\n",
                           rec->name,
                           rec->description,
                           rec->trace, rec->trace);

            fprintf(stderr, "List of available tweaks:\n");
            for (tweak = tweaks; tweak; tweak = tweak->next)
                fprintf(stderr, "%20s : %s = %"PRIdPTR" (0x%"PRIXPTR") \n",
                        tweak->name, tweak->description,
                        tweak->trace, tweak->trace);
        }
        else if (strcmp(param, "share") == 0)
        {
            if (value_ptr)
                recorder_share(value_ptr);
            else
                record(recorder_traces, "No argument to 'share', ignored");
        }
        else if (strcmp(param, "dump") == 0)
        {
            recorder_dump();
        }
        else if (strcmp(param, "traces") == 0)
        {
            for (rec = recorders; rec; rec = rec->next)
                fprintf(stderr, "Recorder %s trace %"PRIdPTR" (0x%"PRIXPTR")\n",
                        rec->name, rec->trace, rec->trace);
        }
        else if (strcmp(param, "output") == 0 ||
                 strcmp(param, "output_append") == 0)
        {
            if (recorder_show != recorder_print)
            {
                record(recorder_warning,
                       "Not changing output for unknown recorder_show");
            }
            else if (value_ptr == NULL)
            {
                record(recorder_warning,
                       "output / output_append expect a file name");
            }
            else
            {
                const char *mode = param[sizeof("output")-1] == '_' ? "a" : "w";
                FILE *f = fopen(value_ptr, mode);
                if (f != NULL)
                {
#if HAVE_SETLINEBUF
                    setlinebuf(f);
#endif // HAVE_SETLINEBUF
                    f = recorder_configure_output(f);
                    if (f)
                        fclose(f);
                }
            }
        }
        else
        {
            record(recorder_warning, "Nothing matched %s", param);
        }

        if (alloc)
            free(alloc);
    } while (next);

    return rc;
}
