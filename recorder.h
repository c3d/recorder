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

#if defined(__cplusplus) && !defined(RECORDER_HPP)
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
typedef unsigned (*recorder_show_fn) (const char *ptr, unsigned len, void *arg);

// Dump all recorder entries for all recorders, sorted between recorders
extern void recorder_dump(void);

// Dump all recorder entries matching recorders with 'what' in the name
extern void recorder_dump_for(const char *name);

// Sort all recorder entries for all recorders with names matching 'what'
extern void recorder_sort(const char *what, recorder_show_fn show, void *arg);

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
extern void recorder_##Name##_record(uintptr_t where,                   \
                                     const char *format,                \
                                     uintptr_t a0,                      \
                                     uintptr_t a1,                      \
                                     uintptr_t a2,                      \
                                     uintptr_t a3);


// Some ugly macro drudgery to make things easy to use.
// Convert types, pad with zeroes.
#define RECORDER_ARG(_1,_2,_3,_4, arg,...)              \
    _Generic(arg,                                       \
             unsigned char:     _recorder_unsigned,     \
             unsigned short:    _recorder_unsigned,     \
             unsigned:          _recorder_unsigned,     \
             unsigned long:     _recorder_unsigned,     \
             unsigned long long:_recorder_unsigned,     \
             signed char:       _recorder_signed,       \
             signed short:      _recorder_signed,       \
             signed:            _recorder_signed,       \
             signed long:       _recorder_signed,       \
             signed long long:  _recorder_signed,       \
             default:           _recorder_pointer,      \
             float:             _recorder_float,        \
             double:            _recorder_double) (arg)

#define RECORD(Name, Format, ...)                                       \
    recorder_##Name##_record(0,                                         \
                             Format,                                    \
                             RECORDER_ARG(0,0,0,0, ## __VA_ARGS__,0),   \
                             RECORDER_ARG(0,0,0,## __VA_ARGS__,0,0),    \
                             RECORDER_ARG(0,0,## __VA_ARGS__,0,0,0),    \
                             RECORDER_ARG(0,## __VA_ARGS__,0,0,0,0))

// Declare available recorders
#define RECORDER(Name, Size)        RECORDER_DECLARE(Name, Size)
#include "recorder.tbl"



// ============================================================================
//
//    Definition for recorders
//
// ============================================================================
//  Recorders in recorders.tbl are defined automatically, but you can
//  add your own by using e.g. RECORDER_DEFINE(MyRecorder, 256)

typedef struct recorder_entry
/// ---------------------------------------------------------------------------
///   Entry in the flight recorder.
///----------------------------------------------------------------------------
///  Notice that the arguments are stored as "intptr_t" because that type
///  is guaranteed to be the same size as a pointer. This allows us to
///  properly align recorder entries to powers of 2 for efficiency.
///  Also read explanations of \ref _recorder_double and \ref _recorder_float below regarding
///  how to use floating-point with the recorder.
{
    const char *format;         ///< Printf-style format for record
    uintptr_t   order;          ///< Global order of events (across recorders)
    uintptr_t   timestamp;      ///< Time at which record took place
    uintptr_t   where;          ///< Location (e.g. program counter)
    uintptr_t   args[4];        ///< Four arguments, for a total of 8 fields
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
RING_TYPE_DECLARE(recorder, recorder_entry);

/// A function pointer type used by generic code to read each recorder.
/// \param entries will be filled with the entries read from the recorder.
/// \param count is the maximum number of entries that can be read.
/// \return the number of recorder entries read.
typedef size_t (*recorder_read_fn)(recorder_entry *entries, size_t count);

/// A function pointer type used by generic code to peek into each recorder.
/// \param entries will be filled with the first readable entry.
/// \return the read index of the entry that was read.
typedef size_t (*recorder_peek_fn)(recorder_entry *entry);

/// A function pointer type used by generic code to count readable items.
/// \return the number of recorder entries that can safely be read.
typedef size_t (*recorder_readable_fn)();

/// The function type for recorder functions
typedef void recorder_record_fn(uintptr_t caller,
                                const char *format,
                                uintptr_t a0,
                                uintptr_t a1,
                                uintptr_t a2,
                                uintptr_t a3);


typedef struct recorder_list
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
    struct recorder_list   *next;      ///< Pointer to next in list
} recorder_list;


/// Activate a recorder, e.g. because something was written in it.
/// An active recorder is processed during \ref recorder_dump.
extern void recorder_activate(recorder_list *recorder);



// ============================================================================
//
//    Utility: Convert floating point values for vararg format
//
// ============================================================================
//
//   The recorder stores only uintptr_t in recorder entries. Integer types
//   are promoted, pointer types are converted. Floating point values
//   are converted a floating point type of the same size as uintptr_t,
//   i.e. float are converted to double on 64-bit platforms, and conversely.


static inline uintptr_t _recorder_unsigned(uintptr_t i)
// ----------------------------------------------------------------------------
//   Necessary because of the way generic selections work
// ----------------------------------------------------------------------------
{
    return i;
}


static inline uintptr_t _recorder_signed(intptr_t i)
// ----------------------------------------------------------------------------
//   Necessary because of the way generic selections work
// ----------------------------------------------------------------------------
{
    return (uintptr_t) i;
}


static inline uintptr_t _recorder_pointer(const void *i)
// ----------------------------------------------------------------------------
//   Necessary because of the way generic selections work
// ----------------------------------------------------------------------------
{
    return (uintptr_t) i;
}


static inline uintptr_t _recorder_float(float f)
// ----------------------------------------------------------------------------
//   Convert floating point number to intptr_t representation for recorder
// ----------------------------------------------------------------------------
{
    if (sizeof(float) == sizeof(intptr_t))
    {
        union { float f; uintptr_t i; } u;
        u.f = f;
        return u.i;
    }
    else
    {
        union { double d; uintptr_t i; } u;
        u.d = (double) f;
        return u.i;
    }
}


static inline uintptr_t _recorder_double(double d)
// ----------------------------------------------------------------------------
//   Convert double-precision floating point number to intptr_t representation
// ----------------------------------------------------------------------------
{
    if (sizeof(double) == sizeof(intptr_t))
    {
        union { double d; uintptr_t i; } u;
        u.d = d;
        return u.i;
    }
    else
    {
        // Better to lose precision than not store any data
        union { float f; uintptr_t i; } u;
        u.f = d;
        return u.i;
    }
}



// ============================================================================
//
//   Portability helpers
//
// ============================================================================

// Return ticks (some kind of time unit) since first called
extern uintptr_t recorder_tick(void);

// Compute the return address (may be different on different compilers)
#ifdef __GNUC__
#define recorder_return_address()       ((uintptr_t)__builtin_return_address(0))
#else
#warning "No return address on this compiler, implement recorder_return_address"
#define recorder_return_address()       0
#endif

#endif // __cplusplus

#endif // RECORDER_H
