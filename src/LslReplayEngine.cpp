#include "LslReplayEngine.h"
#include "XdfLoader.h"
#include <lsl_cpp.h>
#include <QElapsedTimer>
#include <QThread>
#include <cmath>
#include <algorithm>

LslReplayEngine::LslReplayEngine(QObject *parent)
    : QThread(parent)
{
}

LslReplayEngine::~LslReplayEngine()
{
    stop();
    wait();
}

void LslReplayEngine::setStreams(const std::vector<XdfStream> *streams,
                                  double globalMinTime, double globalMaxTime)
{
    m_streams = streams;
    m_globalMinTime = globalMinTime;
    m_globalMaxTime = globalMaxTime;
    m_loopStart = 0.0;
    m_loopEnd = globalMaxTime - globalMinTime;
}

void LslReplayEngine::play()
{
    if (isRunning()) {
        // Resume from pause
        QMutexLocker lock(&m_mutex);
        m_paused = false;
        m_pauseCondition.wakeAll();
    } else {
        m_playing = true;
        m_paused = false;
        m_stopRequested = false;
        start();
    }
}

void LslReplayEngine::pause()
{
    QMutexLocker lock(&m_mutex);
    m_paused = true;
    emit playbackPaused();
}

void LslReplayEngine::stop()
{
    {
        QMutexLocker lock(&m_mutex);
        m_stopRequested = true;
        m_paused = false;
        m_pauseCondition.wakeAll();
    }
    if (isRunning())
        wait();
    emit playbackStopped();
}

void LslReplayEngine::setLoopEnabled(bool enabled)
{
    m_loopEnabled = enabled;
}

void LslReplayEngine::setLoopRegion(double startSec, double endSec)
{
    QMutexLocker lock(&m_mutex);
    m_loopStart = startSec;
    m_loopEnd = endSec;
}

void LslReplayEngine::createOutlets()
{
    destroyOutlets();
    if (!m_streams)
        return;

    for (const auto &s : *m_streams) {
        lsl::stream_info info(
            s.name + "_replay",
            s.type,
            s.channelCount,
            s.nominalSrate,
            lsl::cf_float32,
            s.name + "_replay_uid"
        );

        m_outlets.push_back(std::make_unique<lsl::stream_outlet>(info));
    }
}

void LslReplayEngine::destroyOutlets()
{
    m_outlets.clear();
}

void LslReplayEngine::run()
{
    if (!m_streams || m_streams->empty())
        return;

    createOutlets();
    emit playbackStarted();

    const double duration = m_globalMaxTime - m_globalMinTime;

    auto resetIndices = [&](double startOffset) {
        m_sampleIndices.resize(m_streams->size());
        for (size_t si = 0; si < m_streams->size(); ++si) {
            const auto &ts = (*m_streams)[si].timeStamps;
            double absTime = m_globalMinTime + startOffset;
            auto it = std::lower_bound(ts.begin(), ts.end(), absTime);
            m_sampleIndices[si] = static_cast<size_t>(std::distance(ts.begin(), it));
        }
    };

    bool looping = true;
    while (looping) {
        double startOffset, endOffset;
        {
            QMutexLocker lock(&m_mutex);
            startOffset = m_loopStart;
            endOffset = m_loopEnd;
        }

        startOffset = std::max(0.0, std::min(startOffset, duration));
        endOffset = std::max(startOffset, std::min(endOffset, duration));

        resetIndices(startOffset);

        QElapsedTimer elapsed;
        elapsed.start();
        double pausedAccum = 0.0; // total time spent paused

        double currentOffset = startOffset;

        while (!m_stopRequested) {
            // Handle pause
            {
                QMutexLocker lock(&m_mutex);
                if (m_paused && !m_stopRequested) {
                    qint64 pauseStart = elapsed.elapsed();
                    while (m_paused && !m_stopRequested) {
                        m_pauseCondition.wait(&m_mutex);
                    }
                    pausedAccum += (elapsed.elapsed() - pauseStart) / 1000.0;
                }
            }
            if (m_stopRequested)
                break;

            double wallElapsed = elapsed.elapsed() / 1000.0 - pausedAccum;
            currentOffset = startOffset + wallElapsed;

            if (currentOffset >= endOffset)
                break;

            emit playbackPositionChanged(currentOffset);

            // Push samples that are due
            for (size_t si = 0; si < m_streams->size(); ++si) {
                const auto &stream = (*m_streams)[si];
                auto &idx = m_sampleIndices[si];

                while (idx < stream.timeStamps.size()) {
                    double sampleOffset = stream.timeStamps[idx] - m_globalMinTime;
                    if (sampleOffset > currentOffset)
                        break;
                    if (sampleOffset < startOffset) {
                        ++idx;
                        continue;
                    }

                    // Build sample vector
                    std::vector<float> sample(stream.channelCount);
                    for (int ch = 0; ch < stream.channelCount; ++ch) {
                        sample[ch] = stream.data[ch][idx];
                    }

                    double lslTimestamp = lsl::local_clock();
                    m_outlets[si]->push_sample(sample, lslTimestamp);
                    ++idx;
                }
            }

            // Sleep briefly to avoid busy-waiting (1ms)
            QThread::usleep(1000);
        }

        if (m_stopRequested)
            break;

        // End of region reached
        emit playbackPositionChanged(endOffset);

        if (!m_loopEnabled) {
            looping = false;
        }
        // else loop again from startOffset
    }

    destroyOutlets();
    m_playing = false;

    if (!m_stopRequested)
        emit playbackFinished();
}
