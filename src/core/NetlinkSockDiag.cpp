#include "core/NetlinkSockDiag.h"

#include <linux/inet_diag.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <linux/sock_diag.h>
#include <linux/tcp.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <vector>

namespace sysmon {

namespace {

struct DiagRequest {
    nlmsghdr nlh;
    inet_diag_req_v2 req;
};

} // namespace

NetlinkSockDiag::~NetlinkSockDiag() {
    if (m_sockFd >= 0) {
        ::close(m_sockFd);
        m_sockFd = -1;
    }
}

bool NetlinkSockDiag::ensureSocketOpen() {
    if (m_sockFd >= 0) {
        return true;
    }

    m_sockFd = ::socket(AF_NETLINK, SOCK_DGRAM | SOCK_CLOEXEC, NETLINK_SOCK_DIAG);
    if (m_sockFd < 0) {
        return false;
    }

    timeval tv{};
    tv.tv_sec = 0;
    tv.tv_usec = 200000;
    ::setsockopt(m_sockFd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    return true;
}

void NetlinkSockDiag::queryFamily(std::uint8_t family,
                                  std::unordered_map<std::uint64_t, InodeCounters>& outCounters) {
    if (!ensureSocketOpen()) {
        return;
    }

    DiagRequest request{};
    request.nlh.nlmsg_len = sizeof(DiagRequest);
    request.nlh.nlmsg_type = SOCK_DIAG_BY_FAMILY;
    request.nlh.nlmsg_flags = NLM_F_REQUEST | NLM_F_DUMP;
    request.nlh.nlmsg_seq = ++m_seq;
    request.nlh.nlmsg_pid = 0;

    request.req.sdiag_family = family;
    request.req.sdiag_protocol = IPPROTO_TCP;
    request.req.idiag_ext = static_cast<std::uint8_t>(1U << (INET_DIAG_INFO - 1));
    request.req.idiag_states = 0xFFFFFFFFU;

    sockaddr_nl kernelAddr{};
    kernelAddr.nl_family = AF_NETLINK;

    iovec iov{};
    iov.iov_base = &request;
    iov.iov_len = sizeof(request);

    msghdr msg{};
    msg.msg_name = &kernelAddr;
    msg.msg_namelen = sizeof(kernelAddr);
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;

    if (::sendmsg(m_sockFd, &msg, 0) < 0) {
        return;
    }

    std::vector<std::uint8_t> buf(65536);

    for (;;) {
        const ssize_t received = ::recv(m_sockFd, buf.data(), buf.size(), 0);
        if (received <= 0) {
            break;
        }

        const std::size_t totalBytes = static_cast<std::size_t>(received);
        std::size_t offset = 0;

        while (offset + sizeof(nlmsghdr) <= totalBytes) {
            nlmsghdr nlh{};
            std::memcpy(&nlh, buf.data() + offset, sizeof(nlh));

            if (nlh.nlmsg_len < sizeof(nlmsghdr) || offset + nlh.nlmsg_len > totalBytes) {
                break;
            }

            if (nlh.nlmsg_type == NLMSG_DONE) {
                return;
            }
            if (nlh.nlmsg_type == NLMSG_ERROR) {
                return;
            }

            const std::size_t diagMsgOffset = offset + NLMSG_ALIGN(sizeof(nlmsghdr));
            if (diagMsgOffset + sizeof(inet_diag_msg) <= offset + nlh.nlmsg_len) {
                inet_diag_msg diagMsg{};
                std::memcpy(&diagMsg, buf.data() + diagMsgOffset, sizeof(diagMsg));

                if (diagMsg.idiag_inode != 0) {
                    std::size_t rtaOffset = diagMsgOffset + NLMSG_ALIGN(sizeof(inet_diag_msg));
                    const std::size_t msgEnd = offset + nlh.nlmsg_len;

                    while (rtaOffset + sizeof(rtattr) <= msgEnd) {
                        rtattr rta{};
                        std::memcpy(&rta, buf.data() + rtaOffset, sizeof(rta));

                        if (rta.rta_len < sizeof(rtattr) || rtaOffset + rta.rta_len > msgEnd) {
                            break;
                        }

                        if (rta.rta_type == INET_DIAG_INFO) {
                            const std::size_t payloadOffset =
                                rtaOffset + RTA_ALIGN(sizeof(rtattr));
                            const std::size_t payloadLen =
                                static_cast<std::size_t>(rta.rta_len) - RTA_ALIGN(sizeof(rtattr));
                            constexpr std::size_t needed =
                                offsetof(tcp_info, tcpi_bytes_sent) +
                                sizeof(tcp_info::tcpi_bytes_sent);

                            if (payloadLen >= needed) {
                                tcp_info info{};
                                const std::size_t copyBytes = std::min(payloadLen, sizeof(info));
                                std::memcpy(&info, buf.data() + payloadOffset, copyBytes);

                                const std::uint64_t tx = (info.tcpi_bytes_sent > 0)
                                    ? static_cast<std::uint64_t>(info.tcpi_bytes_sent)
                                    : static_cast<std::uint64_t>(info.tcpi_bytes_acked);
                                const std::uint64_t rx =
                                    static_cast<std::uint64_t>(info.tcpi_bytes_received);

                                auto& slot = outCounters[static_cast<std::uint64_t>(diagMsg.idiag_inode)];
                                slot.rxBytes = std::max(slot.rxBytes, rx);
                                slot.txBytes = std::max(slot.txBytes, tx);
                            }
                        }

                        const std::size_t step = RTA_ALIGN(rta.rta_len);
                        if (step == 0) {
                            break;
                        }
                        rtaOffset += step;
                    }
                }
            }

            const std::size_t nlStep = NLMSG_ALIGN(nlh.nlmsg_len);
            if (nlStep == 0) {
                break;
            }
            offset += nlStep;
        }
    }
}

std::unordered_map<std::uint64_t, SocketRates>
NetlinkSockDiag::collectTcpInodeRates(double elapsedSec) {
    std::unordered_map<std::uint64_t, InodeCounters> currCounters;
    queryFamily(AF_INET, currCounters);
    queryFamily(AF_INET6, currCounters);

    std::unordered_map<std::uint64_t, SocketRates> rates;
    const double safeDt = std::max(elapsedSec, 0.001);

    for (const auto& [inode, curr] : currCounters) {
        if (const auto prevIt = m_prevCounters.find(inode); prevIt != m_prevCounters.end()) {
            const std::uint64_t dRx = (curr.rxBytes >= prevIt->second.rxBytes)
                ? (curr.rxBytes - prevIt->second.rxBytes)
                : 0;
            const std::uint64_t dTx = (curr.txBytes >= prevIt->second.txBytes)
                ? (curr.txBytes - prevIt->second.txBytes)
                : 0;
            rates.emplace(inode,
                          SocketRates{static_cast<double>(dRx) / safeDt,
                                      static_cast<double>(dTx) / safeDt});
        } else {
            rates.emplace(inode, SocketRates{0.0, 0.0});
        }
    }

    m_prevCounters = std::move(currCounters);
    return rates;
}

} // namespace sysmon
