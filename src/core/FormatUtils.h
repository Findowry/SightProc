#pragma once

#include <QString>

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace sysmon::fmt {

[[nodiscard]] QString formatBytes(std::uint64_t bytes);
[[nodiscard]] QString formatRate(double bytesPerSec);
[[nodiscard]] QString formatFrequencyMHz(double mhz);
[[nodiscard]] QString formatUptime(double seconds);
[[nodiscard]] QString formatTemp(double tempC);
[[nodiscard]] QString formatPercent(double pct, int decimals = 1);

} // namespace sysmon::fmt

namespace sysmon::sysfs {

[[nodiscard]] std::string trim(std::string_view sv);
[[nodiscard]] std::optional<std::string> readFirstLine(const std::filesystem::path& path);
[[nodiscard]] std::optional<std::uint64_t> readUint64(const std::filesystem::path& path);
[[nodiscard]] std::optional<std::int64_t> readInt64(const std::filesystem::path& path);

} // namespace sysmon::sysfs
