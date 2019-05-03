// *****************************************************************************
// recorder_view.cpp                                            Recorder project
// *****************************************************************************
//
// File description:
//
//     View that fetches data from the flight recorder and displays it
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

#include <QtCore/QtMath>
#include <QMutex>
#include <QRegExp>
#include <QFileInfo>
#include <QGraphicsLayout>
#include <errno.h>

QT_CHARTS_USE_NAMESPACE


static const double timeUnit = 1e-6;
static const double timeScale = 1.0 / timeUnit;


RecorderView::RecorderView(const char *filename,
                           recorder_chans_p &chans,
                           const char *pattern,
                           QWidget *parent)
// ----------------------------------------------------------------------------
//   Constructor opens the shared memory for recorder data
// ----------------------------------------------------------------------------
    : QChartView(parent),
      filename(filename), pattern(pattern), chans(chans),
      sourceChanged(false),
      viewHasNormal(showNormal),
      viewHasTiming(showTiming),
      viewHasMinMax(showMinMax),
      viewHasAverage(showAverage)
{
    xAxis = new QValueAxis;
    yAxis = new QValueAxis; // Or QLogValueAxis?
    tAxis = new QValueAxis;
    xAxis->setRange(0, 20.0);
    yAxis->setRange(-10.0, 10.0);
    tAxis->setRange(0, 100.0);
    if (viewHasTiming)
        chart->addAxis(tAxis, Qt::AlignRight);

    chart = new QChart();
    chart->layout()->setContentsMargins(0, 0, 0, 0);
    // chart->legend()->hide();
    chart->addAxis(xAxis, Qt::AlignBottom);
    chart->addAxis(yAxis, Qt::AlignLeft);
    setChart(chart);

    QObject::connect(chart->scene(), &QGraphicsScene::changed,
                     this, &RecorderView::sceneChanged);

    // Timer setup
    dataUpdater.setInterval(0);
    dataUpdater.setSingleShot(true);
    QObject::connect(&dataUpdater, &QTimer::timeout,
                     this, &RecorderView::updateSeries);
    chart->setTheme(QChart::ChartThemeBlueCerulean);

    // Data construction
    if (chans)
        setup();
}


RecorderView::~RecorderView()
// ----------------------------------------------------------------------------
//   Destructor suspends data acquisition and deletes view elements
// ----------------------------------------------------------------------------
{
    dataUpdater.stop();
    delete chart;
    delete xAxis;
    delete yAxis;
    delete tAxis;
}


void RecorderView::setup()
// ----------------------------------------------------------------------------
//   Setup the channels that match the pattern
// ----------------------------------------------------------------------------
{
    unsigned i = 0;
    recorder_chan_p chan = NULL;
    bool hasGL = getenv ("RECORDER_NOGL") == NULL;
    const char *colors[] = {
        "yellow", "red", "lightgreen", "orange",
        "cyan", "lightgray", "pink", "lightyellow",
    };
    const unsigned numColors = sizeof(colors) / sizeof(colors[0]);

    while (true)
    {
        chan = recorder_chan_find(chans, pattern, chan);
        if (!chan)
            break;
        if (chanList.indexOf(chan) != -1)
            break;

        const char *name = recorder_chan_name(chan);
        const char *info = recorder_chan_description(chan);
        const char *unit = recorder_chan_unit(chan);
        recorder_data min = recorder_chan_min(chan);
        recorder_data max = recorder_chan_max(chan);

        bool hasNormal = viewHasNormal;
        bool hasMin = viewHasMinMax;
        bool hasMax = viewHasMinMax;
        bool hasAverage = viewHasAverage;
        bool hasTiming = viewHasTiming;
        series_et type = NONE;

        while (true)
        {
            type = hasNormal    ? (hasNormal = false,   NORMAL)
                :  hasMin       ? (hasMin = false,      MINIMUM)
                :  hasMax       ? (hasMax = false,      MAXIMUM)
                :  hasAverage   ? (hasAverage = false,  AVERAGE)
                :  hasTiming    ? (hasTiming = false,   TIMING)
                :  NONE;
            if (type == NONE)
                 break;

            int colorIndex = i++ % numColors;
            QLineSeries *series = new QLineSeries;

            chart->addSeries(series);
            seriesList.append(series);
            data.append(Points());
            chanList.append(chan);
            readerIndex.append(0);
            seriesType.append(type);

            QPen pen(QBrush(QColor(colors[colorIndex])), 2.0);
            pen.setCosmetic(true);
            series->setPen(pen);
            series->setUseOpenGL(hasGL);
            series->attachAxis(xAxis);
            series->attachAxis(type == TIMING ? tAxis : yAxis);

            QString fancyName = name;
            switch(type)
            {
            case MINIMUM: fancyName += " (min)"; break;
            case MAXIMUM: fancyName += " (max)"; break;
            case AVERAGE: fancyName += " (avg)"; break;
            case TIMING:  fancyName += " (dur)"; break;
            default:      break;
            }

            printf("Channel #%u %s (%s): %ld %s-%ld %s\n", i,
                   fancyName.toUtf8().data(),
                   info, min.signed_value, unit,
                   max.signed_value, unit);

            series->setName(fancyName);
        }
    }
}


void RecorderView::updateSetup()
// ----------------------------------------------------------------------------
//   Setup the channels that match the pattern
// ----------------------------------------------------------------------------
{
    // Update chans only once (it's a pointer shared across all views)
    static QMutex chansUpdateMutex;
    chansUpdateMutex.lock();
    if (!recorder_chans_valid(chans))
    {
        fprintf(stderr, "Recorder channels became invalid, re-initializing\n");
        recorder_chans_close(chans);
        chans = recorder_chans_open(filename);
    }
    chansUpdateMutex.unlock();

    // Update the view with the new channels
    chart->removeAllSeries();
    data.clear();
    seriesList.clear();
    chanList.clear();
    readerIndex.clear();
    seriesType.clear();
    setup();
}


void RecorderView::updateSeries()
// ----------------------------------------------------------------------------
//   Update all data series by reading the latest data
// ----------------------------------------------------------------------------
{
    if (!recorder_chans_valid(chans))
    {
        if (!sourceChanged)
        {
            // A new program started with the same shared memory file
            // Wait a bit to make sure all channels are setup by new instance
            // Then update this view.
            // All views will presumably fail at the same time and get updated
            sourceChanged = true;
            dataUpdater.setInterval(100);
            dataUpdater.start();
            return;
        }
    }
    if (sourceChanged)
    {
        updateSetup();
        sourceChanged = false;
    }

    size_t numSeries = seriesList.size();
    size_t width =
          maxWidth > 0    ? maxWidth
        : maxDuration > 0 ? this->width() * 10
        : this->width();
    double minX = -1.0, maxX = 1.0;
    double minY = -1.0, maxY = 1.0;
    double maxT = timeUnit;
    bool first = true;
    bool updated = false;

    for (size_t s = 0; s < numSeries; s++)
    {
        recorder_chan_p chan = chanList[s];
        ringidx_t &ridx = readerIndex[s];
        size_t readable = recorder_chan_readable(chan, &ridx);
        QLineSeries *series = seriesList[s];
        Points &dataPoints = data[s];
        series_et type = seriesType[s];

        if (readable)
        {
            size_t dataLen = dataPoints.size();
            if (dataLen > width)
            {
                dataPoints.remove(0, dataLen - width);
                dataLen = width;
            }
            if (readable > width)
                readable = width;

            QVector<recorder_data> recorderDataRead(2 * readable);
            Points pointsRead(readable);

            recorder_data *rbuf = recorderDataRead.data();
            QPointF *pbuf = pointsRead.data();
            size_t count = recorder_chan_read(chan, rbuf, readable, &ridx);

            Q_ASSERT(count <= readable);
            double scale = 1.0 / RECORDER_HZ;
            if (count)
            {
                switch(recorder_chan_type(chan))
                {
                case RECORDER_NONE:
                    Q_ASSERT(!"Recorder channel has invalid type NONE");
                    break;
                case RECORDER_INVALID:
                    // Recorder format is invalid, put some fake data
                    for (size_t p = 0; p < count; p++)
                        pbuf[p] = QPointF(p, p % 32);
                    break;
                case RECORDER_SIGNED:
                    for (size_t p = 0; p < count; p++)
                        pbuf[p] = QPointF(rbuf[2*p].unsigned_value * scale,
                                          rbuf[2*p+1].signed_value);
                    break;
                case RECORDER_UNSIGNED:
                    for (size_t p = 0; p < count; p++)
                        pbuf[p] = QPointF(rbuf[2*p].unsigned_value * scale,
                                          rbuf[2*p+1].unsigned_value);
                    break;
                case RECORDER_REAL:
                    for (size_t p = 0; p < count; p++)
                        pbuf[p] = QPointF(rbuf[2 * p].unsigned_value * scale,
                                          rbuf[2 * p + 1].real_value);
                    break;
                }

                size_t newLen = count + dataLen;
                size_t toRemove = newLen > width ? newLen - width : 0;
                if (toRemove)
                    dataPoints.remove(0, toRemove);
                if (count != readable)
                    pointsRead.resize(count);
                dataPoints.append(pointsRead);

                switch(type)
                {
                case NORMAL:
                    series->replace(dataPoints);
                    break;
                case MINIMUM:
                    series->replace(minimum(dataPoints));
                    break;
                case MAXIMUM:
                    series->replace(maximum(dataPoints));
                    break;
                case AVERAGE:
                    series->replace(average(dataPoints));
                    break;
                case TIMING:
                    series->replace(timing(dataPoints));
                    break;
                default:
                    Q_ASSERT(!"Unkown series type");
                }
                updated = true;
            }
        }

        QPointF *pbuf = dataPoints.data();
        size_t count = dataPoints.size();
        qreal lastT = pbuf[0].x();
        for (size_t p = 0; p < count; p++)
        {
            double x = pbuf[p].x();
            double y = pbuf[p].y();
            if (first)
            {
                minX = x;
                maxX = x;
                minY = y;
                maxY = y;
                maxT = 0;
                first = false;
            }
            else
            {
                if (maxX < x)
                    maxX = x;
                if (minX > x)
                    minX = x;
                if (type == TIMING)
                {
                    double t = (x - lastT) * timeScale;
                    if (maxT < t)
                        maxT = t;
                    lastT = x;
                }
                else
                {
                    if (maxY < y)
                        maxY = y;
                    if (minY > y)
                        minY = y;
                }
            }
        }

        if (maxDuration > 0.0)
        {
            minX = maxX - maxDuration;
            size_t lowP = 0;
            for (size_t p = 0; p < count; p++)
            {
                double x = pbuf[p].x();
                if (x < minX)
                    lowP = p;
            }
            if (lowP > 0)
                dataPoints.remove(0, lowP);
        }
    }

    if (updated)
    {
        double range = fabs(maxY - minY);
        double scale = 1;
        while (scale < range)
        {
            scale *= 2;
            if (scale >= range)
                break;
            scale *= 2.5;
            if (scale >= range)
                break;
            scale *= 2;
        }
        minY = floor(minY / scale) * scale;
        maxY = ceil(maxY / scale) * scale;

        xAxis->setRange(minX, maxX);
        yAxis->setRange(minY, maxY);

        if (tAxis)
        {
            double range = maxT;
            double scale = 1;
            while (scale < range)
            {
                scale *= 2;
                if (scale >= range)
                    break;
                scale *= 2.5;
                if (scale >= range)
                    break;
                scale *= 2;
            }
            maxT = ceil(maxT / scale) * scale;
            tAxis->setRange(0, maxT);
        }
    }
    else
    {
        dataUpdater.setInterval(30);
        dataUpdater.start();
    }
}


void RecorderView::sceneChanged()
// ----------------------------------------------------------------------------
//   Trigger a data update when a scene change occured
// ----------------------------------------------------------------------------
{
    dataUpdater.setInterval(0);
    dataUpdater.start();
}


void RecorderView::keyPressEvent(QKeyEvent *event)
// ----------------------------------------------------------------------------
//   Respond to 'space' key by capturing image and log values
// ----------------------------------------------------------------------------
{
    int key = event->key();
    bool saveCSV = false;
    bool saveImage = false;

    switch (key)
    {
    case ' ':
        saveCSV = saveImage = true;
        break;
    case 'i': case 'I':
        saveImage = true;
        break;
    case 'c': case 'C':
        saveCSV = true;
        break;
    case 'n': case 'N':
        viewHasNormal = !viewHasNormal;
        sourceChanged = true;
        break;
    case 't': case 'T':
        viewHasTiming = !viewHasTiming;
        sourceChanged = true;
        if (viewHasTiming)
            chart->addAxis(tAxis, Qt::AlignRight);
        else
            chart->removeAxis(tAxis);
        break;
    case 'm': case 'M':
        viewHasMinMax = !viewHasMinMax;
        sourceChanged = true;
        break;
    case 'a': case 'A':
        viewHasAverage = !viewHasAverage;
        sourceChanged = true;
        break;
    }

    if (!saveImage && !saveCSV)
        return;

    // Find a name that can be used both for CSV and PNG file
    static unsigned index = 0;
    QString basename = saveBaseName + "%1";
    QString name = basename.arg(++index);
    while ((saveCSV && QFileInfo(name + ".csv").exists()) ||
           (saveImage && QFileInfo(name + ".png").exists()))
        name = QString(basename).arg(++index);

    if (saveImage)
    {
        bool hasGL = getenv ("RECORDER_NOGL") == NULL;

        QPixmap pixmap(size());
        QPainter painter(&pixmap);
        for (auto s : seriesList)
            s->setUseOpenGL(false);
        render(&painter);
        for (auto s : seriesList)
            s->setUseOpenGL(hasGL);
        pixmap.save(name + ".png");
    }

    if (saveCSV)
    {
        // Use data directly from series, as it may have gone through processing
        // for example through timing() or average() functions
        QVector<Points> data;
        for (auto s : seriesList)
            data.append(s->pointsVector());

        QByteArray cname = (name + ".csv").toUtf8();
        FILE *f = fopen(cname.data(), "w");
        if (f)
        {
            size_t columns = data.size();
            size_t rows = data[0].size();
            for (size_t r = 0; r < rows; r++)
            {
                double t = data[0][r].x();
                fprintf(f, "%f", t);
                for (size_t c = 0; c < columns; c++)
                {
                    Q_ASSERT(data[c][r].x() == t);
                    double value = data[c][r].y();
                    fprintf(f, ",%f", value);
                }
                fprintf(f, "\n");
            }
            fclose(f);
        }
        else
        {
            fprintf(stderr, "Error opening %s: %s\n", cname.data(), strerror(errno));
        }
    }
}


RecorderView::Points RecorderView::minimum(const RecorderView::Points &data)
// ----------------------------------------------------------------------------
//   Compute the running minimum for the input values
// ----------------------------------------------------------------------------
{
    Points result(data);
    QPointF *pbuf = result.data();
    size_t count = result.size();
    qreal min = std::numeric_limits<qreal>::max();
    qreal r = averagingRatio;

    for (size_t p = 0; p < count; p++)
    {
        qreal &y = pbuf[p].ry();
        if (min > y)
            min = y;
        else
            min = r * min + (1-r) * y;
        y = min;
    }

    return result;
}


RecorderView::Points RecorderView::maximum(const RecorderView::Points &data)
// ----------------------------------------------------------------------------
//   Compute the running maximum for the input values
// ----------------------------------------------------------------------------
{
    Points result(data);
    QPointF *pbuf = result.data();
    size_t count = result.size();
    qreal max = std::numeric_limits<qreal>::min();
    qreal r = averagingRatio;

    for (size_t p = 0; p < count; p++)
    {
        qreal &y = pbuf[p].ry();
        if (max < y)
            max = y;
        else
            max = r * max + (1-r) * y;
        y = max;
    }

    return result;
}


RecorderView::Points RecorderView::average(const RecorderView::Points &data)
// ----------------------------------------------------------------------------
//   Compute the running average for the input values
// ----------------------------------------------------------------------------
{
    Points result(data);
    QPointF *pbuf = result.data();
    size_t count = result.size();
    qreal avg = 0.0;
    for (size_t p = 0; p < count; p++)
        avg += pbuf[p].y();
    avg /= count ? count : 1;
    qreal r = averagingRatio;

    for (size_t p = 0; p < count; p++)
    {
        qreal &y = pbuf[p].ry();
        avg = r * avg + (1-r) * y;
        y = avg;
    }

    return result;
}


RecorderView::Points RecorderView::timing(const RecorderView::Points &data)
// ----------------------------------------------------------------------------
//   Compute timing information about the input values
// ----------------------------------------------------------------------------
{
    Points result(data);
    QPointF *pbuf = result.data();
    size_t count = result.size();
    qreal last = data[0].x();

    for (size_t p = 0; p < count; p++)
    {
        qreal t = pbuf[p].x();
        qreal &y = pbuf[p].ry();
        y = (t - last) * timeScale;
        last = t;
    }

    return result;
}


double   RecorderView::maxDuration    = 0.0;
unsigned RecorderView::maxWidth       = 0;
double   RecorderView::averagingRatio = 0.99;
bool     RecorderView::showNormal     = true;
bool     RecorderView::showTiming     = false;
bool     RecorderView::showMinMax     = false;
bool     RecorderView::showAverage    = false;
QString  RecorderView::saveBaseName   = "recorder_scope_data-";
