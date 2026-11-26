#include "core/GpuCollector.h"

#include "core/FormatUtils.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <system_error>
#include <utility>

namespace sysmon {

namespace {

bool parseDrmCardNumber(const std::string& name, int& outNum) {
    if (name.size() <= 4 || !name.starts_with("card")) {
        return false;
    }
    const std::string_view digits(name.data() + 4, name.size() - 4);
    if (!std::all_of(digits.begin(), digits.end(), [](unsigned char c) {
            return std::isdigit(c) != 0;
        })) {
        return false;
    }
    outNum = std::stoi(std::string(digits));
    return true;
}

QString normalizeHex4(const std::string& raw) {
    QString s = QString::fromStdString(raw).trimmed().toLower();
    if (s.startsWith(QStringLiteral("0x"))) {
        s = s.mid(2);
    }
    while (s.size() < 4) {
        s.prepend(QLatin1Char('0'));
    }
    return s;
}

QString readPciSlotFromUevent(const std::filesystem::path& ueventPath) {
    std::ifstream file(ueventPath);
    std::string line;
    while (std::getline(file, line)) {
        if (line.starts_with("PCI_SLOT_NAME=")) {
            return QString::fromStdString(sysfs::trim(line.substr(14)));
        }
    }
    return {};
}

QString fallbackVendorName(const QString& vendorHex) {
    if (vendorHex == QStringLiteral("8086")) {
        return QStringLiteral("Intel Corporation");
    }
    if (vendorHex == QStringLiteral("10de")) {
        return QStringLiteral("NVIDIA Corporation");
    }
    if (vendorHex == QStringLiteral("1002")) {
        return QStringLiteral("AMD / ATI");
    }
    return vendorHex;
}

} // namespace

GpuCollector::GpuCollector(NvmlBridge& nvml)
    : m_nvml(nvml) {}

void GpuCollector::ensureDiscovered() {
    if (m_discovered) {
        return;
    }
    m_discovered = true;

    std::error_code ec;
    const std::filesystem::path drmRoot("/sys/class/drm");
    if (!std::filesystem::exists(drmRoot, ec)) {
        return;
    }

    std::error_code iterEc;
    for (std::filesystem::directory_iterator it(drmRoot, iterEc), end;
         !iterEc && it != end;
         it.increment(iterEc)) {
        const auto& entry = *it;
        const std::string fname = entry.path().filename().string();
        int cardNum = -1;
        if (!parseDrmCardNumber(fname, cardNum)) {
            continue;
        }

        const std::filesystem::path devDir = entry.path() / "device";
        std::error_code entryEc;
        if (!std::filesystem::exists(devDir, entryEc) || entryEc) {
            continue;
        }

        CardStaticInfo info;
        info.cardNum = cardNum;
        info.id = QString::fromStdString(fname);
        info.sysPath = QString::fromStdString(entry.path().string());

        if (const auto v = sysfs::readFirstLine(devDir / "vendor")) {
            info.vendorHex = normalizeHex4(*v);
        }
        if (const auto d = sysfs::readFirstLine(devDir / "device")) {
            info.deviceHex = normalizeHex4(*d);
        }

        entryEc.clear();
        const std::filesystem::path drvLink = std::filesystem::read_symlink(devDir / "driver", entryEc);
        if (!entryEc && !drvLink.empty()) {
            info.driver = QString::fromStdString(drvLink.filename().string());
        } else {
            info.driver = QStringLiteral("unknown");
        }

        info.pciSlot = readPciSlotFromUevent(devDir / "uevent");
        info.vendorName = fallbackVendorName(info.vendorHex);

        m_cards.push_back(std::move(info));
    }

    std::sort(m_cards.begin(), m_cards.end(), [](const CardStaticInfo& a, const CardStaticInfo& b) {
        return a.cardNum < b.cardNum;
    });

    resolvePciNames();
}

void GpuCollector::resolvePciNames() {
    if (m_cards.isEmpty()) {
        return;
    }

    std::ifstream pciIds("/usr/share/hwdata/pci.ids");
    if (!pciIds.is_open()) {
        return;
    }

    std::map<std::string, std::string> vendorNames;
    std::map<std::pair<std::string, std::string>, std::string> deviceNames;
    std::map<std::string, bool> wantedVendors;
    std::map<std::pair<std::string, std::string>, bool> wantedDevices;

    for (const auto& c : m_cards) {
        if (!c.vendorHex.isEmpty()) {
            wantedVendors[c.vendorHex.toStdString()] = true;
            if (!c.deviceHex.isEmpty()) {
                wantedDevices[{c.vendorHex.toStdString(), c.deviceHex.toStdString()}] = true;
            }
        }
    }

    std::string currentVendor;
    bool vendorInteresting = false;
    std::string line;

    while (std::getline(pciIds, line)) {
        if (line.empty() || line[0] == '#') {
            continue;
        }
        if (line[0] != '\t') {
            if (line.size() >= 6 && std::isxdigit(static_cast<unsigned char>(line[0]))) {
                currentVendor = line.substr(0, 4);
                std::transform(currentVendor.begin(), currentVendor.end(), currentVendor.begin(),
                               [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
                vendorInteresting = wantedVendors.contains(currentVendor);
                if (vendorInteresting) {
                    vendorNames[currentVendor] = sysfs::trim(std::string_view(line).substr(4));
                }
            } else {
                vendorInteresting = false;
            }
        } else if (vendorInteresting && line.size() >= 7 && line[1] != '\t' &&
                   std::isxdigit(static_cast<unsigned char>(line[1]))) {
            std::string devId = line.substr(1, 4);
            std::transform(devId.begin(), devId.end(), devId.begin(),
                           [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
            const auto key = std::make_pair(currentVendor, devId);
            if (wantedDevices.contains(key)) {
                deviceNames[key] = sysfs::trim(std::string_view(line).substr(5));
            }
        }
    }

    for (auto& c : m_cards) {
        const std::string v = c.vendorHex.toStdString();
        const std::string d = c.deviceHex.toStdString();
        if (const auto vit = vendorNames.find(v); vit != vendorNames.end()) {
            c.vendorName = QString::fromStdString(vit->second);
        }
        if (const auto dit = deviceNames.find({v, d}); dit != deviceNames.end()) {
            c.deviceName = QString::fromStdString(dit->second);
        }
    }
}

QVector<GpuTelemetry> GpuCollector::collect(double elapsedSec, bool probeSleepingGpus) {
    ensureDiscovered();

    QVector<GpuTelemetry> result;
    result.reserve(m_cards.size());

    const double safeDt = std::max(elapsedSec, 0.001);
    std::error_code ec;

    for (const auto& card : m_cards) {
        GpuTelemetry gpu;
        gpu.id = card.id;
        gpu.vendor = card.vendorName;
        gpu.driver = card.driver;
        gpu.pciSlot = card.pciSlot;
        gpu.vendorHex = card.vendorHex;
        gpu.deviceHex = card.deviceHex;
        gpu.name = !card.deviceName.isEmpty()
            ? card.deviceName
            : QStringLiteral("%1 (%2)").arg(card.vendorName, card.id);

        const std::filesystem::path cardDir(card.sysPath.toStdString());
        const std::filesystem::path devDir = cardDir / "device";

        if (const auto psOpt = sysfs::readFirstLine(devDir / "power_state")) {
            gpu.powerState = QString::fromStdString(*psOpt);
        } else {
            gpu.powerState = QStringLiteral("unknown");
        }

        if (card.driver == QStringLiteral("nvidia")) {
            const bool isD3Cold = (gpu.powerState == QStringLiteral("D3cold"));
            if (isD3Cold && !probeSleepingGpus) {
                gpu.sleeping = true;
                gpu.utilizationPercent = 0.0;
            } else {
                NvmlDeviceStats stats;
                if (m_nvml.queryDeviceByPciBusId(card.pciSlot, stats)) {
                    if (!stats.name.isEmpty()) {
                        gpu.name = stats.name;
                    }
                    gpu.utilizationPercent = stats.gpuUtilPercent;
                    gpu.memoryControllerPercent = stats.memUtilPercent;
                    gpu.vramUsedBytes = stats.vramUsedBytes;
                    gpu.vramTotalBytes = stats.vramTotalBytes;
                    gpu.coreClockMHz = stats.coreClockMHz;
                    gpu.memClockMHz = stats.memClockMHz;
                    gpu.tempC = stats.tempC;
                    gpu.powerWatts = stats.powerWatts;
                    gpu.fanPercent = stats.fanPercent;
                    gpu.activeProcessCount = stats.activeProcessCount;
                }
            }
        } else if (card.driver == QStringLiteral("amdgpu")) {
            if (const auto busy = sysfs::readUint64(devDir / "gpu_busy_percent")) {
                gpu.utilizationPercent = std::clamp(static_cast<double>(*busy), 0.0, 100.0);
            }
            if (const auto used = sysfs::readUint64(devDir / "mem_info_vram_used")) {
                gpu.vramUsedBytes = *used;
            }
            if (const auto total = sysfs::readUint64(devDir / "mem_info_vram_total")) {
                gpu.vramTotalBytes = *total;
            }
            const std::filesystem::path hwRoot = devDir / "hwmon";
            ec.clear();
            if (std::filesystem::exists(hwRoot, ec) && !ec) {
                std::error_code iterEc;
                for (std::filesystem::directory_iterator it(hwRoot, iterEc), end;
                     !iterEc && it != end;
                     it.increment(iterEc)) {
                    if (const auto t = sysfs::readInt64(it->path() / "temp1_input")) {
                        gpu.tempC = static_cast<double>(*t) / 1000.0;
                    }
                    if (const auto p = sysfs::readUint64(it->path() / "power1_average")) {
                        gpu.powerWatts = static_cast<double>(*p) / 1000000.0;
                    }
                    if (const auto pwm = sysfs::readUint64(it->path() / "pwm1")) {
                        gpu.fanPercent = std::clamp(static_cast<double>(*pwm) * 100.0 / 255.0,
                                                    0.0,
                                                    100.0);
                    }
                    break;
                }
            }
        } else if (card.driver == QStringLiteral("i915") || card.driver == QStringLiteral("xe")) {
            std::filesystem::path rc6Path = cardDir / "gt/gt0/rc6_residency_ms";
            if (!std::filesystem::exists(rc6Path, ec)) {
                rc6Path = cardDir / "power/rc6_residency_ms";
            }

            const std::string key = card.id.toStdString();
            if (const auto rc6Opt = sysfs::readUint64(rc6Path)) {
                const auto prevIt = m_prevIntelRc6Ms.find(key);
                if (prevIt != m_prevIntelRc6Ms.end()) {
                    const std::uint64_t dRc6 = (*rc6Opt >= prevIt->second)
                        ? (*rc6Opt - prevIt->second)
                        : 0;
                    if (gpu.powerState != QStringLiteral("D0") && dRc6 == 0) {
                        gpu.sleeping = true;
                        gpu.utilizationPercent = 0.0;
                    } else {
                        const double wallMs = safeDt * 1000.0;
                        gpu.utilizationPercent = std::clamp(
                            100.0 * (1.0 - static_cast<double>(dRc6) / wallMs),
                            0.0,
                            100.0);
                    }
                } else {
                    gpu.utilizationPercent = 0.0;
                    gpu.sleeping = (gpu.powerState != QStringLiteral("D0"));
                }
                m_prevIntelRc6Ms[key] = *rc6Opt;
            }

            if (const auto actFreq = sysfs::readUint64(cardDir / "gt_act_freq_mhz")) {
                gpu.coreClockMHz = static_cast<double>(*actFreq);
            } else if (const auto rpsAct = sysfs::readUint64(cardDir / "gt/gt0/rps_act_freq_mhz")) {
                gpu.coreClockMHz = static_cast<double>(*rpsAct);
            }

            if (const auto maxFreq = sysfs::readUint64(cardDir / "gt_RP0_freq_mhz")) {
                gpu.maxClockMHz = static_cast<double>(*maxFreq);
            } else if (const auto rpsMax = sysfs::readUint64(cardDir / "gt/gt0/rps_RP0_freq_mhz")) {
                gpu.maxClockMHz = static_cast<double>(*rpsMax);
            }
        }

        result.push_back(std::move(gpu));
    }

    return result;
}

} // namespace sysmon
