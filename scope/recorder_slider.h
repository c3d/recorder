#ifndef RECORDER_SLIDER_H
#define RECORDER_SLIDER_H
// *****************************************************************************
// recorder_slider.h                                            Recorder project
// *****************************************************************************
//
// File description:
//
//     Slider that can be used to adjust a recorder tweakable in target app
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

#include "recorder.h"

#include <QtWidgets/QSlider>
#include <QtWidgets/QGroupBox>
#include <QtWidgets/QLabel>

class RecorderSlider : public QSlider
// ----------------------------------------------------------------------------
//   Slider for recorder data
// ----------------------------------------------------------------------------
{
public:
    explicit RecorderSlider(const char       *filename,
                            recorder_chans_p &chans,
                            const char       *specification,
                            QGroupBox        *group,
                            QLabel           *minLabel,
                            QLabel           *maxLabel,
                            QLabel           *valueLabel,
                            QWidget          *parent = 0);
    ~RecorderSlider();

public:
    static QGroupBox *make(const char *filename,
                           recorder_chans_p &chans,
                           const char       *specification);
    void setup(const char *specification);
    void updateSetup();
    void valueChanged(int value);

private:
    const char *      filename;
    recorder_chans_p &chans;
    const char *      specification;
    bool              sourceChanged;
    QString           name;
    int               min;
    int               max;
    QGroupBox *       group;
    QLabel *          minLabel;
    QLabel *          maxLabel;
    QLabel *          valueLabel;
};

#endif // RECORDER_SLIDER_H
