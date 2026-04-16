#ifndef STREAMCHARTVIEW_H
#define STREAMCHARTVIEW_H

#include <QWidget>
#include <QChartView>
#include <QChart>
#include <QLineSeries>
#include <QValueAxis>
#include <vector>

struct XdfStream;

class StreamChartView : public QWidget
{
    Q_OBJECT

public:
    explicit StreamChartView(const XdfStream &stream, double globalMinTime,
                             QWidget *parent = nullptr);
    ~StreamChartView();

    void setPlaybackCursor(double timeSec);
    void fitAxes();
    void zoomToRegion(double startSec, double endSec);

protected:
    bool eventFilter(QObject *obj, QEvent *event) override;

private:
    void buildChart(const XdfStream &stream, double globalMinTime);
    void updateCursorLine();

    QChartView *m_chartView = nullptr;
    QChart *m_chart = nullptr;
    QValueAxis *m_axisX = nullptr;
    QValueAxis *m_axisY = nullptr;

    std::vector<QLineSeries *> m_series;

    // Cursor line for playback position
    QLineSeries *m_cursorSeries = nullptr;
    double m_cursorTime = 0.0;

    // Data range for fit
    double m_dataMinX = 0.0;
    double m_dataMaxX = 0.0;
    double m_dataMinY = 0.0;
    double m_dataMaxY = 0.0;

    // For mouse panning
    bool m_isPanning = false;
    QPointF m_lastMousePos;
};

#endif // STREAMCHARTVIEW_H
