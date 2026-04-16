#ifndef LSLREPLAYENGINE_H
#define LSLREPLAYENGINE_H

#include <QThread>
#include <QMutex>
#include <QWaitCondition>
#include <atomic>
#include <vector>
#include <memory>

namespace lsl { class stream_outlet; class stream_info; }

struct XdfStream;

class LslReplayEngine : public QThread
{
    Q_OBJECT

public:
    explicit LslReplayEngine(QObject *parent = nullptr);
    ~LslReplayEngine() override;

    void setStreams(const std::vector<XdfStream> *streams,
                    double globalMinTime, double globalMaxTime);

    void play();
    void pause();
    void stop();

    void setLoopEnabled(bool enabled);
    bool isLoopEnabled() const { return m_loopEnabled.load(); }

    void setLoopRegion(double startSec, double endSec);
    double loopStart() const { return m_loopStart; }
    double loopEnd() const { return m_loopEnd; }

    bool isPlaying() const { return m_playing.load(); }
    bool isPaused() const { return m_paused.load(); }

signals:
    void playbackPositionChanged(double seconds);
    void playbackStarted();
    void playbackPaused();
    void playbackStopped();
    void playbackFinished();

protected:
    void run() override;

private:
    void createOutlets();
    void destroyOutlets();

    const std::vector<XdfStream> *m_streams = nullptr;
    double m_globalMinTime = 0.0;
    double m_globalMaxTime = 0.0;

    std::vector<std::unique_ptr<lsl::stream_outlet>> m_outlets;

    std::atomic<bool> m_playing{false};
    std::atomic<bool> m_paused{false};
    std::atomic<bool> m_stopRequested{false};
    std::atomic<bool> m_loopEnabled{false};

    QMutex m_mutex;
    QWaitCondition m_pauseCondition;

    double m_loopStart = 0.0;
    double m_loopEnd = 0.0;

    // Per-stream playback index
    std::vector<size_t> m_sampleIndices;
};

#endif // LSLREPLAYENGINE_H
