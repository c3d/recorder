# recorder
A lock-free, real-time flight recorder for your C or C++ programs


## Instrumentation that is always there when you need it

The flight recorder is designed to help you debug complex, real-time,
multi-CPU programs. It lets you instrument their execution with
non-intrusive `printf`-like *record statements*, that capture what is
happening in your program. These record statements are very
inexpensive, so you can leave them in your code all the time, even for
optimized code. See *Performance considerations* at end of this
document for details.

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
push away data from a slower one. This ensures that you can record
important, but slow, events, as well as much more frequent ones.

Here is what a recorder dump can look like:

    recorder.c:518: [0 0.000000] signals: Activating dump for signal mask 0xE3001C58
    recorder_test.c:126: [1 0.000008] MAIN: Launching 16 normal recorder threads
    recorder_test.c:128: [2 0.000045] MAIN: Starting normal speed test for 10s with 16 threads
    recorder_test.c:137: [2392 0.000230] MAIN: Normal recorder testing in progress, please wait about 10s
    recorder_test.c:141: [198750457 9.999914] MAIN: Normal recorder testing completed, stopping threads
    recorder_test.c:98: [198750757 9.999929] SpeedTest: [thread 3] Recording 12448096
    recorder_test.c:98: [198750760 9.999955] SpeedTest: [thread 7] Recording 12527067
    recorder_test.c:98: [198750758 9.999929] SpeedTest: [thread 15] Recording 12379912
    recorder_test.c:98: [198750759 9.999929] SpeedTest: [thread 8] Recording 12446846
    recorder_test.c:98: [198750761 9.999929] SpeedTest: [thread 5] Recording 12429933
    recorder_test.c:147: [198750785 9.999930] Pauses: Waiting for recorder threads to stop, 16 remaining
    recorder_test.c:89: [198750787 9.999946] Pauses: Pausing #0 2401.618us
    recorder_test.c:98: [198750789 10.000008] SpeedTest: [thread 11] Recording 12385726
    recorder_test.c:98: [198750763 9.999929] SpeedTest: [thread 3] Recording 12448097
    recorder_test.c:98: [198750765 9.999929] SpeedTest: [thread 13] Recording 12336853
    recorder_test.c:98: [198750764 9.999929] SpeedTest: [thread 15] Recording 12379913
    recorder_test.c:98: [198750767 9.999929] SpeedTest: [thread 8] Recording 12446847
    recorder_test.c:98: [198750766 9.999929] SpeedTest: [thread 5] Recording 12429934
    recorder_test.c:98: [198738706 10.000035] SpeedTest: [thread 9] Recording 12423995
    recorder_test.c:98: [198750769 9.999929] SpeedTest: [thread 15] Recording 12379914
    recorder_test.c:98: [198750770 9.999929] SpeedTest: [thread 3] Recording 12448098
    recorder_test.c:98: [198750771 9.999929] SpeedTest: [thread 13] Recording 12336854
    recorder_test.c:98: [198750772 9.999929] SpeedTest: [thread 8] Recording 12446848
    recorder_test.c:98: [198750773 9.999929] SpeedTest: [thread 5] Recording 12429935
    recorder_test.c:98: [198750774 9.999929] SpeedTest: [thread 10] Recording 12481275
    recorder_test.c:98: [198750775 9.999929] SpeedTest: [thread 15] Recording 12379915
    recorder_test.c:98: [198750776 9.999929] SpeedTest: [thread 3] Recording 12448099
    recorder_test.c:98: [198750777 9.999929] SpeedTest: [thread 13] Recording 12336855
    recorder_test.c:98: [198750778 9.990261] SpeedTest: [thread 2] Recording 12374034
    recorder_test.c:98: [198750780 9.999930] SpeedTest: [thread 8] Recording 12446849
    recorder_test.c:98: [198750779 9.999930] SpeedTest: [thread 15] Recording 12379916
    recorder_test.c:98: [198750781 9.999930] SpeedTest: [thread 10] Recording 12481276
    recorder_test.c:98: [198750782 9.999930] SpeedTest: [thread 5] Recording 12429937
    recorder_test.c:98: [198750784 9.999930] SpeedTest: [thread 3] Recording 12448100
    recorder_test.c:98: [198750783 9.999930] SpeedTest: [thread 15] Recording 12379917
    recorder_test.c:98: [198750786 9.999930] SpeedTest: [thread 13] Recording 12336856
    recorder_test.c:98: [198750788 9.999971] SpeedTest: [thread 12] Recording 12356775
    recorder_test.c:98: [198750790 10.000029] SpeedTest: [thread 4] Recording 12412805
    recorder_test.c:151: [198750791 10.003075] MAIN: Normal test: all threads have stopped, 198750784 iterations
    recorder_test.c:165: [198750792 10.003117] MAIN: Recorder test complete (Normal version), 16 threads.
    recorder_test.c:166: [198750793 10.003119] MAIN:   Iterations      =  198750784
    recorder_test.c:167: [198750794 10.003121] MAIN:   Iterations / ms =      19875
    recorder_test.c:169: [198750795 10.003122] MAIN:   Record cost     =         50ns

Lines begin with the source code location in the program where the
record was taken. The number following the source location is the
*order* of records, a global sequence number that helps relate records
made in different recorders or from different threads.
The order is followed by a *timestamp*, which counds the number of
seconds since the start of the program. On 32-bit machines,
the timestamp is precise to the ms. On 64-bit machines, it is precise to the
microsecond. Finally, the rest of the record is a printout of what was
recorded.

The recorder dump is generally sorted according to order and should
show time stamps in increasing order. However, as the example above
shows, this may not always be the case. See *Multithreading
considerations* below for an explanation of what this means.

See [this blog article](https://grenouillebouillie.wordpress.com/2016/12/09/a-real-time-lock-free-multi-cpu-flight-recorder)
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
regular expression given as an argument:

    recorder_dump_for(".*TIMING.*"); // Dumps "TIMING", but also "MY_TIMING_2"

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

A given instrumentation or program behavior may require multiple
configurable options, or *tweaks*. The `RECORDER_TWEAK_DEFINE` defines
a tweak adn its initial value. The `RECORDER_TWEAK` macro accesses the
value of a tweak (at a cost comparable to accessing a global
variable).

    RECORDER_TWEAK_DEFINE(foo_loop, 600, "Number of foo iterations");
    for (int i = 0; i < RECORDER_TWEAK(foo_loops); i++) { foo(); }


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


## Performance considerations

The flight recorder `RECORD` statement is designed to cost so little
that you should be able to use it practically anywhere, and in
practically any context, including in signal handlers, interrupt
handlers, etc.

As shown in the data below, a `RECORD` call is faster than a
corresponding call to`snprintf`, because text formatting is only done
at dump time. It is comparable to the cost of a best-case `malloc`
(directly from the free list), and faster than the cost of a typical
`malloc` (with random size).

Most of the cost is actually from keeping track of time, i.e. updating
the `timestamp` field. If you need to instrument the tightest loops in
your code, the `RECORD_FAST` variant can be about twice as fast by
reusing the last time that was recorded in that recorder.

The following figures can help you compare `RECORD` to
various low-cost operations. In all cases, the message being recorded
or printed was the same, `"Speed test %u", i`:

Function                    | Xeon  | Mac   | Pi    | Pi-2  |
----------------------------|-------|-------|-------|-------|
`RECORD_FAST`               |  20ns |  20ns | 129ns |  124ns|
`RECORD`                    |  35ns |  64ns |1070ns |  726ns|
`gettimeofday`              |  16ns |  36ns | 913ns |  675ns|
`memcpy` (512 bytes)        |  26ns |  15ns |1669ns |  499ns|
`malloc` (512 bytes)        |  40ns |  61ns | 603ns |  499ns|
`snprintf`                  |  64ns |  98ns |2530ns | 1071ns|
`fprintf`                   |  64ns |  14ns |4840ns | 2318ns|
Flushed `fprintf`           | 751ns |1334ns |4509ns |14730ns|
`malloc` (512-4K jigsaw)    | 508ns | 483   |3690ns | 5466ns|
Hanoi 20 (printing | wc)    | 320ms | 180ms |19337ms| 7110ms|
Hanoi 20 (recording)        |  60ms |  60ms | 1203ms|  834ms|
Hanoi 20 (fast recording)   |  23ms |  24ms |  262ms|  175ms|


Scalability depending on number of threads

Function                    | Xeon  | Mac   | Pi    | Pi-2  |
----------------------------|-------|-------|-------|-------|
`RECORD_FAST` * 1           |  20ns | 21ns  | 137ns | 133ns |
`RECORD_FAST` * 2           |  92ns | 86ns  | 137ns | 110ns |
`RECORD_FAST` * 4           |  94ns | 76ns  | 137ns | 152ns |
`RECORD_FAST` * 8           |  57ns | 55ns  | 137ns | 152ns |
`RECORD_FAST` * 16          |  52ns | 52ns  | 137ns | 152ns |
`RECORD_FAST` * 32          |  52ns | 54ns  | 137ns | 152ns |
`RECORD` * 1                |  37ns | 60ns  |1315ns | 742ns |
`RECORD` * 2                |  97ns | 93ns  |1076ns | 412ns |
`RECORD` * 4                |  95ns | 71ns  |1080ns | 224ns |
`RECORD` * 8                |  59ns | 54ns  |1083ns | 224ns |
`RECORD` * 16               |  54ns | 50ns  |1084ns | 224ns |
`RECORD` * 32               |  54ns | 53ns  |1372ns | 224ns |


The platforms that were tested are:

* Xeon: a 6-CPU (12-thread) Intel(R) Xeon(R) CPU E5-1650 v4 @ 3.60GHz
  running Fedora 26 Linux kernel, GCC 7.1.1

* Mac: a 4-CPU (8-thread) 2.5GHz Intel Core i7 MacBook Pro (15'
  Retina, mid 2015), Xcode 8.1.0 clang

* Pi: First generation Raspberry Pi, ARMv6 CPU, running Raspbian Linux
  with kernel 4.4.50, GCC 4.9.2

* Pi-2: Second generation Raspberry Pi, 4-way ARMv7 CPU, running
  Raspbian Linux with kernel 4.4.50, GCC 4.9.2


## Multithreading considerations

The example of recorder dump given at the beginning of this document
shows record entries that are printed out of order, and with
non-monotonic time stamps.

Here is an example of non-monotonic timestamp (notice that time for
thread 7 is ahead of time for thread 3 and thread 15):

    recorder_test.c:98: [198750757 9.999929] SpeedTest: [thread 3] Recording 12448096
    recorder_test.c:98: [198750760 9.999955] SpeedTest: [thread 7] Recording 12527067
    recorder_test.c:98: [198750758 9.999929] SpeedTest: [thread 15] Recording 12379912

Here is an example of the entries being out of order (notice that the
order ending in 80 is between those ending in 78 and 79):

    recorder_test.c:98: [198750778 9.990261] SpeedTest: [thread 2] Recording 12374034
    recorder_test.c:98: [198750780 9.999930] SpeedTest: [thread 8] Recording 12446849
    recorder_test.c:98: [198750779 9.999930] SpeedTest: [thread 15] Recording 12379916


This is normal behaviour under heavy load, but requires an
explanation. The `RECORD` statements can be performed simultaneously
from multiple threads. If there is "contention", i.e. if multiple CPUs
are attempting to write at the same time, one CPU may acquire its
order and timestamp *before* another CPU, but may end up writing the
record *after* that other CPU. The same thing may also happen if the
operating system suspends a thread while it is writing the record, in
which case a timestamp discrepancy of several milliseconds may appear
between nearby records.

In general, this should have a minimal impact on the understanding of
what is happening, and may help you pinpoint risks of race condition
in your code.
