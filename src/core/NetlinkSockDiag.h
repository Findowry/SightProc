#pragma once

#include <cstdint>
#include <unordered_map>

namespace sysmon {

struct SocketRates {
    double rxBytesPerSec{0.0};
    double txBytesPerSec{0.0};
};

/**
 * @brief Kök yetkisi gerektirmeden NETLINK_SOCK_DIAG (AF_INET / AF_INET6 TCP)
 *        üzerinden soket inode bazlı anlık ağ trafiği hızlarını hesaplar.
 */
class NetlinkSockDiag {
public:
    NetlinkSockDiag() = default;
    ~NetlinkSockDiag();

    NetlinkSockDiag(const NetlinkSockDiag&) = delete;
    NetlinkSockDiag& operator=(const NetlinkSockDiag&) = delete;

    [[nodiscard]] std::unordered_map<std::uint64_t, SocketRates>
    collectTcpInodeRates(double elapsedSec);

private:
    struct InodeCounters {
        std::uint64_t rxBytes{0};
        std::uint64_t txBytes{0};
    };

    bool ensureSocketOpen();
    void queryFamily(std::uint8_t family,
                     std::unordered_map<std::uint64_t, InodeCounters>& outCounters);

    int m_sockFd{-1};
    std::uint32_t m_seq{1};
    std::unordered_map<std::uint64_t, InodeCounters> m_prevCounters;
};

} // namespace sysmon
