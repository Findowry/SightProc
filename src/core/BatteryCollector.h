#pragma once

#include "TelemetryTypes.h"

#include <QVector>

namespace sysmon {

class BatteryCollector {
public:
    QVector<BatteryTelemetry> collect() const;
};

} // namespace sysmon
