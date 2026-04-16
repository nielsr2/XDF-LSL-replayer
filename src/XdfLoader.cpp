#include "XdfLoader.h"
#include "xdf.h"
#include <algorithm>
#include <limits>

bool XdfLoader::load(const QString &filePath)
{
    m_streams.clear();
    m_error.clear();

    Xdf xdf;
    int result = 0;
    try {
        result = xdf.load_xdf(filePath.toStdString());
    } catch (const std::exception &e) {
        m_error = QString("Failed to load XDF file: %1").arg(e.what());
        return false;
    }

    if (result != 0) {
        m_error = QString("libxdf returned error code %1. File may be corrupted.").arg(result);
        return false;
    }

    if (xdf.streams.empty()) {
        m_error = "XDF file contains no streams.";
        return false;
    }

    m_globalMinTime = std::numeric_limits<double>::max();
    m_globalMaxTime = std::numeric_limits<double>::lowest();

    for (size_t i = 0; i < xdf.streams.size(); ++i) {
        const auto &xs = xdf.streams[i];

        XdfStream s;
        s.id = static_cast<int>(i);

        // Extract metadata from the stream info
        s.name = xs.info.name;
        s.type = xs.info.type;
        s.channelCount = xs.info.channel_count;
        s.nominalSrate = xs.info.nominal_srate;
        s.channelFormat = xs.info.channel_format;
        s.sampleCount = static_cast<int>(xs.time_stamps.size());

        // Channel labels (handle missing "label" key gracefully)
        for (const auto &chMap : xs.info.channels) {
            auto it = chMap.find("label");
            if (it != chMap.end())
                s.channelLabels.push_back(it->second);
            else
                s.channelLabels.push_back("");
        }

        // Only copy data for streams that have samples
        if (!xs.time_stamps.empty()) {
            s.timeStamps = xs.time_stamps;
            s.data = xs.time_series;

            m_globalMinTime = std::min(m_globalMinTime, s.minTime());
            m_globalMaxTime = std::max(m_globalMaxTime, s.maxTime());
        }

        m_streams.push_back(std::move(s));
    }

    // If no streams had data, still use file-level min/max
    if (m_globalMinTime > m_globalMaxTime) {
        m_globalMinTime = 0.0;
        m_globalMaxTime = 0.0;
    }

    return true;
}
