// ****************************************************************************
//  recorder_view.cpp                                  Flight recorder project
// ****************************************************************************
//
//   File Description:
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
// ****************************************************************************
//  (C) 2017 Christophe de Dinechin <christophe@dinechin.org>
//   This software is licensed under the GNU General Public License v3
//   See LICENSE file for details.
// ****************************************************************************

#include "recorder_view.h"

#include <QtCore/QtMath>
#include <QMutex>
#include <QRegExp>

QT_CHARTS_USE_NAMESPACE


RecorderView::RecorderView(const char *filename,
                           recorder_chans_p &chans,
                           const char *pattern,
                           QWidget *parent)
// ----------------------------------------------------------------------------
//   Constructor opens the shared memory for recorder data
// ----------------------------------------------------------------------------
    : QChartView(parent),
      filename(filename), pattern(pattern), chans(chans),
      sourceChanged(false)
{
    xAxis = new QValueAxis;
    yAxis = new QValueAxis; // Or QLogValueAxis?
    xAxis->setRange(0, 20.0);
    yAxis->setRange(-10.0, 10.0);

    chart = new QChart();
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
}


void RecorderView::setup()
// ----------------------------------------------------------------------------
//   Setup the channels that match the pattern
// ----------------------------------------------------------------------------
{
    unsigned i = 0;
    recorder_chan_p chan = NULL;
    bool hasGL = getenv ("RECORDER_NOGL") == NULL;
    const char *colors[] = { "red", "blue", "green", "black" };
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

        printf("Channel #%u %s (%s): %ld %s-%ld %s\n", i,
               name, info, min.signed_value, unit,
               max.signed_value, unit);

        int colorIndex = i++ % numColors;
        QLineSeries *series = new QLineSeries;

        chart->addSeries(series);
        seriesList.append(series);
        data.append(Points());
        chanList.append(chan);
        readerIndex.append(0);

        QPen pen(QBrush(QColor(colors[colorIndex])), 2.0);
        pen.setCosmetic(true);
        series->setPen(pen);
        series->setUseOpenGL(hasGL);
        series->attachAxis(xAxis);
        series->attachAxis(yAxis);
        series->setName(name);
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
    seriesList.clear();
    chanList.clear();
    readerIndex.clear();
    data.clear();
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
          max_width > 0    ? max_width
        : max_duration > 0 ? this->width() * 10
        : this->width();
    double minX = -1.0, maxX = 1.0, minY = -1.0, maxY = 1.0;
    bool first = true;
    bool updated = false;

    for (size_t s = 0; s < numSeries; s++)
    {
        recorder_chan_p chan = chanList[s];
        ringidx_t &ridx = readerIndex[s];
        size_t readable = recorder_chan_readable(chan, &ridx);
        QLineSeries *series = seriesList[s];
        Points &dataPoints = data[s];

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
                        pbuf[p] = QPointF(rbuf[2*p].unsigned_value * scale,
                                          rbuf[2*p+1].real_value);
                    break;
                }

                size_t newLen = count + dataLen;
                size_t toRemove = newLen > width ? newLen - width : 0;
                if (toRemove)
                    dataPoints.remove(0, toRemove);
                if (count != readable)
                    pointsRead.resize(count);
                dataPoints.append(pointsRead);

                pbuf = dataPoints.data();
                count = dataPoints.size();
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
                        first = false;
                    }
                    else
                    {
                        if (maxX < x)
                            maxX = x;
                        if (minX > x)
                            minX = x;
                        if (maxY < y)
                            maxY = y;
                        if (minY > y)
                            minY = y;
                    }
                }

                if (max_duration > 0.0)
                {
                    minX = maxX - max_duration;
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

                series->replace(dataPoints);
                updated = true;
            }
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


double   RecorderView::max_duration = 0.0;
unsigned RecorderView::max_width    = 0;
