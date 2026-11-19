#pragma once

#include "core/TelemetryTypes.h"

#include <QVector>

#include <string>
#include <unordered_map>
#include <utility>

namespace sysmon {

struct HwmonReadings {
    QVector<FanTelemetry> fans;
    QVector<ThermalSensor> thermals;
};

/**
 * @brief /sys/class/hwmon üzerinden ardışık olmayan sensör indekslerini güvenle
 *        okuyan fan devri ve sıcaklık toplayıcısı.
 */
class HwmonCollector {
public:
    [[nodiscard]] HwmonReadings collect();
    [[nodiscard]] static double findCpuPackageTemp(const QVector<ThermalSensor>& thermals);

private:
    std::unordered_map<std::string, std::pair<int, int>> m_fanMinMax;
};

} // namespace sysmon
