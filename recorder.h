#ifndef RECORDER_H
#define RECORDER_H
// *****************************************************************************
// recorder.h                                                   Recorder project
// *****************************************************************************
//
// File description:
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
// *****************************************************************************
// This software is licensed under the GNU General Public License v3
// (C) 2017-2019, Christophe de Dinechin <christophe@dinechin.org>
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

#include "recorder_ring.h"
#include <stdarg.h>
#include <stdint.h>


#ifdef __cplusplus
extern "C" {
#endif // __cplusplus


// ============================================================================
//
//   Information about the API
//
// ============================================================================

#define RECORDER_CURRENT_VERSION                RECORDER_VERSION(1,0,5)
#define RECORDER_VERSION(majr,minr,ptch)        ((majr)<<16|((minr)<<8|(ptch)))
#define RECORDER_VERSION_MAJOR(version)         (((version) >> 16) & 0xFF)
#define RECORDER_VERSION_MINOR(version)         (((version) >>  8)& 0xFF)
#define RECORDER_VERSION_PATCH(version)         (((version) >>  0)& 0xFF)
#define RECORDER_64BIT                          (INTPTR_MAX > 0x7fffffff)


// ============================================================================
//
//   Recorder dump, to use in fault handlers or within a debugger
//
// ============================================================================
//  Within gdb, you can use: 'p recorder_dump()' to get a dump of what
//  happened in your program until this point
//
//  Ownership of `const char *` strings:
//  In general, when a 'const char *' argument is given, it is expected
//  that its lifetime will be sufficient for the duration of all its uses.
//  The application is responsible for enforcing this rule. In particular,
//      recorder_traces_set(getenv("RECORDER_TRACES"));
//  is considered a correct usage. Overwriting the environment
//  afterwards is considered an evil and bogus mis-use of the library.

// Dump all recorder entries for all recorders, sorted between recorders
extern unsigned recorder_dump(void);

// Dump all recorder entries with a name matching regular expression 'what'
extern unsigned recorder_dump_for(const char *what);

// Dump recorder entries on signal
extern void recorder_dump_on_signal(int signal);

// Dump recorder entries on common signals (exact signals depend on platform)
// By default, when called with (0,0), this dumps for:
//    SIGQUIT, SIGILL, SIGABRT, SIGBUS, SIGSEGV, SIGSYS,
//    SIGXCPU, SIGXFSZ, SIGINFO, SIGUSR1, SIGUSR2, SIGSTKFLT, SIGPWR
// You can add add or remove signals by setting the bitmasks 'add' and 'remove'
extern void recorder_dump_on_common_signals(unsigned add, unsigned remove);


// Configuration of the function used to dump the recorder
typedef unsigned (*recorder_show_fn) (const char *text,size_t len,void *output);
typedef void (*recorder_format_fn)(recorder_show_fn show,
                                   void *output,
                                   const char *label,
                                   const char *location,
                                   uintptr_t order,
                                   uintptr_t timestamp,
                                   const char *message);
typedef size_t (*recorder_type_fn)(intptr_t trace,
                                   const char *format,
                                   char *buffer, size_t length,
                                   uintptr_t data);

// Configure function used to print and format entries
extern void *             recorder_configure_output(void *output);
extern recorder_show_fn   recorder_configure_show(recorder_show_fn show);
extern recorder_format_fn recorder_configure_format(recorder_format_fn format);
extern recorder_type_fn   recorder_configure_type(uint8_t id,
                                                  recorder_type_fn type);

// Sort recorder entries with specific format and output functions
extern unsigned recorder_sort(const char *what,
                              recorder_format_fn format,
                              recorder_show_fn show, void *show_arg);

// Background recorder dump thread
extern void recorder_background_dump(const char *what);
extern void recorder_background_dump_stop(void);



// ============================================================================
//
//    Recorder data structures
//
// ============================================================================

typedef struct recorder_entry
/// ---------------------------------------------------------------------------
///   Entry in the flight recorder.
///----------------------------------------------------------------------------
///  Notice that the arguments are stored as "intptr_t" because that type
///  is guaranteed to be the same size as a pointer. This allows us to
///  properly align recorder entries to powers of 2 for efficiency.
///  Also read explanations of \ref _recorder_double and \ref _recorder_float
///  below regarding how to use floating-point with the recorder.
{
    const char *format;         ///< Printf-style format for record + file/line
    uintptr_t   order;          ///< Global order of events (across recorders)
    uintptr_t   timestamp;      ///< Time at which record took place
    const char *where;          ///< Source code function
    uintptr_t   args[4];        ///< Four arguments, for a total of 8 fields
} recorder_entry;


/// A global counter indicating the order of entries across recorders.
/// this is incremented atomically for each record() call.
/// It must be exposed because all XYZ_record() implementations need to
/// touch the same shared variable in order to provide a global order.
extern uintptr_t recorder_order;


typedef struct recorder_info
///----------------------------------------------------------------------------
///   A linked list of the activated recorders
///----------------------------------------------------------------------------
{
    intptr_t                trace;      ///< Trace this recorder
    const char *            name;       ///< Name of this parameter / recorder
    const char *            description;///< Description of what is recorded
    struct recorder_info *  next;       ///< Pointer to next in list
    struct recorder_chan *  exported[4];///< Shared-memory ring export
    recorder_ring_t         ring;       ///< Pointer to ring for this recorder
    recorder_entry          data[0];    ///< Data for this recorder
} recorder_info;


typedef struct recorder_tweak
///----------------------------------------------------------------------------
///   A linked list of recorder tweaks
///----------------------------------------------------------------------------
{
    intptr_t                trace;      ///< Tweak for this recorder
    const char *            name;       ///< Name of this parameter / recorder
    const char *            description;///< Description of what is recorded
    struct recorder_tweak * next;       ///< Pointer to next in list
} recorder_tweak;


// List all the activated recorders
extern recorder_info *recorder_list(void);


// ============================================================================
//
//   Adding data to a recorder
//
// ============================================================================

extern ringidx_t recorder_append(recorder_info *rec,
                                 const char *where,
                                 const char *format,
                                 uintptr_t a0,
                                 uintptr_t a1,
                                 uintptr_t a2,
                                 uintptr_t a3);
extern ringidx_t recorder_append2(recorder_info *rec,
                                  const char *where,
                                  const char *format,
                                  uintptr_t a0,
                                  uintptr_t a1,
                                  uintptr_t a2,
                                  uintptr_t a3,
                                  uintptr_t a4,
                                  uintptr_t a5,
                                  uintptr_t a6,
                                  uintptr_t a7);
extern ringidx_t recorder_append3(recorder_info *rec,
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
                                  uintptr_t a11);

extern ringidx_t recorder_append_fast(recorder_info *rec,
                                      const char *where,
                                      const char *format,
                                      uintptr_t a0,
                                      uintptr_t a1,
                                      uintptr_t a2,
                                      uintptr_t a3);
extern ringidx_t recorder_append_fast2(recorder_info *rec,
                                       const char *where,
                                       const char *format,
                                       uintptr_t a0,
                                       uintptr_t a1,
                                       uintptr_t a2,
                                       uintptr_t a3,
                                       uintptr_t a4,
                                       uintptr_t a5,
                                       uintptr_t a6,
                                       uintptr_t a7);
extern ringidx_t recorder_append_fast3(recorder_info *rec,
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
                                       uintptr_t a11);

/// Activate a recorder (during construction time)
extern void recorder_activate(recorder_info *recorder);

/// Activate a tweak
extern void recorder_tweak_activate(recorder_tweak *tweak);

/// Show one recorder entry when a trace is enabled
extern void recorder_trace_entry(recorder_info *info, recorder_entry *entry);

/// Activate or deactivate traces (e.g. from command line or env variable)
extern int recorder_trace_set(const char *set);
enum { RECORDER_TRACE_OK,
       RECORDER_TRACE_INVALID_NAME,
       RECORDER_TRACE_INVALID_VALUE };



// ============================================================================
//
//    Declaration of recorders and tweaks
//
// ============================================================================

#define RECORDER_DECLARE(Name)                                          \
/* ----------------------------------------------------------------*/   \
/*  Declare a recorder with the given name (for use in headers)    */   \
/* ----------------------------------------------------------------*/   \
    extern recorder_info * const recorder_info_ptr_for_##Name;          \
    extern struct recorder_info_for_##Name recorder_info_for_##Name


#define RECORDER_TWEAK_DECLARE(Name)                                    \
/* ----------------------------------------------------------------*/   \
/*  Declare a tweak with the given name (for use in headers)       */   \
/* ----------------------------------------------------------------*/   \
    extern recorder_tweak * const recorder_info_ptr_for_##Name;         \
    extern struct recorder_tweak_for_##Name recorder_info_for_##Name



// ============================================================================
//
//    Definition of recorders and tweaks
//
// ============================================================================

#define RECORDER(Name, Size, Info)      RECORDER_DEFINE(Name,Size,Info)

#define RECORDER_DEFINE(Name, Size, Info)                               \
/*!----------------------------------------------------------------*/   \
/*! Define a recorder type with Size elements                      */   \
/*!----------------------------------------------------------------*/   \
/*! \param Name is the C name fo the recorder.                          \
 *! \param Size is the number of entries in the circular buffer.        \
 *! \param Info is a description of the recorder for help. */           \
                                                                        \
/* The entry in linked list for this type */                            \
struct recorder_info_for_##Name                                         \
{                                                                       \
    recorder_info       info;                                           \
    recorder_entry      data[Size];                                     \
}                                                                       \
recorder_info_for_##Name =                                              \
{                                                                       \
    {                                                                   \
        0, #Name, Info, NULL,                                           \
        { NULL, NULL, NULL, NULL },                                     \
        { Size, sizeof(recorder_entry), 0, 0, 0, 0 },                   \
        {}                                                              \
    },                                                                  \
    {}                                                                  \
};                                                                      \
recorder_info * const recorder_info_ptr_for_##Name =                    \
    &recorder_info_for_##Name.info;                                     \
                                                                        \
RECORDER_CONSTRUCTOR                                                    \
static void recorder_activate_##Name(void)                              \
/* ----------------------------------------------------------------*/   \
/*  Activate recorder before entering main()                       */   \
/* ----------------------------------------------------------------*/   \
{                                                                       \
    recorder_activate(RECORDER_INFO(Name));                             \
}                                                                       \
                                                                        \
/* Purposefully generate compile error if macro not followed by ; */    \
extern void recorder_activate(recorder_info *recorder)



#define RECORDER_TWEAK_DEFINE(Name, Value, Info)                        \
struct recorder_tweak_for_##Name                                        \
{                                                                       \
    recorder_tweak info;                                                \
}                                                                       \
recorder_info_for_##Name =                                              \
{                                                                       \
    { Value, #Name, Info, NULL }                                        \
};                                                                      \
recorder_tweak * const recorder_info_ptr_for_##Name =                   \
    &recorder_info_for_##Name.info;                                     \
                                                                        \
RECORDER_CONSTRUCTOR                                                    \
static void recorder_tweak_activate_##Name(void)                        \
/* ----------------------------------------------------------------*/   \
/*  Activate a tweak before entering main()                        */   \
/* ----------------------------------------------------------------*/   \
{                                                                       \
    recorder_tweak_activate(&recorder_info_for_##Name.info);            \
}



// ============================================================================
//
//    Access to recorder and tweak info
//
// ============================================================================

#define RECORDER_INFO(Name)     (recorder_info_ptr_for_##Name)
#define RECORDER_TRACE(Name)    (RECORDER_INFO(Name)->trace)
#define RECORDER_TWEAK(Name)    RECORDER_TRACE(Name)



// ============================================================================
//
//    Recording stuff
//
// ============================================================================

#define record(Name, ...)               RECORD_MACRO(Name, __VA_ARGS__)
#define RECORD(Name,...)                RECORD_MACRO(Name, __VA_ARGS__)
#define RECORD_MACRO(Name, Format,...)                                  \
    RECORD_(RECORD,RECORD_COUNT_(__VA_ARGS__),Name,Format,##__VA_ARGS__)
#define RECORD_(RECORD,RCOUNT,Name,Format,...)                          \
    RECORD__(RECORD,RCOUNT,Name,Format,## __VA_ARGS__)
#define RECORD__(RECORD,RCOUNT,Name,Format,...)                         \
    RECORD##RCOUNT(Name,Format,##__VA_ARGS__)
#define RECORD_COUNT_(...)      RECORD_COUNT__(Dummy,##__VA_ARGS__,_X,_X,_12,_11,_10,_9,_8,_7,_6,_5,_4,_3,_2,_1,_0)
#define RECORD_COUNT__(Dummy,_0,_1,_2,_3,_4,_5,_6,_7,_8,_9,_10,_11,_12,_13,_N,...)      _N

#define RECORD_0(Name, Format)                          \
    recorder_append(RECORDER_INFO(Name),                \
                    RECORDER_SOURCE_FUNCTION,           \
                    RECORDER_SOURCE_LOCATION            \
                    Format, 0, 0, 0, 0)
#define RECORD_1(Name, Format, a)                       \
    recorder_append(RECORDER_INFO(Name),                \
                    RECORDER_SOURCE_FUNCTION,           \
                    RECORDER_SOURCE_LOCATION            \
                    Format,                             \
                    RECORDER_ARG(a), 0, 0, 0)
#define RECORD_2(Name, Format, a,b)                     \
    recorder_append(RECORDER_INFO(Name),                \
                    RECORDER_SOURCE_FUNCTION,           \
                    RECORDER_SOURCE_LOCATION            \
                    Format,                             \
                    RECORDER_ARG(a),                    \
                    RECORDER_ARG(b), 0, 0)
#define RECORD_3(Name, Format, a,b,c)                   \
    recorder_append(RECORDER_INFO(Name),                \
                    RECORDER_SOURCE_FUNCTION,           \
                    RECORDER_SOURCE_LOCATION            \
                    Format,                             \
                    RECORDER_ARG(a),                    \
                    RECORDER_ARG(b),                    \
                    RECORDER_ARG(c), 0)
#define RECORD_4(Name, Format, a,b,c,d)                 \
    recorder_append(RECORDER_INFO(Name),                \
                    RECORDER_SOURCE_FUNCTION,           \
                    RECORDER_SOURCE_LOCATION            \
                    Format,                             \
                    RECORDER_ARG(a),                    \
                    RECORDER_ARG(b),                    \
                    RECORDER_ARG(c),                    \
                    RECORDER_ARG(d))
#define RECORD_5(Name, Format, a,b,c,d,e)               \
    recorder_append2(RECORDER_INFO(Name),               \
                     RECORDER_SOURCE_FUNCTION,          \
                     RECORDER_SOURCE_LOCATION           \
                     Format,                            \
                     RECORDER_ARG(a),                   \
                     RECORDER_ARG(b),                   \
                     RECORDER_ARG(c),                   \
                     RECORDER_ARG(d),                   \
                     RECORDER_ARG(e), 0, 0, 0)
#define RECORD_6(Name, Format, a,b,c,d,e,f)             \
    recorder_append2(RECORDER_INFO(Name),               \
                     RECORDER_SOURCE_FUNCTION,          \
                     RECORDER_SOURCE_LOCATION           \
                     Format,                            \
                     RECORDER_ARG(a),                   \
                     RECORDER_ARG(b),                   \
                     RECORDER_ARG(c),                   \
                     RECORDER_ARG(d),                   \
                     RECORDER_ARG(e),                   \
                     RECORDER_ARG(f), 0, 0)
#define RECORD_7(Name, Format, a,b,c,d,e,f,g)           \
    recorder_append2(RECORDER_INFO(Name),               \
                     RECORDER_SOURCE_FUNCTION,          \
                     RECORDER_SOURCE_LOCATION           \
                     Format,                            \
                     RECORDER_ARG(a),                   \
                     RECORDER_ARG(b),                   \
                     RECORDER_ARG(c),                   \
                     RECORDER_ARG(d),                   \
                     RECORDER_ARG(e),                   \
                     RECORDER_ARG(f),                   \
                     RECORDER_ARG(g), 0)
#define RECORD_8(Name, Format, a,b,c,d,e,f,g,h)         \
    recorder_append2(RECORDER_INFO(Name),               \
                     RECORDER_SOURCE_FUNCTION,          \
                     RECORDER_SOURCE_LOCATION           \
                     Format,                            \
                     RECORDER_ARG(a),                   \
                     RECORDER_ARG(b),                   \
                     RECORDER_ARG(c),                   \
                     RECORDER_ARG(d),                   \
                     RECORDER_ARG(e),                   \
                     RECORDER_ARG(f),                   \
                     RECORDER_ARG(g),                   \
                     RECORDER_ARG(h))
#define RECORD_9(Name, Format, a,b,c,d,e,f,g,h,i)       \
    recorder_append3(RECORDER_INFO(Name),               \
                     RECORDER_SOURCE_FUNCTION,          \
                     RECORDER_SOURCE_LOCATION           \
                     Format,                            \
                     RECORDER_ARG(a),                   \
                     RECORDER_ARG(b),                   \
                     RECORDER_ARG(c),                   \
                     RECORDER_ARG(d),                   \
                     RECORDER_ARG(e),                   \
                     RECORDER_ARG(f),                   \
                     RECORDER_ARG(g),                   \
                     RECORDER_ARG(h),                   \
                     RECORDER_ARG(i), 0,0,0)
#define RECORD_10(Name, Format, a,b,c,d,e,f,g,h,i,j)    \
    recorder_append3(RECORDER_INFO(Name),               \
                     RECORDER_SOURCE_FUNCTION,          \
                     RECORDER_SOURCE_LOCATION           \
                     Format,                            \
                     RECORDER_ARG(a),                   \
                     RECORDER_ARG(b),                   \
                     RECORDER_ARG(c),                   \
                     RECORDER_ARG(d),                   \
                     RECORDER_ARG(e),                   \
                     RECORDER_ARG(f),                   \
                     RECORDER_ARG(g),                   \
                     RECORDER_ARG(h),                   \
                     RECORDER_ARG(i),                   \
                     RECORDER_ARG(j), 0,0)
#define RECORD_11(Name, Format, a,b,c,d,e,f,g,h,i,j,k)  \
    recorder_append3(RECORDER_INFO(Name),               \
                     RECORDER_SOURCE_FUNCTION,          \
                     RECORDER_SOURCE_LOCATION           \
                     Format,                            \
                     RECORDER_ARG(a),                   \
                     RECORDER_ARG(b),                   \
                     RECORDER_ARG(c),                   \
                     RECORDER_ARG(d),                   \
                     RECORDER_ARG(e),                   \
                     RECORDER_ARG(f),                   \
                     RECORDER_ARG(g),                   \
                     RECORDER_ARG(h),                   \
                     RECORDER_ARG(i),                   \
                     RECORDER_ARG(j),                   \
                     RECORDER_ARG(k),0)
#define RECORD_12(Name,Format,a,b,c,d,e,f,g,h,i,j,k,l)  \
    recorder_append3(RECORDER_INFO(Name),               \
                     RECORDER_SOURCE_FUNCTION,          \
                     RECORDER_SOURCE_LOCATION           \
                     Format,                            \
                     RECORDER_ARG(a),                   \
                     RECORDER_ARG(b),                   \
                     RECORDER_ARG(c),                   \
                     RECORDER_ARG(d),                   \
                     RECORDER_ARG(e),                   \
                     RECORDER_ARG(f),                   \
                     RECORDER_ARG(g),                   \
                     RECORDER_ARG(h),                   \
                     RECORDER_ARG(i),                   \
                     RECORDER_ARG(j),                   \
                     RECORDER_ARG(k),                   \
                     RECORDER_ARG(l))
#define RECORD_X(Name, Format, ...)   RECORD_TOO_MANY_ARGS(printf(Format, __VA_ARGS__))

// Faster version that does not record time, about 2x faster on x86
#define record_fast(Name, ...)     RECORD_FAST(Name, __VA_ARGS__)
#define RECORD_FAST(Name,Format,...)                                    \
    RECORD_(RECORD_FAST,RECORD_COUNT_(__VA_ARGS__),Name,Format,##__VA_ARGS__)
#define RECORD_FAST_0(Name, Format)                             \
    recorder_append_fast(RECORDER_INFO(Name),                   \
                         RECORDER_SOURCE_FUNCTION,              \
                         RECORDER_SOURCE_LOCATION               \
                         Format, 0, 0, 0, 0)
#define RECORD_FAST_1(Name, Format, a)                          \
    recorder_append_fast(RECORDER_INFO(Name),                   \
                         RECORDER_SOURCE_FUNCTION,              \
                         RECORDER_SOURCE_LOCATION               \
                         Format,                                \
                         RECORDER_ARG(a), 0, 0, 0)
#define RECORD_FAST_2(Name, Format, a,b)                        \
    recorder_append_fast(RECORDER_INFO(Name),                   \
                         RECORDER_SOURCE_FUNCTION,              \
                         RECORDER_SOURCE_LOCATION               \
                         Format,                                \
                         RECORDER_ARG(a),                       \
                         RECORDER_ARG(b), 0, 0)
#define RECORD_FAST_3(Name, Format, a,b,c)                      \
    recorder_append_fast(RECORDER_INFO(Name),                   \
                         RECORDER_SOURCE_FUNCTION,              \
                         RECORDER_SOURCE_LOCATION               \
                         Format,                                \
                         RECORDER_ARG(a),                       \
                         RECORDER_ARG(b),                       \
                         RECORDER_ARG(c), 0)
#define RECORD_FAST_4(Name, Format, a,b,c,d)                    \
    recorder_append_fast(RECORDER_INFO(Name),                   \
                         RECORDER_SOURCE_FUNCTION,              \
                             RECORDER_SOURCE_LOCATION           \
                         Format,                                \
                         RECORDER_ARG(a),                       \
                         RECORDER_ARG(b),                       \
                         RECORDER_ARG(c),                       \
                         RECORDER_ARG(d))
#define RECORD_FAST_5(Name, Format, a,b,c,d,e)                  \
    recorder_append_fast2(RECORDER_INFO(Name),                  \
                          RECORDER_SOURCE_FUNCTION,             \
                              RECORDER_SOURCE_LOCATION          \
                          Format,                               \
                          RECORDER_ARG(a),                      \
                          RECORDER_ARG(b),                      \
                          RECORDER_ARG(c),                      \
                          RECORDER_ARG(d),                      \
                          RECORDER_ARG(e), 0, 0, 0)
#define RECORD_FAST_6(Name, Format, a,b,c,d,e,f)                \
    recorder_append_fast2(RECORDER_INFO(Name),                  \
                          RECORDER_SOURCE_FUNCTION,             \
                          RECORDER_SOURCE_LOCATION              \
                          Format,                               \
                          RECORDER_ARG(a),                      \
                          RECORDER_ARG(b),                      \
                          RECORDER_ARG(c),                      \
                          RECORDER_ARG(d),                      \
                          RECORDER_ARG(e),                      \
                          RECORDER_ARG(f), 0, 0)
#define RECORD_FAST_7(Name, Format, a,b,c,d,e,f,g)              \
    recorder_append_fast2(RECORDER_INFO(Name),                  \
                          RECORDER_SOURCE_FUNCTION,             \
                          RECORDER_SOURCE_LOCATION              \
                          Format,                               \
                          RECORDER_ARG(a),                      \
                          RECORDER_ARG(b),                      \
                          RECORDER_ARG(c),                      \
                          RECORDER_ARG(d),                      \
                          RECORDER_ARG(e),                      \
                          RECORDER_ARG(f),                      \
                          RECORDER_ARG(g), 0)
#define RECORD_FAST_8(Name, Format, a,b,c,d,e,f,g,h)            \
    recorder_append_fast2(RECORDER_INFO(Name),                  \
                          RECORDER_SOURCE_FUNCTION,             \
                          RECORDER_SOURCE_LOCATION              \
                          Format,                               \
                          RECORDER_ARG(a),                      \
                          RECORDER_ARG(b),                      \
                          RECORDER_ARG(c),                      \
                          RECORDER_ARG(d),                      \
                          RECORDER_ARG(e),                      \
                          RECORDER_ARG(f),                      \
                          RECORDER_ARG(g),                      \
                          RECORDER_ARG(h))
#define RECORD_FAST_9(Name, Format, a,b,c,d,e,f,g,h,i)          \
    recorder_append_fast3(RECORDER_INFO(Name),                  \
                          RECORDER_SOURCE_FUNCTION,             \
                          RECORDER_SOURCE_LOCATION              \
                          Format,                               \
                          RECORDER_ARG(a),                      \
                          RECORDER_ARG(b),                      \
                          RECORDER_ARG(c),                      \
                          RECORDER_ARG(d),                      \
                          RECORDER_ARG(e),                      \
                          RECORDER_ARG(f),                      \
                          RECORDER_ARG(g),                      \
                          RECORDER_ARG(h),                      \
                          RECORDER_ARG(i), 0,0,0)
#define RECORD_FAST_10(Name, Format, a,b,c,d,e,f,g,h,i,j)       \
    recorder_append_fast3(RECORDER_INFO(Name),                  \
                          RECORDER_SOURCE_FUNCTION,             \
                          RECORDER_SOURCE_LOCATION              \
                          Format,                               \
                          RECORDER_ARG(a),                      \
                          RECORDER_ARG(b),                      \
                          RECORDER_ARG(c),                      \
                          RECORDER_ARG(d),                      \
                          RECORDER_ARG(e),                      \
                          RECORDER_ARG(f),                      \
                          RECORDER_ARG(g),                      \
                          RECORDER_ARG(h),                      \
                          RECORDER_ARG(i),                      \
                          RECORDER_ARG(j), 0,0)
#define RECORD_FAST_11(Name, Format, a,b,c,d,e,f,g,h,i,j,k)     \
    recorder_append_fast3(RECORDER_INFO(Name),                  \
                          RECORDER_SOURCE_FUNCTION,             \
                          RECORDER_SOURCE_LOCATION              \
                          Format,                               \
                          RECORDER_ARG(a),                      \
                          RECORDER_ARG(b),                      \
                          RECORDER_ARG(c),                      \
                          RECORDER_ARG(d),                      \
                          RECORDER_ARG(e),                      \
                          RECORDER_ARG(f),                      \
                          RECORDER_ARG(g),                      \
                          RECORDER_ARG(h),                      \
                          RECORDER_ARG(i),                      \
                          RECORDER_ARG(j),                      \
                          RECORDER_ARG(k),0)
#define RECORD_FAST_12(Name, Format, a,b,c,d,e,f,g,h,i,j,k,l)   \
    recorder_append_fast3(RECORDER_INFO(Name),                  \
                          RECORDER_SOURCE_FUNCTION,             \
                          RECORDER_SOURCE_LOCATION              \
                          Format,                               \
                          RECORDER_ARG(a),                      \
                          RECORDER_ARG(b),                      \
                          RECORDER_ARG(c),                      \
                          RECORDER_ARG(d),                      \
                          RECORDER_ARG(e),                      \
                          RECORDER_ARG(f),                      \
                          RECORDER_ARG(g),                      \
                          RECORDER_ARG(h),                      \
                          RECORDER_ARG(i),                      \
                          RECORDER_ARG(j),                      \
                          RECORDER_ARG(k),                      \
                          RECORDER_ARG(l))
#define RECORD_FAST_X(Name, Format, ...)   RECORD_TOO_MANY_ARGS(printf(Format, __VA_ARGS__))

// Some ugly macro drudgery to make things easy to use. Adjust type.
#ifdef __cplusplus
#define RECORDER_ARG(arg)       _recorder_arg(arg)
#else // !__cplusplus

#if defined(__GNUC__) && !defined(__clang__)
#  if __GNUC__ <= 4 || (__GNUC__ == 4 && __GNUC_MINOR__ < 9)
#    define RECORDER_WITHOUT_GENERIC
#  endif
#endif // __GNUC__

#ifdef RECORDER_WITHOUT_GENERIC
#define RECORDER_ARG(arg) ((uintptr_t) (arg))
#else // !RECORDER_WITHOUT_GENERIC
#define RECORDER_ARG(arg)                               \
    _Generic(arg,                                       \
             unsigned char:     _recorder_unsigned,     \
             unsigned short:    _recorder_unsigned,     \
             unsigned:          _recorder_unsigned,     \
             unsigned long:     _recorder_unsigned,     \
             unsigned long long:_recorder_unsigned,     \
             char:              _recorder_char,         \
             signed char:       _recorder_signed,       \
             signed short:      _recorder_signed,       \
             signed:            _recorder_signed,       \
             signed long:       _recorder_signed,       \
             signed long long:  _recorder_signed,       \
             float:             _recorder_float,        \
             double:            _recorder_double,       \
             default:           _recorder_pointer)(arg)
#endif // RECORDER_WITHOUT_GENERIC
#endif // __cplusplus



// ============================================================================
//
//    Default recorders declared in recorder.c
//
// ============================================================================

RECORDER_DECLARE(signals);
RECORDER_DECLARE(recorders);
RECORDER_DECLARE(recorder_traces);



// ============================================================================
//
//    Timing information
//
// ============================================================================

#define RECORD_TIMING_BEGIN(Recorder)                           \
do {                                                            \
    static uintptr_t _last_second = 0;                          \
    static uintptr_t _total = 0;                                \
    static uintptr_t _total_last_second = 0;                    \
    static uintptr_t _duration_last_second = 0;                 \
    static uintptr_t _iterations_last_second = 0;               \
    uintptr_t _start_time = recorder_tick()

#define RECORD_TIMING_END(Recorder, Operation, Name, Value)             \
    uintptr_t _end_time = recorder_tick();                              \
    uintptr_t _duration = _end_time - _start_time;                      \
    uintptr_t _value = (Value);                                         \
    recorder_ring_fetch_add(_total, _value);                            \
    recorder_ring_fetch_add(_total_last_second, _value);                \
    recorder_ring_fetch_add(_duration_last_second, _duration);          \
    recorder_ring_fetch_add(_iterations_last_second, 1);                \
    uintptr_t _print_interval =                                         \
        ((RECORDER_INFO(Recorder)->trace == RECORDER_CHAN_MAGIC         \
          ? 100                                                         \
          : RECORDER_INFO(Recorder)->trace)                             \
         * (RECORDER_HZ / 1000));                                       \
    uintptr_t _known = _last_second;                                    \
    uintptr_t _interval = _end_time - _known;                           \
    double _scale = (double) RECORDER_HZ / _interval;                   \
    if (_interval >= _print_interval &&                                 \
        recorder_ring_compare_exchange(_last_second,_known,_end_time))  \
    {                                                                   \
        record(Recorder,                                                \
               Operation " %.2f " Name "/s, total %lu, "                \
               "%.2f loops/s, avg duration %.2f us, ",                  \
               _total_last_second * _scale,                             \
               _total,                                                  \
               _iterations_last_second * _scale,                        \
               _duration_last_second * _scale);                         \
        _total_last_second = 0;                                         \
        _duration_last_second = 0;                                      \
        _iterations_last_second = 0;                                    \
    }                                                                   \
} while (0)


// ============================================================================
//
//    Data export from recorders
//
// ============================================================================

typedef enum recorder_type
// ----------------------------------------------------------------------------
//    Type of data exported
// ----------------------------------------------------------------------------
{
    RECORDER_NONE,              // Nothing exported (pending format)
    RECORDER_INVALID,           // Invalid data
    RECORDER_SIGNED,            // Signed value
    RECORDER_UNSIGNED,          // Unsigned value
    RECORDER_REAL               // Real number
} recorder_type;


typedef union recorder_data
// ----------------------------------------------------------------------------
//    Data exported
// ----------------------------------------------------------------------------
{
    intptr_t    signed_value;
    uintptr_t   unsigned_value;
#if RECORDER_64BIT
    double      real_value;
#else // Small intptr_t, so we save 32-bit float values
    float       real_value;
#endif // INTPTR_MAX
} recorder_data;

// A collection of data recorder_shmem in memory
typedef struct recorder_chans *recorder_chans_p;
typedef struct recorder_chan  *recorder_chan_p;

// Magic number in export channel is different for 32-bit and 64-bit values
#define RECORDER_CHAN_MAGIC           (0xC0DABABE ^ RECORDER_64BIT)
#define RECORDER_CHAN_VERSION         RECORDER_VERSION(1,0,5)
#define RECORDER_EXPORT_SIZE          2048

extern const char *recorder_export_file(void);


// ============================================================================
//
//    Interface for the local process
//
// ============================================================================

// Creating recorder_chans for the local process to write into
extern recorder_chans_p recorder_chans_new(const char *file);
extern void             recorder_chans_delete(recorder_chans_p);

extern recorder_chan_p  recorder_chan_new(recorder_chans_p chans,
                                          recorder_type    type,
                                          size_t           size,
                                          const char *     name,
                                          const char *     description,
                                          const char *     unit,
                                          recorder_data    min,
                                          recorder_data    max);
extern void             recorder_chan_delete(recorder_chan_p chan);



// ============================================================================
//
//    Subscribing to recorder_chans in a remote process
//
// ============================================================================

// Subscribing to recorder_chans from another process
extern recorder_chans_p recorder_chans_open(const char *file);
extern void             recorder_chans_close(recorder_chans_p chans);
extern bool             recorder_chans_configure(recorder_chans_p chans,
                                                 const char *message);
extern bool             recorder_chans_valid(recorder_chans_p chans);

extern recorder_chan_p  recorder_chan_find(recorder_chans_p chans,
                                           const char *pattern,
                                           recorder_chan_p after);
extern const char *     recorder_chan_name(recorder_chan_p chan);
extern const char *     recorder_chan_description(recorder_chan_p chan);
extern const char *     recorder_chan_unit(recorder_chan_p chan);
extern recorder_data    recorder_chan_min(recorder_chan_p chan);
extern recorder_data    recorder_chan_max(recorder_chan_p chan);
extern recorder_type    recorder_chan_type(recorder_chan_p chan);
extern size_t           recorder_chan_size(recorder_chan_p chan);
extern size_t           recorder_chan_readable(recorder_chan_p chan,
                                               ringidx_t *readerID);
extern size_t           recorder_chan_read(recorder_chan_p chan,
                                           recorder_data *ptr, size_t count,
                                           ringidx_t *readerID);



// ============================================================================
//
//   Support macros
//
// ============================================================================

#define RECORDER_SOURCE_FUNCTION    __func__ /* Works in C99 and C++11 */
#define RECORDER_SOURCE_LOCATION    __FILE__ ":" RECORDER_STRING(__LINE__) ":"
#define RECORDER_STRING(LINE)       RECORDER_STRING_(LINE)
#define RECORDER_STRING_(LINE)      #LINE

#ifdef __GNUC__
#define RECORDER_CONSTRUCTOR            __attribute__((constructor))
#else
#define RECORDER_CONSTRUCTOR
#endif


// ============================================================================
//
//   Portability helpers
//
// ============================================================================

#ifndef recorder_tick
// Return ticks (some kind of time unit) since first called
extern uintptr_t recorder_tick(void);
#endif

#ifndef RECORDER_HZ
#if RECORDER_64BIT
#define RECORDER_HZ     1000000
#else // Small time stamp, do not generate huge values
#define RECORDER_HZ     1000
#endif // INTPTR_MAX
#endif // RECORDER_HZ

#ifdef __cplusplus
}
#endif // __cplusplus



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

#ifdef __cplusplus
// In C++, we don't use _Generic but actual overloading
template <class inttype>
static inline uintptr_t         _recorder_arg(inttype i)  { return (uintptr_t) i; }
#define _recorder_float         _recorder_arg
#define _recorder_double        _recorder_arg

#else // !__cplusplus

static inline uintptr_t _recorder_char(char c)
// ----------------------------------------------------------------------------
//   Necessary because of the way generic selections work
// ----------------------------------------------------------------------------
{
    return c;
}


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

#endif // __cplusplus


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


#endif // RECORDER_H
