# recorder
A lock-free, real-time flight recorder for your C or C++ programs


## Instrumentation that is always there when you need it

The flight recorder is designed to help you debug complex, real-time,
multi-CPU programs. It lets you instrument their execution with
non-intrusive `printf`-like *record statements*, that capture what is
happening in your program. These record statements are very
inexpensive (about 80-100ns on a modern x86), so you can leave them in
your code all the time, even for optimized code.

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

Here is what a recorder dump can look like:

     recorder_test.c:95: [64944745 10.001962] SpeedTest: Recording 64944741
     recorder_test.c:88: [64944746 10.001979] Pauses: Pausing #0 2401.618us
     recorder_test.c:95: [64944747 10.001986] SpeedTest: Recording 64944729
     recorder_test.c:95: [64944540 10.001940] SpeedTest: Recording 64944537
     recorder_test.c:95: [64944523 10.001998] SpeedTest: Recording 64944520
     recorder_test.c:95: [64885029 9.992381] SpeedTest: Recording 64885026
     recorder_test.c:95: [64883353 9.992112] SpeedTest: Recording 64883349
     recorder_test.c:95: [64936311 10.002076] SpeedTest: Recording 64936306
     recorder_test.c:95: [64944748 10.002082] SpeedTest: Recording 64938309
     recorder_test.c:125: [64944749 10.005009] MAIN: All threads have stopped.
     recorder_test.c:137: [64944750 10.005034] MAIN: Recorder test complete, 16 threads.
     recorder_test.c:138: [64944751 10.005035] MAIN:   Iterations           =   64944742
     recorder_test.c:139: [64944752 10.005037] MAIN:   Iterations / ms      =       6494
     recorder_test.c:141: [64944753 10.005038] MAIN:   Duration per record  =        153ns


The first column is the *order* of records, a sequence number that
helps relate records made from different threads. The second column is
the time stamp in seconds from the first record. On 32-bit machines,
it is precise to the ms. On 64-bit machines, it is precise to the
microsecond. The third column is the location in the program where the
record was taken, which you can use in a debugger to identify the code
that was recording. The last part of the record is what was recorded.

See
[this blog article](https://grenouillebouillie.wordpress.com/2016/12/09/a-real-time-lock-free-multi-cpu-flight-recorder)
for a more extensive description of the design and rationale.

## Building the recorder library

The recorder is normally designed to be included directly in your applications,
not as a separate library, because it uses a per-application configuration
file called `recorder.tbl` specifying the recorders and their size.
However, you can still build and test the library to see how it works. To build
and test the library on your system, type:

`make test`

This should build the library itself, which really consists of a
two headers and a single C file, and then execute three tests that
perform some operations and record what is happening while they do so.


## Adding recorders to your own project

In order to add recorders to your own C project, you need to integrate
three source files:

* The `recorder.h` file is the header, which is designed to work for
  either C programs.

* The `recorder.c` file is the implementation file, which provides
  support for C programs.

To define recorders, you use `RECORDER` statements, which takes
three arguments: the name of the recorder, the number of entries to
keep, and a description. You can look at the `hanoi_test.c` file for
an example of use.  This example defines for example a recorder called
`MOVES` with 1024 entries, declared as follows:

    RECORDER(MOVES, 1024, "Moving pieces around")

It is also possible to declare recorders in a header file using the
`RECORDER_DECLARE` statement that takes the name of the recorder.

    RECORDER_DECLARE(MOVES)

This allows to share a recorder across multiple C source files.


## Recording events

To record events in C, you use a `printf`-like `RECORD` statement,
which begins with the name of the recorder as defined in
`recorder.tbl`.

To record data in C, you replace `printf` statements with a `RECORD`
statement, specifying the name of the recorder as the first argument:

    RECORD(MOVES, "Move disk from %s to %s\n", name[left], name[right]);

While a `RECORD` behaves mostly like `printf`, there are important
caveats and limitations to be aware of, see below.


## Caveats and limitations

Each record can store up to 4 arguments. Therefore, unlike `printf`,
you can only pass 4 values to `RECORD`.

You can pass integer values, floating-point values (limited to `float`
on 32-bit machines for size reasons), pointers or strings as `RECORD` argument.

However, unlike `printf`, the rendering of the final message is done
*at the time of the dump*. This is not a problem for integer, pointer or
floating-point values, but for strings (the `%s` format of `printf`),
you must make sure that the string is still valid at the time of the
dump. A good practice is to only record string constants.

    // OK if 0 <= i and i < 5
    const char *array[5] = { "ONE", "TWO", "THREE", "FOUR", "FIVE" };
    RECORD(Main, "Looking at %s", array[i]);

    // Not OK because the value of the string has been freed at dump time
    char *tempStr = strdup("Hello");
    RECORD(Main, "You will see garbage here: %s", tempStr);
    free(tempStr); // At dump time, the string no longer exists

The `RECORD` macro automatically converts floating point values
to `uintptr_t` based on their type.


## Dumping recorder events

To dump recorded events, you use the `recorder_dump` function. This
dumps all the recorders:

    recorder_dump();

This can be used in a number of situations, in particular from a
debugger. In `gdb` for example, you could run the following command to
dump the recorder during a debugging session:

    p recorder_dump()

If you want to dump specific recorders, you can use
`recorder_dump_for`, which matches the recorder names against the
string given as an argument:

    recorder_dump_for("TIMING"); // Dumps "TIMING", but also "MY_TIMING_2"

During a dump, events are sorted according to the global event order.

Note that sorting only happens across recorders. Within a single
recorder, events may be out of order. For example, CPU1 may get order
1, CPU2 then gets order 2, then CPU2 writes its record entry, then
CPU1. In that case, the recorder will contain entry 2, then entry 1.

In other words, recorder entries are only sorted across different
recorders, but may be out of order within the same recorder.


## Recorder tracing

In some cases, it is useful to print specific information as you go
instead of after the fact. In this flight recorder, this is called
*tracing*, and can be enabled for each recorder individually.

When tracing for a given recorder is enabled, the trace entries for
that recorder are dumped immediately after being recorded. They are
still stored in the flight recorder for later replay by
`recorder_dump`.

Tracing can be activated by the `recorder_trace_set` function, which
takes a string specifying which traces to activate. The specification
is a colon or space separated list of trace settings, each of them
specifying a regular expression to match recorder names, optionally
followed by an `=` sign and a numerical value. If no numerical value
is given, then the value `1` is assumed.

For example, the trace specification `foo:bar=0:b[a-z]z.*=3` sets the
recorder trace for `foo` to value `1` (enabling tracing for that
recorder), sets the recorder trace for `bar` to `0`, and sets all
recorders with a name matching regular expression `b[a-z]z.*` to
value `3`.

Three values for the trace specification have special meaning:

* The `list` and `help` values will print the list of available
  recorders on `stderr`.

* The `all` value will be turned into the catch-all `.*`
  regular expression.


## Using the RECORDER_TRACES environment variables

If your application calls `recorder_dump_on_common_signals` (see below),
 then traces will be activated or deactivated according to the
`RECORDER_TRACES` environment variable.

Your application can define traces from another application-specific
environment variable with code like:

    recorder_trace_set(getenv("MY_APP_TRACES"));


## Recorder trace value

The `RECORDER_TRACE(name)` macro lets you test if the recorder trace
for `name` is activated. The associated value is an `intptr_t`.
Any non-zero value activates tracing for that recorder.

You may set the value for `RECORDER_TRACE` to any value that fits in
an `intptr_t` with `recorder_trace_set`. This can be used for example
to activate special, possibly expensive instrumentation.

For example, to measure the duration and average of a function over N
iterations, you could use code like the following:

    if (RECORDER_TRACE(foo_loops))
    {
        intptr_t loops = RECORDER_TRACE(foo_loops);
        RECORD(foo_loops, "Testing foo() duration");
        uintptr_t start = recorder_tick();
        double sum = 0.0;
        for (int i = 0; i < loops; i++)
            sum += foo();
        uintptr_t duration = recorder_tick() - start;
        RECORD(foo_loops, "Average duration %.3f us, average value %f",
               1e6 * duration / RECORDER_HZ / loops,
               sum / loops);
    }

While this is less-often useful, it is also possible to assign to a
recorder trace value, for example:

    RECORDER_TRACE(foo_loops) = 1000;


## Reacting to signals

It is often desirable to dump the recorder when some specific signal is
received. To detect crashes, for example, you can dump the recorder
when receiving `SIGBUS`, `SIGSEGV`, `SIGILL`, `SIGABRT`, etc. To do
this, call the function `record_dump_on_signal`

    recorder_dump_on_signal(SIGBUS);

When running BSD or macOS, you can have your program dump the current
state of the recorder by adding a signal handler for `SIGINFO`. You
can then dump the recorder at any time by pressing a key (typically
Control-T) in the terminal.

In order to dump the recorder on common signals, the call
`recorder_dump_on_common_signals (0,0)` will install handlers for the
following signals if they exist on the platform:

* `SIGQUIT`
* `SIGILL`
* `SIGABRT`
* `SIGBUS`
* `SIGSEGV`
* `SIGSYS`
* `SIGXCPU`
* `SIGXFSZ`
* `SIGINFO`
* `SIGUSR1`
* `SIGUSR2`
* `SIGSTKFLT`
* `SIGPWR`

The two arguments are bitmask that you can use to add or remove
signals. For instance, if you want to get a recorder dump on `SIGINT`
but none on `SIGSEGV` or `SIGBUS`, you can use:

    unsigned enable = 1U << SIGINT;
    unsigned disable = (1U << SIGSEGV) | (1U << SIGBUS);
    recorder_dump_on_common_signals(enable, disable);
