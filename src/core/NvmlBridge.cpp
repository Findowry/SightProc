#include "core/NvmlBridge.h"

#include "core/FormatUtils.h"

#include <dlfcn.h>

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <system_error>
#include <vector>

namespace sysmon {

namespace {

constexpr int kNvmlSuccess = 0;
constexpr int kNvmlErrorInsufficientSize = 7;
constexpr unsigned int kNvmlTempGpu = 0;
constexpr unsigned int kNvmlClockGraphics = 0;
constexpr unsigned int kNvmlClockMem = 2;

template <typename Fn>
bool loadSymbol(void* handle, const char* name, Fn& outFn) {
    void* sym = dlsym(handle, name);
    if (!sym) {
        outFn = nullptr;
        return false;
    }
    std::memcpy(&outFn, &sym, sizeof(outFn));
    return true;
}

template <typename Fn>
bool loadVersionedSymbol(void* handle,
                           const char* primaryName,
                           const char* fallbackName,
                           Fn& outFn) {
    if (loadSymbol(handle, primaryName, outFn)) {
        return true;
    }
    return fallbackName && loadSymbol(handle, fallbackName, outFn);
}

template <typename ProcStruct, typename QueryFn>
void collectRunningProcMemory(void* dev,
                              QueryFn fn,
                              std::unordered_map<std::uint32_t, std::uint64_t>& outMem) {
    if (!fn || !dev) {
        return;
    }
    unsigned int count = 64;
    std::vector<ProcStruct> procs(count);
    int ret = fn(dev, &count, procs.data());
    if (ret == kNvmlErrorInsufficientSize && count > 0 && count <= 4096) {
        const unsigned int allocCount = std::min<unsigned int>(count + 16U, 4096U);
        procs.resize(allocCount);
        count = allocCount;
        ret = fn(dev, &count, procs.data());
    }
    if (ret == kNvmlSuccess) {
        for (unsigned int i = 0; i < count && i < procs.size(); ++i) {
            const std::uint32_t pid = static_cast<std::uint32_t>(procs[i].pid);
            const std::uint64_t rawMem = static_cast<std::uint64_t>(procs[i].usedGpuMemory);
            const std::uint64_t bytes = (rawMem == ~0ULL) ? 0ULL : rawMem;
            auto it = outMem.find(pid);
            if (it == outMem.end()) {
                outMem.emplace(pid, bytes);
            } else {
                it->second = std::max(it->second, bytes);
            }
        }
    }
}

bool hasAwakeNvidiaCard(bool probeSleepingGpus) {
    std::error_code ec;
    const std::filesystem::path drmRoot("/sys/class/drm");
    if (!std::filesystem::exists(drmRoot, ec)) {
        return false;
    }
    bool foundNvidia = false;
    std::error_code iterEc;
    std::filesystem::directory_iterator it(drmRoot, iterEc);
    const std::filesystem::directory_iterator end;
    for (; !iterEc && it != end; it.increment(iterEc)) {
        const auto& entry = *it;
        const std::string name = entry.path().filename().string();
        if (!name.starts_with("card") || name.find('-') != std::string::npos) {
            continue;
        }
        std::error_code entryEc;
        const std::filesystem::path drvLink =
            std::filesystem::read_symlink(entry.path() / "device/driver", entryEc);
        if (entryEc || drvLink.filename().string() != "nvidia") {
            continue;
        }
        foundNvidia = true;
        if (probeSleepingGpus) {
            return true;
        }
        const auto ps = sysfs::readFirstLine(entry.path() / "device/power_state");
        if (!ps || *ps != "D3cold") {
            return true;
        }
    }
    return foundNvidia && probeSleepingGpus;
}

} // namespace

NvmlBridge::~NvmlBridge() {
    shutdown();
}

bool NvmlBridge::ensureInitialized() {
    std::lock_guard<std::mutex> lock(m_mutex);
    return ensureInitializedLocked();
}

bool NvmlBridge::isAvailable() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_initialized;
}

bool NvmlBridge::ensureInitializedLocked() {
    if (m_initialized) {
        return true;
    }
    if (m_attemptedInit) {
        return false;
    }
    m_attemptedInit = true;

    m_handle = dlopen("libnvidia-ml.so.1", RTLD_LAZY | RTLD_LOCAL);
    if (!m_handle) {
        m_handle = dlopen("libnvidia-ml.so", RTLD_LAZY | RTLD_LOCAL);
    }
    if (!m_handle) {
        return false;
    }

    const bool hasInit = loadVersionedSymbol(m_handle, "nvmlInit_v2", "nvmlInit", m_init);
    loadSymbol(m_handle, "nvmlShutdown", m_shutdown);
    loadVersionedSymbol(m_handle, "nvmlDeviceGetCount_v2", "nvmlDeviceGetCount", m_getCount);
    loadVersionedSymbol(m_handle,
                        "nvmlDeviceGetHandleByIndex_v2",
                        "nvmlDeviceGetHandleByIndex",
                        m_getHandleByIndex);
    loadVersionedSymbol(m_handle,
                        "nvmlDeviceGetHandleByPciBusId_v2",
                        "nvmlDeviceGetHandleByPciBusId",
                        m_getHandleByPciBusId);
    loadSymbol(m_handle, "nvmlDeviceGetName", m_getName);
    loadSymbol(m_handle, "nvmlDeviceGetUtilizationRates", m_getUtilizationRates);
    loadSymbol(m_handle, "nvmlDeviceGetMemoryInfo_v2", m_getMemoryInfoV2);
    loadSymbol(m_handle, "nvmlDeviceGetMemoryInfo", m_getMemoryInfoV1);
    loadSymbol(m_handle, "nvmlDeviceGetTemperature", m_getTemperature);
    loadSymbol(m_handle, "nvmlDeviceGetClockInfo", m_getClockInfo);
    loadSymbol(m_handle, "nvmlDeviceGetPowerUsage", m_getPowerUsage);
    loadSymbol(m_handle, "nvmlDeviceGetFanSpeed_v2", m_getFanSpeedV2);
    loadSymbol(m_handle, "nvmlDeviceGetFanSpeed", m_getFanSpeedV1);

    if (!loadVersionedSymbol(m_handle,
                             "nvmlDeviceGetComputeRunningProcesses_v3",
                             "nvmlDeviceGetComputeRunningProcesses_v2",
                             m_getComputeProcsV2)) {
        loadSymbol(m_handle, "nvmlDeviceGetComputeRunningProcesses", m_getComputeProcsV1);
    }
    if (!loadVersionedSymbol(m_handle,
                             "nvmlDeviceGetGraphicsRunningProcesses_v3",
                             "nvmlDeviceGetGraphicsRunningProcesses_v2",
                             m_getGraphicsProcsV2)) {
        loadSymbol(m_handle, "nvmlDeviceGetGraphicsRunningProcesses", m_getGraphicsProcsV1);
    }
    loadSymbol(m_handle, "nvmlDeviceGetProcessUtilization", m_getProcessUtilization);

    if (!hasInit || m_init() != kNvmlSuccess) {
        shutdownLocked();
        m_attemptedInit = true;
        return false;
    }

    m_initialized = true;
    return true;
}

bool NvmlBridge::queryDeviceByPciBusId(const QString& pciSlot, NvmlDeviceStats& out) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!ensureInitializedLocked()) {
        return false;
    }

    NvmlDevice dev = nullptr;
    if (m_getHandleByPciBusId && !pciSlot.isEmpty()) {
        const QByteArray slotBytes = pciSlot.toLatin1();
        if (m_getHandleByPciBusId(slotBytes.constData(), &dev) != kNvmlSuccess) {
            dev = nullptr;
        }
    }

    if (!dev && m_getCount && m_getHandleByIndex) {
        unsigned int count = 0;
        if (m_getCount(&count) == kNvmlSuccess && count == 1) {
            if (m_getHandleByIndex(0, &dev) != kNvmlSuccess) {
                dev = nullptr;
            }
        }
    }

    if (!dev) {
        return false;
    }

    if (m_getName) {
        char nameBuf[96] = {};
        if (m_getName(dev, nameBuf, sizeof(nameBuf)) == kNvmlSuccess) {
            nameBuf[sizeof(nameBuf) - 1] = '\0';
            out.name = QString::fromUtf8(nameBuf);
        }
    }

    if (m_getUtilizationRates) {
        NvmlUtilization util{};
        if (m_getUtilizationRates(dev, &util) == kNvmlSuccess) {
            out.gpuUtilPercent = static_cast<double>(util.gpu);
            out.memUtilPercent = static_cast<double>(util.memory);
        }
    }

    bool gotMemory = false;
    if (m_getMemoryInfoV2) {
        NvmlMemoryV2 memV2{};
        memV2.version = kNvmlMemoryV2Version;
        if (m_getMemoryInfoV2(dev, &memV2) == kNvmlSuccess) {
            out.vramUsedBytes = static_cast<std::uint64_t>(memV2.used);
            out.vramTotalBytes = static_cast<std::uint64_t>(memV2.total);
            gotMemory = true;
        }
    }
    if (!gotMemory && m_getMemoryInfoV1) {
        NvmlMemoryV1 memV1{};
        if (m_getMemoryInfoV1(dev, &memV1) == kNvmlSuccess) {
            out.vramUsedBytes = static_cast<std::uint64_t>(memV1.used);
            out.vramTotalBytes = static_cast<std::uint64_t>(memV1.total);
        }
    }

    if (m_getTemperature) {
        unsigned int temp = 0;
        if (m_getTemperature(dev, kNvmlTempGpu, &temp) == kNvmlSuccess) {
            out.tempC = static_cast<double>(temp);
        }
    }

    if (m_getClockInfo) {
        unsigned int clk = 0;
        if (m_getClockInfo(dev, kNvmlClockGraphics, &clk) == kNvmlSuccess) {
            out.coreClockMHz = static_cast<double>(clk);
        }
        if (m_getClockInfo(dev, kNvmlClockMem, &clk) == kNvmlSuccess) {
            out.memClockMHz = static_cast<double>(clk);
        }
    }

    if (m_getPowerUsage) {
        unsigned int mw = 0;
        if (m_getPowerUsage(dev, &mw) == kNvmlSuccess) {
            out.powerWatts = static_cast<double>(mw) / 1000.0;
        }
    }

    bool gotFan = false;
    if (m_getFanSpeedV2) {
        unsigned int fanPct = 0;
        if (m_getFanSpeedV2(dev, 0U, &fanPct) == kNvmlSuccess) {
            out.fanPercent = static_cast<double>(fanPct);
            gotFan = true;
        }
    }
    if (!gotFan && m_getFanSpeedV1) {
        unsigned int fanPct = 0;
        if (m_getFanSpeedV1(dev, &fanPct) == kNvmlSuccess) {
            out.fanPercent = static_cast<double>(fanPct);
        }
    }

    std::unordered_map<std::uint32_t, std::uint64_t> procMem;
    if (m_getComputeProcsV2) {
        collectRunningProcMemory<NvmlProcessInfoV2>(dev, m_getComputeProcsV2, procMem);
    } else if (m_getComputeProcsV1) {
        collectRunningProcMemory<NvmlProcessInfoV1>(dev, m_getComputeProcsV1, procMem);
    }

    if (m_getGraphicsProcsV2) {
        collectRunningProcMemory<NvmlProcessInfoV2>(dev, m_getGraphicsProcsV2, procMem);
    } else if (m_getGraphicsProcsV1) {
        collectRunningProcMemory<NvmlProcessInfoV1>(dev, m_getGraphicsProcsV1, procMem);
    }

    out.activeProcessCount = static_cast<int>(procMem.size());
    return true;
}

std::unordered_map<std::uint32_t, NvmlProcessMetrics>
NvmlBridge::queryAllProcessMetrics(bool probeSleepingGpus) {
    std::unordered_map<std::uint32_t, NvmlProcessMetrics> out;
    if (!hasAwakeNvidiaCard(probeSleepingGpus)) {
        return out;
    }
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!ensureInitializedLocked() || !m_getCount || !m_getHandleByIndex) {
        return out;
    }

    unsigned int count = 0;
    if (m_getCount(&count) != kNvmlSuccess || count == 0) {
        return out;
    }

    for (unsigned int devIdx = 0; devIdx < count; ++devIdx) {
        NvmlDevice dev = nullptr;
        if (m_getHandleByIndex(devIdx, &dev) != kNvmlSuccess || !dev) {
            continue;
        }

        std::unordered_map<std::uint32_t, std::uint64_t> procMem;
        if (m_getComputeProcsV2) {
            collectRunningProcMemory<NvmlProcessInfoV2>(dev, m_getComputeProcsV2, procMem);
        } else if (m_getComputeProcsV1) {
            collectRunningProcMemory<NvmlProcessInfoV1>(dev, m_getComputeProcsV1, procMem);
        }
        if (m_getGraphicsProcsV2) {
            collectRunningProcMemory<NvmlProcessInfoV2>(dev, m_getGraphicsProcsV2, procMem);
        } else if (m_getGraphicsProcsV1) {
            collectRunningProcMemory<NvmlProcessInfoV1>(dev, m_getGraphicsProcsV1, procMem);
        }

        for (const auto& [pid, bytes] : procMem) {
            auto& entry = out[pid];
            entry.pid = pid;
            entry.vramBytes += bytes;
        }

        if (m_getProcessUtilization) {
            unsigned int sampleCount = 64;
            std::vector<NvmlProcessUtilizationSample> samples(sampleCount);
            const unsigned long long lastTs = m_lastUtilTsUs[devIdx];
            int ret = m_getProcessUtilization(dev, samples.data(), &sampleCount, lastTs);
            if (ret == kNvmlErrorInsufficientSize && sampleCount > 0 && sampleCount <= 4096) {
                const unsigned int allocCount = std::min<unsigned int>(sampleCount + 16U, 4096U);
                samples.resize(allocCount);
                sampleCount = allocCount;
                ret = m_getProcessUtilization(dev, samples.data(), &sampleCount, lastTs);
            }
            if (ret == kNvmlSuccess) {
                unsigned long long maxTs = lastTs;
                for (unsigned int i = 0; i < sampleCount && i < samples.size(); ++i) {
                    const auto& s = samples[i];
                    if (s.timeStamp > maxTs) {
                        maxTs = s.timeStamp;
                    }
                    if (s.pid == 0 || s.smUtil > 100U) {
                        continue;
                    }
                    auto& entry = out[static_cast<std::uint32_t>(s.pid)];
                    entry.pid = static_cast<std::uint32_t>(s.pid);
                    entry.smUtilPercent = std::max(
                        entry.smUtilPercent, static_cast<double>(s.smUtil));
                }
                m_lastUtilTsUs[devIdx] = maxTs;
            }
        }
    }

    return out;
}

void NvmlBridge::shutdown() {
    std::lock_guard<std::mutex> lock(m_mutex);
    shutdownLocked();
}

void NvmlBridge::shutdownLocked() {
    if (m_initialized && m_shutdown) {
        m_shutdown();
    }
    m_initialized = false;
    m_attemptedInit = false;
    m_init = nullptr;
    m_shutdown = nullptr;
    m_getCount = nullptr;
    m_getHandleByIndex = nullptr;
    m_getHandleByPciBusId = nullptr;
    m_getName = nullptr;
    m_getUtilizationRates = nullptr;
    m_getMemoryInfoV2 = nullptr;
    m_getMemoryInfoV1 = nullptr;
    m_getTemperature = nullptr;
    m_getClockInfo = nullptr;
    m_getPowerUsage = nullptr;
    m_getFanSpeedV2 = nullptr;
    m_getFanSpeedV1 = nullptr;
    m_getComputeProcsV2 = nullptr;
    m_getGraphicsProcsV2 = nullptr;
    m_getComputeProcsV1 = nullptr;
    m_getGraphicsProcsV1 = nullptr;
    m_getProcessUtilization = nullptr;
    m_lastUtilTsUs.clear();
    if (m_handle) {
        dlclose(m_handle);
        m_handle = nullptr;
    }
}

} // namespace sysmon
