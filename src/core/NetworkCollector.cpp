#include "core/NetworkCollector.h"

#include "core/FormatUtils.h"

#include <arpa/inet.h>
#include <ifaddrs.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <system_error>
#include <unordered_map>
#include <vector>

namespace sysmon {

namespace {

struct ProcNetDevStats {
    std::uint64_t rxBytes{0};
    std::uint64_t rxPackets{0};
    std::uint64_t rxErrors{0};
    std::uint64_t rxDropped{0};
    std::uint64_t txBytes{0};
    std::uint64_t txPackets{0};
    std::uint64_t txErrors{0};
    std::uint64_t txDropped{0};
};

std::unordered_map<std::string, ProcNetDevStats> readProcNetDev() {
    std::unordered_map<std::string, ProcNetDevStats> map;
    std::ifstream file("/proc/net/dev");
    std::string line;
    int lineNum = 0;
    while (std::getline(file, line)) {
        if (++lineNum <= 2) {
            continue;
        }
        const auto colonPos = line.find(':');
        if (colonPos == std::string::npos) {
            continue;
        }
        const std::string ifName = sysfs::trim(std::string_view(line).substr(0, colonPos));
        std::istringstream iss(line.substr(colonPos + 1));

        ProcNetDevStats s{};
        std::uint64_t rxFifo = 0;
        std::uint64_t rxFrame = 0;
        std::uint64_t rxCompressed = 0;
        std::uint64_t rxMulticast = 0;
        if (iss >> s.rxBytes >> s.rxPackets >> s.rxErrors >> s.rxDropped
                >> rxFifo >> rxFrame >> rxCompressed >> rxMulticast
                >> s.txBytes >> s.txPackets >> s.txErrors >> s.txDropped) {
            map[ifName] = s;
        }
    }
    return map;
}

} // namespace

QVector<NetworkTelemetry> NetworkCollector::collect(double elapsedSec) {
    QVector<NetworkTelemetry> result;
    std::error_code ec;
    const std::filesystem::path netRoot("/sys/class/net");
    if (!std::filesystem::exists(netRoot, ec)) {
        return result;
    }

    std::vector<std::string> interfaces;
    {
        std::error_code iterEc;
        for (std::filesystem::directory_iterator it(netRoot, iterEc), end;
             !iterEc && it != end;
             it.increment(iterEc)) {
            const std::string name = it->path().filename().string();
            if (name != "lo") {
                interfaces.push_back(name);
            }
        }
    }
    std::sort(interfaces.begin(), interfaces.end());

    const auto procStats = readProcNetDev();

    std::unordered_map<std::string, QStringList> ipv4Map;
    std::unordered_map<std::string, QStringList> ipv6Map;
    ifaddrs* ifap = nullptr;
    if (getifaddrs(&ifap) == 0 && ifap != nullptr) {
        for (ifaddrs* ifa = ifap; ifa != nullptr; ifa = ifa->ifa_next) {
            if (!ifa->ifa_name || !ifa->ifa_addr) {
                continue;
            }
            const std::string ifName(ifa->ifa_name);
            char buf[INET6_ADDRSTRLEN] = {};
            if (ifa->ifa_addr->sa_family == AF_INET) {
                const auto* sin = reinterpret_cast<const sockaddr_in*>(ifa->ifa_addr);
                if (inet_ntop(AF_INET, &sin->sin_addr, buf, sizeof(buf))) {
                    ipv4Map[ifName].push_back(QString::fromLatin1(buf));
                }
            } else if (ifa->ifa_addr->sa_family == AF_INET6) {
                const auto* sin6 = reinterpret_cast<const sockaddr_in6*>(ifa->ifa_addr);
                if (inet_ntop(AF_INET6, &sin6->sin6_addr, buf, sizeof(buf))) {
                    ipv6Map[ifName].push_back(QString::fromLatin1(buf));
                }
            }
        }
        freeifaddrs(ifap);
    }

    const double safeDt = std::max(elapsedSec, 0.001);

    for (const auto& ifName : interfaces) {
        const std::filesystem::path ifDir = netRoot / ifName;
        NetworkTelemetry net;
        net.name = QString::fromStdString(ifName);
        net.isWireless = std::filesystem::exists(ifDir / "wireless", ec);

        if (const auto opOpt = sysfs::readFirstLine(ifDir / "operstate")) {
            net.operState = QString::fromStdString(*opOpt);
        } else {
            net.operState = QStringLiteral("unknown");
        }

        if (const auto speedOpt = sysfs::readInt64(ifDir / "speed")) {
            if (*speedOpt > 0) {
                net.linkSpeedMbps = static_cast<int>(*speedOpt);
            }
        }

        if (const auto macOpt = sysfs::readFirstLine(ifDir / "address")) {
            net.macAddress = QString::fromStdString(*macOpt);
        }

        if (const auto mtuOpt = sysfs::readInt64(ifDir / "mtu")) {
            net.mtu = static_cast<int>(*mtuOpt);
        }

        const std::filesystem::path drvLink = std::filesystem::read_symlink(ifDir / "device/driver", ec);
        if (!ec && !drvLink.empty()) {
            net.driver = QString::fromStdString(drvLink.filename().string());
        }
        if (const auto dupOpt = sysfs::readFirstLine(ifDir / "duplex")) {
            net.duplex = QString::fromStdString(*dupOpt);
        }
        net.carrierChanges = sysfs::readUint64(ifDir / "carrier_changes").value_or(0);

        if (const auto it4 = ipv4Map.find(ifName); it4 != ipv4Map.end()) {
            net.ipv4Addresses = it4->second;
        }
        if (const auto it6 = ipv6Map.find(ifName); it6 != ipv6Map.end()) {
            net.ipv6Addresses = it6->second;
        }

        if (const auto statIt = procStats.find(ifName); statIt != procStats.end()) {
            net.rxTotalBytes = statIt->second.rxBytes;
            net.txTotalBytes = statIt->second.txBytes;
            net.rxPackets = statIt->second.rxPackets;
            net.txPackets = statIt->second.txPackets;
            net.rxErrors = statIt->second.rxErrors;
            net.txErrors = statIt->second.txErrors;
            net.rxDropped = statIt->second.rxDropped;
            net.txDropped = statIt->second.txDropped;
        } else {
            const std::filesystem::path statsDir = ifDir / "statistics";
            net.rxTotalBytes = sysfs::readUint64(statsDir / "rx_bytes").value_or(0);
            net.txTotalBytes = sysfs::readUint64(statsDir / "tx_bytes").value_or(0);
            net.rxPackets = sysfs::readUint64(statsDir / "rx_packets").value_or(0);
            net.txPackets = sysfs::readUint64(statsDir / "tx_packets").value_or(0);
            net.rxErrors = sysfs::readUint64(statsDir / "rx_errors").value_or(0);
            net.txErrors = sysfs::readUint64(statsDir / "tx_errors").value_or(0);
            net.rxDropped = sysfs::readUint64(statsDir / "rx_dropped").value_or(0);
            net.txDropped = sysfs::readUint64(statsDir / "tx_dropped").value_or(0);
        }

        if (const auto prevIt = m_prevCounters.find(ifName); prevIt != m_prevCounters.end()) {
            const std::uint64_t dRx = (net.rxTotalBytes >= prevIt->second.rxBytes)
                ? (net.rxTotalBytes - prevIt->second.rxBytes)
                : 0;
            const std::uint64_t dTx = (net.txTotalBytes >= prevIt->second.txBytes)
                ? (net.txTotalBytes - prevIt->second.txBytes)
                : 0;
            net.rxBytesPerSec = static_cast<double>(dRx) / safeDt;
            net.txBytesPerSec = static_cast<double>(dTx) / safeDt;
        }

        m_prevCounters[ifName] = NetCounters{net.rxTotalBytes, net.txTotalBytes};
        result.push_back(std::move(net));
    }

    return result;
}

} // namespace sysmon
