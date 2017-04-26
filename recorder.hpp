#ifndef RECORDER_HPP
#define RECORDER_HPP
// ****************************************************************************
//  recorder.hpp                                             Recorder project
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
// This document is released under the GNU General Public License version 3
// See http://www.gnu.org/copyleft/gpl.html and Matthew 25:22 for details
//  (C) 1992-2017 Christophe de Dinechin <christophe@dinechin.org>
// ****************************************************************************

#include "ring.h"


// ****************************************************************************
// 
//   Global recorder dump functions, for use within a debugger
//
// ****************************************************************************
//
//  Within gdb, you can use: 'p recorder_dump()' to get a dump of what
//  happened in your program until this point
//


#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

// Dump all recorder entries for all recorders, sorted between recorders
extern void recorder_dump(void);

// Dump all recorder entries for all recorders with names matchin 'select'    
extern void recorder_dump_for(const char *select);

// Dump recorder entries on signal
extern void recorder_dump_on_signal(int signal);

// Dump recorder entries on common signals (exact signals depend on platform)
// By default, when called with (0,0), this dumps for:
//    SIGQUIT, SIGILL, SIGABRT, SIGBUS, SIGSEGV, SIGSYS,
//    SIGXCPU, SIGXFSZ, SIGINFO, SIGUSR1, SIGUSR2, SIGSTKFLT, SIGPWR
// You can add add or remove signals by setting the bitmasks 'add' and 'remove'
extern void recorder_dump_on_common_signals(unsigned add, unsigned remove);
  

#ifdef __cplusplus
} // extern "C"

// ****************************************************************************
// 
//    C++ implementation
// 
// ****************************************************************************

#include <vector>
#include <iostream>


// ============================================================================
//
//    Higher-evel interface
//
// ============================================================================

struct Recorder
// ----------------------------------------------------------------------------
//    A base for all flight recorders, to store them in a linked list
// ----------------------------------------------------------------------------
{
    typedef std::ostream        ostream;

    // The following struct is where we keep the data.
    // Should be a power-of-two size on all regular architectures
    struct Entry
    {
        const char *format;     // printf-style format for record
        uintptr_t   order;      // Global order of events across recorders
        uintptr_t   timestamp;  // Time at which record occured
        void *      caller;     // Caller of the record (PC)
        intptr_t    args[4];    // Four additional arguments, total 8 fields

        ostream &Dump(ostream &out, const char *label);
    };

public:
    Recorder(): next(NULL) {}

    virtual const char *        Name()                      = 0;
    virtual unsigned            Size()                      = 0;
    virtual unsigned            Readable()                  = 0;
    virtual unsigned            Writeable()                 = 0;
    virtual unsigned            Peek(Entry &entry)          = 0;
    virtual bool                Read(Entry &entry)          = 0;
    virtual unsigned            Write(const Entry &entry)   = 0;
    void                        Link();
    Recorder *                  Next()  { return next; }

public:
    static Recorder *           Head()  { return head; }
    static ostream &            Dump(ostream &out, const char *pattern = "");
    static uintptr_t            Order();
    static uintptr_t            Now();

    static void                 Block()     { blocked++; }
    static void                 Unblock()   { blocked--; }
    static bool                 Blocked()   { return blocked > 0; }

    static void                 DumpOnSignal(int signal, bool install=true);
private:
    static std::atomic<Recorder *>  head;
    static std::atomic<uintptr_t>   order;
    static std::atomic<unsigned>    blocked;

private:
    Recorder *                      next;
};


inline std::ostream &operator<< (std::ostream &out, Recorder &fr)
// ----------------------------------------------------------------------------
//   Dump a single flight recorder entry on the given ostream
// ----------------------------------------------------------------------------
{
    return fr.Dump(out);
}


template <unsigned RecSize>
struct RecorderRing : private Recorder,
                      private Ring<Recorder::Entry, RecSize>
// ----------------------------------------------------------------------------
//    Record events in a circular buffer, up to Size events recorded
// ----------------------------------------------------------------------------
{
    typedef Recorder            Rec;
    typedef Rec::Entry          Entry;
    typedef Ring<Entry, RecSize>Buf;

    RecorderRing(const char *name): Recorder(), Buf(name) {}

    virtual const char *        Name()              { return Buf::Name(); }
    virtual unsigned            Size()              { return Buf::size; }
    virtual unsigned            Readable()          { return Buf::Readable(); }
    virtual unsigned            Writeable()         { return Buf::Writable(); }
    virtual unsigned            Peek(Entry &e)      { return Buf::Peek(e); }
    virtual bool                Read(Entry &e)      { return Buf::Read(e); }
    virtual unsigned            Write(const Entry&e){ return Buf::Write(e); }

public:
    struct Arg
    {
        Arg(float f):           value(f2i(f))               {}
        Arg(double d):          value(f2i(d))               {}
        template<class T>
        Arg(T t):               value(intptr_t(t))          {}

        operator intptr_t()     { return value; }

    private:
        template <typename float_type>
        static intptr_t f2i(float_type f)
        {
            if (sizeof(float) == sizeof(intptr_t))
            {
                union { float f; intptr_t i; } u;
                u.f = f;
                return u.i;
            }
            else
            {
                union { double d; intptr_t i; } u;
                u.d = f;
                return u.i;
            }
        }

    private:
        intptr_t                value;
    };

    void Record(const char *what, void *caller,
                Arg a1=0, Arg a2=0, Arg a3=0, Arg a4=0)
    {
        if (Rec::Blocked())
            return;
        Entry e = {
            what, Rec::Order(), Rec::Now(), caller,
            { a1, a2, a3, a4 }
        };
        unsigned writeIndex = Buf::Write(e);
        if (!writeIndex)
            Rec::Link();
    }
    
    void operator()(const char *what, Arg a1=0, Arg a2=0, Arg a3=0, Arg a4=0)
    {
        Record(what, __builtin_return_address(0), a1,a2,a3,a4);
    }
};



// ============================================================================
//
//   Inline functions for Recorder
//
// ============================================================================

inline uintptr_t Recorder::Order()
// ----------------------------------------------------------------------------
//   Generate a unique sequence number for ordering entries in recorders
// ----------------------------------------------------------------------------
{
    return order++;
}


// ============================================================================
//
//    Available recorders
//
// ============================================================================

#define RECORDER(Name, Size)    extern RecorderRing<Size> Name;
#include "recorder.tbl"

#define externc extern "C"


#else // !__cplusplus

#define externc extern

#endif // __cplusplus

// ****************************************************************************
// 
//     C interface
// 
// ****************************************************************************

#include <stdarg.h>
#include <stdint.h>



// ============================================================================
// 
//    Declaration of recorders
// 
// ============================================================================

#define RECORDER_DECLARE(Name, Size)                                    \
/* ----------------------------------------------------------------*/   \
/*  Declare a recorder type with Size elements                     */   \
/* ----------------------------------------------------------------*/   \
                                                                        \
externc void Name##_record(const char *format, unsigned count, ...);

// Some ugly macro drudgery to make things easy to use
#define RECORDER_COUNT_ARGS(...) RECORDER_COUNT_(,##__VA_ARGS__,4,3,2,1,0)
#define RECORDER_COUNT_(z,_1,_2,_3,_4, cnt, ...) cnt

#define RECORD(Name, Format, ...)                                       \
    Name##_record(Format,                                               \
                  RECORDER_COUNT_ARGS(__VA_ARGS__),                     \
                  ## __VA_ARGS__)


#define RECORDER(Name, Size)        RECORDER_DECLARE(Name, Size)
#include "recorder.tbl"



// ============================================================================
// 
//    Definition for recorders
// 
// ============================================================================
//  Recorders in recorders.tbl are defined automatically, but you can
//  add your own by using e.g. RECORDER_DEFINE(MyRecorder, 256)

#ifdef __cplusplus

#define RECORDER_DEFINE(Name, Size)                                     \
/*!----------------------------------------------------------------*/   \
/*! Define a recorder type with Size elements                      */   \
/*!----------------------------------------------------------------*/   \
/*! \param Name is the C name fo the recorder.                          \
 *! \param Size is the number of entries in the circular buffer. */     \
                                                                        \
void Name##_record(const char *format, unsigned count, ...)             \
/* ----------------------------------------------------------------*/   \
/*  Enter a record in a ring buffer with given set of args         */   \
/* ----------------------------------------------------------------*/   \
{                                                                       \
    intptr_t args[4] = { 0 };                                           \
    va_list ap;                                                         \
    va_start(ap, count);                                                \
    const unsigned max = sizeof(args) / sizeof(args[0]);                \
    for (unsigned i = 0; i < count && i < max; i++)                     \
        args[i] = va_arg(ap, intptr_t);                                 \
    Name.Record(format, __builtin_return_address(0),                    \
                args[0], args[1], args[2], args[3]);                    \
}

#endif // __cplusplus



// ============================================================================
//
//    Utility: Convert floating point values for vararg format
//
// ============================================================================
//
//   If you want to record a floating point value, you need to use F2I, as in:
//
//      RECORD(MyRecoder, "My FP value is %6.2f", F2I(3.1415));
//
//   On platforms such as x86_64 or ARM32, different registers are used to
//   pass floating-point values than integral or pointer values.
//   So when you call printf("%d %f %d", 1, 3.1415, 42), you may end up on
//   ARM32 with:
//   - R0 containing the format pointer
//   - R1 containing 1
//   - R2 containing 42
//   - D0 containing 3.1415
//   Since the recorder always uses intptr_t to represent the values,
//   the corresponding va_arg() call uses intptr_t, reading from R0-R3.
//   If we want to pass a floating-point value, we need to first convert it
//   to integral format to make sure that it is passed in Rn and not Dn.

static inline intptr_t F2I(float f)
// ----------------------------------------------------------------------------
//   Convert floating point number to intptr_t representation for recorder
// ----------------------------------------------------------------------------
{
    if (sizeof(float) == sizeof(intptr_t))
    {
        union { float f; intptr_t i; } u;
        u.f = f;
        return u.i;
    }
    else
    {
        union { double d; intptr_t i; } u;
        u.d = (double) f;
        return u.i;
    }
}


static inline intptr_t D2I(double d)
// ----------------------------------------------------------------------------
//   Convert double-precision floating point number to intptr_t representation
// ----------------------------------------------------------------------------
{
    if (sizeof(double) == sizeof(intptr_t))
    {
        union { double d; intptr_t i; } u;
        u.d = d;
        return u.i;
    }
    else
    {
        union { float f; intptr_t i; } u;
        u.f = d;
        return u.i;
    }
}

#endif // RECORDER_HPP
