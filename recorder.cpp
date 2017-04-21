// ****************************************************************************
//  flight_recorder.cpp                                       Recorder project
// ****************************************************************************
//
//   File Description:
//
//     Record information about what's going on in the application
//     (Lifted from ELFE / XL / Tao with specific improvements)
//
//
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
//  (C) 1992-2017 Christophe de Dinechin <christophe@taodyne.com>
//  (C) 2010 Taodyne SAS
// ****************************************************************************

#include "recorder.h"
#include <cstdio>
#include <cstring>
#include <time.h>
#include <fcntl.h>
#include <unistd.h>
#include <inttypes.h>
#include <sys/time.h>
#include <signal.h>

// Define all the recorders we need
#define RECORDER(Name, Size)    RecorderRing<Size>  Name(#Name);
#include "recorder.tbl"



// ============================================================================
// 
//    Recorder utilities
// 
// ============================================================================

std::atomic<Recorder *> Recorder::head;
std::atomic<uintptr_t>  Recorder::order;
std::atomic<unsigned>   Recorder::blocked;


uintptr_t Recorder::Now()
// ----------------------------------------------------------------------------
//    Give a high-resolution timer for the flight recorder
// ----------------------------------------------------------------------------
{
    struct timeval t;
    gettimeofday(&t, NULL);
    uintptr_t tick = 
        (UINTPTR_MAX >= 0x7fffffff)
        ? t.tv_sec * 1000000ULL + t.tv_usec
        : t.tv_sec * 1000ULL + t.tv_usec / 1000; // Wraps around in 49 days
    static uintptr_t initialTick = 0;
    if (!initialTick)
        initialTick = tick;
    return tick - initialTick;
}


void Recorder::Link()
// ----------------------------------------------------------------------------
//    Link a flight recorder that was activated into the list
// ----------------------------------------------------------------------------
{
    Recorder *next = head;
    do
    {
        this->next = next;
    } while (!head.compare_exchange_weak(next, this));
}


std::ostream &Recorder::Entry::Dump(ostream &out, const char *label)
// ----------------------------------------------------------------------------
//    Dump one recorder entry
// ----------------------------------------------------------------------------
{
    static char buffer[1024];
    static char format_buffer[32];

    if (UINTPTR_MAX >= 0x7fffffff) // Static if to detect how to display time
    {
        // Time stamp in us, show in seconds
        snprintf(buffer, sizeof(buffer),
                 "%lu [%lu.%06lu:%p] %s: ",
                 (unsigned long) order,
                 (unsigned long) timestamp / 1000000,
                 (unsigned long) timestamp % 1000000,
                 caller,
                 label);
        out << buffer;
    }
    else
    {
        // Time stamp  in ms, show in seconds
        snprintf(buffer, sizeof(buffer),
                 "%lu [%lu.%03lu:%p] %s: ",
                 (unsigned long) order,
                 (unsigned long) timestamp / 1000,
                 (unsigned long) timestamp % 1000,
                 caller,
                 label);
    }

    const char *fmt = format;
    char *dst = buffer;
    unsigned argIndex = 0;

    // Apply formatting. This complicated loop is because
    // we need to detect floating-point formats, which are passed
    // differently on many architectures such as x86 or ARM
    // (passed in different registers), and so we need to cast here.
    const char *end = buffer + sizeof buffer;
    bool finishedInNewline = false;
    while (dst < end)
    {
        char c = *fmt++;
        if (c != '%')
        {
            *dst++ = c;
            if (!c)
                break;
        }
        else
        {
            char *fmtCopy = format_buffer;
            int floatingPoint = 0;
            int done = 0;
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
                case 'n':           // Does not make sense here, but hey
                case 0:             // End of string
                    done = 1;
                    break;
                case '0' ... '9':
                case '+':
                case '-':
                case '.':
                case 'l': case 'L':
                case 'h':
                case 'j':
                case 't':
                case 'z':
                case 'q':
                case 'v':
                    break;
                }
            }
            if (!c)
                break;
            *fmtCopy++ = 0;
            if (floatingPoint)
            {
                double floatArg;
                if (sizeof(intptr_t) == sizeof(float))
                {
                    union { float f; intptr_t i; } u;
                    u.i = args[argIndex++];
                    floatArg = (double) u.f;
                }
                else
                {
                    union { double d; intptr_t i; } u;
                    u.i = args[argIndex++];
                    floatArg = u.d;
                }
                dst += snprintf(dst, end-dst, format_buffer, floatArg);
            }
            else
            {
                intptr_t intArg = args[argIndex++];
                dst += snprintf(dst, end-dst, format_buffer, intArg);
            }
        }
        finishedInNewline = c == '\n';
    }

    out << buffer;
    if (!finishedInNewline)
        out << '\n';

    return out;
}


std::ostream &Recorder::Dump(ostream &out, const char *pattern)
// ----------------------------------------------------------------------------
//   Dump all entries, sorted by their global 'order' field
// ----------------------------------------------------------------------------
//   IMPORTANT: Sorting only happens between recorders, not within a recorder.
//   It is possible and normal that two threads record something exactly
//   at the same time, and perform the commit within a single recorder in
//   a different order than they received their "order". For example, threads
//   T1 and T2 could receive orders 1 and 2, but write in the order 2 then 1.
//   In that case, the dump order will be 2 then 1.
{
    Block();

    uintptr_t      nextOrder = 0;
    Recorder::Entry entry;

    for(;;)
    {
        uintptr_t  lowestOrder = (uintptr_t) ~0ULL;
        Recorder  *lowest      = NULL;
        
        for (Recorder *rec = Head(); rec; rec = rec->Next())
        {
            // Skip recorders that do not match pattern
            if (!strstr(rec->Name(), pattern))
                continue;
            
            // Loop while this recorder is readable and we can find next order
            while (rec->Readable())
            {
                rec->Peek(entry);
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
                if (!rec->Read(entry))
                    continue;
                    
                entry.Dump(out, rec->Name());
                nextOrder++;
            }
        }

        if (!lowest)
            break;

        // The first read may fail due to 'catch up', if so continue
        if (!lowest->Read(entry))
            continue;

        entry.Dump(out, lowest->Name());
        nextOrder = entry.order + 1;
    }
    
    Unblock();

    return out;
}


static void signal_handler(int sig)
// ----------------------------------------------------------------------------
//    Dump the recorder when receiving a signal
// ----------------------------------------------------------------------------
{
    RECORD(MAIN, "Received signal %d", sig);
    std::cerr << "Received signal " << sig << ", dumping recorder\n";
    signal(sig, SIG_DFL);
    recorder_dump();
}


void Recorder::DumpOnSignal(int sig, bool install)
// ----------------------------------------------------------------------------
//    Install a signal handler for the given signal that dumps the recorder
// ----------------------------------------------------------------------------
{
    if (install)
        signal(sig, signal_handler);
    else
        signal(sig, SIG_DFL);
}


void recorder_dump()
// ----------------------------------------------------------------------------
//   Dump the recorder to standard error (for use in the debugger)
// ----------------------------------------------------------------------------
{
    recorder_dump_for("");
}



void recorder_dump_for(const char *select)
// ----------------------------------------------------------------------------
//   Dump one specific recorder to standard error
// ----------------------------------------------------------------------------
{
    Recorder::Dump(std::cerr, select);
}


void recorder_dump_on_signal(int sig)
// ----------------------------------------------------------------------------
//    C interface for Recorder::DumpOnSignal
// ----------------------------------------------------------------------------
{
    Recorder::DumpOnSignal(sig);
}


void recorder_dump_on_common_signals(unsigned add, unsigned remove)
// ----------------------------------------------------------------------------
//    Easy interface to dump on the most common signals
// ----------------------------------------------------------------------------
{
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

    for (unsigned sig = 0; signals; sig++)
    {
        unsigned mask = 1U << sig;
        if (signals & mask)
            recorder_dump_on_signal(sig);
        signals &= ~mask;
    }
}



// ****************************************************************************
// 
//    C interface for the recorder
// 
// ****************************************************************************

#define RECORDER(Name, Size)    RECORDER_DEFINE(Name, Size)
#include "recorder.tbl"
