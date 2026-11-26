#include "BatteryCollector.h"
#include "FormatUtils.h"

#include <algorithm>
#include <cmath>
#include <filesystem>

namespace sysmon {

QVector<BatteryTelemetry> BatteryCollector::collect() const {
    QVector<BatteryTelemetry> result;
    const std::filesystem::path root("/sys/class/power_supply");
    std::error_code ec;
    if (!std::filesystem::exists(root, ec)) {
        return result;
    }

    std::vector<std::filesystem::path> dirs;
    {
        std::error_code iterEc;
        for (std::filesystem::directory_iterator it(root, iterEc), end;
             !iterEc && it != end;
             it.increment(iterEc)) {
            std::error_code entryEc;
            const bool isDir = it->is_directory(entryEc) && !entryEc;
            entryEc.clear();
            const bool isLink = it->is_symlink(entryEc) && !entryEc;
            if (!isDir && !isLink) {
                continue;
            }
            dirs.push_back(it->path());
        }
    }
    std::sort(dirs.begin(), dirs.end());

    for (const auto& dir : dirs) {
        const auto typeOpt = sysfs::readFirstLine(dir / "type");
        if (!typeOpt || *typeOpt != "Battery") {
            continue;
        }
        const auto scopeOpt = sysfs::readFirstLine(dir / "scope");
        if (scopeOpt && *scopeOpt == "Device") {
            continue;
        }
        if (const auto presentOpt = sysfs::readInt64(dir / "present")) {
            if (*presentOpt == 0) {
                continue;
            }
        }

        BatteryTelemetry bat;
        bat.id = QString::fromStdString(dir.filename().string());

        if (const auto modelOpt = sysfs::readFirstLine(dir / "model_name")) {
            bat.modelName = QString::fromStdString(*modelOpt).trimmed();
        }
        if (const auto mfgOpt = sysfs::readFirstLine(dir / "manufacturer")) {
            bat.manufacturer = QString::fromStdString(*mfgOpt).trimmed();
        }
        if (const auto techOpt = sysfs::readFirstLine(dir / "technology")) {
            bat.technology = QString::fromStdString(*techOpt).trimmed();
        }
        if (const auto statusOpt = sysfs::readFirstLine(dir / "status")) {
            bat.status = QString::fromStdString(*statusOpt).trimmed();
        }
        if (const auto cycOpt = sysfs::readInt64(dir / "cycle_count")) {
            if (*cycOpt >= 0) {
                bat.cycleCount = static_cast<int>(*cycOpt);
            }
        }

        auto voltOpt = sysfs::readInt64(dir / "voltage_now");
        if (!voltOpt || *voltOpt <= 0) {
            voltOpt = sysfs::readInt64(dir / "voltage_min_design");
        }
        if (voltOpt && *voltOpt > 0) {
            bat.voltageVolts = static_cast<double>(*voltOpt) / 1'000'000.0;
        }

        const auto eNowOpt = sysfs::readInt64(dir / "energy_now");
        const auto eFullOpt = sysfs::readInt64(dir / "energy_full");
        const auto eDesignOpt = sysfs::readInt64(dir / "energy_full_design");

        if (eNowOpt && *eNowOpt >= 0) {
            bat.energyNowWh = static_cast<double>(*eNowOpt) / 1'000'000.0;
        }
        if (eFullOpt && *eFullOpt > 0) {
            bat.energyFullWh = static_cast<double>(*eFullOpt) / 1'000'000.0;
        }
        if (eDesignOpt && *eDesignOpt > 0) {
            bat.energyDesignWh = static_cast<double>(*eDesignOpt) / 1'000'000.0;
        }

        // Fallback to charge_* (uAh) * voltage (V) for ACPI drivers that report Coulombs instead of Wh.
        if (bat.voltageVolts > 0.0) {
            if (bat.energyNowWh < 0.0) {
                if (const auto cNowOpt = sysfs::readInt64(dir / "charge_now"); cNowOpt && *cNowOpt >= 0) {
                    bat.energyNowWh = (static_cast<double>(*cNowOpt) / 1'000'000.0) * bat.voltageVolts;
                }
            }
            if (bat.energyFullWh < 0.0) {
                if (const auto cFullOpt = sysfs::readInt64(dir / "charge_full"); cFullOpt && *cFullOpt > 0) {
                    bat.energyFullWh = (static_cast<double>(*cFullOpt) / 1'000'000.0) * bat.voltageVolts;
                }
            }
            if (bat.energyDesignWh < 0.0) {
                if (const auto cDesOpt = sysfs::readInt64(dir / "charge_full_design"); cDesOpt && *cDesOpt > 0) {
                    bat.energyDesignWh = (static_cast<double>(*cDesOpt) / 1'000'000.0) * bat.voltageVolts;
                }
            }
        }

        if (const auto pNowOpt = sysfs::readInt64(dir / "power_now")) {
            bat.powerWatts = std::abs(static_cast<double>(*pNowOpt)) / 1'000'000.0;
        } else if (bat.voltageVolts > 0.0) {
            if (const auto currOpt = sysfs::readInt64(dir / "current_now")) {
                bat.powerWatts = (std::abs(static_cast<double>(*currOpt)) / 1'000'000.0) * bat.voltageVolts;
            }
        }

        if (const auto capOpt = sysfs::readInt64(dir / "capacity")) {
            bat.capacityPercent = std::clamp(static_cast<double>(*capOpt), 0.0, 100.0);
        } else if (bat.energyNowWh >= 0.0 && bat.energyFullWh > 0.0) {
            bat.capacityPercent = std::clamp(100.0 * bat.energyNowWh / bat.energyFullWh, 0.0, 100.0);
        }

        if (bat.energyFullWh > 0.0 && bat.energyDesignWh > 0.0) {
            bat.healthPercent = 100.0 * bat.energyFullWh / bat.energyDesignWh;
        }

        if (bat.powerWatts > 0.1) {
            if (bat.status == QStringLiteral("Discharging") && bat.energyNowWh > 0.0) {
                bat.remainingHours = bat.energyNowWh / bat.powerWatts;
            } else if (bat.status == QStringLiteral("Charging")
                       && bat.energyFullWh > bat.energyNowWh && bat.energyNowWh >= 0.0) {
                bat.remainingHours = (bat.energyFullWh - bat.energyNowWh) / bat.powerWatts;
            }
        }

        if (const auto tempOpt = sysfs::readInt64(dir / "temp")) {
            if (*tempOpt > -500 && *tempOpt < 1500) {
                bat.tempC = static_cast<double>(*tempOpt) / 10.0;
            }
        }

        result.push_back(bat);
    }

    return result;
}

} // namespace sysmon
