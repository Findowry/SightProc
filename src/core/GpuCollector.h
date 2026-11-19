#pragma once

#include "core/NvmlBridge.h"
#include "core/TelemetryTypes.h"

#include <QVector>

#include <cstdint>
#include <string>
#include <unordered_map>

namespace sysmon {

/**
 * @brief /sys/class/drm/card[0-9]* üzerinden Intel (i915/xe), AMD (amdgpu)
 *        ve NVIDIA (dlopen NVML, D3cold korumalı) GPU verilerini toplar.
 */
class GpuCollector {
public:
    explicit GpuCollector(NvmlBridge& nvml);

    [[nodiscard]] QVector<GpuTelemetry> collect(double elapsedSec, bool probeSleepingGpus);

private:
    struct CardStaticInfo {
        int cardNum{0};
        QString id;
        QString sysPath;
        QString vendorHex;
        QString deviceHex;
        QString vendorName;
        QString deviceName;
        QString driver;
        QString pciSlot;
    };

    void ensureDiscovered();
    void resolvePciNames();

    NvmlBridge& m_nvml;
    bool m_discovered{false};
    QVector<CardStaticInfo> m_cards;
    std::unordered_map<std::string, std::uint64_t> m_prevIntelRc6Ms;
};

} // namespace sysmon
