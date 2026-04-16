#include "StreamChartView.h"
#include "XdfLoader.h"

#include <QVBoxLayout>
#include <QMouseEvent>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>
#include <limits>

StreamChartView::StreamChartView(const XdfStream &stream, double globalMinTime,
                                 QWidget *parent)
    : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    m_chart = new QChart;
    m_chart->setAnimationOptions(QChart::NoAnimation);
    m_chart->legend()->hide();
    m_chart->setBackgroundBrush(QBrush(QColor(22, 22, 30)));
    m_chart->setPlotAreaBackgroundBrush(QBrush(QColor(26, 26, 36)));
    m_chart->setPlotAreaBackgroundVisible(true);
    m_chart->setTitleBrush(QBrush(QColor(200, 200, 220)));
    m_chart->setMargins(QMargins(4, 4, 4, 4));

    m_chartView = new QChartView(m_chart);
    m_chartView->setRenderHint(QPainter::Antialiasing);
    m_chartView->setRubberBand(QChartView::RectangleRubberBand);
    m_chartView->viewport()->installEventFilter(this);
    layout->addWidget(m_chartView, 1);

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
    m_axisX->setLabelsColor(QColor(140, 150, 180));
    m_axisX->setTitleBrush(QBrush(QColor(140, 150, 180)));
    m_axisX->setGridLineColor(QColor(40, 40, 55));
    m_chart->addAxis(m_axisX, Qt::AlignBottom);

    m_axisY = new QValueAxis;
    m_axisY->setTitleText("Amplitude");
    m_axisY->setLabelsColor(QColor(140, 150, 180));
    m_axisY->setTitleBrush(QBrush(QColor(140, 150, 180)));
    m_axisY->setGridLineColor(QColor(40, 40, 55));
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
    m_cursorSeries->setPen(QPen(QColor(255, 80, 80), 2));
    m_chart->addSeries(m_cursorSeries);
    m_cursorSeries->attachAxis(m_axisX);
    m_cursorSeries->attachAxis(m_axisY);

    // Color palette for channels
    static const QColor channelColors[] = {
        QColor(80, 160, 255), QColor(255, 120, 80), QColor(100, 220, 140),
        QColor(220, 180, 60), QColor(180, 100, 255), QColor(255, 100, 180),
        QColor(80, 220, 220), QColor(200, 200, 100), QColor(160, 120, 220),
        QColor(120, 200, 80), QColor(255, 160, 120), QColor(100, 180, 220)
    };
    for (size_t i = 0; i < m_series.size(); ++i) {
        QColor c = channelColors[i % 12];
        m_series[i]->setColor(c);
        QPen pen = m_series[i]->pen();
        pen.setWidthF(1.2);
        m_series[i]->setPen(pen);
    }

    // Show legend only if multiple channels
    if (stream.channelCount > 1) {
        m_chart->legend()->show();
        m_chart->legend()->setLabelColor(QColor(180, 180, 200));
        m_chart->legend()->setBrush(QBrush(QColor(22, 22, 30, 180)));
    }

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

    // Use the data range (not axis range) for cursor to avoid feedback loops
    QVector<QPointF> pts;
    pts.append(QPointF(m_cursorTime, m_dataMinY));
    pts.append(QPointF(m_cursorTime, m_dataMaxY));
    m_cursorSeries->replace(pts);
}

void StreamChartView::fitAxes()
{
    fitHorizontal();
    fitVertical();
}

void StreamChartView::fitHorizontal()
{
    if (!m_axisX) return;
    double xMargin = (m_dataMaxX - m_dataMinX) * 0.02;
    if (xMargin < 1e-9) xMargin = 1.0;
    m_axisX->setRange(m_dataMinX - xMargin, m_dataMaxX + xMargin);
}

void StreamChartView::fitVertical()
{
    if (!m_axisY) return;

    // Compute Y range from visible series only
    double minY = std::numeric_limits<double>::max();
    double maxY = std::numeric_limits<double>::lowest();
    for (auto *s : m_series) {
        if (!s->isVisible()) continue;
        for (const auto &pt : s->points()) {
            if (pt.y() < minY) minY = pt.y();
            if (pt.y() > maxY) maxY = pt.y();
        }
    }
    if (minY > maxY) {
        // No visible series — fall back to full data range
        minY = m_dataMinY;
        maxY = m_dataMaxY;
    }

    double yMargin = (maxY - minY) * 0.05;
    if (yMargin < 1e-9) yMargin = 1.0;
    m_axisY->setRange(minY - yMargin, maxY + yMargin);
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

void StreamChartView::setChannelVisible(int channelIndex, bool visible)
{
    if (channelIndex < 0 || channelIndex >= static_cast<int>(m_series.size()))
        return;
    m_series[channelIndex]->setVisible(visible);
}
