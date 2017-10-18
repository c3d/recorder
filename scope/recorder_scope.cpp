// ****************************************************************************
//  recorder_scope.cpp                                 Flight recorder project
// ****************************************************************************
//
//   File Description:
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
// ****************************************************************************
//  (C) 2017 Christophe de Dinechin <christophe@dinechin.org>
//   This software is licensed under the GNU General Public License v3
//   See LICENSE file for details.
// ****************************************************************************

#include "recorder_view.h"
#include "recorder_slider.h"
#include "recorder.h"

#include <QtWidgets/QApplication>
#include <QtWidgets/QMainWindow>
#include <QtWidgets/QVBoxLayout>


void usage(const char *progname)
// ----------------------------------------------------------------------------
//   Display usage information about the program
// ----------------------------------------------------------------------------
{
    printf("Usage: %s [[-c config][-s slider][chan_re]...]\n"
           "\n"
           "  Arguments:\n"
           "    -c config       : Send configuration command\n"
           "    -s slider       : Setup a control slider\n"
           "    -d delay        : Set max delay in seconds\n"
           "    -w delay        : Set max width in samples (0 = window width)\n"
           "    chan_re         : Add view with channels matching regexp\n"
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
    for (int a = 1; a < argc; a++)
    {
        QString arg = argv[a];
        if (arg == "-h")
        {
            usage(argv[0]);
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
            RecorderView::max_duration = strtod(argv[++a], NULL);
        }
        else if (arg == "-w" && a+1 < argc)
        {
            RecorderView::max_width = strtoul(argv[++a], NULL, 10);
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
        widget->setLayout(layout);
        window.setCentralWidget(widget);
        window.resize(600, 400);
        window.show();
        result = a.exec();
    }

    recorder_chans_close(chans);
    return result;
}
