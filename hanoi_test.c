// *****************************************************************************
// hanoi_test.c                                                 Recorder project
// *****************************************************************************
//
// File description:
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
// *****************************************************************************
// This software is licensed under the GNU Lesser General Public License v2+
// (C) 2017-2019, Christophe de Dinechin <christophe@dinechin.org>
// *****************************************************************************
// This file is part of Recorder
//
// Recorder is free software: you can redistribute it and/or modify
// it under the terms of the GNU Lesser General Public License as published by
// the Free Software Foundation, either version 2 of the License, or
// (at your option) any later version.
//
// Recorder is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Lesser General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public License
// along with Recorder, in a file named COPYING.
// If not, see <https://www.gnu.org/licenses/>.
// *****************************************************************************

#include "recorder.h"
#include <stdio.h>
#include <stdlib.h>


RECORDER(MOVE, 1024, "Moving pieces around");
RECORDER(TIMING, 32, "Timing information");

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
        record(MOVE,"Move disk from %s to %s", postName[left], postName[right]);
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
