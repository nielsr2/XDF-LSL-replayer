#ifndef XDFLOADER_H
#define XDFLOADER_H

#include <QString>
#include <vector>
#include <string>
#include <cstdint>

struct XdfStream {
    int id;
    std::string name;
    std::string type;
    int channelCount;
    double nominalSrate;
    std::string channelFormat;
    int sampleCount = 0;
    std::vector<std::string> channelLabels;

    // Sample data: time_stamps[i] corresponds to data[channel][i]
    std::vector<double> timeStamps;
    std::vector<std::vector<float>> data; // [channel][sample] (float matches libxdf)

    bool hasData() const { return !timeStamps.empty(); }
    double minTime() const { return timeStamps.empty() ? 0.0 : timeStamps.front(); }
    double maxTime() const { return timeStamps.empty() ? 0.0 : timeStamps.back(); }
};

class XdfLoader
{
public:
    XdfLoader() = default;

    bool load(const QString &filePath);
    QString errorString() const { return m_error; }

    int streamCount() const { return static_cast<int>(m_streams.size()); }
    const XdfStream &stream(int index) const { return m_streams.at(index); }
    const std::vector<XdfStream> &streams() const { return m_streams; }

    double globalMinTime() const { return m_globalMinTime; }
    double globalMaxTime() const { return m_globalMaxTime; }
    double duration() const { return m_globalMaxTime - m_globalMinTime; }

private:
    std::vector<XdfStream> m_streams;
    QString m_error;
    double m_globalMinTime = 0.0;
    double m_globalMaxTime = 0.0;
};

#endif // XDFLOADER_H
