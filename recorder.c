// ****************************************************************************
//  recorder.c                                                Recorder project
// ****************************************************************************
//
//   File Description:
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
// ****************************************************************************
//  (C) 2017 Christophe de Dinechin <christophe@dinechin.org>
//   This software is licensed under the GNU General Public License v3
//   See file LICENSE for details.
// ****************************************************************************

#include "recorder.h"
#include "config.h"
#include <string.h>
#include <stdio.h>
#include <stdbool.h>
#include <unistd.h>
#include <signal.h>
#include <sys/time.h>


#ifndef recorder_tick
uintptr_t recorder_tick()
// ----------------------------------------------------------------------------
//   Return the "ticks" as stored in the recorder
// ----------------------------------------------------------------------------
{
    static uintptr_t initialTick = 0;
    struct timeval t;
    gettimeofday(&t, NULL);
#if INTPTR_MAX < 0x8000000
    uintptr_t tick = t.tv_sec * 1000ULL + t.tv_usec / 1000;
#else
    uintptr_t tick = t.tv_sec * 1000000ULL + t.tv_usec;
#endif
    if (!initialTick)
        initialTick = tick;
    return tick - initialTick;
}
#endif // recorder_tick


#define RECORDER(Name, Size)        RECORDER_DEFINE(Name, Size)
#include "recorder.tbl"



// ============================================================================
//
//    Recorder dump utility
//
// ============================================================================

/// Global counter indicating the order of entries across recorders.
unsigned        recorder_order   = 0;

/// Counter of how many clients are currently blocking the recorder.
unsigned        recorder_blocked = 0;

/// List of the currently active flight recorders (ring buffers)
recorder_list * recorders        = NULL;


static unsigned recorder_print_to_stderr(const char *ptr, unsigned len,
                                         void *arg)
// ----------------------------------------------------------------------------
//   The default printing function - prints to stderr
// ----------------------------------------------------------------------------
{
    return (unsigned) write(2, ptr, len);
}


// Truly shocking that Visual Studio before 2015 does not have a working
// snprintf or vsnprintf. Note that the proposed replacements are not accurate
// since they return -1 on overflow instead of the length that would have
// been written as the standard mandates.
// http://stackoverflow.com/questions/2915672/snprintf-and-visual-studio-2010.
#if defined(_MSC_VER) && _MSC_VER < 1900
#  define snprintf  _snprintf
#endif

static void recorder_dump_entry(const char       *label,
                                recorder_entry   *entry,
                                recorder_show_fn  show,
                                void             *arg)
// ----------------------------------------------------------------------------
//  Dump a recorder entry in a buffer between dst and dst_end, return last pos
// ----------------------------------------------------------------------------
{
    static char buffer[1024];
    static char format_buffer[32];

    char *dst = buffer;
    char *dst_end = buffer + sizeof buffer;

    if (UINTPTR_MAX >= 0x7fffffff) // Static if to detect how to display time
    {
        // Time stamp in us, show in seconds
        dst += snprintf(dst, dst_end - dst,
                        "%lu [%lu.%06lu:%p] %s: ",
                        (unsigned long) entry->order,
                        (unsigned long) entry->timestamp / 1000000,
                        (unsigned long) entry->timestamp % 1000000,
                        (void *) entry->where,
                        label);
    }
    else
    {
        // Time stamp  in ms, show in seconds
        dst += snprintf(dst, dst_end - dst,
                        "%lu [%lu.%03lu:%p] %s: ",
                        (unsigned long) entry->order,
                        (unsigned long) entry->timestamp / 1000,
                        (unsigned long) entry->timestamp % 1000,
                        (void *) entry->where,
                 label);
    }
    const char *fmt = entry->format;
    unsigned argIndex = 0;
    const unsigned maxArgIndex = sizeof(entry->args) / sizeof(entry->args[0]);

    // Apply formatting. This complicated loop is because
    // we need to detect floating-point values, which are passed
    // differently on many architectures such as x86 or ARM
    // (passed in different registers). So we detect them from the format,
    // convert intptr_t to float or double depending on its size,
    // and call the variadic snprintf passing a double value that will
    // naturally go in the right register. A bit ugly.
    bool finishedInNewline = false;
    while (dst < dst_end && argIndex < maxArgIndex)
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
            char *fmtCopy = format_buffer;
            int floatingPoint = 0;
            int done = 0;
            int unsupported = 0;
            *fmtCopy++ = c;
            char *fmt_end = format_buffer + sizeof format_buffer - 1;
            while (!done && fmt < fmt_end)
            {
                c = *fmt++;
                *fmtCopy++ = c;
                switch(c)
                {
                case 'f': case 'F':  // Floating point formatting
                case 'g': case 'G':
                case 'e': case 'E':
                case 'a': case 'A':
                    floatingPoint = 1;
                    // Fall through here on purpose
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
                    done = 1;
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
                    unsupported = 1;
                    break;
                }
            }
            if (!c || unsupported)
                break;
            int isString = (c == 's' || c == 'S');
            *fmtCopy++ = 0;
            if (floatingPoint)
            {
                double floatArg;
                if (sizeof(intptr_t) == sizeof(float))
                {
                    union { float f; intptr_t i; } u;
                    u.i = entry->args[argIndex++];
                    floatArg = (double) u.f;
                }
                else
                {
                    union { double d; intptr_t i; } u;
                    u.i = entry->args[argIndex++];
                    floatArg = u.d;
                }
                dst += snprintf(dst, dst_end - dst,
                                format_buffer,
                                floatArg);
            }
            else
            {
                intptr_t arg = entry->args[argIndex++];
                if (isString && arg == 0)
                    arg = (intptr_t) "<NULL>";
                dst += snprintf(dst, dst_end - dst,
                                format_buffer, arg);
            }
        }
        finishedInNewline = c == '\n';
    }
    if (!finishedInNewline && dst < dst_end)
        *dst++ = '\n';
    show(buffer, dst - buffer, arg);
}


void recorder_sort(const char *what, recorder_show_fn show, void *arg)
// ----------------------------------------------------------------------------
//   Dump all entries, sorted by their global 'order' field
// ----------------------------------------------------------------------------
{
    ring_fetch_add(recorder_blocked, 1);

    uintptr_t      nextOrder = 0;
    recorder_entry entry;

    for(;;)
    {
        uintptr_t      lowestOrder = ~0UL;
        recorder_list *lowest      = NULL;
        recorder_list *rec;

        for (rec = recorders; rec; rec = rec->next)
        {
            // Skip recorders that don't match the pattern
            if (!strstr(rec->name, what))
                continue;

            // Loop while this recorder is readable and we can find next order
            while (rec->readable())
            {
                rec->peek(&entry);
                if (entry.order != nextOrder)
                {
                    if (entry.order < lowestOrder)
                    {
                        lowest = rec;
                        lowestOrder = entry.order;
                    }
                    break;
                }

                // We may have one read that fails due to overflow/catchup
                if (!rec->read(&entry, 1))
                    continue;

                recorder_dump_entry(rec->name, &entry, show, arg);
                nextOrder++;
            }
        }

        if (!lowest)
            break;

        // The first read may fail due to 'catch up', if so continue
        if (!lowest->read(&entry, 1))
            continue;

        recorder_dump_entry(lowest->name, &entry, show, arg);
        nextOrder = entry.order + 1;
    }

    ring_fetch_add(recorder_blocked, -1);
}


void recorder_dump(void)
// ----------------------------------------------------------------------------
//   Dump all entries, sorted by their global 'order' field
// ----------------------------------------------------------------------------
{
    recorder_sort("", recorder_print_to_stderr, NULL);
}


void recorder_dump_for(const char *what)
// ----------------------------------------------------------------------------
//   Dump all entries for recorder with names matching 'what'
// ----------------------------------------------------------------------------
{
    recorder_sort(what, recorder_print_to_stderr, NULL);
}


void recorder_activate (recorder_list *recorder)
// ----------------------------------------------------------------------------
//   Activate the given recorder by putting it in linked list
// ----------------------------------------------------------------------------
{
    /* This was the first write in this recorder, put it in list */
    recorder_list  *head = recorders;
    do { recorder->next = head; }
    while (!ring_compare_exchange(recorders, head, recorder));
}


// Saved old actions
#if HAVE_STRUCT_SIGACTION
typedef void (*sig_fn)(int, siginfo_t *, void *);
static struct sigaction old_action[NSIG] = { };

static void signal_handler(int sig, siginfo_t *info, void *ucontext)
// ----------------------------------------------------------------------------
//    Dump the recorder when receiving a signal
// ----------------------------------------------------------------------------
{
    RECORD(MAIN, "Received signal %s (%d) si_addr=%p, dumping recorder",
           strsignal(sig), sig, info->si_addr);
    fprintf(stderr, "Received signal %s (%d), dumping recorder\n",
            strsignal(sig), sig);

    // Restore previous handler in case we crash during the dump
    struct sigaction save, next;
    sigaction(sig, &old_action[sig], &save);
    recorder_dump();
    sigaction(sig, &save, &next);

    // If there is another handler, call it now
    if (next.sa_sigaction != (sig_fn) SIG_DFL &&
        next.sa_sigaction != (sig_fn) SIG_IGN)
        next.sa_sigaction(sig, info, ucontext);
}


void recorder_dump_on_signal(int sig)
// ----------------------------------------------------------------------------
//    C interface for Recorder::DumpOnSignal
// ----------------------------------------------------------------------------
{
    if (sig < 0 || sig >= NSIG)
        return;

    struct sigaction action;
    action.sa_sigaction = signal_handler;
    sigemptyset(&action.sa_mask);
    action.sa_flags = SA_SIGINFO;
    sigaction(sig, &action, &old_action[sig]);
}

#else // !HAVE_STRUCT_SIGACTION

/* For MinGW, there is no struct sigaction */
typedef void (*sig_fn)(int);
static sig_fn old_handler[NSIG] = { };

static void signal_handler(int sig)
// ----------------------------------------------------------------------------
//   Dump the recorder when receiving the given signal
// ----------------------------------------------------------------------------
{
    RECORD(MAIN, "Received signal %d, dumping recorder", sig);
    fprintf(stderr, "Received signal %d, dumping recorder\n", sig);

    // Restore previous handler
    sig_fn save = signal(sig, old_handler[sig]);
    recorder_dump();
    sig_fn next = signal(sig, save);

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
}

#endif // HAVE_STRUCT_SIGACTION


void recorder_dump_on_common_signals(unsigned add, unsigned remove)
// ----------------------------------------------------------------------------
//    Easy interface to dump on the most common signals
// ----------------------------------------------------------------------------
{
    unsigned sig;
    unsigned signals = add
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
        ;
    signals &= ~remove;

    for (sig = 0; signals; sig++)
    {
        unsigned mask = 1U << sig;
        if (signals & mask)
            recorder_dump_on_signal(sig);
        signals &= ~mask;
    }
}
