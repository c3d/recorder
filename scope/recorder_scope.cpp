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
    if (argc <= 1)
    {
        RecorderView *recorderView = new RecorderView(chans, ".*");
        layout->addWidget(recorderView);
    }
    else for (int arg = 1; arg < argc; arg++)
    {
        RecorderView *recorderView = new RecorderView(chans, argv[arg]);
        layout->addWidget(recorderView);
    }
    widget->setLayout(layout);
    window.setCentralWidget(widget);
    window.resize(600, 400);
    window.show();

    int result = a.exec();
    recorder_chans_close(chans);
    return result;
}
