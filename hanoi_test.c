// ****************************************************************************
//  hanoi_test.c                                              Recorder project
// ****************************************************************************
//
//   File Description:
//
//      A simple illustration of the recorder on the Towers of Hanoi problem
//
//
//
//
//
//
//
//
// ****************************************************************************
//  (C) 2017-2018 Christophe de Dinechin <christophe@dinechin.org>
//   This software is licensed under the GNU General Public License v3
//   See file LICENSE for details.
// ****************************************************************************

#include "recorder.h"
#include <stdio.h>
#include <stdlib.h>


RECORDER(MOVE, 1024, "Moving pieces around")
RECORDER(TIMING, 32, "Timing information")

typedef enum post { LEFT, MIDDLE, RIGHT } post;
const char *postName[] = { "LEFT", "MIDDLE", "RIGHT" };

void hanoi_print(int n, post left, post right, post middle)
{
    if (n == 1)
    {
        printf("Move disk from %s to %s\n", postName[left], postName[right]);
    }
    else
    {
        hanoi_print(n-1, left, middle, right);
        hanoi_print(1, left, right, middle);
        hanoi_print(n-1, middle, right, left);
    }
}

void hanoi_record(int n, post left, post right, post middle)
{
    if (n == 1)
    {
        RECORD(MOVE,"Move disk from %s to %s", postName[left], postName[right]);
    }
    else
    {
        hanoi_record(n-1, left, middle, right);
        hanoi_record(1, left, right, middle);
        hanoi_record(n-1, middle, right, left);
    }
}

void hanoi_record_fast(int n, post left, post right, post middle)
{
    if (n == 1)
    {
        RECORD_FAST(MOVE,"Move disk from %s to %s",
                    postName[left], postName[right]);
    }
    else
    {
        hanoi_record_fast(n-1, left, middle, right);
        hanoi_record_fast(1, left, right, middle);
        hanoi_record_fast(n-1, middle, right, left);
    }
}

int main(int argc, char **argv)
{
    int i;
    uintptr_t duration;
    recorder_dump_on_common_signals(0, 0);
#define DURATION ((double) duration / RECORDER_HZ)
    for (i = 1; i < argc; i++)
    {
        int count = atoi(argv[i]);

#define TEST(Info, Code)                                        \
        record(TIMING,                                          \
               "Begin " Info " with %d iterations", count);     \
        duration = recorder_tick();                             \
        Code;                                                   \
        duration = recorder_tick() - duration;                  \
        record(TIMING,                                          \
               "End " Info " with %d iterations, "              \
               "duration %.6fs",                                \
               count, DURATION);

        TEST("printing Hanoi",
             hanoi_print(count, LEFT, MIDDLE, RIGHT));
        TEST("recording Hanoi",
             hanoi_record(count, LEFT, MIDDLE, RIGHT));
        TEST("fast recording Hanoi",
             hanoi_record_fast(count, LEFT, MIDDLE, RIGHT));
    }
    recorder_dump_for("TIMING");
}
