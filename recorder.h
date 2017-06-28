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
extern "C" {
#endif // __cplusplus


// ============================================================================
//
//   Recorder dump, to use in fault handlers or within a debugger
//
// ============================================================================
//  Within gdb, you can use: 'p recorder_dump()' to get a dump of what
//  happened in your program until this point

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

// Configure function used to print and format entries
extern void *             recorder_configure_output(void *output);
extern recorder_show_fn   recorder_configure_show(recorder_show_fn show);
extern recorder_format_fn recorder_configure_format(recorder_format_fn format);

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
///  Also read explanations of \ref _recorder_double and \ref _recorder_float below regarding
///  how to use floating-point with the recorder.
{
    const char *format;         ///< Printf-style format for record
    uintptr_t   order;          ///< Global order of events (across recorders)
    uintptr_t   timestamp;      ///< Time at which record took place
    const char *where;          ///< Source code location (__FILE__ : __LINE__)
    uintptr_t   args[4];        ///< Four arguments, for a total of 8 fields
} recorder_entry;


/// A global counter indicating the order of entries across recorders.
/// This is incremented atomically for each RECORD() call.
/// It must be exposed because all XYZ_record() implementations need to
/// touch the same shared variable in order to provide a global order.
extern uintptr_t recorder_order;

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


typedef struct recorder_info
///----------------------------------------------------------------------------
///   A linked list of the activated recorders
///----------------------------------------------------------------------------
{
    intptr_t                trace;      ///< Trace this recorder
    const char *            name;       ///< Name of this parameter / recorder
    const char *            description;///< Description of what is recorded
    struct recorder_info *  next;       ///< Pointer to next in list

    recorder_read_fn        read;       ///< Read entries
    recorder_peek_fn        peek;       ///< Peek first readable entry
    recorder_readable_fn    readable;   ///< Count readable entries
    size_t                  size;       ///< Size (maximum number of entries)

    struct recorder_chan *  exported[4];///< Shared-memory ring export
} recorder_info;


typedef struct recorder_tweak
///----------------------------------------------------------------------------
///   A linked list of recorder tweaks
///----------------------------------------------------------------------------
{
    intptr_t                tweak;      ///< Tweak for this recorder
    const char *            name;       ///< Name of this parameter / recorder
    const char *            description;///< Description of what is recorded
    struct recorder_tweak * next;       ///< Pointer to next in list
} recorder_tweak;


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
//    Declaration of recorders
//
// ============================================================================

#define RECORDER_DECLARE(Name)                                          \
/* ----------------------------------------------------------------*/   \
/*  Declare a recorder type with Size elements                     */   \
/* ----------------------------------------------------------------*/   \
                                                                        \
extern ringidx_t recorder_##Name##_record(const char *where,            \
                                          const char *format,           \
                                          uintptr_t a0,                 \
                                          uintptr_t a1,                 \
                                          uintptr_t a2,                 \
                                          uintptr_t a3);                \
                                                                        \
extern ringidx_t recorder_##Name##_record_fast(const char *where,       \
                                               const char *format,      \
                                               uintptr_t a0,            \
                                               uintptr_t a1,            \
                                               uintptr_t a2,            \
                                               uintptr_t a3);           \
                                                                        \
extern recorder_info recorder_##Name##_info;


#define RECORDER_TWEAK_DECLARE(Name)                                    \
extern recorder_tweak recorder_##Name##_tweak;



// ============================================================================
//
//    Definition of recorders
//
// ============================================================================

#define RECORDER(Name, Size, Info)                                      \
/*!----------------------------------------------------------------*/   \
/*! Define a recorder type with Size elements                      */   \
/*!----------------------------------------------------------------*/   \
/*! \param Name is the C name fo the recorder.                          \
 *! \param Size is the number of entries in the circular buffer. */     \
                                                                        \
RING_DECLARE(recorder_##Name, recorder_entry, Size);                    \
RING_DEFINE (recorder_##Name, recorder_entry, Size);                    \
                                                                        \
/* The entry in linked list for this type */                            \
recorder_info recorder_##Name##_info =                                  \
{                                                                       \
    0, #Name, Info, NULL,                                               \
    (recorder_read_fn) recorder_##Name##_read,                          \
    (recorder_peek_fn) recorder_##Name##_peek,                          \
    (recorder_readable_fn) recorder_##Name##_readable,                  \
    Size,                                                               \
    { NULL, NULL, NULL, NULL }                                          \
};                                                                      \
                                                                        \
                                                                        \
RECORDER_CONSTRUCTOR                                                    \
static void recorder_##Name##_activate()                                \
/* ----------------------------------------------------------------*/   \
/*  Activate recorder before entering main()                       */   \
/* ----------------------------------------------------------------*/   \
{                                                                       \
    recorder_activate(&recorder_##Name##_info);                         \
}                                                                       \
                                                                        \
                                                                        \
ringidx_t recorder_##Name##_record(const char *where,                   \
                                   const char *format,                  \
                                   uintptr_t a0,                        \
                                   uintptr_t a1,                        \
                                   uintptr_t a2,                        \
                                   uintptr_t a3)                        \
/* ----------------------------------------------------------------*/   \
/*  Enter a record entry in ring buffer with given set of args     */   \
/* ----------------------------------------------------------------*/   \
{                                                                       \
    ringidx_t writer = ring_fetch_add(recorder_##Name.ring.writer, 1);  \
    recorder_entry *entry = &recorder_##Name.data[writer % Size];       \
    entry->format = format;                                             \
    entry->order = ring_fetch_add(recorder_order, 1);                   \
    entry->timestamp = recorder_tick();                                 \
    entry->where = where;                                               \
    entry->args[0] = a0;                                                \
    entry->args[1] = a1;                                                \
    entry->args[2] = a2;                                                \
    entry->args[3] = a3;                                                \
    ring_fetch_add(recorder_##Name.ring.commit, 1);                     \
    if (recorder_##Name##_info.trace)                                   \
        recorder_trace_entry(&recorder_##Name##_info, entry);           \
    return writer;                                                      \
}                                                                       \
                                                                        \
                                                                        \
ringidx_t recorder_##Name##_recfast(const char *where,                  \
                                    const char *format,                 \
                                    uintptr_t a0,                       \
                                    uintptr_t a1,                       \
                                    uintptr_t a2,                       \
                                    uintptr_t a3)                       \
/* ----------------------------------------------------------------*/   \
/*  Enter a record entry in ring buffer with given set of args     */   \
/* ----------------------------------------------------------------*/   \
{                                                                       \
    ringidx_t writer = ring_fetch_add(recorder_##Name.ring.writer, 1);  \
    recorder_entry *entry = &recorder_##Name.data[writer % Size];       \
    entry->format = format;                                             \
    entry->order = ring_fetch_add(recorder_order, 1);                   \
    entry->timestamp = recorder_##Name.data[(writer-1)%Size].timestamp; \
    entry->where = where;                                               \
    entry->args[0] = a0;                                                \
    entry->args[1] = a1;                                                \
    entry->args[2] = a2;                                                \
    entry->args[3] = a3;                                                \
    ring_fetch_add(recorder_##Name.ring.commit, 1);                     \
    if (recorder_##Name##_info.trace)                                   \
        recorder_trace_entry(&recorder_##Name##_info, entry);           \
    return writer;                                                      \
}


#define RECORDER_TWEAK_DEFINE(Name, Value, Info)                        \
recorder_tweak recorder_##Name##_tweak = { Value, #Name, Info, NULL };  \
                                                                        \
RECORDER_CONSTRUCTOR                                                    \
static void recorder_##Name##_tweak_activate()                          \
/* ----------------------------------------------------------------*/   \
/*  Activate a tweak before entering main()                        */   \
/* ----------------------------------------------------------------*/   \
{                                                                       \
    recorder_tweak_activate(&recorder_##Name##_tweak);                  \
}


#define RECORDER_TRACE(Name)    (recorder_##Name##_info.trace)
#define RECORDER_TWEAK(Name)    (recorder_##Name##_tweak.tweak)



// ============================================================================
//
//    Recording stuff
//
// ============================================================================

#define RECORD(Name, ...)      RECORD_(Name, __VA_ARGS__);
#define RECORD_(Name, Format, ...)                                      \
    recorder_##Name##_record(RECORDER_SOURCE_LOCATION,                  \
                             Format,                                    \
                             RECORDER_ARG(0,0,0,0, ## __VA_ARGS__,0),   \
                             RECORDER_ARG(0,0,0,## __VA_ARGS__,0,0),    \
                             RECORDER_ARG(0,0,## __VA_ARGS__,0,0,0),    \
                             RECORDER_ARG(0,## __VA_ARGS__,0,0,0,0))

// Faster version that does not record time, about 2x faster on x86
#define RECORD_FAST(Name, ...)      RECORD_FAST_(Name, __VA_ARGS__)
#define RECORD_FAST_(Name, Format, ...)                                 \
    recorder_##Name##_recfast(RECORDER_SOURCE_LOCATION,                 \
                              Format,                                   \
                              RECORDER_ARG(0,0,0,0,##__VA_ARGS__,0),    \
                              RECORDER_ARG(0,0,0,##__VA_ARGS__,0,0),    \
                              RECORDER_ARG(0,0,##__VA_ARGS__,0,0,0),    \
                              RECORDER_ARG(0,##__VA_ARGS__,0,0,0,0))

// Some ugly macro drudgery to make things easy to use.
// Convert types, pad with zeroes.
#define RECORDER_ARG(_1,_2,_3,_4, arg,...)              \
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
             default:           _recorder_pointer,      \
             float:             _recorder_float,        \
             double:            _recorder_double) (arg)



// ============================================================================
//
//    Default recorders declared in recorder.c
//
// ============================================================================

RECORDER_DECLARE(signals);
RECORDER_DECLARE(recorder_trace_set);



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
#if INTPTR_MAX < 0x8000000
    float       real_value;
#else // Large enough intptr_t
    double      real_value;
#endif // INTPTR_MAX
} recorder_data;

// A collection of data recorder_shmem in memory
typedef struct recorder_chans *recorder_chans_p;
typedef struct recorder_chan  *recorder_chan_p;

#define RECORDER_CHAN_MAGIC           0xC0DABABE // Historical reference
#define RECORDER_CHAN_VERSION         0x010000   // Version 1.0.0
#define RECORDER_EXPORT_SIZE          2048

extern const char *recorder_export_file();


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

#define RECORDER_SOURCE_LOCATION        __FILE__ ":" RECORDER_STRING(__LINE__)
#define RECORDER_STRING(LINE)           RECORDER_STRING_(LINE)
#define RECORDER_STRING_(LINE)          #LINE

#ifdef __GNUC__
#define RECORDER_CONSTRUCTOR            __attribute__((constructor))
#else
#define RECORDER_CONSTRUCTOR
#endif


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

#ifndef recorder_tick
// Return ticks (some kind of time unit) since first called
extern uintptr_t recorder_tick(void);
#endif

#ifndef RECORDER_HZ
#if INTPTR_MAX < 0x8000000
#define RECORDER_HZ     1000
#else // Large enough intptr_t
#define RECORDER_HZ     1000000
#endif // INTPTR_MAX
#endif // RECORDER_HZ

#ifdef __cplusplus
}
#endif // __cplusplus

#endif // RECORDER_H
