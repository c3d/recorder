// *****************************************************************************
// recorder_slider.cpp                                          Recorder project
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

#include "recorder_slider.h"
#include <QtCore/QTextStream>
#include <QtWidgets/QGridLayout>

RecorderSlider::RecorderSlider(const char       *filename,
                               recorder_chans_p &chans,
                               const char       *specification,
                               QGroupBox        *group,
                               QLabel           *minLabel,
                               QLabel           *maxLabel,
                               QLabel           *valueLabel,
                               QWidget          *parent)
// ----------------------------------------------------------------------------
//   Define a slider according to the input spec
// ----------------------------------------------------------------------------
    : QSlider(Qt::Horizontal, parent),
      filename(filename),
      chans(chans),
      specification(specification),
      sourceChanged(false),
      name(specification),
      min(0),
      max(100),
      group(group),
      minLabel(minLabel),
      maxLabel(maxLabel),
      valueLabel(valueLabel)
{
    QObject::connect(this, &QSlider::valueChanged,
                     this, &RecorderSlider::valueChanged);
    setup(specification);
}


RecorderSlider::~RecorderSlider()
// ----------------------------------------------------------------------------
//   Destructor for slider has nothing to do
// ----------------------------------------------------------------------------
{
}


QGroupBox *RecorderSlider::make(const char       *filename,
                                recorder_chans_p &chans,
                                const char       *spec)
// ----------------------------------------------------------------------------
//   Build a control group holding the slider and associated labels
// ----------------------------------------------------------------------------
{
    QGroupBox      *group      = new QGroupBox;
    QGridLayout    *layout     = new QGridLayout;
    QLabel         *minLabel   = new QLabel;
    QLabel         *maxLabel   = new QLabel;
    QLabel         *valueLabel = new QLabel;
    RecorderSlider *slider     = new RecorderSlider(filename, chans, spec,
                                                    group,
                                                    minLabel, maxLabel,
                                                    valueLabel);
    layout->addWidget(minLabel, 0, 0);
    layout->addWidget(slider, 0, 1);
    layout->addWidget(maxLabel, 0, 2);
    layout->addWidget(valueLabel, 1, 1);
    layout->setContentsMargins(0, 0, 0, 0);
    valueLabel->setAlignment(Qt::AlignCenter);
    group->setLayout(layout);

    return group;
}


void RecorderSlider::setup(const char *specification)
// ----------------------------------------------------------------------------
//   Parse the specification into name, min and max
// ----------------------------------------------------------------------------
//   This accepts specifications that look like:
//      name
//      name=3
//      name=3:-10:10
{
    int value = 0;
    QString spec = specification;
    int eq = spec.indexOf('=');
    if (eq < 0)
    {
        name = spec;
    }
    else
    {
        name = spec.mid(0, eq);
        spec = spec.mid(eq+1);

        QStringList args = spec.split(':');
        switch(args.length())
        {
        case 3:
            min = args[1].toInt();
            max = args[2].toInt();
            /* Falls through */
        case 1:
            value = args[0].toInt();
            break;
        default:
            fprintf(stderr,
                    "Invalid slider specification %s\n"
                    "   Expecting one of:\n"
                    "      name\n"
                    "      name=value\n"
                    "      name=value:min:max\n"
                    "   Example: -s slider=0:-10:10\n",
                    specification);
        }
    }

    setRange(min, max);
    setValue(value);

    group->setTitle(name);
    minLabel->setText(QString("%1").arg(min));
    maxLabel->setText(QString("%1").arg(max));
}


void RecorderSlider::updateSetup()
// ----------------------------------------------------------------------------
//   Nothing yet
// ----------------------------------------------------------------------------
{
}


void RecorderSlider::valueChanged(int value)
// ----------------------------------------------------------------------------
//   When the value changes, send the configuration to the target
// ----------------------------------------------------------------------------
{
    valueLabel->setText(QString("%1").arg(value));

    QString config;
    QTextStream ts(&config);
    ts << name << "=" << value;
    const char *constData = config.toUtf8().data();
    if (!recorder_chans_configure(chans, constData))
        fprintf(stderr, "Configuration %s failed\n", constData);
}
