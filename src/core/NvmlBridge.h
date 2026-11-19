#pragma once

#include <QString>

#include <cstdint>
#include <unordered_map>

namespace sysmon {

struct NvmlProcessMetrics {
    std::uint32_t pid{0};
    double smUtilPercent{0.0};
    std::uint64_t vramBytes{0};
};

struct NvmlDeviceStats {
    QString name;
    double gpuUtilPercent{-1.0};
    double memUtilPercent{-1.0};
    std::uint64_t vramUsedBytes{0};
    std::uint64_t vramTotalBytes{0};
    double coreClockMHz{-1.0};
    double memClockMHz{-1.0};
    double tempC{-100.0};
    double powerWatts{-1.0};
    double fanPercent{-1.0};
    int activeProcessCount{-1};
};

/**
 * @brief libnvidia-ml.so.1 kütüphanesini çalışma zamanında dlopen ile yükleyen
 *        güvenli NVML köprüsü (derleme zamanı CUDA/NVML bağımlılığı gerektirmez).
 */
class NvmlBridge {
public:
    NvmlBridge() = default;
    ~NvmlBridge();

    NvmlBridge(const NvmlBridge&) = delete;
    NvmlBridge& operator=(const NvmlBridge&) = delete;

    [[nodiscard]] bool ensureInitialized();
    [[nodiscard]] bool isAvailable() const noexcept { return m_initialized; }
    [[nodiscard]] bool queryDeviceByPciBusId(const QString& pciSlot, NvmlDeviceStats& out);
    [[nodiscard]] std::unordered_map<std::uint32_t, NvmlProcessMetrics>
    queryAllProcessMetrics(bool probeSleepingGpus);
    void shutdown();

private:
    struct NvmlUtilization {
        unsigned int gpu;
        unsigned int memory;
    };

    struct NvmlMemory {
        unsigned long long total;
        unsigned long long free;
        unsigned long long used;
    };

    struct NvmlProcessInfoV3 {
        unsigned int pid;
        unsigned long long usedGpuMemory;
        unsigned int gpuInstanceId;
        unsigned int computeInstanceId;
    };

    struct NvmlProcessInfoV1 {
        unsigned int pid;
        unsigned long long usedGpuMemory;
    };

    struct NvmlProcessUtilizationSample {
        unsigned int pid;
        unsigned long long timeStamp;
        unsigned int smUtil;
        unsigned int memUtil;
        unsigned int encUtil;
        unsigned int decUtil;
    };

    using NvmlReturn = int;
    using NvmlDevice = void*;

    using FnInitV2 = NvmlReturn (*)();
    using FnShutdown = NvmlReturn (*)();
    using FnDeviceGetCountV2 = NvmlReturn (*)(unsigned int*);
    using FnDeviceGetHandleByIndexV2 = NvmlReturn (*)(unsigned int, NvmlDevice*);
    using FnDeviceGetHandleByPciBusIdV2 = NvmlReturn (*)(const char*, NvmlDevice*);
    using FnDeviceGetName = NvmlReturn (*)(NvmlDevice, char*, unsigned int);
    using FnDeviceGetUtilizationRates = NvmlReturn (*)(NvmlDevice, NvmlUtilization*);
    using FnDeviceGetMemoryInfo = NvmlReturn (*)(NvmlDevice, NvmlMemory*);
    using FnDeviceGetTemperature = NvmlReturn (*)(NvmlDevice, unsigned int, unsigned int*);
    using FnDeviceGetClockInfo = NvmlReturn (*)(NvmlDevice, unsigned int, unsigned int*);
    using FnDeviceGetPowerUsage = NvmlReturn (*)(NvmlDevice, unsigned int*);
    using FnDeviceGetFanSpeed = NvmlReturn (*)(NvmlDevice, unsigned int*);
    using FnGetRunningProcsV3 = NvmlReturn (*)(NvmlDevice, unsigned int*, NvmlProcessInfoV3*);
    using FnGetRunningProcsV1 = NvmlReturn (*)(NvmlDevice, unsigned int*, NvmlProcessInfoV1*);
    using FnGetProcessUtilization = NvmlReturn (*)(NvmlDevice,
                                                   NvmlProcessUtilizationSample*,
                                                   unsigned int*,
                                                   unsigned long long);

    void* m_handle{nullptr};
    bool m_attemptedInit{false};
    bool m_initialized{false};

    FnInitV2 m_initV2{nullptr};
    FnShutdown m_shutdown{nullptr};
    FnDeviceGetCountV2 m_getCountV2{nullptr};
    FnDeviceGetHandleByIndexV2 m_getHandleByIndexV2{nullptr};
    FnDeviceGetHandleByPciBusIdV2 m_getHandleByPciBusIdV2{nullptr};
    FnDeviceGetName m_getName{nullptr};
    FnDeviceGetUtilizationRates m_getUtilizationRates{nullptr};
    FnDeviceGetMemoryInfo m_getMemoryInfo{nullptr};
    FnDeviceGetTemperature m_getTemperature{nullptr};
    FnDeviceGetClockInfo m_getClockInfo{nullptr};
    FnDeviceGetPowerUsage m_getPowerUsage{nullptr};
    FnDeviceGetFanSpeed m_getFanSpeed{nullptr};
    FnGetRunningProcsV3 m_getComputeProcsV3{nullptr};
    FnGetRunningProcsV3 m_getGraphicsProcsV3{nullptr};
    FnGetRunningProcsV1 m_getComputeProcsV1{nullptr};
    FnGetRunningProcsV1 m_getGraphicsProcsV1{nullptr};
    FnGetProcessUtilization m_getProcessUtilization{nullptr};
    std::unordered_map<unsigned int, unsigned long long> m_lastUtilTsUs;
};

} // namespace sysmon
