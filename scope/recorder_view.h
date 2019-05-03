#ifndef RECORDER_VIEW_H
#define RECORDER_VIEW_H
// *****************************************************************************
// recorder_view.h                                              Recorder project
// *****************************************************************************
//
// File description:
//
//     View source that feeds from the flight recorder
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

#include <QtCharts/QChartView>
#include <QtCharts/QLineSeries>
#include <QtCharts/QLogValueAxis>
#include <QtCharts/QScatterSeries>
#include <QtCharts/QValueAxis>
#include <QtCharts/QXYSeries>
#include <QtCore/QElapsedTimer>
#include <QtCore/QObject>
#include <QtCore/QTimer>
#include <QtWidgets/QLabel>

QT_CHARTS_USE_NAMESPACE

class RecorderView : public QChartView
// ----------------------------------------------------------------------------
//   View for recorder data
// ----------------------------------------------------------------------------
{
    Q_OBJECT
public:
    explicit RecorderView(const char *filename,
                          recorder_chans_p &chans,
                          const char *pattern,
                          QWidget *parent = 0);
    ~RecorderView();
    void setup();
    void updateSetup();

public slots:
    void updateSeries();
    void sceneChanged();

public:
    void keyPressEvent(QKeyEvent *event) override;

public:
    static double            maxDuration;
    static unsigned          maxWidth;
    static double            averagingRatio;
    static bool              showNormal;
    static bool              showTiming;
    static bool              showMinMax;
    static bool              showAverage;
    static QString           saveBaseName;

private:
    typedef enum { NONE, NORMAL, MINIMUM, MAXIMUM, AVERAGE, TIMING } series_et;
    const char *             filename;
    const char *             pattern;
    recorder_chans_p         &chans;
    bool                     sourceChanged;

    typedef QVector<QPointF> Points;
    QVector<Points>          data;
    QVector<QLineSeries *>   seriesList;
    QVector<recorder_chan_p> chanList;
    QVector<ringidx_t>       readerIndex;
    QVector<series_et>       seriesType;

    QChart *                 chart;
    QValueAxis *             xAxis;
    QValueAxis *             yAxis;
    QValueAxis *             tAxis;
    QTimer                   dataUpdater;

    bool                     viewHasNormal;
    bool                     viewHasTiming;
    bool                     viewHasMinMax;
    bool                     viewHasAverage;

    static Points            minimum(const Points &);
    static Points            maximum(const Points &);
    static Points            average(const Points &);
    static Points            timing(const Points &);
};

#endif // RECORDER_VIEW_H
