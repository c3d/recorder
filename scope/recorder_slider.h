#ifndef RECORDER_SLIDER_H
#define RECORDER_SLIDER_H
// ****************************************************************************
//  recorder_slider.h                                  Flight recorder project
// ****************************************************************************
//
//   File Description:
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
// ****************************************************************************
//  (C) 2017-2018 Christophe de Dinechin <christophe@dinechin.org>
//   This software is licensed under the GNU General Public License v3
//   See LICENSE file for details.
// ****************************************************************************

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
