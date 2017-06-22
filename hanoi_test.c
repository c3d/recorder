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
//  (C) 2017 Christophe de Dinechin <christophe@dinechin.org>
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

int main(int argc, char **argv)
{
    int i;
    recorder_dump_on_common_signals(0, 0);
    for (i = 1; i < argc; i++)
    {
        int count = atoi(argv[i]);
        RECORD(TIMING,"Begin printing Hanoi with %d", count);
        hanoi_print(count, LEFT, MIDDLE, RIGHT);
        RECORD(TIMING,"End printing Hanoi with %d", count);
        RECORD(TIMING,"Begin recording Hanoi with %d", count);
        hanoi_record(count, LEFT, MIDDLE, RIGHT);
        RECORD(TIMING,"End recording Hanoi with %d", count);
    }
    recorder_dump_for("TIMING");
}
