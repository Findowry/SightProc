#include "core/MemoryCollector.h"

#include "core/FormatUtils.h"

#include <algorithm>
#include <fstream>
#include <sstream>
#include <string>

namespace sysmon {

MemoryTelemetry MemoryCollector::collect(const QVector<ThermalSensor>& thermals) const {
    MemoryTelemetry out;

    std::ifstream meminfo("/proc/meminfo");
    std::string line;
    std::uint64_t cachedRaw = 0;

    while (std::getline(meminfo, line)) {
        const auto colonPos = line.find(':');
        if (colonPos == std::string::npos) {
            continue;
        }
        const std::string key = line.substr(0, colonPos);
        std::istringstream iss(line.substr(colonPos + 1));
        std::uint64_t kbVal = 0;
        if (!(iss >> kbVal)) {
            continue;
        }
        const std::uint64_t bytes = kbVal * 1024ULL;

        if (key == "MemTotal") {
            out.totalBytes = bytes;
        } else if (key == "MemFree") {
            out.freeBytes = bytes;
        } else if (key == "MemAvailable") {
            out.availableBytes = bytes;
        } else if (key == "Buffers") {
            out.buffersBytes = bytes;
        } else if (key == "Cached") {
            cachedRaw = bytes;
        } else if (key == "SReclaimable") {
            out.reclaimableBytes = bytes;
        } else if (key == "Shmem") {
            out.sharedBytes = bytes;
        } else if (key == "Dirty") {
            out.dirtyBytes = bytes;
        } else if (key == "Writeback") {
            out.writebackBytes = bytes;
        } else if (key == "Active") {
            out.activeBytes = bytes;
        } else if (key == "Inactive") {
            out.inactiveBytes = bytes;
        } else if (key == "Committed_AS") {
            out.committedAsBytes = bytes;
        } else if (key == "PageTables") {
            out.pageTablesBytes = bytes;
        } else if (key == "SwapTotal") {
            out.swapTotalBytes = bytes;
        } else if (key == "SwapFree") {
            out.swapUsedBytes = (out.swapTotalBytes > bytes) ? (out.swapTotalBytes - bytes) : 0;
        }
    }

    // MemAvailable henüz okunmamışsa SwapFree sırasını düzelt
    if (out.swapTotalBytes > 0 && out.swapUsedBytes > out.swapTotalBytes) {
        out.swapUsedBytes = 0;
    }

    out.cachedBytes = cachedRaw + out.reclaimableBytes;
    out.usedBytes = (out.totalBytes > out.availableBytes)
        ? (out.totalBytes - out.availableBytes)
        : 0;

    if (out.totalBytes > 0) {
        out.usagePercent = std::clamp(
            100.0 * static_cast<double>(out.usedBytes) / static_cast<double>(out.totalBytes),
            0.0,
            100.0);
    }
    if (out.swapTotalBytes > 0) {
        out.swapUsagePercent = std::clamp(
            100.0 * static_cast<double>(out.swapUsedBytes) / static_cast<double>(out.swapTotalBytes),
            0.0,
            100.0);
    }

    std::ifstream swaps("/proc/swaps");
    bool firstLine = true;
    while (std::getline(swaps, line)) {
        if (firstLine) {
            firstLine = false;
            continue;
        }
        std::istringstream iss(line);
        std::string filename;
        std::string type;
        std::uint64_t sizeKb = 0;
        std::uint64_t usedKb = 0;
        int priority = 0;
        if (iss >> filename >> type >> sizeKb >> usedKb >> priority) {
            out.swapDevices.push_back(QStringLiteral("%1 (%2 / %3)")
                                          .arg(QString::fromStdString(filename),
                                               fmt::formatBytes(usedKb * 1024ULL),
                                               fmt::formatBytes(sizeKb * 1024ULL)));
        }
    }

    int dimmIndex = 1;
    for (const auto& t : thermals) {
        if (t.hwmonName == QStringLiteral("spd5118")) {
            out.dimmTempsC.push_back(
                qMakePair(QStringLiteral("DIMM %1").arg(dimmIndex++), t.tempC));
        }
    }

    return out;
}

} // namespace sysmon
