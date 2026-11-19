#include "core/CpuCollector.h"

#include "core/FormatUtils.h"

#include <sys/utsname.h>

#include <algorithm>
#include <cctype>
#include <charconv>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <system_error>
#include <utility>

namespace sysmon {

namespace {

std::uint64_t parseCacheSizeBytes(const std::string& raw) {
    if (raw.empty()) {
        return 0;
    }
    std::uint64_t multiplier = 1;
    std::string_view numView(raw);
    const char last = static_cast<char>(std::toupper(static_cast<unsigned char>(numView.back())));
    if (last == 'K') {
        multiplier = 1024ULL;
        numView.remove_suffix(1);
    } else if (last == 'M') {
        multiplier = 1024ULL * 1024ULL;
        numView.remove_suffix(1);
    } else if (last == 'G') {
        multiplier = 1024ULL * 1024ULL * 1024ULL;
        numView.remove_suffix(1);
    }
    std::uint64_t value = 0;
    const auto [ptr, ec] = std::from_chars(numView.data(), numView.data() + numView.size(), value);
    if (ec != std::errc{} || ptr != numView.data() + numView.size()) {
        return 0;
    }
    return value * multiplier;
}

bool parseCoreIndexFromDir(const std::string& name, int& outId) {
    if (name.size() <= 3 || !name.starts_with("cpu")) {
        return false;
    }
    const std::string_view digits(name.data() + 3, name.size() - 3);
    if (!std::all_of(digits.begin(), digits.end(), [](unsigned char c) {
            return std::isdigit(c) != 0;
        })) {
        return false;
    }
    outId = std::stoi(std::string(digits));
    return true;
}

} // namespace

CpuCollector::CpuCollector() {
    initStaticMetadata();
}

void CpuCollector::initStaticMetadata() {
    utsname uts{};
    if (uname(&uts) == 0) {
        m_architecture = QString::fromUtf8(uts.machine);
    }

    std::ifstream cpuinfo("/proc/cpuinfo");
    std::set<std::pair<int, int>> distinctPhysicalCores;
    int currentPhysId = 0;
    int currentCoreId = -1;
    int logicalCount = 0;

    std::string line;
    while (std::getline(cpuinfo, line)) {
        if (line.empty()) {
            if (currentCoreId >= 0) {
                distinctPhysicalCores.emplace(currentPhysId, currentCoreId);
            }
            currentPhysId = 0;
            currentCoreId = -1;
            continue;
        }
        const auto colonPos = line.find(':');
        if (colonPos == std::string::npos) {
            continue;
        }
        const std::string key = sysfs::trim(std::string_view(line).substr(0, colonPos));
        const std::string val = sysfs::trim(std::string_view(line).substr(colonPos + 1));

        if (key == "processor") {
            ++logicalCount;
        } else if (key == "model name" && m_modelName.isEmpty()) {
            m_modelName = QString::fromStdString(val);
        } else if (key == "vendor_id" && m_vendor.isEmpty()) {
            m_vendor = QString::fromStdString(val);
        } else if (key == "physical id") {
            std::from_chars(val.data(), val.data() + val.size(), currentPhysId);
        } else if (key == "core id") {
            std::from_chars(val.data(), val.data() + val.size(), currentCoreId);
        } else if (key == "microcode" && m_microcode.isEmpty()) {
            m_microcode = QString::fromStdString(val);
        } else if (key == "bogomips" && m_bogoMips <= 0.0) {
            std::istringstream biss(val);
            biss >> m_bogoMips;
        } else if (key == "flags" && m_virtualization.isEmpty()) {
            std::istringstream fiss(val);
            std::string flag;
            while (fiss >> flag) {
                if (flag == "vmx") {
                    m_virtualization = QStringLiteral("Intel VT-x");
                    break;
                }
                if (flag == "svm") {
                    m_virtualization = QStringLiteral("AMD-V");
                    break;
                }
            }
        }
    }
    if (currentCoreId >= 0) {
        distinctPhysicalCores.emplace(currentPhysId, currentCoreId);
    }

    m_logicalCores = logicalCount;
    m_physicalCores = distinctPhysicalCores.empty()
        ? logicalCount
        : static_cast<int>(distinctPhysicalCores.size());

    std::error_code ec;
    const std::filesystem::path cpuSysRoot("/sys/devices/system/cpu");
    std::set<std::string> seenCaches;

    if (std::filesystem::exists(cpuSysRoot, ec)) {
        for (const auto& cpuEntry : std::filesystem::directory_iterator(cpuSysRoot, ec)) {
            int cpuId = -1;
            if (!parseCoreIndexFromDir(cpuEntry.path().filename().string(), cpuId)) {
                continue;
            }

            const std::filesystem::path cpufreqDir = cpuEntry.path() / "cpufreq";
            if (const auto baseOpt = sysfs::readUint64(cpufreqDir / "base_frequency")) {
                m_baseFreqMHz = std::max(m_baseFreqMHz, static_cast<double>(*baseOpt) / 1000.0);
            }
            if (const auto maxOpt = sysfs::readUint64(cpufreqDir / "cpuinfo_max_freq")) {
                m_boostFreqMHz = std::max(m_boostFreqMHz, static_cast<double>(*maxOpt) / 1000.0);
            }
            if (m_scalingDriver.isEmpty()) {
                if (const auto drv = sysfs::readFirstLine(cpufreqDir / "scaling_driver")) {
                    m_scalingDriver = QString::fromStdString(*drv);
                }
            }
            if (m_energyPreference.isEmpty()) {
                if (const auto epp = sysfs::readFirstLine(
                        cpufreqDir / "energy_performance_preference")) {
                    m_energyPreference = QString::fromStdString(*epp);
                }
            }

            const std::filesystem::path cacheRoot = cpuEntry.path() / "cache";
            if (!std::filesystem::exists(cacheRoot, ec)) {
                continue;
            }
            for (const auto& idxEntry : std::filesystem::directory_iterator(cacheRoot, ec)) {
                if (!idxEntry.path().filename().string().starts_with("index")) {
                    continue;
                }
                const auto levelOpt = sysfs::readUint64(idxEntry.path() / "level");
                const auto typeOpt = sysfs::readFirstLine(idxEntry.path() / "type");
                const auto sizeOpt = sysfs::readFirstLine(idxEntry.path() / "size");
                const auto sharedOpt = sysfs::readFirstLine(idxEntry.path() / "shared_cpu_list");
                if (!levelOpt || !typeOpt || !sizeOpt || !sharedOpt) {
                    continue;
                }

                // Hibrit P/E çekirdeklerde aynı paylaşımlı önbelleği çift saymayı önle.
                const std::string dedupKey = std::to_string(*levelOpt) + "|" + *typeOpt + "|" + *sharedOpt;
                if (!seenCaches.insert(dedupKey).second) {
                    continue;
                }

                const std::uint64_t bytes = parseCacheSizeBytes(*sizeOpt);
                if (*levelOpt == 1 && *typeOpt == "Data") {
                    m_l1dBytes += bytes;
                } else if (*levelOpt == 1 && *typeOpt == "Instruction") {
                    m_l1iBytes += bytes;
                } else if (*levelOpt == 2) {
                    m_l2Bytes += bytes;
                } else if (*levelOpt == 3) {
                    m_l3Bytes += bytes;
                }
            }
        }
    }
}

CpuTelemetry CpuCollector::collect(double elapsedSec,
                                   double packageTempC,
                                   const QVector<ThermalSensor>& thermals) {
    CpuTelemetry out;
    out.modelName = m_modelName;
    out.vendor = m_vendor;
    out.architecture = m_architecture;
    out.physicalCores = m_physicalCores;
    out.logicalCores = m_logicalCores;
    out.baseFreqMHz = m_baseFreqMHz;
    out.boostFreqMHz = m_boostFreqMHz;
    out.l1dBytes = m_l1dBytes;
    out.l1iBytes = m_l1iBytes;
    out.l2Bytes = m_l2Bytes;
    out.l3Bytes = m_l3Bytes;
    out.scalingDriver = m_scalingDriver;
    out.energyPreference = m_energyPreference;
    out.virtualization = m_virtualization;
    out.bogoMips = m_bogoMips;
    out.microcode = m_microcode;
    out.packageTempC = packageTempC;

    double minCoreT = 1000.0;
    double maxCoreT = -100.0;
    for (const auto& t : thermals) {
        if ((t.hwmonName == QStringLiteral("coretemp") &&
             t.label.startsWith(QStringLiteral("Core"), Qt::CaseInsensitive)) ||
            ((t.hwmonName == QStringLiteral("k10temp") ||
              t.hwmonName == QStringLiteral("zenpower")) &&
             t.label.startsWith(QStringLiteral("Tccd"), Qt::CaseInsensitive))) {
            minCoreT = std::min(minCoreT, t.tempC);
            maxCoreT = std::max(maxCoreT, t.tempC);
        }
    }
    if (maxCoreT > -50.0) {
        out.coreTempMinC = minCoreT;
        out.coreTempMaxC = maxCoreT;
    }

    if (const auto gov = sysfs::readFirstLine("/sys/devices/system/cpu/cpu0/cpufreq/scaling_governor")) {
        out.governor = QString::fromStdString(*gov);
    }
    if (const auto minSc = sysfs::readUint64("/sys/devices/system/cpu/cpu0/cpufreq/scaling_min_freq")) {
        out.minScalingFreqMHz = static_cast<double>(*minSc) / 1000.0;
    }
    if (const auto maxSc = sysfs::readUint64("/sys/devices/system/cpu/cpu0/cpufreq/scaling_max_freq")) {
        out.maxScalingFreqMHz = static_cast<double>(*maxSc) / 1000.0;
    }

    if (const auto loadLine = sysfs::readFirstLine("/proc/loadavg")) {
        std::istringstream liss(*loadLine);
        liss >> out.loadAvg1 >> out.loadAvg5 >> out.loadAvg15;
    }

    if (const auto upLine = sysfs::readFirstLine("/proc/uptime")) {
        std::istringstream iss(*upLine);
        double upSec = 0.0;
        if (iss >> upSec) {
            out.uptimeSeconds = upSec;
        }
    }

    std::ifstream statFile("/proc/stat");
    std::string line;
    CpuJiffies currTotal{};
    std::unordered_map<int, CpuJiffies> currPerCore;
    std::uint64_t currCtxt = 0;
    std::uint64_t currIntr = 0;

    while (std::getline(statFile, line)) {
        if (line.starts_with("cpu ")) {
            std::istringstream iss(line);
            std::string tag;
            iss >> tag >> currTotal.user >> currTotal.nice >> currTotal.system
                >> currTotal.idle >> currTotal.iowait >> currTotal.irq
                >> currTotal.softirq >> currTotal.steal;
        } else if (line.starts_with("cpu") && line.size() > 3 &&
                   std::isdigit(static_cast<unsigned char>(line[3]))) {
            std::istringstream iss(line);
            std::string tag;
            CpuJiffies j{};
            iss >> tag >> j.user >> j.nice >> j.system
                >> j.idle >> j.iowait >> j.irq >> j.softirq >> j.steal;
            int coreId = -1;
            if (parseCoreIndexFromDir(tag, coreId)) {
                currPerCore[coreId] = j;
            }
        } else if (line.starts_with("intr ")) {
            std::istringstream iss(line);
            std::string tag;
            iss >> tag >> currIntr;
        } else if (line.starts_with("ctxt ")) {
            std::istringstream iss(line);
            std::string tag;
            iss >> tag >> currCtxt;
        } else if (line.starts_with("processes ")) {
            std::istringstream iss(line);
            std::string tag;
            iss >> tag >> out.totalForks;
        } else if (line.starts_with("procs_running ")) {
            std::istringstream iss(line);
            std::string tag;
            iss >> tag >> out.processesRunning;
        } else if (line.starts_with("procs_blocked ")) {
            std::istringstream iss(line);
            std::string tag;
            iss >> tag >> out.processesBlocked;
        }
    }

    if (m_hasPrev) {
        const std::uint64_t prevAll = m_prevTotal.allTotal();
        const std::uint64_t currAll = currTotal.allTotal();
        const std::uint64_t deltaTotal = (currAll > prevAll) ? (currAll - prevAll) : 0;

        if (deltaTotal > 0) {
            const std::uint64_t dActive = (currTotal.activeTotal() > m_prevTotal.activeTotal())
                ? (currTotal.activeTotal() - m_prevTotal.activeTotal())
                : 0;
            const std::uint64_t dUser = ((currTotal.user + currTotal.nice) >
                                         (m_prevTotal.user + m_prevTotal.nice))
                ? ((currTotal.user + currTotal.nice) - (m_prevTotal.user + m_prevTotal.nice))
                : 0;
            const std::uint64_t dSys = ((currTotal.system + currTotal.irq + currTotal.softirq) >
                                        (m_prevTotal.system + m_prevTotal.irq + m_prevTotal.softirq))
                ? ((currTotal.system + currTotal.irq + currTotal.softirq) -
                   (m_prevTotal.system + m_prevTotal.irq + m_prevTotal.softirq))
                : 0;
            const std::uint64_t dIowait = (currTotal.iowait > m_prevTotal.iowait)
                ? (currTotal.iowait - m_prevTotal.iowait)
                : 0;

            const double denom = static_cast<double>(deltaTotal);
            out.totalUsagePercent = std::clamp(100.0 * static_cast<double>(dActive) / denom, 0.0, 100.0);
            out.userPercent = std::clamp(100.0 * static_cast<double>(dUser) / denom, 0.0, 100.0);
            out.systemPercent = std::clamp(100.0 * static_cast<double>(dSys) / denom, 0.0, 100.0);
            out.iowaitPercent = std::clamp(100.0 * static_cast<double>(dIowait) / denom, 0.0, 100.0);
        }

        const double safeDt = std::max(elapsedSec, 0.001);
        if (currCtxt >= m_prevCtxt) {
            out.contextSwitchesPerSec = static_cast<std::uint64_t>(
                static_cast<double>(currCtxt - m_prevCtxt) / safeDt);
        }
        if (currIntr >= m_prevIntr) {
            out.interruptsPerSec = static_cast<std::uint64_t>(
                static_cast<double>(currIntr - m_prevIntr) / safeDt);
        }
    }

    double sumFreqMHz = 0.0;
    int freqCount = 0;

    out.cores.reserve(static_cast<qsizetype>(currPerCore.size()));
    for (const auto& [coreId, currJ] : currPerCore) {
        CoreTelemetry core;
        core.id = coreId;

        if (m_hasPrev) {
            if (const auto it = m_prevPerCore.find(coreId); it != m_prevPerCore.end()) {
                const std::uint64_t prevAll = it->second.allTotal();
                const std::uint64_t currAll = currJ.allTotal();
                const std::uint64_t deltaAll = (currAll > prevAll) ? (currAll - prevAll) : 0;
                if (deltaAll > 0) {
                    const std::uint64_t dActive = (currJ.activeTotal() > it->second.activeTotal())
                        ? (currJ.activeTotal() - it->second.activeTotal())
                        : 0;
                    core.usagePercent = std::clamp(
                        100.0 * static_cast<double>(dActive) / static_cast<double>(deltaAll),
                        0.0,
                        100.0);
                }
            }
        }

        const std::filesystem::path curFreqPath =
            std::filesystem::path("/sys/devices/system/cpu") /
            ("cpu" + std::to_string(coreId)) / "cpufreq/scaling_cur_freq";
        if (const auto freqKHz = sysfs::readUint64(curFreqPath)) {
            core.curFreqMHz = static_cast<double>(*freqKHz) / 1000.0;
            sumFreqMHz += core.curFreqMHz;
            out.maxCurrentFreqMHz = std::max(out.maxCurrentFreqMHz, core.curFreqMHz);
            ++freqCount;
        }

        out.cores.push_back(core);
    }

    std::sort(out.cores.begin(), out.cores.end(), [](const CoreTelemetry& a, const CoreTelemetry& b) {
        return a.id < b.id;
    });

    if (freqCount > 0) {
        out.avgFreqMHz = sumFreqMHz / static_cast<double>(freqCount);
    }
    if (out.logicalCores == 0 && !out.cores.isEmpty()) {
        out.logicalCores = static_cast<int>(out.cores.size());
    }

    m_prevTotal = currTotal;
    m_prevPerCore = std::move(currPerCore);
    m_prevCtxt = currCtxt;
    m_prevIntr = currIntr;
    m_hasPrev = true;

    return out;
}

} // namespace sysmon
