#pragma once

#include "core/TelemetryTypes.h"

#include <QVector>

namespace sysmon {

/**
 * @brief /proc/meminfo, /proc/swaps ve spd5118 hwmon sensörlerinden
 *        bellek ve takas/ZRAM telemetrisini toplar.
 */
class MemoryCollector {
public:
    [[nodiscard]] MemoryTelemetry collect(const QVector<ThermalSensor>& thermals) const;
};

} // namespace sysmon
