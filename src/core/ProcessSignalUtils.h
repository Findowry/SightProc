#pragma once

#include <cstdint>
#include <optional>

namespace sysmon::proc {

enum class SignalResult {
    Ok,
    NotFound,
    PidReused,
    PermissionDenied,
    Failed
};

[[nodiscard]] std::optional<std::uint64_t> readProcStartTimeTicks(int pid);
[[nodiscard]] bool verifySameProcess(int pid, std::uint64_t expectedStartTime);
[[nodiscard]] int openPidFd(int pid);

[[nodiscard]] SignalResult sendSignalSafe(int pid,
                                          std::uint64_t expectedStartTime,
                                          int sig,
                                          int* outErrno = nullptr);

[[nodiscard]] SignalResult setPrioritySafe(int pid,
                                           std::uint64_t expectedStartTime,
                                           int niceValue,
                                           int* outErrno = nullptr);

} // namespace sysmon::proc
