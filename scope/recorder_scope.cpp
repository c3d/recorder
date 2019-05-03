// *****************************************************************************
// recorder_scope.cpp                                           Recorder project
// *****************************************************************************
//
// File description:
//
//     A quick oscilloscope-like visualizer for the flight recorder
//
//
//
//
//
//
//
//
// *****************************************************************************
// This software is licensed under the GNU General Public License v3+
// (C) 2017-2019, Christophe de Dinechin <christophe@dinechin.org>
// (C) 2018, Frediano Ziglio <fziglio@redhat.com>
// *****************************************************************************
// This file is part of Recorder
//
// Recorder is free software: you can redistribute it and/or modify
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

#include "recorder_view.h"
#include "recorder_slider.h"
#include "recorder.h"

#include <QtWidgets/QApplication>
#include <QtWidgets/QMainWindow>
#include <QtWidgets/QVBoxLayout>


static void usage(const char *progname)
// ----------------------------------------------------------------------------
//   Display usage information about the program
// ----------------------------------------------------------------------------
{
    printf("Usage: %s [[-c config][-s slider][chan_re]...]\n"
           "\n"
           "  Arguments:\n"
           "    chan_re         : Add view with channels matching regexp\n"
           "    -c config       : Send configuration command\n"
           "    -s slider       : Setup a control slider\n"
           "    -d delay        : Set max delay in seconds\n"
           "    -w samples      : Set max width in samples (0 = window width)\n"
           "    -t              : Show/hide time graph\n"
           "    -m              : Show/hide min/max graph\n"
           "    -a              : Show/hide average graph\n"
           "    -n              : Show/hide normal vaue graph\n"
           "    -r ratio        : Set averaging ratio in percent\n"
           "    -b basename     : Set basename for saving data\n"
           "    -g WxH@XxY      : Set window geometry to W x H pixels\n"
           "\n"
           "  Configuration syntax for -c matches RECORDER_TRACES syntax\n"
           "  Slider syntax is slider[=value[:min:max]]\n"
           "\n"
           "  See http://github.com/c3d/recorder for more information\n"
           "\n"
           "  Examples of arguments:\n"
           "    -c '.*errors'     : Enable display of all errors\n"
           "    -c rate=10        : Set 'rate' tweak to 10\n"
           "    -s rate=10:2:39   : Create slider to set 'rate'\n"
           "                        initial value 10, range 2 to 39\n"
           "    my_graph          : Show graph for channel 'my_graph'\n"
           "    (min|max)_rate    : Show min_rate and max_rate graph\n",
           progname);
}


int main(int argc, char *argv[])
// ----------------------------------------------------------------------------
//   Create the main widget for the oscilloscope and display it
// ----------------------------------------------------------------------------
{
    recorder_trace_set(".*_warning|.*_error");
    const char *path = recorder_export_file();
    recorder_chans_p chans = recorder_chans_open(path);
    if (!chans)
    {
        fprintf(stderr, "Unable to open recorder shared memory '%s'\n", path);
        return 1;
    }

    QApplication a(argc, argv);
    QMainWindow window;
    QWidget *widget = new QWidget;
    QVBoxLayout *layout = new QVBoxLayout;
    int views = 0;
    int configurations = 0;
    int width = -1, height = -1;
    int posx = -1, posy = -1;
    for (int a = 1; a < argc; a++)
    {
        QString arg = argv[a];
        if (arg == "-h")
        {
            usage(argv[0]);
        }
        else if (arg == "-n")
        {
            RecorderView::showNormal = !RecorderView::showNormal;
        }
        else if (arg == "-t")
        {
            RecorderView::showTiming = !RecorderView::showTiming;
        }
        else if (arg == "-m")
        {
            RecorderView::showMinMax = !RecorderView::showMinMax;
        }
        else if (arg == "-a")
        {
            RecorderView::showAverage = !RecorderView::showAverage;
        }
        else if (arg == "-c" && a+1 < argc)
        {
            if (!recorder_chans_configure(chans, argv[++a]))
            {
                fprintf(stderr, "Insufficient command space to send '%s'\n",
                        argv[a]);
                return 3;
            }
            configurations++;
        }
        else if (arg == "-s" && a+1 < argc)
        {
            QGroupBox *slider = RecorderSlider::make(path, chans, argv[++a]);
            layout->addWidget(slider);
        }
        else if (arg == "-d" && a+1 < argc)
        {
            RecorderView::maxDuration = strtod(argv[++a], NULL);
        }
        else if (arg == "-w" && a+1 < argc)
        {
            RecorderView::maxWidth = strtoul(argv[++a], NULL, 10);
        }
        else if (arg == "-r" && a+1 < argc)
        {
            double ratio = strtod(argv[++a], NULL);
            if (ratio <= 0.0 || ratio >= 100.0)
                fprintf(stderr, "Ratio %f must be in 0-100\n", ratio);
            else
                RecorderView::averagingRatio = ratio * 0.01;
        }
        else if (arg == "-b" && a+1 < argc)
        {
            RecorderView::saveBaseName = argv[++a];
        }
        else if (arg == "-g" && a+1 < argc)
        {
            int rc = sscanf(argv[++a], "%dx%d@%dx%d",
                            &width, &height, &posx, &posy);
            if (rc != 2 && rc != 4)
                fprintf(stderr, "-g %s was invalid, "
                        "width=%d, height=%d, x=%d, y=%d\n",
                        argv[a], width, height, posx, posy);
        }
        else if (arg[0] == '-')
        {
            fprintf(stderr, "Invalid option %s\n", argv[a]);
            usage(argv[0]);
        }
        else
        {
            RecorderView *view = new RecorderView(path, chans, argv[a]);
            layout->addWidget(view);
            views++;
        }
    }

    if (views == 0 && configurations == 0)
    {
        RecorderView *recorderView = new RecorderView(path, chans, ".*");
        layout->addWidget(recorderView);
    }

    int result = 0;
    if (views > 0 || configurations == 0)
    {
        layout->setContentsMargins(4, 4, 4, 4);
        widget->setLayout(layout);
        window.setCentralWidget(widget);
        if (width > 0 && height > 0)
            window.resize(width, height);
        if (posx > 0 && posy > 0)
            window.move(posx, posy);
        window.show();
        result = a.exec();
    }

    recorder_chans_close(chans);
    return result;
}
