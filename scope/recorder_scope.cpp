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


int main(int argc, char *argv[])
// ----------------------------------------------------------------------------
//   Create the main widget for the oscilloscope and display it
// ----------------------------------------------------------------------------
{
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
        if (arg == "-c" && ++a < argc)
        {
            if (!recorder_chans_configure(chans, argv[a]))
            {
                fprintf(stderr, "Insufficient command space to send '%s'\n",
                        argv[a]);
                return 3;
            }
            configurations++;
        }
        else if (arg == "-s" && ++a < argc)
        {
            QGroupBox *slider = RecorderSlider::make(path, chans, argv[a]);
            layout->addWidget(slider);
        }
        else
        {
            RecorderView *recorderView = new RecorderView(path, chans, argv[a]);
            layout->addWidget(recorderView);
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
