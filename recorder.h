#ifndef RECORDER_H
#define RECORDER_H
// ****************************************************************************
//  recorder.h                                               Recorder project
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
#include <stdarg.h>
#include <stdint.h>

#ifdef __cplusplus
// If a C++ program includes 'recorder.h', switch to the C++ version
#include "recorder.hpp"


#else // ! __cplusplus

// ============================================================================
// 
//   Global recorder dump functions, for use within a debugger
//
// ============================================================================
//
//  Within gdb, you can use: 'p recorder_dump()' to get a dump of what
//  happened in your program until this point
//

// Configuration of the function used to dump the recorder
typedef unsigned (*recorder_show_fn) (const char *ptr, unsigned length);
extern recorder_show_fn recorder_show;
extern unsigned recorder_show_to_stderr(const char *ptr, unsigned length);

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
extern void Name##_record(const char *format, unsigned count, ...);

// Some ugly macro drudgery to make things easy to use
// You see an error with this macro if you pass too many arguments
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

typedef struct recorder_entry_s
/// ---------------------------------------------------------------------------
///   Entry in the flight recorder.
///----------------------------------------------------------------------------
///  Notice that the arguments are stored as "intptr_t" because that type
///  is guaranteed to be the same size as a pointer. This allows us to
///  properly align recorder entries to powers of 2 for efficiency.
///  Also read explanations of \ref D2I and \ref F2I below regarding
///  how to use floating-point with the recorder.
{
    const char *format;         ///< Printf-style format for record
    uintptr_t   order;          ///< Global order of events (across recorders)
    uintptr_t   timestamp;      ///< Time at which record took place
    uintptr_t   where;          ///< Location (e.g. program counter)
    intptr_t    args[4];        ///< Five arguments, for a total of 8 fields
} recorder_entry;


/// A global counter indicating the order of entries across recorders.
/// This is incremented atomically for each RECORD() call.
/// It must be exposed because all XYZ_record() implementations need to
/// touch the same shared variable in order to provide a global order.
extern unsigned recorder_order;

/// A counter of how many clients are currently blocking the recorder.
/// Code that needs to block the recorder for any reason can atomically
/// increase this on entry, and atomically decrease this on exit.
/// Typically, functions like \ref recorder_dump block the recorder
/// while dumping in order to maximize the amount of "relevant" data
/// they can show. This variable needs to be public because all
/// recording functions need to test it.
extern unsigned recorder_blocked;

/// Generic recorder type, used for common code, e.g. \ref recorder_dump
RING_TYPE_DECLARE(recorder, recorder_entry, 4);

/// A function pointer type used by generic code to read each recorder.
/// \param entries will be filled with the entries read from the recorder.
/// \param count is the maximum number of entries that can be read.
/// \return the number of recorder entries read.
typedef unsigned (*recorder_read_fn)(recorder_entry *entries, unsigned count);

/// A function pointer type used by generic code to peek into each recorder.
/// \param entries will be filled with the first readable entry.
/// \return the read index of the entry that was read.
typedef unsigned (*recorder_peek_fn)(recorder_entry *entry);

/// A function pointer type used by generic code to count readable items.
/// \return the number of recorder entries that can safely be read.
typedef unsigned (*recorder_readable_fn)();


typedef struct recorder_list_s
///----------------------------------------------------------------------------
///   A linked list of the activated recorders.
///----------------------------------------------------------------------------
///   The first time you write into a recorder, it is 'activated' by placing
///   it on a linked list that will be then traversed by functions like
///   \see recorder_dump. The content of this structure is automatically
///   generated by the \ref RECORDER_DEFINE macro.
{
    const char *            name;       ///< Name of this recorder (generated)
    recorder_read_fn        read;       ///< Read entries
    recorder_peek_fn        peek;       ///< Peek first readable entry
    recorder_readable_fn    readable;   ///< Count readable entries
    unsigned                size;       ///< Size (maximum number of entries)
    struct recorder_list_s  *next;      ///< Pointer to next in list
} recorder_list;


/// Activate a recorder, e.g. because something was written in it.
/// An active recorder is processed during \ref recorder_dump.
extern void recorder_activate(recorder_list *recorder);

/// Dump a single recorder.
/// This is more of an internal function used by \ref recorder_dump,
/// but it may be useful to dump a specific recorder in some cases.
extern void recorder_dump_one(recorder_list *recorder,
                              recorder_show_fn show);



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

#endif // __cplusplus

#endif // RECORDER_H
