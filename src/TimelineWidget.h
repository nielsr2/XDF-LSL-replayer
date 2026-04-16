#ifndef TIMELINEWIDGET_H
#define TIMELINEWIDGET_H

#include <QWidget>

class QDoubleSpinBox;
class QHBoxLayout;

class TimelineWidget : public QWidget
{
    Q_OBJECT

public:
    explicit TimelineWidget(QWidget *parent = nullptr);

    void setDuration(double totalSeconds);
    void setPlaybackPosition(double seconds);
    void setLoopRegion(double startSec, double endSec);

    double loopStart() const { return m_loopStart; }
    double loopEnd() const { return m_loopEnd; }

signals:
    void loopRegionChanged(double startSec, double endSec);
    void seekRequested(double seconds);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;

private slots:
    void onSpinBoxChanged();

private:
    double timeFromX(int x) const;
    int xFromTime(double t) const;
    void updateSpinBoxes();

    double m_duration = 0.0;
    double m_playbackPos = 0.0;
    double m_loopStart = 0.0;
    double m_loopEnd = 0.0;

    enum DragTarget { None, LoopStart, LoopEnd };
    DragTarget m_dragTarget = None;

    QDoubleSpinBox *m_startSpin = nullptr;
    QDoubleSpinBox *m_endSpin = nullptr;

    // Timeline drawing area (below the spin boxes)
    int m_trackTop = 0;

    static constexpr int kMargin = 10;
    static constexpr int kHandleWidth = 8;
};

#endif // TIMELINEWIDGET_H
