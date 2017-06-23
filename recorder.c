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
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <regex.h>
#include <sys/time.h>



// ============================================================================
//
//    Recorder dump utility
//
// ============================================================================

/// Global counter indicating the order of entries across recorders.
uintptr_t       recorder_order   = 0;

/// Counter of how many clients are currently blocking the recorder.
unsigned        recorder_blocked = 0;

/// List of the currently active flight recorders (ring buffers)
recorder_info * recorders        = NULL;

/// List of the currently active tweaks
recorder_tweak *tweaks           = NULL;


static void recorder_dump_entry(const char         *label,
                                recorder_entry     *entry,
                                recorder_format_fn  format,
                                recorder_show_fn    show,
                                void               *output)
// ----------------------------------------------------------------------------
//  Dump a recorder entry in a buffer between dst and dst_end, return last pos
// ----------------------------------------------------------------------------
{
    char            buffer[256];
    char            format_buffer[32];
    char           *dst         = buffer;
    char           *dst_end     = buffer + sizeof buffer - 2;
    const char     *fmt         = entry->format;
    unsigned        argIndex    = 0;
    const unsigned  maxArgIndex = sizeof(entry->args) / sizeof(entry->args[0]);

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
    if (!finishedInNewline)
        *dst++ = '\n';
    *dst++ = 0;

    format(show, output, label,
           entry->where, entry->order, entry->timestamp, buffer);
}



// ============================================================================
//
//    Default output prints things to stderr
//
// ============================================================================

static void * recorder_output = NULL;
void *recorder_configure_output(void *output)
// ----------------------------------------------------------------------------
//   Configure the output stream
// ----------------------------------------------------------------------------
{
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
    return (unsigned) fwrite(ptr, 1, len, file);
}


static recorder_show_fn recorder_show = recorder_print;
recorder_show_fn  recorder_configure_show(recorder_show_fn show)
// ----------------------------------------------------------------------------
//   Configure the function used to output data to the stream
// ----------------------------------------------------------------------------
{
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

static void recorder_format_entry(recorder_show_fn show,
                                  void *output,
                                  const char *label,
                                  const char *location,
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

    if (UINTPTR_MAX >= 0x7fffffff) // Static if to detect how to display time
    {
        // Time stamp in us, show in seconds
        dst += snprintf(dst, dst_end - dst,
                        "%s: [%lu %.6f] %s: %s",
                        location,
                        (unsigned long) order,
                        (double) timestamp / RECORDER_HZ,
                        label, message);
    }
    else
    {
        // Time stamp  in ms, show in seconds
        dst += snprintf(dst, dst_end - dst,
                        "%s: [%lu %.3f] %s: %s",
                        location,
                        (unsigned long) order,
                        (float) timestamp / RECORDER_HZ,
                        label, message);
    }

    show(buffer, dst - buffer, output);
}


static recorder_format_fn recorder_format = recorder_format_entry;
recorder_format_fn recorder_configure_format(recorder_format_fn format)
// ----------------------------------------------------------------------------
//   Configure the function used to format entries
// ----------------------------------------------------------------------------
{
    recorder_format_fn previous = recorder_format;
    recorder_format = format;
    return previous;
}


void recorder_sort(const char *what,
                   recorder_format_fn format,
                   recorder_show_fn show, void *output)
// ----------------------------------------------------------------------------
//   Dump all entries, sorted by their global 'order' field
// ----------------------------------------------------------------------------
{
    ring_fetch_add(recorder_blocked, 1);

    recorder_entry entry;
    regex_t        re;

    int status = regcomp(&re, what, REG_EXTENDED|REG_NOSUB|REG_ICASE);

    for(;;)
    {
        uintptr_t      lowestOrder = ~0UL;
        recorder_info *lowest      = NULL;
        recorder_info *rec;

        for (rec = recorders; rec; rec = rec->next)
        {
            // Skip recorders that don't match the pattern
            if (status == 0 && regexec(&re, rec->name, 0, NULL, 0) != 0)
                continue;

            // Loop while this recorder is readable and we can find next order
            if (rec->readable())
            {
                rec->peek(&entry);
                if (entry.order < lowestOrder)
                {
                    lowest = rec;
                    lowestOrder = entry.order;
                }
            }
        }

        if (!lowest)
            break;

        // The first read may fail due to 'catch up', if so continue
        if (!lowest->read(&entry, 1))
            continue;

        recorder_dump_entry(lowest->name, &entry, format, show, output);
    }

    regfree(&re);
    ring_fetch_add(recorder_blocked, -1);
}


void recorder_dump(void)
// ----------------------------------------------------------------------------
//   Dump all entries, sorted by their global 'order' field
// ----------------------------------------------------------------------------
{
    recorder_sort(".*", recorder_format, recorder_show, recorder_output);
}


void recorder_dump_for(const char *what)
// ----------------------------------------------------------------------------
//   Dump all entries for recorder with names matching 'what'
// ----------------------------------------------------------------------------
{
    recorder_sort(what, recorder_format, recorder_show, recorder_output);
}


void recorder_trace_entry(const char *label, recorder_entry *entry)
// ----------------------------------------------------------------------------
//   Show a recorder entry when a traqce is enabled
// ----------------------------------------------------------------------------
{
    recorder_dump_entry(label, entry,
                        recorder_format, recorder_show, recorder_output);
}



// ============================================================================
//
//    Signal handling
//
// ============================================================================

RECORDER(signals, 32, "Information about signals");

// Saved old actions
#if HAVE_STRUCT_SIGACTION
typedef void (*sig_fn)(int, siginfo_t *, void *);
static struct sigaction old_action[NSIG] = { };

static void signal_handler(int sig, siginfo_t *info, void *ucontext)
// ----------------------------------------------------------------------------
//    Dump the recorder when receiving a signal
// ----------------------------------------------------------------------------
{
    RECORD(signals, "Received signal %s (%d) si_addr=%p, dumping recorder",
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
    RECORD(signals, "Received signal %d, dumping recorder", sig);
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


RECORDER_TWEAK_DEFINE(recorder_signals,
                      RECORDER_SIGNALS_MASK,
                      "Default mask for signals");


void recorder_dump_on_common_signals(unsigned add, unsigned remove)
// ----------------------------------------------------------------------------
//    Easy interface to dump on the most common signals
// ----------------------------------------------------------------------------
{
    // Normally, this is called after constructors have run, so this is
    // a good time to check environment settings
    recorder_trace_set(getenv("RECORDER_TRACES"));

    unsigned sig;
    unsigned signals = (add | RECORDER_TWEAK(recorder_signals)) & ~remove;

    RECORD(signals, "Activating dump for signal mask 0x%X", signals);
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


void recorder_activate (recorder_info *recorder)
// ----------------------------------------------------------------------------
//   Activate the given recorder by putting it in linked list
// ----------------------------------------------------------------------------
{
    recorder_info  *head = recorders;
    do { recorder->next = head; }
    while (!ring_compare_exchange(recorders, head, recorder));
}


void recorder_tweak_activate (recorder_tweak *tweak)
// ----------------------------------------------------------------------------
//   Activate the given recorder by putting it in linked list
// ----------------------------------------------------------------------------
{
    recorder_tweak  *head = tweaks;
    do { tweak->next = head; }
    while (!ring_compare_exchange(tweaks, head, tweak));
}


RECORDER(recorder_trace_set, 64, "Setting recorder traces");

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
    regex_t         re;
    static char     error[128];

    // Facilitate usage such as: recorder_trace_set(getenv("RECORDER_TRACES"))
    if (!param_spec)
        return 0;

    if (strcmp(param_spec, "help") == 0 || strcmp(param_spec, "list") == 0)
    {
        printf("List of available recorders:\n");
        for (rec = recorders; rec; rec = rec->next)
            printf("%20s : %s\n", rec->name, rec->description);

        printf("List of available tweaks:\n");
        for (tweak = tweaks; tweak; tweak = tweak->next)
            printf("%20s : %s = %ld (0x%lX) \n",
                   tweak->name, tweak->description, tweak->tweak, tweak->tweak);

        return 0;
    }
    if (strcmp(param_spec, "all") == 0)
        next = param_spec = ".*";

    RECORD(recorder_trace_set, "Setting traces to %s", param_spec);

    // Loop splitting input at ':' and ' ' boundaries
    do
    {
        // Default value is 1 if not specified
        int value = 1;
        char *param = (char *) next;
        const char *original = param;
        const char *value_ptr = NULL;
        char *alloc = NULL;
        char *end = NULL;

        // Split foo:bar:baz so that we consider only foo in this loop
        next = strpbrk(param, ": ");
        if (next)
        {
            if (next - param < sizeof(buffer)-1)
            {
                memcpy(buffer, param, next - param);
                param = buffer;
            }
            else
            {
                alloc = malloc(next - param + 1);
                memcpy(alloc, param, next - param);
            }
            param[next - param] = 0;
            next++;
        }

        // Check if we have an explicit value (foo=1), otherwise use default
        value_ptr = strchr(param, '=');
        if (value_ptr)
        {
            if (param == buffer)
            {
                if (value_ptr - buffer < sizeof(buffer)-1)
                {
                    memcpy(buffer, param, value_ptr - param);
                    param = buffer;
                }
                else
                {
                    alloc = malloc(value_ptr - param + 1);
                    memcpy(alloc, param, value_ptr - param);
                }
            }
            param[value_ptr - param] = 0;
            value = strtol(value_ptr + 1, &end, 0);
            if (*end != 0)
            {
                rc = RECORDER_TRACE_INVALID_VALUE;
                RECORD(recorder_trace_set,
                       "Invalid numerical value %s (ends with %c)",
                       original + (value_ptr + 1 - param),
                       *end);
            }
        }

        int status = regcomp(&re, param, REG_EXTENDED|REG_NOSUB|REG_ICASE);
        if (status == 0)
        {
            for (rec = recorders; rec; rec = rec->next)
            {
                int re_result = regexec(&re, rec->name, 0, NULL, 0);
                if (re_result == 0)
                {
                    RECORD(recorder_trace_set, "Set %s from %ld to %ld",
                           rec->name, rec->trace, value);
                    rec->trace = value;
                }
            }
            for (tweak = tweaks; tweak; tweak = tweak->next)
            {
                int re_result = regexec(&re, tweak->name, 0, NULL, 0);
                if (re_result == 0)
                {
                    RECORD(recorder_trace_set, "Set tweak %s from %ld to %ld",
                           tweak->name, tweak->tweak, value);
                    tweak->tweak = value;
                }
            }
        }
        else
        {
            rc = RECORDER_TRACE_INVALID_NAME;
            regerror(status, &re, error, sizeof(error));
            RECORD(recorder_trace_set, "regcomp returned %d: %s",
                   status, error);
        }

        if (alloc)
            free(alloc);
    } while (next);

    return rc;
}
