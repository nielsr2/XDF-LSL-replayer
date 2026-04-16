#include "StreamChartView.h"
#include "XdfLoader.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QGraphicsLineItem>
#include <algorithm>
#include <cmath>
#include <limits>

StreamChartView::StreamChartView(const XdfStream &stream, double globalMinTime,
                                 QWidget *parent)
    : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    // Toolbar with fit button
    auto *toolLayout = new QHBoxLayout;
    auto *fitBtn = new QPushButton("Fit");
    fitBtn->setFixedWidth(60);
    connect(fitBtn, &QPushButton::clicked, this, &StreamChartView::fitAxes);
    toolLayout->addWidget(fitBtn);
    toolLayout->addStretch();
    layout->addLayout(toolLayout);

    m_chart = new QChart;
    m_chart->setAnimationOptions(QChart::NoAnimation);
    m_chart->legend()->hide();

    m_chartView = new QChartView(m_chart);
    m_chartView->setRenderHint(QPainter::Antialiasing);
    m_chartView->setRubberBand(QChartView::RectangleRubberBand);
    m_chartView->viewport()->installEventFilter(this);
    layout->addWidget(m_chartView);

    buildChart(stream, globalMinTime);
}

StreamChartView::~StreamChartView() = default;

void StreamChartView::buildChart(const XdfStream &stream, double globalMinTime)
{
    m_dataMinX = std::numeric_limits<double>::max();
    m_dataMaxX = std::numeric_limits<double>::lowest();
    m_dataMinY = std::numeric_limits<double>::max();
    m_dataMaxY = std::numeric_limits<double>::lowest();

    m_axisX = new QValueAxis;
    m_axisX->setTitleText("Time (s)");
    m_chart->addAxis(m_axisX, Qt::AlignBottom);

    m_axisY = new QValueAxis;
    m_axisY->setTitleText("Amplitude");
    m_chart->addAxis(m_axisY, Qt::AlignLeft);

    // Downsampling threshold: if more than this many points, use min-max decimation
    constexpr int kMaxPointsPerSeries = 10000;

    for (int ch = 0; ch < stream.channelCount; ++ch) {
        auto *series = new QLineSeries;
        if (ch < static_cast<int>(stream.channelLabels.size()) && !stream.channelLabels[ch].empty())
            series->setName(QString::fromStdString(stream.channelLabels[ch]));
        else
            series->setName(QString("Ch %1").arg(ch));

        const auto &ts = stream.timeStamps;
        const auto &chdata = stream.data[ch];
        int totalSamples = static_cast<int>(ts.size());

        if (totalSamples <= kMaxPointsPerSeries) {
            // No downsampling needed
            QVector<QPointF> points;
            points.reserve(totalSamples);
            for (int i = 0; i < totalSamples; ++i) {
                double x = ts[i] - globalMinTime;
                double y = static_cast<double>(chdata[i]);
                points.append(QPointF(x, y));
                m_dataMinX = std::min(m_dataMinX, x);
                m_dataMaxX = std::max(m_dataMaxX, x);
                m_dataMinY = std::min(m_dataMinY, y);
                m_dataMaxY = std::max(m_dataMaxY, y);
            }
            series->replace(points);
        } else {
            // Min-max decimation
            int binSize = totalSamples / (kMaxPointsPerSeries / 2);
            QVector<QPointF> points;
            points.reserve(kMaxPointsPerSeries + 2);

            for (int i = 0; i < totalSamples; i += binSize) {
                int end = std::min(i + binSize, totalSamples);
                double minVal = std::numeric_limits<double>::max();
                double maxVal = std::numeric_limits<double>::lowest();
                double minT = 0, maxT = 0;
                for (int j = i; j < end; ++j) {
                    double v = static_cast<double>(chdata[j]);
                    if (v < minVal) { minVal = v; minT = ts[j] - globalMinTime; }
                    if (v > maxVal) { maxVal = v; maxT = ts[j] - globalMinTime; }
                }
                if (minT <= maxT) {
                    points.append(QPointF(minT, minVal));
                    points.append(QPointF(maxT, maxVal));
                } else {
                    points.append(QPointF(maxT, maxVal));
                    points.append(QPointF(minT, minVal));
                }
                m_dataMinX = std::min(m_dataMinX, std::min(minT, maxT));
                m_dataMaxX = std::max(m_dataMaxX, std::max(minT, maxT));
                m_dataMinY = std::min(m_dataMinY, minVal);
                m_dataMaxY = std::max(m_dataMaxY, maxVal);
            }
            series->replace(points);
        }

        m_chart->addSeries(series);
        series->attachAxis(m_axisX);
        series->attachAxis(m_axisY);
        m_series.push_back(series);
    }

    // Cursor line
    m_cursorSeries = new QLineSeries;
    m_cursorSeries->setPen(QPen(Qt::red, 2));
    m_chart->addSeries(m_cursorSeries);
    m_cursorSeries->attachAxis(m_axisX);
    m_cursorSeries->attachAxis(m_axisY);

    // Show legend only if multiple channels
    if (stream.channelCount > 1)
        m_chart->legend()->show();

    m_chart->setTitle(QString::fromStdString(stream.name));
    fitAxes();
}

void StreamChartView::setPlaybackCursor(double timeSec)
{
    m_cursorTime = timeSec;
    updateCursorLine();
}

void StreamChartView::updateCursorLine()
{
    if (!m_cursorSeries || !m_axisY)
        return;

    QVector<QPointF> pts;
    pts.append(QPointF(m_cursorTime, m_axisY->min()));
    pts.append(QPointF(m_cursorTime, m_axisY->max()));
    m_cursorSeries->replace(pts);
}

void StreamChartView::fitAxes()
{
    if (!m_axisX || !m_axisY)
        return;

    double xMargin = (m_dataMaxX - m_dataMinX) * 0.02;
    double yMargin = (m_dataMaxY - m_dataMinY) * 0.05;

    if (xMargin < 1e-9) xMargin = 1.0;
    if (yMargin < 1e-9) yMargin = 1.0;

    m_axisX->setRange(m_dataMinX - xMargin, m_dataMaxX + xMargin);
    m_axisY->setRange(m_dataMinY - yMargin, m_dataMaxY + yMargin);
}

void StreamChartView::zoomToRegion(double startSec, double endSec)
{
    if (m_axisX)
        m_axisX->setRange(startSec, endSec);
}

bool StreamChartView::eventFilter(QObject *obj, QEvent *event)
{
    if (obj == m_chartView->viewport()) {
        if (event->type() == QEvent::Wheel) {
            auto *we = static_cast<QWheelEvent *>(event);
            double factor = we->angleDelta().y() > 0 ? 0.8 : 1.25;

            QPointF chartPos = m_chart->mapToValue(we->position(), m_series.empty() ? nullptr : m_series[0]);

            double xMin = m_axisX->min();
            double xMax = m_axisX->max();
            double yMin = m_axisY->min();
            double yMax = m_axisY->max();

            double newXMin = chartPos.x() - (chartPos.x() - xMin) * factor;
            double newXMax = chartPos.x() + (xMax - chartPos.x()) * factor;
            double newYMin = chartPos.y() - (chartPos.y() - yMin) * factor;
            double newYMax = chartPos.y() + (yMax - chartPos.y()) * factor;

            m_axisX->setRange(newXMin, newXMax);
            m_axisY->setRange(newYMin, newYMax);
            return true;
        }

        if (event->type() == QEvent::MouseButtonPress) {
            auto *me = static_cast<QMouseEvent *>(event);
            if (me->button() == Qt::MiddleButton) {
                m_isPanning = true;
                m_lastMousePos = me->position();
                return true;
            }
        }
        if (event->type() == QEvent::MouseMove && m_isPanning) {
            auto *me = static_cast<QMouseEvent *>(event);
            QPointF delta = me->position() - m_lastMousePos;
            m_chart->scroll(-delta.x(), delta.y());
            m_lastMousePos = me->position();
            return true;
        }
        if (event->type() == QEvent::MouseButtonRelease) {
            auto *me = static_cast<QMouseEvent *>(event);
            if (me->button() == Qt::MiddleButton) {
                m_isPanning = false;
                return true;
            }
        }
    }
    return QWidget::eventFilter(obj, event);
}
