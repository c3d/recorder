// ****************************************************************************
//  recorder.cpp                                             Recorder project
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
#define RECORDER(Name, Size)                                    \
Recorder<Name##_record, Name##_recorder_list> Name;
#include "recorder.tbl"



// ============================================================================
// 
//    Recorder utilities
// 
// ============================================================================


static unsigned recorder_print_to_ostream(const char *ptr, unsigned len,
                                          std::ostream *out)
// ----------------------------------------------------------------------------
//   Print to a C++ ostream
// ----------------------------------------------------------------------------
{
    *out << ptr;
    return out->good();
}


std::ostream &operator<< (std::ostream &out, recorder_list &list)
// ----------------------------------------------------------------------------
//   Dump the given recorder on an ostream
// ----------------------------------------------------------------------------
{
    recorder_sort(list.name, (recorder_show_fn) recorder_print_to_ostream, &out);
    return out;
}

