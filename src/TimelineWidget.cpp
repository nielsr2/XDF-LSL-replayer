#include "TimelineWidget.h"

#include <QPainter>
#include <QMouseEvent>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QDoubleSpinBox>
#include <QLabel>
#include <algorithm>
#include <cmath>

TimelineWidget::TimelineWidget(QWidget *parent)
    : QWidget(parent)
{
    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(kMargin, 4, kMargin, 0);
    mainLayout->setSpacing(2);

    // Loop region spin boxes
    auto *spinRow = new QHBoxLayout;
    spinRow->setSpacing(8);

    auto *loopLabel = new QLabel("Loop:");
    loopLabel->setStyleSheet("color: #6a6a80; font-size: 11px;");
    spinRow->addWidget(loopLabel);

    m_startSpin = new QDoubleSpinBox;
    m_startSpin->setPrefix("Start: ");
    m_startSpin->setSuffix("s");
    m_startSpin->setDecimals(1);
    m_startSpin->setRange(0, 0);
    m_startSpin->setStyleSheet(
        "QDoubleSpinBox { background: #1a1a24; color: #8890a0; border: 1px solid #2a2a3a; "
        "border-radius: 3px; padding: 2px 4px; font-size: 11px; }"
    );
    spinRow->addWidget(m_startSpin);

    m_endSpin = new QDoubleSpinBox;
    m_endSpin->setPrefix("End: ");
    m_endSpin->setSuffix("s");
    m_endSpin->setDecimals(1);
    m_endSpin->setRange(0, 0);
    m_endSpin->setStyleSheet(m_startSpin->styleSheet());
    spinRow->addWidget(m_endSpin);

    spinRow->addStretch();
    mainLayout->addLayout(spinRow);

    // Reserve space for the painted timeline track
    mainLayout->addSpacing(40);

    setMinimumHeight(70);
    setMaximumHeight(80);
    setMouseTracking(true);

    connect(m_startSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, &TimelineWidget::onSpinBoxChanged);
    connect(m_endSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, &TimelineWidget::onSpinBoxChanged);
}

void TimelineWidget::setDuration(double totalSeconds)
{
    m_duration = totalSeconds;
    m_loopStart = 0.0;
    m_loopEnd = totalSeconds;
    m_playbackPos = 0.0;

    m_startSpin->blockSignals(true);
    m_endSpin->blockSignals(true);
    m_startSpin->setRange(0, totalSeconds);
    m_startSpin->setValue(0);
    m_endSpin->setRange(0, totalSeconds);
    m_endSpin->setValue(totalSeconds);
    m_startSpin->blockSignals(false);
    m_endSpin->blockSignals(false);

    update();
}

void TimelineWidget::setPlaybackPosition(double seconds)
{
    m_playbackPos = seconds;
    update();
}

void TimelineWidget::setLoopRegion(double startSec, double endSec)
{
    m_loopStart = startSec;
    m_loopEnd = endSec;
    updateSpinBoxes();
    update();
}

void TimelineWidget::onSpinBoxChanged()
{
    double s = m_startSpin->value();
    double e = m_endSpin->value();
    if (e <= s) e = s + 0.1;
    m_loopStart = std::clamp(s, 0.0, m_duration);
    m_loopEnd = std::clamp(e, m_loopStart + 0.1, m_duration);
    update();
    emit loopRegionChanged(m_loopStart, m_loopEnd);
}

void TimelineWidget::updateSpinBoxes()
{
    m_startSpin->blockSignals(true);
    m_endSpin->blockSignals(true);
    m_startSpin->setValue(m_loopStart);
    m_endSpin->setValue(m_loopEnd);
    m_startSpin->blockSignals(false);
    m_endSpin->blockSignals(false);
}

double TimelineWidget::timeFromX(int x) const
{
    if (m_duration <= 0.0)
        return 0.0;
    int w = width() - 2 * kMargin;
    double t = static_cast<double>(x - kMargin) / w * m_duration;
    return std::clamp(t, 0.0, m_duration);
}

int TimelineWidget::xFromTime(double t) const
{
    if (m_duration <= 0.0)
        return kMargin;
    int w = width() - 2 * kMargin;
    return kMargin + static_cast<int>(t / m_duration * w);
}

void TimelineWidget::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    int w = width();
    int h = height();
    // Track is in the bottom portion (below spin boxes)
    int trackY = h - 30;
    int trackH = 12;
    int trackLeft = kMargin;
    int trackRight = w - kMargin;

    // Background
    p.fillRect(rect(), QColor(22, 22, 30));

    // Track background
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(40, 40, 55));
    p.drawRoundedRect(trackLeft, trackY, trackRight - trackLeft, trackH, 6, 6);

    // Loop region highlight
    if (m_duration > 0) {
        int loopX1 = xFromTime(m_loopStart);
        int loopX2 = xFromTime(m_loopEnd);

        QLinearGradient grad(loopX1, 0, loopX2, 0);
        grad.setColorAt(0.0, QColor(80, 140, 255, 60));
        grad.setColorAt(1.0, QColor(160, 80, 255, 60));
        p.setBrush(grad);
        p.drawRoundedRect(loopX1, trackY, loopX2 - loopX1, trackH, 4, 4);

        // Loop start handle
        p.setBrush(QColor(80, 140, 255));
        p.drawRoundedRect(loopX1 - kHandleWidth / 2, trackY - 5, kHandleWidth, trackH + 10, 3, 3);

        // Loop end handle
        p.setBrush(QColor(160, 80, 255));
        p.drawRoundedRect(loopX2 - kHandleWidth / 2, trackY - 5, kHandleWidth, trackH + 10, 3, 3);
    }

    // Playback cursor
    if (m_duration > 0) {
        int cx = xFromTime(m_playbackPos);
        p.setPen(QPen(QColor(255, 80, 80), 2));
        p.drawLine(cx, trackY - 10, cx, trackY + trackH + 10);

        // Cursor head
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(255, 80, 80));
        p.drawEllipse(QPoint(cx, trackY - 10), 4, 4);
    }

    // Time labels
    p.setPen(QColor(100, 110, 140));
    QFont font = p.font();
    font.setPointSize(8);
    p.setFont(font);

    auto formatTime = [](double secs) -> QString {
        int mins = static_cast<int>(secs) / 60;
        double s = secs - mins * 60;
        return QString("%1:%2").arg(mins).arg(s, 5, 'f', 1, '0');
    };

    p.drawText(trackLeft, h - 4, formatTime(0));
    if (m_duration > 0)
        p.drawText(trackRight - 40, h - 4, formatTime(m_duration));
}

void TimelineWidget::mousePressEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton || m_duration <= 0)
        return;

    int mx = event->position().toPoint().x();
    int loopStartX = xFromTime(m_loopStart);
    int loopEndX = xFromTime(m_loopEnd);

    if (std::abs(mx - loopStartX) <= kHandleWidth) {
        m_dragTarget = LoopStart;
    } else if (std::abs(mx - loopEndX) <= kHandleWidth) {
        m_dragTarget = LoopEnd;
    } else {
        m_dragTarget = None;
    }
}

void TimelineWidget::mouseMoveEvent(QMouseEvent *event)
{
    if (m_dragTarget == None || m_duration <= 0)
        return;

    double t = timeFromX(event->position().toPoint().x());

    if (m_dragTarget == LoopStart) {
        m_loopStart = std::min(t, m_loopEnd - 0.1);
        m_loopStart = std::max(0.0, m_loopStart);
    } else if (m_dragTarget == LoopEnd) {
        m_loopEnd = std::max(t, m_loopStart + 0.1);
        m_loopEnd = std::min(m_duration, m_loopEnd);
    }

    update();
    updateSpinBoxes();
    emit loopRegionChanged(m_loopStart, m_loopEnd);
}

void TimelineWidget::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton)
        m_dragTarget = None;
}
