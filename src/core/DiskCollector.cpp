#include "core/DiskCollector.h"

#include "core/FormatUtils.h"

#include <sys/statvfs.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

namespace sysmon {

namespace {

bool isVirtualBlockDevice(const std::string& name) {
    return name.starts_with("loop") ||
           name.starts_with("ram") ||
           name.starts_with("zram") ||
           name.starts_with("sr") ||
           name.starts_with("fd") ||
           name.starts_with("dm-");
}

QString extractActiveScheduler(const std::string& raw) {
    const auto openPos = raw.find('[');
    const auto closePos = raw.find(']', openPos == std::string::npos ? 0 : openPos);
    if (openPos != std::string::npos && closePos != std::string::npos && closePos > openPos + 1) {
        return QString::fromStdString(raw.substr(openPos + 1, closePos - openPos - 1));
    }
    return raw.empty() ? QStringLiteral("—") : QString::fromStdString(raw);
}

std::string unescapeMountString(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\\' && i + 3 < s.size() &&
            s[i + 1] == '0' && s[i + 2] == '4' && s[i + 3] == '0') {
            out.push_back(' ');
            i += 3;
        } else {
            out.push_back(s[i]);
        }
    }
    return out;
}

double readDiskHwmonTemp(const std::filesystem::path& blockDir) {
    std::error_code ec;
    const std::filesystem::path candidates[] = {
        blockDir / "device/hwmon",
        blockDir / "device/device/hwmon",
    };
    for (const auto& hwRoot : candidates) {
        ec.clear();
        if (!std::filesystem::exists(hwRoot, ec) || ec) {
            continue;
        }
        std::error_code iterEc;
        for (std::filesystem::directory_iterator it(hwRoot, iterEc), end;
             !iterEc && it != end;
             it.increment(iterEc)) {
            if (const auto milliOpt = sysfs::readInt64(it->path() / "temp1_input")) {
                return static_cast<double>(*milliOpt) / 1000.0;
            }
        }
    }
    return -100.0;
}

} // namespace

QVector<DiskTelemetry> DiskCollector::collect(double elapsedSec,
                                              const QVector<ThermalSensor>& thermals) {
    QVector<DiskTelemetry> result;
    std::error_code ec;
    const std::filesystem::path blockRoot("/sys/block");
    if (!std::filesystem::exists(blockRoot, ec)) {
        return result;
    }

    std::vector<std::string> diskNames;
    {
        std::error_code iterEc;
        for (std::filesystem::directory_iterator it(blockRoot, iterEc), end;
             !iterEc && it != end;
             it.increment(iterEc)) {
            const std::string name = it->path().filename().string();
            if (!isVirtualBlockDevice(name)) {
                diskNames.push_back(name);
            }
        }
    }
    std::sort(diskNames.begin(), diskNames.end());

    // /proc/diskstats oku
    std::unordered_map<std::string, DiskCounters> currStats;
    {
        std::ifstream dsFile("/proc/diskstats");
        std::string line;
        while (std::getline(dsFile, line)) {
            std::istringstream iss(line);
            unsigned int major = 0;
            unsigned int minor = 0;
            std::string devName;
            DiskCounters c{};
            std::uint64_t rMerged = 0;
            std::uint64_t wMerged = 0;
            std::uint64_t ioInProg = 0;
            if (iss >> major >> minor >> devName
                    >> c.readsCompleted >> rMerged >> c.sectorsRead >> c.readMs
                    >> c.writesCompleted >> wMerged >> c.sectorsWritten >> c.writeMs
                    >> ioInProg >> c.ioMs) {
                currStats[devName] = c;
            }
        }
    }

    // Bağlama noktalarını /proc/mounts üzerinden eşleştir
    struct MountEntry {
        std::string source;
        std::string target;
    };
    std::vector<MountEntry> mounts;
    {
        std::ifstream mFile("/proc/mounts");
        std::string line;
        while (std::getline(mFile, line)) {
            std::istringstream iss(line);
            std::string src;
            std::string target;
            if (iss >> src >> target) {
                if (src.starts_with("/dev/")) {
                    mounts.push_back(MountEntry{src, unescapeMountString(target)});
                }
            }
        }
    }

    const double safeDt = std::max(elapsedSec, 0.001);

    for (const auto& name : diskNames) {
        const std::filesystem::path devDir = blockRoot / name;
        DiskTelemetry disk;
        disk.name = QString::fromStdString(name);
        disk.devicePath = QStringLiteral("/dev/%1").arg(disk.name);

        if (const auto modelOpt = sysfs::readFirstLine(devDir / "device/model")) {
            disk.model = QString::fromStdString(*modelOpt);
        } else {
            disk.model = disk.name;
        }

        if (const auto sectorsOpt = sysfs::readUint64(devDir / "size")) {
            disk.capacityBytes = (*sectorsOpt) * 512ULL;
        }
        if (const auto rotOpt = sysfs::readUint64(devDir / "queue/rotational")) {
            disk.isRotational = (*rotOpt != 0);
        }
        if (const auto lbsOpt = sysfs::readUint64(devDir / "queue/logical_block_size")) {
            disk.logicalBlockSize = *lbsOpt;
        }

        if (const auto schedOpt = sysfs::readFirstLine(devDir / "queue/scheduler")) {
            disk.scheduler = extractActiveScheduler(*schedOpt);
        } else {
            disk.scheduler = QStringLiteral("—");
        }

        disk.tempC = readDiskHwmonTemp(devDir);
        if (disk.tempC < -50.0 && name.starts_with("nvme")) {
            for (const auto& t : thermals) {
                if (t.hwmonName == QStringLiteral("nvme")) {
                    disk.tempC = t.tempC;
                    break;
                }
            }
        }

        const std::string devPrefix = "/dev/" + name;
        for (const auto& m : mounts) {
            if (m.source == devPrefix || m.source.starts_with(devPrefix)) {
                struct statvfs vfs{};
                if (statvfs(m.target.c_str(), &vfs) == 0 && vfs.f_blocks > 0) {
                    const std::uint64_t total =
                        static_cast<std::uint64_t>(vfs.f_blocks) * vfs.f_frsize;
                    const std::uint64_t avail =
                        static_cast<std::uint64_t>(vfs.f_bavail) * vfs.f_frsize;
                    const std::uint64_t used = (total > avail) ? (total - avail) : 0;
                    disk.mountPoints.push_back(QStringLiteral("%1 (%2 / %3)")
                                                   .arg(QString::fromStdString(m.target),
                                                        fmt::formatBytes(used),
                                                        fmt::formatBytes(total)));
                } else {
                    disk.mountPoints.push_back(QString::fromStdString(m.target));
                }
            }
        }

        if (const auto currIt = currStats.find(name); currIt != currStats.end()) {
            const DiskCounters& curr = currIt->second;
            disk.totalReadBytes = curr.sectorsRead * 512ULL;
            disk.totalWriteBytes = curr.sectorsWritten * 512ULL;
            disk.totalReadOps = curr.readsCompleted;
            disk.totalWriteOps = curr.writesCompleted;

            if (const auto prevIt = m_prevCounters.find(name); prevIt != m_prevCounters.end()) {
                const DiskCounters& prev = prevIt->second;
                const std::uint64_t dReadSec = (curr.sectorsRead >= prev.sectorsRead)
                    ? (curr.sectorsRead - prev.sectorsRead)
                    : 0;
                const std::uint64_t dWriteSec = (curr.sectorsWritten >= prev.sectorsWritten)
                    ? (curr.sectorsWritten - prev.sectorsWritten)
                    : 0;
                const std::uint64_t dReads = (curr.readsCompleted >= prev.readsCompleted)
                    ? (curr.readsCompleted - prev.readsCompleted)
                    : 0;
                const std::uint64_t dWrites = (curr.writesCompleted >= prev.writesCompleted)
                    ? (curr.writesCompleted - prev.writesCompleted)
                    : 0;
                const std::uint64_t dReadMs = (curr.readMs >= prev.readMs)
                    ? (curr.readMs - prev.readMs)
                    : 0;
                const std::uint64_t dWriteMs = (curr.writeMs >= prev.writeMs)
                    ? (curr.writeMs - prev.writeMs)
                    : 0;
                const std::uint64_t dIoMs = (curr.ioMs >= prev.ioMs)
                    ? (curr.ioMs - prev.ioMs)
                    : 0;

                disk.readBytesPerSec = static_cast<double>(dReadSec * 512ULL) / safeDt;
                disk.writeBytesPerSec = static_cast<double>(dWriteSec * 512ULL) / safeDt;
                disk.readIops = static_cast<double>(dReads) / safeDt;
                disk.writeIops = static_cast<double>(dWrites) / safeDt;
                disk.activePercent = std::clamp(
                    static_cast<double>(dIoMs) / (safeDt * 10.0),
                    0.0,
                    100.0);

                const std::uint64_t dOps = dReads + dWrites;
                if (dOps > 0) {
                    disk.avgResponseMs =
                        static_cast<double>(dReadMs + dWriteMs) / static_cast<double>(dOps);
                }
            }
            m_prevCounters[name] = curr;
        }

        result.push_back(std::move(disk));
    }

    return result;
}

} // namespace sysmon
