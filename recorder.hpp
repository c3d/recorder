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
#include <vector>
#include <iostream>

// Include the C interface
extern "C" {
#undef RECORDER_H // Include the C interface now that RECORDER_HPP is set
#include "recorder.h"
} // extern "C"



// ============================================================================
//
//    Higher-evel interface
//
// ============================================================================
//    This used to be a nice C++ template with atomics.
//    It's now a wrapper around C code
//    for better portability and interoperability between C and C++ records

template <recorder_record_fn RecordFunction, recorder_list &List>
struct Recorder
// ----------------------------------------------------------------------------
//    A base for all flight recorders, to store them in a linked list
// ----------------------------------------------------------------------------
{
    Recorder() {}

    const char *Name()          { return List.name; }
    unsigned    Size()          { return List.size; }

    // Helper struct to safely pass floating-point arguments
    struct Arg
    {
        Arg(float f):           value(_recorder_float(f))   {}
        Arg(double d):          value(_recorder_double(d))  {}
        template<class T>
        Arg(T t):               value(intptr_t(t))          {}
        operator uintptr_t()    { return value; }
    private:
        uintptr_t               value;
    };

    void operator()(const char *what, Arg a1=0, Arg a2=0, Arg a3=0, Arg a4=0)
    {
        RecordFunction(recorder_return_address(), what, a1,a2,a3,a4);
    }
};


extern std::ostream &operator<< (std::ostream &out, recorder_list &list);

template <recorder_record_fn R, recorder_list &L>
inline std::ostream &operator<< (std::ostream &out, Recorder<R,L> &recorder)
// ----------------------------------------------------------------------------
//   Dump a single flight recorder entry on the given ostream
// ----------------------------------------------------------------------------
{
    return out << L;
}



// ============================================================================
//
//    Available recorders
//
// ============================================================================

#define RECORDER(Name, Size)                                            \
    extern recorder_list recorder_##Name##_list_entry;                  \
    extern Recorder<recorder_##Name##_record,                           \
                    recorder_##Name##_list_entry> Name;
#include "recorder.tbl"

#endif // RECORDER_HPP
