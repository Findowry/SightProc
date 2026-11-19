#pragma once

#include "core/TelemetryTypes.h"

#include <QVector>

#include <cstdint>
#include <unordered_map>

namespace sysmon {

/**
 * @brief /proc/stat, /proc/cpuinfo, /proc/uptime ve sysfs cpufreq/cache
 *        kaynaklarından hibrit P/E çekirdek uyumlu işlemci telemetrisi toplar.
 */
class CpuCollector {
public:
    CpuCollector();

    [[nodiscard]] CpuTelemetry collect(double elapsedSec,
                                       double packageTempC,
                                       const QVector<ThermalSensor>& thermals = {});

private:
    struct CpuJiffies {
        std::uint64_t user{0};
        std::uint64_t nice{0};
        std::uint64_t system{0};
        std::uint64_t idle{0};
        std::uint64_t iowait{0};
        std::uint64_t irq{0};
        std::uint64_t softirq{0};
        std::uint64_t steal{0};

        [[nodiscard]] std::uint64_t activeTotal() const noexcept {
            return user + nice + system + irq + softirq + steal;
        }
        [[nodiscard]] std::uint64_t idleTotal() const noexcept {
            return idle + iowait;
        }
        [[nodiscard]] std::uint64_t allTotal() const noexcept {
            return activeTotal() + idleTotal();
        }
    };

    void initStaticMetadata();

    QString m_modelName;
    QString m_vendor;
    QString m_architecture;
    int m_physicalCores{0};
    int m_logicalCores{0};

    double m_baseFreqMHz{0.0};
    double m_boostFreqMHz{0.0};
    std::uint64_t m_l1dBytes{0};
    std::uint64_t m_l1iBytes{0};
    std::uint64_t m_l2Bytes{0};
    std::uint64_t m_l3Bytes{0};

    QString m_scalingDriver;
    QString m_energyPreference;
    QString m_virtualization;
    QString m_microcode;
    double m_bogoMips{0.0};

    bool m_hasPrev{false};
    CpuJiffies m_prevTotal{};
    std::unordered_map<int, CpuJiffies> m_prevPerCore;
    std::uint64_t m_prevCtxt{0};
    std::uint64_t m_prevIntr{0};
};

} // namespace sysmon
