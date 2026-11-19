#include "core/FormatUtils.h"

#include "core/Lang.h"

#include <charconv>
#include <cmath>
#include <fstream>
#include <system_error>

namespace sysmon::fmt {

QString formatBytes(std::uint64_t bytes) {
    constexpr double kStep = 1024.0;
    const char* units[] = {"B", "KiB", "MiB", "GiB", "TiB"};
    double value = static_cast<double>(bytes);
    int unitIdx = 0;
    while (value >= kStep && unitIdx < 4) {
        value /= kStep;
        ++unitIdx;
    }
    const int decimals = (unitIdx == 0) ? 0 : (value >= 100.0 ? 1 : 2);
    return QStringLiteral("%1 %2")
        .arg(QString::number(value, 'f', decimals), QString::fromLatin1(units[unitIdx]));
}

QString formatRate(double bytesPerSec) {
    if (bytesPerSec < 0.0 || !std::isfinite(bytesPerSec)) {
        return QStringLiteral("0 B/s");
    }
    constexpr double kStep = 1024.0;
    const char* units[] = {"B/s", "KiB/s", "MiB/s", "GiB/s", "TiB/s"};
    double value = bytesPerSec;
    int unitIdx = 0;
    while (value >= kStep && unitIdx < 4) {
        value /= kStep;
        ++unitIdx;
    }
    const int decimals = (unitIdx == 0) ? 0 : (value >= 100.0 ? 1 : 2);
    return QStringLiteral("%1 %2")
        .arg(QString::number(value, 'f', decimals), QString::fromLatin1(units[unitIdx]));
}

QString formatFrequencyMHz(double mhz) {
    if (mhz <= 0.0 || !std::isfinite(mhz)) {
        return QStringLiteral("—");
    }
    if (mhz >= 1000.0) {
        return QStringLiteral("%1 GHz").arg(QString::number(mhz / 1000.0, 'f', 2));
    }
    return QStringLiteral("%1 MHz").arg(QString::number(mhz, 'f', 0));
}

QString formatUptime(double seconds) {
    if (seconds < 0.0 || !std::isfinite(seconds)) {
        return QStringLiteral("—");
    }
    const auto totalSec = static_cast<std::uint64_t>(seconds);
    const std::uint64_t days = totalSec / 86400;
    const std::uint64_t hours = (totalSec % 86400) / 3600;
    const std::uint64_t mins = (totalSec % 3600) / 60;
    const std::uint64_t secs = totalSec % 60;

    const QString hms = QStringLiteral("%1:%2:%3")
                            .arg(hours, 2, 10, QLatin1Char('0'))
                            .arg(mins, 2, 10, QLatin1Char('0'))
                            .arg(secs, 2, 10, QLatin1Char('0'));
    if (days > 0) {
        return QStringLiteral("%1 %2 %3")
            .arg(days)
            .arg(trText("d", "gün"), hms);
    }
    return hms;
}

QString formatTemp(double tempC) {
    if (tempC < -50.0 || !std::isfinite(tempC)) {
        return QStringLiteral("—");
    }
    return QStringLiteral("%1 °C").arg(QString::number(tempC, 'f', 1));
}

QString formatPercent(double pct, int decimals) {
    if (pct < 0.0 || !std::isfinite(pct)) {
        return QStringLiteral("—");
    }
    return QStringLiteral("%1%").arg(QString::number(pct, 'f', decimals));
}

} // namespace sysmon::fmt

namespace sysmon::sysfs {

std::string trim(std::string_view sv) {
    const auto isSpace = [](unsigned char ch) {
        return ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n';
    };
    while (!sv.empty() && isSpace(static_cast<unsigned char>(sv.front()))) {
        sv.remove_prefix(1);
    }
    while (!sv.empty() && isSpace(static_cast<unsigned char>(sv.back()))) {
        sv.remove_suffix(1);
    }
    return std::string(sv);
}

std::optional<std::string> readFirstLine(const std::filesystem::path& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        return std::nullopt;
    }
    std::string line;
    if (!std::getline(file, line)) {
        return std::nullopt;
    }
    return trim(line);
}

std::optional<std::uint64_t> readUint64(const std::filesystem::path& path) {
    const auto lineOpt = readFirstLine(path);
    if (!lineOpt || lineOpt->empty()) {
        return std::nullopt;
    }
    std::uint64_t value = 0;
    const char* begin = lineOpt->data();
    const char* end = begin + lineOpt->size();
    const auto [ptr, ec] = std::from_chars(begin, end, value);
    if (ec != std::errc{} || ptr != end) {
        return std::nullopt;
    }
    return value;
}

std::optional<std::int64_t> readInt64(const std::filesystem::path& path) {
    const auto lineOpt = readFirstLine(path);
    if (!lineOpt || lineOpt->empty()) {
        return std::nullopt;
    }
    std::int64_t value = 0;
    const char* begin = lineOpt->data();
    const char* end = begin + lineOpt->size();
    const auto [ptr, ec] = std::from_chars(begin, end, value);
    if (ec != std::errc{} || ptr != end) {
        return std::nullopt;
    }
    return value;
}

} // namespace sysmon::sysfs
