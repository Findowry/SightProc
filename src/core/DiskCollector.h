#pragma once

#include "core/TelemetryTypes.h"

#include <QVector>

#include <cstdint>
#include <string>
#include <unordered_map>

namespace sysmon {

/**
 * @brief /sys/block, /proc/diskstats ve /proc/mounts üzerinden fiziksel
 *        diskler (NVMe/SATA/SCSI) için performans ve sıcaklık toplar.
 */
class DiskCollector {
public:
    [[nodiscard]] QVector<DiskTelemetry> collect(double elapsedSec,
                                                 const QVector<ThermalSensor>& thermals);

private:
    struct DiskCounters {
        std::uint64_t readsCompleted{0};
        std::uint64_t sectorsRead{0};
        std::uint64_t readMs{0};
        std::uint64_t writesCompleted{0};
        std::uint64_t sectorsWritten{0};
        std::uint64_t writeMs{0};
        std::uint64_t ioMs{0};
    };

    std::unordered_map<std::string, DiskCounters> m_prevCounters;
};

} // namespace sysmon
