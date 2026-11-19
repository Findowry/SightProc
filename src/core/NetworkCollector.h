#pragma once

#include "core/TelemetryTypes.h"

#include <QVector>

#include <cstdint>
#include <string>
#include <unordered_map>

namespace sysmon {

/**
 * @brief /sys/class/net, /proc/net/dev ve getifaddrs() üzerinden ağ arayüzü
 *        hızlarını, oturum toplamlarını ve IP adreslerini toplar.
 */
class NetworkCollector {
public:
    [[nodiscard]] QVector<NetworkTelemetry> collect(double elapsedSec);

private:
    struct NetCounters {
        std::uint64_t rxBytes{0};
        std::uint64_t txBytes{0};
    };

    std::unordered_map<std::string, NetCounters> m_prevCounters;
};

} // namespace sysmon
