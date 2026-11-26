#pragma once

#include <QString>

#include <cstddef>
#include <cstdint>
#include <mutex>
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
 *        resmi NVML C ABI sürümlemeli (_v2/_v3) köprü.
 */
class NvmlBridge {
public:
    NvmlBridge() = default;
    ~NvmlBridge();

    NvmlBridge(const NvmlBridge&) = delete;
    NvmlBridge& operator=(const NvmlBridge&) = delete;

    [[nodiscard]] bool ensureInitialized();
    [[nodiscard]] bool isAvailable() const;
    [[nodiscard]] bool queryDeviceByPciBusId(const QString& pciSlot, NvmlDeviceStats& out);
    [[nodiscard]] std::unordered_map<std::uint32_t, NvmlProcessMetrics>
    queryAllProcessMetrics(bool probeSleepingGpus);
    void shutdown();

private:
    [[nodiscard]] bool ensureInitializedLocked();
    void shutdownLocked();
    // Resmi nvml.h ABI yapıları (Linux x86_64/aarch64 LP64)
    struct NvmlUtilization {
        unsigned int gpu;
        unsigned int memory;
    };

    struct NvmlMemoryV1 {
        unsigned long long total;
        unsigned long long free;
        unsigned long long used;
    };

    struct NvmlMemoryV2 {
        unsigned int version;
        unsigned long long total;
        unsigned long long reserved;
        unsigned long long free;
        unsigned long long used;
    };

    struct NvmlProcessInfoV1 {
        unsigned int pid;
        unsigned long long usedGpuMemory;
    };

    // Hem _v2 hem _v3 çalışan süreç sorguları tarafından paylaşılan MIG uyumlu düzen
    struct NvmlProcessInfoV2 {
        unsigned int pid;
        unsigned long long usedGpuMemory;
        unsigned int gpuInstanceId;
        unsigned int computeInstanceId;
    };

    struct NvmlProcessUtilizationSample {
        unsigned int pid;
        unsigned long long timeStamp;
        unsigned int smUtil;
        unsigned int memUtil;
        unsigned int encUtil;
        unsigned int decUtil;
    };

    static constexpr unsigned int kNvmlMemoryV2Version =
        static_cast<unsigned int>(sizeof(NvmlMemoryV2) | (2U << 24U));

#if defined(__LP64__)
    static_assert(sizeof(NvmlUtilization) == 8, "nvmlUtilization_t size mismatch");
    static_assert(sizeof(NvmlMemoryV1) == 24, "nvmlMemory_t size mismatch");
    static_assert(sizeof(NvmlMemoryV2) == 40, "nvmlMemory_v2_t size mismatch");
    static_assert(offsetof(NvmlMemoryV2, reserved) == 16, "nvmlMemory_v2_t layout mismatch");
    static_assert(sizeof(NvmlProcessInfoV1) == 16, "nvmlProcessInfo_v1_t size mismatch");
    static_assert(sizeof(NvmlProcessInfoV2) == 24, "nvmlProcessInfo_v2_t size mismatch");
    static_assert(offsetof(NvmlProcessInfoV2, gpuInstanceId) == 16,
                  "nvmlProcessInfo_v2_t layout mismatch");
    static_assert(sizeof(NvmlProcessUtilizationSample) == 32,
                  "nvmlProcessUtilizationSample_t size mismatch");
#endif

    using NvmlReturn = int;
    using NvmlDevice = void*;

    using FnInit = NvmlReturn (*)();
    using FnShutdown = NvmlReturn (*)();
    using FnDeviceGetCount = NvmlReturn (*)(unsigned int*);
    using FnDeviceGetHandleByIndex = NvmlReturn (*)(unsigned int, NvmlDevice*);
    using FnDeviceGetHandleByPciBusId = NvmlReturn (*)(const char*, NvmlDevice*);
    using FnDeviceGetName = NvmlReturn (*)(NvmlDevice, char*, unsigned int);
    using FnDeviceGetUtilizationRates = NvmlReturn (*)(NvmlDevice, NvmlUtilization*);
    using FnDeviceGetMemoryInfoV1 = NvmlReturn (*)(NvmlDevice, NvmlMemoryV1*);
    using FnDeviceGetMemoryInfoV2 = NvmlReturn (*)(NvmlDevice, NvmlMemoryV2*);
    using FnDeviceGetTemperature = NvmlReturn (*)(NvmlDevice, unsigned int, unsigned int*);
    using FnDeviceGetClockInfo = NvmlReturn (*)(NvmlDevice, unsigned int, unsigned int*);
    using FnDeviceGetPowerUsage = NvmlReturn (*)(NvmlDevice, unsigned int*);
    using FnDeviceGetFanSpeedV1 = NvmlReturn (*)(NvmlDevice, unsigned int*);
    using FnDeviceGetFanSpeedV2 = NvmlReturn (*)(NvmlDevice, unsigned int, unsigned int*);
    using FnGetRunningProcsV2 = NvmlReturn (*)(NvmlDevice, unsigned int*, NvmlProcessInfoV2*);
    using FnGetRunningProcsV1 = NvmlReturn (*)(NvmlDevice, unsigned int*, NvmlProcessInfoV1*);
    using FnGetProcessUtilization = NvmlReturn (*)(NvmlDevice,
                                                   NvmlProcessUtilizationSample*,
                                                   unsigned int*,
                                                   unsigned long long);

    mutable std::mutex m_mutex;
    void* m_handle{nullptr};
    bool m_attemptedInit{false};
    bool m_initialized{false};

    FnInit m_init{nullptr};
    FnShutdown m_shutdown{nullptr};
    FnDeviceGetCount m_getCount{nullptr};
    FnDeviceGetHandleByIndex m_getHandleByIndex{nullptr};
    FnDeviceGetHandleByPciBusId m_getHandleByPciBusId{nullptr};
    FnDeviceGetName m_getName{nullptr};
    FnDeviceGetUtilizationRates m_getUtilizationRates{nullptr};
    FnDeviceGetMemoryInfoV2 m_getMemoryInfoV2{nullptr};
    FnDeviceGetMemoryInfoV1 m_getMemoryInfoV1{nullptr};
    FnDeviceGetTemperature m_getTemperature{nullptr};
    FnDeviceGetClockInfo m_getClockInfo{nullptr};
    FnDeviceGetPowerUsage m_getPowerUsage{nullptr};
    FnDeviceGetFanSpeedV2 m_getFanSpeedV2{nullptr};
    FnDeviceGetFanSpeedV1 m_getFanSpeedV1{nullptr};
    FnGetRunningProcsV2 m_getComputeProcsV2{nullptr};
    FnGetRunningProcsV2 m_getGraphicsProcsV2{nullptr};
    FnGetRunningProcsV1 m_getComputeProcsV1{nullptr};
    FnGetRunningProcsV1 m_getGraphicsProcsV1{nullptr};
    FnGetProcessUtilization m_getProcessUtilization{nullptr};
    std::unordered_map<unsigned int, unsigned long long> m_lastUtilTsUs;
};

} // namespace sysmon
