#ifndef RECORDER_VIEW_H
#define RECORDER_VIEW_H
// ****************************************************************************
//  recorder_view.h                                    Flight recorder project
// ****************************************************************************
//
//   File Description:
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
// ****************************************************************************
//  (C) 2017-2018 Christophe de Dinechin <christophe@dinechin.org>
//   This software is licensed under the GNU General Public License v3
//   See LICENSE file for details.
// ****************************************************************************

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
    static double            maxDuration;
    static unsigned          maxWidth;

private:
    const char *             filename;
    const char *             pattern;
    recorder_chans_p         &chans;
    bool                     sourceChanged;

    typedef QVector<QPointF> Points;
    QVector<Points>          data;
    QVector<QLineSeries *>   seriesList;
    QVector<recorder_chan_p> chanList;
    QVector<ringidx_t>       readerIndex;

    QChart *                 chart;
    QValueAxis *             xAxis;
    QValueAxis *             yAxis;
    QTimer                   dataUpdater;

};

#endif // RECORDER_VIEW_H
