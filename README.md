# recorder
A lock-free, real-time flight recorder for your C or C++ programs


## Instrumentation that is always there when you need it

The flight recorder is designed to help you debug complex, real-time,
multi-CPU programs. It lets you instrument their execution with
very inexpensive, non-intrusive  `printf`-like *record statements*, that
capture what is happening in your program. These record statements are
very inexpensive (about 80-100ns on a modern x86), so you can leave
them in your code all the time, even for optimized code.

Then, when something bad happens or from within a debugger, you can
*dump the recorder*, which gives you a very detailed account of recent
events, helping you figure out how you came there. The recorder dump
also contains highly detailed ordering and timing information, which
can be very precious in multi-CPU systems.

Multiple recorders can be active simultaneously, for example to
capture events from different subsystems. When a recorder dump occurs,
events from different recorders are sorted so that you get a picture
of how the systems interacted. Different recorders can have different
size and different refresh rates, but a very fast recorder will not
push away data from a slower one. This ensure you can record
important, but slow, events, as well as much more frequent ones.

See
[this blog article](https://grenouillebouillie.wordpress.com/2016/12/09/a-real-time-lock-free-multi-cpu-flight-recorder)
for a more extensive description of the design and rationale.

## Building the recorder library

The recorder is normally designed to be included in your applications,
but you can build and test the library to see how it works. To build
and test the library on your system, type:

`make test`

This should build the library itself, which really consists of a
single header and a single C++ file, and then execute three tests that
perform some operations and record what is happening while they do so.

## Adding recorders to your own project

In order to add recorders to your own project, you need to integrate
three source files:

* The `recorder.h` file is the header, which is designed to work for
  either C or C++ programs.
  
* The `recorder.cpp` file is the implemetation file, which provides
  support for both C and C++ programs.
  
* The `recorder.tbl` file lists the recorders your application will
  use, and their size.

You can look at the `hanoi_test.c` file for an example of use.
To use a recorder called `MOVES` with 256 entries, you declare
it as follows in the `recorder.tbl` file:

    RECORDER(MOVES, 256)
    

## Recording events

To record events in C, you use a `printf`-like `RECORD` statement,
which begins with the name of the recorder as defined in
`recorder.tbl`.

To record data in C, you replace `printf` statements with a `RECORD`
statement, specifying the name of the recorder as the first argument:

    RECORD(MOVES, "Move disk from %s to %s\n", name[left], name[right]);

To record events in C++, you can also use the name of the recorder
itself, using the recorder as a functional object:

    MOVES("Move disk from %s to %s\n", name[left], name[right]);


## Dumping recorder events

To dump recorded events, you use the `recorder_dump` function. This
dumps all the recorders:

    recorder_dump();
    
In C++, you can use the `Recorder::Dump` function and pass an
`ostream` object.

If you want to dump specific recorders, you can use
`recorder_dump_for`, which matches the recorder names against the
string given as an argument:

    recorder_dump_for("TIMING"); // Dumps "TIMING", but also "MY_TIMING_2"

During a dump, events are sorted according to the global event order.

Note that sorting only happens across recorders. Within a single
recorder, events may be out of order. For example, CPU1 may get order
1, CPU2 then gets order 2, then CPU2 writes its record entry, then
CPU1. In that case, the recorder will contain entry 2, then entry 1.


## Reacting to signals

It is often desirable to dump the recorder when some specific signal is
received. To detect crashes, for example, you can dump the recorder
when receiving `SIGBUS`, `SIGSEGV`, `SIGILL`, `SIGABRT`, etc. To do
this, call the function `record_dump_on_signal` or the C++ functoin
`Recorder::DumpOnSignal`:

    recorder_dump_on_signal(SIGBUS);
    Recorder::DumpOnSignal(SIGSEGV);
    
When running BSD or macOS, you can have your program dump the current
state of the recorder by adding a signal handler for `SIGINFO`. You
can then dump the recorder at any time by pressing a key (typically
Control-T) in the terminal.


## Adding new recorders in your code

If you want to add recorders to your code, the recommended method is
to list them in `recorders.tbl`. This makes it easy to locate all your
recorders in one place.

You can also define a recorder named `MyRecorder` with 256 entries
that can be called from C using:

    RECORDER_DEFINE(MyRecorder, 256)

However, note that since the actual implementation of recorders is
written in C++, the definition of recorders with `RECORDER_DEFINE`
must be compiled in C++, not in C.

In C++, you would use:

    Recorder<256> MyRecorder;

