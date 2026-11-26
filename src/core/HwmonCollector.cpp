#include "core/HwmonCollector.h"

#include "core/FormatUtils.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <map>
#include <string>
#include <system_error>
#include <vector>

namespace sysmon {

namespace {

bool isAllDigits(std::string_view sv) {
    if (sv.empty()) {
        return false;
    }
    return std::all_of(sv.begin(), sv.end(), [](unsigned char ch) {
        return std::isdigit(ch) != 0;
    });
}

bool extractSensorIndex(const std::string& filename,
                        std::string_view prefix,
                        std::string_view suffix,
                        int& outIndex) {
    if (filename.size() <= prefix.size() + suffix.size()) {
        return false;
    }
    if (!filename.starts_with(prefix) || !filename.ends_with(suffix)) {
        return false;
    }
    const std::string_view numPart(filename.data() + prefix.size(),
                                   filename.size() - prefix.size() - suffix.size());
    if (!isAllDigits(numPart)) {
        return false;
    }
    outIndex = std::stoi(std::string(numPart));
    return true;
}

QString pwmModeString(std::uint64_t mode) {
    switch (mode) {
    case 0:
        return QStringLiteral("Tam Hız");
    case 1:
        return QStringLiteral("Manuel");
    case 2:
        return QStringLiteral("Otomatik");
    default:
        return QString::number(mode);
    }
}

} // namespace

HwmonReadings HwmonCollector::collect() {
    HwmonReadings result;
    std::error_code ec;
    const std::filesystem::path root("/sys/class/hwmon");
    if (!std::filesystem::exists(root, ec)) {
        return result;
    }

    std::vector<std::filesystem::path> hwmonDirs;
    {
        std::error_code iterEc;
        for (std::filesystem::directory_iterator it(root, iterEc), end;
             !iterEc && it != end;
             it.increment(iterEc)) {
            std::error_code entryEc;
            if (it->exists(entryEc) && !entryEc) {
                hwmonDirs.push_back(it->path());
            }
        }
    }
    std::sort(hwmonDirs.begin(), hwmonDirs.end());

    struct InternalThermal {
        std::string hwmonDir;
        ThermalSensor sensor;
    };
    struct PendingFan {
        std::string hwmonDir;
        FanTelemetry fan;
    };

    std::vector<InternalThermal> internalThermals;
    std::vector<PendingFan> pendingFans;

    for (const auto& dir : hwmonDirs) {
        const std::string dirKey = dir.filename().string();
        const auto nameOpt = sysfs::readFirstLine(dir / "name");
        const QString hwmonName = nameOpt
            ? QString::fromStdString(*nameOpt)
            : QString::fromStdString(dirKey);

        std::map<int, std::filesystem::path> tempInputs;
        std::map<int, std::filesystem::path> fanInputs;

        {
            std::error_code iterEc;
            for (std::filesystem::directory_iterator it(dir, iterEc), end;
                 !iterEc && it != end;
                 it.increment(iterEc)) {
                const std::string fname = it->path().filename().string();
                int idx = 0;
                if (extractSensorIndex(fname, "temp", "_input", idx)) {
                    tempInputs[idx] = it->path();
                } else if (extractSensorIndex(fname, "fan", "_input", idx)) {
                    fanInputs[idx] = it->path();
                }
            }
        }

        for (const auto& [idx, inputPath] : tempInputs) {
            const auto milliOpt = sysfs::readInt64(inputPath);
            if (!milliOpt) {
                continue;
            }
            ThermalSensor sensor;
            sensor.id = QString::fromStdString(dirKey + ":temp" + std::to_string(idx));
            sensor.hwmonName = hwmonName;
            sensor.tempC = static_cast<double>(*milliOpt) / 1000.0;

            const auto labelOpt = sysfs::readFirstLine(
                dir / ("temp" + std::to_string(idx) + "_label"));
            sensor.label = labelOpt && !labelOpt->empty()
                ? QString::fromStdString(*labelOpt)
                : QStringLiteral("temp%1").arg(idx);

            if (const auto maxOpt = sysfs::readInt64(
                    dir / ("temp" + std::to_string(idx) + "_max"))) {
                sensor.maxTempC = static_cast<double>(*maxOpt) / 1000.0;
            }
            if (const auto critOpt = sysfs::readInt64(
                    dir / ("temp" + std::to_string(idx) + "_crit"))) {
                sensor.critTempC = static_cast<double>(*critOpt) / 1000.0;
            }

            result.thermals.push_back(sensor);
            internalThermals.push_back(InternalThermal{dirKey, sensor});
        }

        for (const auto& [idx, inputPath] : fanInputs) {
            const auto rpmOpt = sysfs::readInt64(inputPath);
            if (!rpmOpt) {
                continue;
            }
            const int rpm = std::max(0, static_cast<int>(*rpmOpt));
            const std::string fanId = dirKey + ":fan" + std::to_string(idx);

            auto [it, inserted] = m_fanMinMax.emplace(fanId, std::make_pair(rpm, rpm));
            if (!inserted) {
                if (rpm > 0 && (it->second.first == 0 || rpm < it->second.first)) {
                    it->second.first = rpm;
                }
                it->second.second = std::max(it->second.second, rpm);
            }

            FanTelemetry fan;
            fan.id = QString::fromStdString(fanId);
            fan.hwmonName = hwmonName;

            const auto labelOpt = sysfs::readFirstLine(
                dir / ("fan" + std::to_string(idx) + "_label"));
            fan.label = labelOpt && !labelOpt->empty()
                ? QString::fromStdString(*labelOpt)
                : QStringLiteral("%1 Fan %2").arg(hwmonName).arg(idx);

            fan.rpm = rpm;
            fan.minObservedRpm = it->second.first;
            fan.maxObservedRpm = it->second.second;

            if (const auto pwmOpt = sysfs::readUint64(dir / ("pwm" + std::to_string(idx)))) {
                fan.pwmPercent = std::clamp(static_cast<double>(*pwmOpt) * 100.0 / 255.0, 0.0, 100.0);
            }
            if (const auto modeOpt = sysfs::readUint64(
                    dir / ("pwm" + std::to_string(idx) + "_enable"))) {
                fan.pwmControlMode = pwmModeString(*modeOpt);
            } else {
                fan.pwmControlMode = QStringLiteral("—");
            }

            pendingFans.push_back(PendingFan{dirKey, fan});
        }
    }

    const ThermalSensor* fallbackSensor = nullptr;
    for (const auto& t : result.thermals) {
        if ((t.hwmonName == QStringLiteral("coretemp") &&
             t.label.contains(QStringLiteral("Package"), Qt::CaseInsensitive)) ||
            ((t.hwmonName == QStringLiteral("k10temp") ||
              t.hwmonName == QStringLiteral("zenpower")) &&
             (t.label == QStringLiteral("Tctl") || t.label == QStringLiteral("Tdie")))) {
            fallbackSensor = &t;
            break;
        }
    }
    if (!fallbackSensor) {
        for (const auto& t : result.thermals) {
            if (t.hwmonName == QStringLiteral("acpitz")) {
                fallbackSensor = &t;
                break;
            }
        }
    }
    if (!fallbackSensor && !result.thermals.isEmpty()) {
        fallbackSensor = &result.thermals.front();
    }

    for (auto& pf : pendingFans) {
        const ThermalSensor* matched = nullptr;
        for (const auto& it : internalThermals) {
            if (it.hwmonDir == pf.hwmonDir) {
                matched = &it.sensor;
                break;
            }
        }
        if (!matched) {
            matched = fallbackSensor;
        }
        if (matched) {
            pf.fan.pairedTempLabel = QStringLiteral("%1 (%2)").arg(matched->hwmonName, matched->label);
            pf.fan.pairedTempC = matched->tempC;
        }
        result.fans.push_back(std::move(pf.fan));
    }

    return result;
}

double HwmonCollector::findCpuPackageTemp(const QVector<ThermalSensor>& thermals) {
    for (const auto& t : thermals) {
        if (t.hwmonName == QStringLiteral("coretemp") &&
            t.label.contains(QStringLiteral("Package"), Qt::CaseInsensitive)) {
            return t.tempC;
        }
        if ((t.hwmonName == QStringLiteral("k10temp") ||
             t.hwmonName == QStringLiteral("zenpower")) &&
            (t.label == QStringLiteral("Tctl") || t.label == QStringLiteral("Tdie"))) {
            return t.tempC;
        }
    }
    for (const auto& t : thermals) {
        if (t.hwmonName == QStringLiteral("coretemp") ||
            t.hwmonName == QStringLiteral("k10temp")) {
            return t.tempC;
        }
    }
    return -100.0;
}

} // namespace sysmon
