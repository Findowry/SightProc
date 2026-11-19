#pragma once

#include "core/DesktopEntryIndex.h"
#include "core/NetlinkSockDiag.h"
#include "core/NvmlBridge.h"
#include "core/TelemetryTypes.h"

#include <cstdint>
#include <unordered_map>

namespace sysmon {

/**
 * @brief /proc/[pid] taraması, NETLINK_SOCK_DIAG TCP hızları, NVML + DRM fdinfo
 *        GPU metrikleri ve cgroup v2 / .desktop uygulama gruplamasını toplar.
 */
class ProcessCollector {
public:
    explicit ProcessCollector(NvmlBridge& nvml);

    [[nodiscard]] ProcessSnapshot collect(double elapsedSec, bool probeSleepingGpus);
    [[nodiscard]] int desktopEntryCount() const noexcept { return m_desktopIndex.entryCount(); }

private:
    struct ProcKey {
        int pid{0};
        std::uint64_t starttime{0};

        bool operator==(const ProcKey& other) const noexcept {
            return pid == other.pid && starttime == other.starttime;
        }
    };

    struct ProcKeyHash {
        std::size_t operator()(const ProcKey& k) const noexcept {
            return (static_cast<std::size_t>(k.pid) * 11400714819323198485ULL) ^
                   static_cast<std::size_t>(k.starttime);
        }
    };

    struct PrevIo {
        std::uint64_t readBytes{0};
        std::uint64_t writeBytes{0};
    };

    [[nodiscard]] QString resolveUsername(std::uint32_t uid);
    [[nodiscard]] QVector<ApplicationInfo> buildApplications(
        const QVector<ProcessInfo>& processes) const;

    NvmlBridge& m_nvml;
    NetlinkSockDiag m_sockDiag;
    DesktopEntryIndex m_desktopIndex;

    std::uint64_t m_pageSize{4096};
    std::uint32_t m_currentUid{0};

    bool m_hasPrevSysTicks{false};
    std::uint64_t m_prevSysTotalTicks{0};

    std::unordered_map<std::uint32_t, QString> m_uidCache;
    std::unordered_map<ProcKey, std::uint64_t, ProcKeyHash> m_prevCpuTicks;
    std::unordered_map<ProcKey, PrevIo, ProcKeyHash> m_prevIoBytes;
    std::unordered_map<ProcKey, std::uint64_t, ProcKeyHash> m_prevDrmNs;
};

} // namespace sysmon
