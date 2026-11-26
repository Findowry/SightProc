#include "core/ProcessSignalUtils.h"

#include <cerrno>
#include <csignal>
#include <fstream>
#include <sstream>
#include <string>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <unistd.h>

namespace sysmon::proc {

namespace {

enum class VerifyStatus {
    Match,
    NotFound,
    Mismatch
};

VerifyStatus checkSameProcess(int pid, std::uint64_t expectedStartTime) {
    if (expectedStartTime == 0) {
        return VerifyStatus::Match;
    }
    errno = 0;
    const auto live = readProcStartTimeTicks(pid);
    if (!live.has_value()) {
        const int err = errno;
        if (err == ENOENT || err == ESRCH) {
            return VerifyStatus::NotFound;
        }
        return VerifyStatus::Mismatch;
    }
    return (*live == expectedStartTime) ? VerifyStatus::Match : VerifyStatus::Mismatch;
}

} // namespace

std::optional<std::uint64_t> readProcStartTimeTicks(int pid) {
    if (pid <= 0) {
        errno = ESRCH;
        return std::nullopt;
    }
    std::ifstream file("/proc/" + std::to_string(pid) + "/stat");
    std::string line;
    if (!std::getline(file, line)) {
        return std::nullopt;
    }
    const auto closeParen = line.rfind(')');
    if (closeParen == std::string::npos) {
        return std::nullopt;
    }
    std::istringstream iss(line.substr(closeParen + 1));
    char stateChar = '?';
    std::uint64_t skipU = 0;
    long skipL = 0;
    int skipI = 0;
    std::uint64_t starttime = 0;
    if (!(iss >> stateChar >> skipI >> skipI >> skipI >> skipI >> skipI
              >> skipU >> skipU >> skipU >> skipU >> skipU >> skipU >> skipU
              >> skipU >> skipU >> skipL >> skipL >> skipL >> skipU >> starttime)) {
        return std::nullopt;
    }
    return starttime;
}

bool verifySameProcess(int pid, std::uint64_t expectedStartTime) {
    if (expectedStartTime == 0) {
        return true;
    }
    const auto live = readProcStartTimeTicks(pid);
    return live.has_value() && *live == expectedStartTime;
}

int openPidFd(int pid) {
#ifdef SYS_pidfd_open
    return static_cast<int>(::syscall(SYS_pidfd_open, static_cast<pid_t>(pid), 0U));
#else
    (void)pid;
    errno = ENOSYS;
    return -1;
#endif
}

SignalResult sendSignalSafe(int pid,
                            std::uint64_t expectedStartTime,
                            int sig,
                            int* outErrno) {
    if (outErrno) {
        *outErrno = 0;
    }
    if (pid <= 0) {
        if (outErrno) {
            *outErrno = ESRCH;
        }
        return SignalResult::NotFound;
    }

    const int pidfd = openPidFd(pid);
    if (pidfd < 0 && errno == ESRCH) {
        if (outErrno) {
            *outErrno = ESRCH;
        }
        return SignalResult::NotFound;
    }

    const VerifyStatus verified = checkSameProcess(pid, expectedStartTime);
    if (verified != VerifyStatus::Match) {
        if (pidfd >= 0) {
            ::close(pidfd);
        }
        if (verified == VerifyStatus::NotFound) {
            if (outErrno) {
                *outErrno = ESRCH;
            }
            return SignalResult::NotFound;
        }
        return SignalResult::PidReused;
    }

    int ret = -1;
    int err = 0;
    if (pidfd >= 0) {
#ifdef SYS_pidfd_send_signal
        ret = static_cast<int>(::syscall(SYS_pidfd_send_signal, pidfd, sig, nullptr, 0U));
#else
        ret = ::kill(static_cast<pid_t>(pid), sig);
#endif
        err = errno;
        ::close(pidfd);
    } else {
        ret = ::kill(static_cast<pid_t>(pid), sig);
        err = errno;
    }

    if (ret == 0) {
        return SignalResult::Ok;
    }
    if (outErrno) {
        *outErrno = err;
    }
    if (err == ESRCH) {
        return SignalResult::NotFound;
    }
    if (err == EPERM || err == EACCES) {
        return SignalResult::PermissionDenied;
    }
    return SignalResult::Failed;
}

SignalResult setPrioritySafe(int pid,
                             std::uint64_t expectedStartTime,
                             int niceValue,
                             int* outErrno) {
    if (outErrno) {
        *outErrno = 0;
    }
    if (pid <= 0) {
        if (outErrno) {
            *outErrno = ESRCH;
        }
        return SignalResult::NotFound;
    }

    const int pidfd = openPidFd(pid);
    if (pidfd < 0 && errno == ESRCH) {
        if (outErrno) {
            *outErrno = ESRCH;
        }
        return SignalResult::NotFound;
    }

    const VerifyStatus verified = checkSameProcess(pid, expectedStartTime);
    if (verified != VerifyStatus::Match) {
        if (pidfd >= 0) {
            ::close(pidfd);
        }
        if (verified == VerifyStatus::NotFound) {
            if (outErrno) {
                *outErrno = ESRCH;
            }
            return SignalResult::NotFound;
        }
        return SignalResult::PidReused;
    }

    errno = 0;
    const int ret = ::setpriority(PRIO_PROCESS, static_cast<id_t>(pid), niceValue);
    const int err = errno;
    if (pidfd >= 0) {
        ::close(pidfd);
    }

    if (ret == 0) {
        return SignalResult::Ok;
    }
    if (outErrno) {
        *outErrno = err;
    }
    if (err == ESRCH) {
        return SignalResult::NotFound;
    }
    if (err == EPERM || err == EACCES) {
        return SignalResult::PermissionDenied;
    }
    return SignalResult::Failed;
}

} // namespace sysmon::proc
