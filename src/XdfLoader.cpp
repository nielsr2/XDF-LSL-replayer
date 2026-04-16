#include "XdfLoader.h"
#include <fstream>
#include <algorithm>
#include <limits>
#include <cstring>
#include <iostream>
#include <sstream>

// Minimal pugixml for header parsing — we embed a tiny XML parser
// Actually, use a simple approach: parse the XML with Qt
#include <QXmlStreamReader>

namespace {

template <typename T>
T readBin(std::ifstream &f) {
    T val{};
    f.read(reinterpret_cast<char*>(&val), sizeof(T));
    return val;
}

uint64_t readVarLen(std::ifstream &f) {
    uint8_t nBytes = readBin<uint8_t>(f);
    switch (nBytes) {
    case 1: return readBin<uint8_t>(f);
    case 4: return readBin<uint32_t>(f);
    case 8: return readBin<uint64_t>(f);
    default: return 0;
    }
}

// Read length-prefixed variable-length integer (used for string events)
uint64_t readStringLength(std::ifstream &f) {
    return readVarLen(f);
}

struct RawStream {
    uint32_t streamId = 0;
    std::string name;
    std::string type;
    int channelCount = 0;
    double nominalSrate = 0;
    std::string channelFormat;
    std::vector<std::string> channelLabels;

    std::vector<double> timestamps;
    std::vector<std::vector<float>> data; // [channel][sample]

    // Clock offset correction
    std::vector<double> clockTimes;
    std::vector<double> clockValues;
    double lastTimestamp = 0;
    double samplingInterval = 0;

    int bytesPerSample() const {
        if (channelFormat == "float32") return 4;
        if (channelFormat == "double64") return 8;
        if (channelFormat == "int8") return 1;
        if (channelFormat == "int16") return 2;
        if (channelFormat == "int32") return 4;
        if (channelFormat == "int64") return 8;
        return 0; // string or unknown
    }
};

void parseStreamHeader(const std::string &xml, RawStream &s) {
    QXmlStreamReader reader(QString::fromStdString(xml));
    QString currentElement;
    bool inChannels = false;
    bool inChannel = false;
    std::string currentLabel;

    while (!reader.atEnd()) {
        auto token = reader.readNext();
        if (token == QXmlStreamReader::StartElement) {
            currentElement = reader.name().toString();
            if (currentElement == "channels") inChannels = true;
            if (currentElement == "channel" && inChannels) {
                inChannel = true;
                currentLabel.clear();
            }
        } else if (token == QXmlStreamReader::Characters) {
            QString text = reader.text().toString().trimmed();
            if (text.isEmpty()) continue;

            if (!inChannels) {
                if (currentElement == "name") s.name = text.toStdString();
                else if (currentElement == "type") s.type = text.toStdString();
                else if (currentElement == "channel_count") s.channelCount = text.toInt();
                else if (currentElement == "nominal_srate") s.nominalSrate = text.toDouble();
                else if (currentElement == "channel_format") s.channelFormat = text.toStdString();
            } else if (inChannel) {
                if (currentElement == "label") currentLabel = text.toStdString();
            }
        } else if (token == QXmlStreamReader::EndElement) {
            QString eName = reader.name().toString();
            if (eName == "channel" && inChannel) {
                s.channelLabels.push_back(currentLabel);
                inChannel = false;
            }
            if (eName == "channels") inChannels = false;
        }
    }

    if (s.nominalSrate > 0)
        s.samplingInterval = 1.0 / s.nominalSrate;
}

// Bulk-read numeric samples — the key optimization
void readNumericChunk(std::ifstream &f, RawStream &s, uint64_t numSamp) {
    if (s.data.empty())
        s.data.resize(s.channelCount);

    int bps = s.bytesPerSample();
    if (bps == 0 || s.channelCount == 0) return;

    size_t oldSize = s.timestamps.size();
    s.timestamps.resize(oldSize + numSamp);
    for (auto &ch : s.data)
        ch.resize(oldSize + numSamp);

    int rowBytes = s.channelCount * bps;
    // Max bytes per sample: 1 (tsFlag) + 8 (timestamp) + rowBytes
    int maxSampleBytes = 1 + 8 + rowBytes;

    // Read the entire chunk into a memory buffer for maximum speed
    // Calculate max possible size and read in bulk
    std::vector<char> chunkBuf(numSamp * maxSampleBytes);
    // We can't know exact size upfront (variable timestamp), so read sample by sample
    // but from a large pre-read buffer

    // Actually, let's read the whole remaining chunk data at once
    // We know the data layout: for each sample, 1 byte tsFlag + (0 or 8 bytes ts) + rowBytes
    // First pass: figure out total bytes needed by reading just the ts flags
    // Better approach: read in large blocks

    // Use a larger read buffer — read 64KB at a time
    constexpr size_t BLOCK_SIZE = 65536;
    std::vector<char> blockBuf(BLOCK_SIZE);
    size_t blockPos = 0;
    size_t blockLen = 0;

    auto ensureBytes = [&](size_t needed) {
        if (blockPos + needed <= blockLen) return;
        // Move remaining bytes to front
        size_t remaining = blockLen - blockPos;
        if (remaining > 0)
            std::memmove(blockBuf.data(), blockBuf.data() + blockPos, remaining);
        blockPos = 0;
        blockLen = remaining;
        // Read more
        size_t toRead = BLOCK_SIZE - remaining;
        f.read(blockBuf.data() + remaining, toRead);
        blockLen += static_cast<size_t>(f.gcount());
    };

    auto readBytes = [&](void *dst, size_t n) {
        ensureBytes(n);
        std::memcpy(dst, blockBuf.data() + blockPos, n);
        blockPos += n;
    };

    bool isFloat32 = (s.channelFormat == "float32");
    bool isDouble64 = (s.channelFormat == "double64");

    for (uint64_t i = 0; i < numSamp; ++i) {
        size_t idx = oldSize + i;

        // Read timestamp flag
        uint8_t tsFlag;
        readBytes(&tsFlag, 1);

        double ts;
        if (tsFlag == 8) {
            readBytes(&ts, 8);
        } else {
            ts = s.lastTimestamp + s.samplingInterval;
        }
        s.timestamps[idx] = ts;
        s.lastTimestamp = ts;

        // Read sample row
        ensureBytes(rowBytes);
        const char *ptr = blockBuf.data() + blockPos;
        blockPos += rowBytes;

        // Fast path for float32 (most common for LSL)
        if (isFloat32 && bps == 4) {
            for (int ch = 0; ch < s.channelCount; ++ch) {
                float val;
                std::memcpy(&val, ptr + ch * 4, 4);
                s.data[ch][idx] = val;
            }
        } else if (isDouble64 && bps == 8) {
            for (int ch = 0; ch < s.channelCount; ++ch) {
                double dval;
                std::memcpy(&dval, ptr + ch * 8, 8);
                s.data[ch][idx] = static_cast<float>(dval);
            }
        } else {
            for (int ch = 0; ch < s.channelCount; ++ch) {
                float val = 0.0f;
                if (bps == 2) {
                    int16_t ival;
                    std::memcpy(&ival, ptr, 2);
                    val = static_cast<float>(ival);
                } else if (bps == 4) {
                    int32_t ival;
                    std::memcpy(&ival, ptr, 4);
                    val = static_cast<float>(ival);
                } else if (bps == 8) {
                    int64_t ival;
                    std::memcpy(&ival, ptr, 8);
                    val = static_cast<float>(ival);
                } else if (bps == 1) {
                    int8_t ival;
                    std::memcpy(&ival, ptr, 1);
                    val = static_cast<float>(ival);
                }
                s.data[ch][idx] = val;
                ptr += bps;
            }
        }
    }

    // Put back any unread bytes from our buffer
    size_t remaining = blockLen - blockPos;
    if (remaining > 0) {
        f.seekg(-static_cast<std::streamoff>(remaining), std::ios::cur);
    }
}

void readStringChunk(std::ifstream &f, RawStream &s, uint64_t numSamp) {
    // Skip string events — we just note timestamps
    for (uint64_t i = 0; i < numSamp; ++i) {
        uint8_t tsBytes = readBin<uint8_t>(f);
        double ts;
        if (tsBytes == 8) {
            ts = readBin<double>(f);
        } else {
            ts = s.lastTimestamp + s.samplingInterval;
        }
        s.timestamps.push_back(ts);
        s.lastTimestamp = ts;

        // Read and skip the string
        uint64_t strLen = readVarLen(f);
        f.seekg(strLen, std::ios::cur);
    }
}

void syncTimestamps(RawStream &s) {
    if (s.clockTimes.empty()) return;

    size_t n = 0;
    for (size_t m = 0; m < s.timestamps.size(); ++m) {
        if (s.clockTimes[n] < s.timestamps[m]) {
            while (n < s.clockTimes.size() - 1 && s.clockTimes[n + 1] < s.timestamps[m])
                ++n;
            s.timestamps[m] += s.clockValues[n];
        } else if (n == 0) {
            s.timestamps[m] += s.clockValues[0];
        }
    }
}

} // anonymous namespace

bool XdfLoader::load(const QString &filePath)
{
    m_streams.clear();
    m_error.clear();

    std::ifstream file(filePath.toStdString(), std::ios::in | std::ios::binary);
    if (!file.is_open()) {
        m_error = "Cannot open file: " + filePath;
        return false;
    }

    // Check magic
    char magic[5] = {};
    file.read(magic, 4);
    if (std::string(magic) != "XDF:") {
        m_error = "Not a valid XDF file (bad magic number).";
        return false;
    }

    std::vector<uint32_t> idmap;
    std::vector<RawStream> rawStreams;

    auto findOrCreateStream = [&](uint32_t streamId) -> int {
        auto it = std::find(idmap.begin(), idmap.end(), streamId);
        if (it != idmap.end())
            return static_cast<int>(std::distance(idmap.begin(), it));
        int idx = static_cast<int>(idmap.size());
        idmap.push_back(streamId);
        rawStreams.emplace_back();
        rawStreams.back().streamId = streamId;
        return idx;
    };

    // Parse chunks
    while (file.good()) {
        uint64_t chunkLen = readVarLen(file);
        if (chunkLen == 0 || !file.good())
            break;

        uint16_t tag = readBin<uint16_t>(file);

        switch (tag) {
        case 1: { // FileHeader — skip
            file.seekg(chunkLen - 2, std::ios::cur);
            break;
        }
        case 2: { // StreamHeader
            uint32_t streamId = readBin<uint32_t>(file);
            int idx = findOrCreateStream(streamId);

            std::string xml(chunkLen - 6, '\0');
            file.read(xml.data(), chunkLen - 6);
            parseStreamHeader(xml, rawStreams[idx]);
            break;
        }
        case 3: { // Samples
            uint32_t streamId = readBin<uint32_t>(file);
            int idx = findOrCreateStream(streamId);
            uint64_t numSamp = readVarLen(file);

            auto &rs = rawStreams[idx];
            if (rs.channelFormat == "string") {
                readStringChunk(file, rs, numSamp);
            } else {
                readNumericChunk(file, rs, numSamp);
            }
            break;
        }
        case 4: { // ClockOffset
            uint32_t streamId = readBin<uint32_t>(file);
            int idx = findOrCreateStream(streamId);

            double collectionTime = readBin<double>(file);
            double offsetValue = readBin<double>(file);

            rawStreams[idx].clockTimes.push_back(collectionTime);
            rawStreams[idx].clockValues.push_back(offsetValue);
            break;
        }
        case 6: { // StreamFooter — skip
            file.seekg(chunkLen - 2, std::ios::cur);
            break;
        }
        default: {
            file.seekg(chunkLen - 2, std::ios::cur);
            break;
        }
        }
    }

    // Apply clock offset synchronization
    for (auto &rs : rawStreams)
        syncTimestamps(rs);

    // Convert to XdfStream
    m_globalMinTime = std::numeric_limits<double>::max();
    m_globalMaxTime = std::numeric_limits<double>::lowest();

    for (size_t i = 0; i < rawStreams.size(); ++i) {
        auto &rs = rawStreams[i];
        XdfStream s;
        s.id = static_cast<int>(i);
        s.name = rs.name;
        s.type = rs.type;
        s.channelCount = rs.channelCount;
        s.nominalSrate = rs.nominalSrate;
        s.channelFormat = rs.channelFormat;
        s.sampleCount = static_cast<int>(rs.timestamps.size());
        s.channelLabels = std::move(rs.channelLabels);
        s.timeStamps = std::move(rs.timestamps);
        s.data = std::move(rs.data);

        if (s.hasData()) {
            m_globalMinTime = std::min(m_globalMinTime, s.minTime());
            m_globalMaxTime = std::max(m_globalMaxTime, s.maxTime());
        }
        m_streams.push_back(std::move(s));
    }

    if (m_globalMinTime > m_globalMaxTime) {
        m_globalMinTime = 0.0;
        m_globalMaxTime = 0.0;
    }

    if (m_streams.empty()) {
        m_error = "XDF file contains no streams.";
        return false;
    }

    return true;
}
