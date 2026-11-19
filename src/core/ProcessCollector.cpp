#include "core/ProcessCollector.h"

#include "core/FormatUtils.h"

#include <QDateTime>
#include <QFileInfo>
#include <QHash>
#include <QSet>

#include <dirent.h>
#include <fcntl.h>
#include <pwd.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace sysmon {

namespace {

bool isNumericName(const char* name) {
    if (!name || *name == '\0') {
        return false;
    }
    for (const char* p = name; *p != '\0'; ++p) {
        if (std::isdigit(static_cast<unsigned char>(*p)) == 0) {
            return false;
        }
    }
    return true;
}

std::uint64_t readTotalCpuTicks() {
    std::ifstream statFile("/proc/stat");
    std::string line;
    if (!std::getline(statFile, line) || !line.starts_with("cpu ")) {
        return 0;
    }
    std::istringstream iss(line);
    std::string tag;
    iss >> tag;
    std::uint64_t val = 0;
    std::uint64_t sum = 0;
    while (iss >> val) {
        sum += val;
    }
    return sum;
}

bool readSmallFile(const char* path, std::string& out) {
    const int fd = ::open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        return false;
    }
    out.clear();
    char buf[4096];
    for (;;) {
        const ssize_t n = ::read(fd, buf, sizeof(buf));
        if (n <= 0) {
            break;
        }
        out.append(buf, static_cast<std::size_t>(n));
        if (out.size() >= 65536) {
            break;
        }
    }
    ::close(fd);
    return true;
}

QString stateDescription(char stateChar) {
    switch (stateChar) {
    case 'R':
        return QStringLiteral("Çalışıyor (R)");
    case 'S':
        return QStringLiteral("Uyuyor (S)");
    case 'D':
        return QStringLiteral("Disk Uykusu (D)");
    case 'T':
    case 't':
        return QStringLiteral("Durduruldu (%1)").arg(QLatin1Char(stateChar));
    case 'Z':
        return QStringLiteral("Zombi (Z)");
    case 'I':
        return QStringLiteral("Boşta (I)");
    default:
        return QStringLiteral("Diğer (%1)").arg(QLatin1Char(stateChar));
    }
}

std::uint64_t parseMemValueWithUnit(std::string_view sv) {
    sv = std::string_view(sysfs::trim(sv));
    while (!sv.empty() && std::isdigit(static_cast<unsigned char>(sv.front())) == 0) {
        sv.remove_prefix(1);
    }
    std::uint64_t val = 0;
    const auto [ptr, ec] = std::from_chars(sv.data(), sv.data() + sv.size(), val);
    if (ec != std::errc{}) {
        return 0;
    }
    const std::string unit = sysfs::trim(std::string_view(ptr, sv.data() + sv.size() - ptr));
    if (unit == "KiB" || unit == "kB") {
        return val * 1024ULL;
    }
    if (unit == "MiB" || unit == "MB") {
        return val * 1024ULL * 1024ULL;
    }
    if (unit == "GiB" || unit == "GB") {
        return val * 1024ULL * 1024ULL * 1024ULL;
    }
    return val;
}

std::uint64_t parseNanosecondsValue(std::string_view sv) {
    const std::string trimmed = sysfs::trim(sv);
    std::uint64_t val = 0;
    std::from_chars(trimmed.data(), trimmed.data() + trimmed.size(), val);
    return val;
}

bool parseSocketInode(std::string_view target, std::uint64_t& outInode) {
    if (!target.starts_with("socket:[") || !target.ends_with(']')) {
        return false;
    }
    const std::string_view num = target.substr(8, target.size() - 9);
    const auto [ptr, ec] = std::from_chars(num.data(), num.data() + num.size(), outInode);
    return ec == std::errc{} && ptr == num.data() + num.size();
}

QString unescapeSystemdUnit(const QString& input) {
    QString out;
    out.reserve(input.size());
    for (int i = 0; i < input.size(); ++i) {
        if (input[i] == QLatin1Char('\\') && i + 3 < input.size() && input[i + 1] == QLatin1Char('x')) {
            bool ok = false;
            const int byteVal = input.mid(i + 2, 2).toInt(&ok, 16);
            if (ok) {
                out.append(QLatin1Char(static_cast<char>(byteVal)));
                i += 3;
                continue;
            }
        }
        out.append(input[i]);
    }
    return out;
}

QString cleanScopeAppName(const QString& rawUnit) {
    QString s = unescapeSystemdUnit(rawUnit);
    if (s.endsWith(QStringLiteral(".scope"))) {
        s.chop(6);
    } else if (s.endsWith(QStringLiteral(".service"))) {
        s.chop(8);
    }
    if (s.startsWith(QStringLiteral("app-"))) {
        s = s.mid(4);
    }
    if (s.startsWith(QStringLiteral("flatpak-"))) {
        s = s.mid(8);
    }
    const int atPos = s.indexOf(QLatin1Char('@'));
    if (atPos > 0) {
        s = s.left(atPos);
    }
    return s;
}

bool isSessionHelperUnit(const QString& unitLower) {
    return unitLower.startsWith(QStringLiteral("plasma-")) ||
           unitLower.startsWith(QStringLiteral("dbus-")) ||
           unitLower.startsWith(QStringLiteral("xdg-")) ||
           unitLower.startsWith(QStringLiteral("at-spi")) ||
           unitLower.startsWith(QStringLiteral("kwin")) ||
           unitLower.startsWith(QStringLiteral("gnome-keyring"));
}

} // namespace

ProcessCollector::ProcessCollector(NvmlBridge& nvml)
    : m_nvml(nvml) {
    const long ps = ::sysconf(_SC_PAGESIZE);
    if (ps > 0) {
        m_pageSize = static_cast<std::uint64_t>(ps);
    }
    m_currentUid = static_cast<std::uint32_t>(::getuid());
}

QString ProcessCollector::resolveUsername(std::uint32_t uid) {
    if (const auto it = m_uidCache.find(uid); it != m_uidCache.end()) {
        return it->second;
    }

    struct passwd pwd{};
    struct passwd* result = nullptr;
    std::vector<char> buf(4096);
    if (::getpwuid_r(static_cast<uid_t>(uid), &pwd, buf.data(), buf.size(), &result) == 0 &&
        result != nullptr && result->pw_name != nullptr) {
        QString name = QString::fromUtf8(result->pw_name);
        m_uidCache.emplace(uid, name);
        return name;
    }

    QString fallback = QString::number(uid);
    m_uidCache.emplace(uid, fallback);
    return fallback;
}

ProcessSnapshot ProcessCollector::collect(double elapsedSec, bool probeSleepingGpus) {
    ProcessSnapshot snapshot;
    snapshot.timestampMs = QDateTime::currentMSecsSinceEpoch();
    snapshot.currentUserUid = m_currentUid;

    const double safeDt = std::max(elapsedSec, 0.001);

    const std::uint64_t currSysTotalTicks = readTotalCpuTicks();
    const std::uint64_t deltaSysTicks = (m_hasPrevSysTicks && currSysTotalTicks > m_prevSysTotalTicks)
        ? (currSysTotalTicks - m_prevSysTotalTicks)
        : 0;
    m_prevSysTotalTicks = currSysTotalTicks;
    m_hasPrevSysTicks = true;

    const auto tcpRates = m_sockDiag.collectTcpInodeRates(safeDt);
    const auto nvmlProcs = m_nvml.queryAllProcessMetrics(probeSleepingGpus);

    std::unordered_map<ProcKey, std::uint64_t, ProcKeyHash> nextCpuTicks;
    std::unordered_map<ProcKey, PrevIo, ProcKeyHash> nextIoBytes;
    std::unordered_map<ProcKey, std::uint64_t, ProcKeyHash> nextDrmNs;

    DIR* procDir = ::opendir("/proc");
    if (!procDir) {
        return snapshot;
    }

    std::string statContent;
    std::string fileContent;
    char pathBuf[512];
    char linkBuf[4096];

    while (const dirent* ent = ::readdir(procDir)) {
        if (!isNumericName(ent->d_name)) {
            continue;
        }

        int pid = 0;
        const std::string_view pidSv(ent->d_name);
        const auto [pPtr, pEc] = std::from_chars(pidSv.data(), pidSv.data() + pidSv.size(), pid);
        if (pEc != std::errc{} || pid <= 0) {
            continue;
        }

        std::snprintf(pathBuf, sizeof(pathBuf), "/proc/%d", pid);
        struct stat procSt{};
        if (::stat(pathBuf, &procSt) != 0) {
            continue;
        }

        std::snprintf(pathBuf, sizeof(pathBuf), "/proc/%d/stat", pid);
        if (!readSmallFile(pathBuf, statContent)) {
            continue;
        }

        const auto openParen = statContent.find('(');
        const auto closeParen = statContent.rfind(')');
        if (openParen == std::string::npos || closeParen == std::string::npos ||
            closeParen <= openParen) {
            continue;
        }

        ProcessInfo proc;
        proc.pid = pid;
        proc.uid = static_cast<std::uint32_t>(procSt.st_uid);
        proc.user = resolveUsername(proc.uid);
        proc.name = QString::fromUtf8(
            statContent.data() + openParen + 1,
            static_cast<qsizetype>(closeParen - openParen - 1));

        std::uint64_t utime = 0;
        std::uint64_t stime = 0;
        std::uint64_t starttime = 0;
        {
            std::istringstream iss(statContent.substr(closeParen + 1));
            std::uint64_t skipU = 0;
            std::int64_t skipI = 0;
            int pgrp = 0;
            int session = 0;
            int ttyNr = 0;
            int tpgid = 0;
            unsigned int flags = 0;
            std::uint64_t cutime = 0;
            std::uint64_t cstime = 0;
            long priority = 0;
            long niceVal = 0;
            long numThreads = 1;
            std::uint64_t itreal = 0;

            if (!(iss >> proc.stateChar >> proc.ppid >> pgrp >> session >> ttyNr >> tpgid
                      >> flags >> skipU >> skipU >> skipU >> skipU >> utime >> stime
                      >> cutime >> cstime >> priority >> niceVal >> numThreads >> itreal
                      >> starttime)) {
                continue;
            }
            (void)skipI;
            proc.niceValue = static_cast<int>(niceVal);
            proc.threads = std::max(1, static_cast<int>(numThreads));
            proc.stateText = stateDescription(proc.stateChar);
        }

        const ProcKey key{pid, starttime};
        const std::uint64_t procTicks = utime + stime;
        nextCpuTicks.emplace(key, procTicks);

        if (deltaSysTicks > 0) {
            if (const auto it = m_prevCpuTicks.find(key); it != m_prevCpuTicks.end()) {
                const std::uint64_t dProc = (procTicks >= it->second)
                    ? (procTicks - it->second)
                    : 0;
                proc.cpuPercent = std::clamp(
                    100.0 * static_cast<double>(dProc) / static_cast<double>(deltaSysTicks),
                    0.0,
                    100.0);
            }
        }

        // Bellek (RSS ve Paylaşılan)
        std::snprintf(pathBuf, sizeof(pathBuf), "/proc/%d/statm", pid);
        if (readSmallFile(pathBuf, fileContent)) {
            std::istringstream iss(fileContent);
            std::uint64_t vmPages = 0;
            std::uint64_t rssPages = 0;
            std::uint64_t sharedPages = 0;
            if (iss >> vmPages >> rssPages >> sharedPages) {
                proc.rssBytes = rssPages * m_pageSize;
                proc.sharedBytes = sharedPages * m_pageSize;
            }
        }

        // Komut satırı
        std::string rawArgv0;
        std::string rawArgv1;
        std::snprintf(pathBuf, sizeof(pathBuf), "/proc/%d/cmdline", pid);
        if (readSmallFile(pathBuf, fileContent) && !fileContent.empty()) {
            const auto firstNul = fileContent.find('\0');
            if (firstNul == std::string::npos) {
                rawArgv0 = fileContent;
            } else {
                rawArgv0 = fileContent.substr(0, firstNul);
                const std::size_t arg1Start = firstNul + 1;
                if (arg1Start < fileContent.size()) {
                    const auto secondNul = fileContent.find('\0', arg1Start);
                    rawArgv1 = (secondNul == std::string::npos)
                        ? fileContent.substr(arg1Start)
                        : fileContent.substr(arg1Start, secondNul - arg1Start);
                }
            }
            for (char& ch : fileContent) {
                if (ch == '\0') {
                    ch = ' ';
                }
            }
            proc.cmdline = QString::fromUtf8(sysfs::trim(fileContent));
        }
        if (proc.cmdline.isEmpty()) {
            proc.isKernelThread = (proc.ppid == 2 || proc.pid == 2);
            proc.cmdline = QStringLiteral("[%1]").arg(proc.name);
        }

        // Yürütülebilir dosya yolu
        std::snprintf(pathBuf, sizeof(pathBuf), "/proc/%d/exe", pid);
        const ssize_t exeLen = ::readlink(pathBuf, linkBuf, sizeof(linkBuf) - 1);
        if (exeLen > 0) {
            linkBuf[exeLen] = '\0';
            proc.exePath = QString::fromUtf8(linkBuf, static_cast<qsizetype>(exeLen));
        }

        // Linux çekirdeği /proc/[pid]/stat içindeki comm değerini 15 baytta keser.
        // Kısaltılmış ise exe/cmdline üzerinden gerçek yürütülebilir/betik adını geri yükle.
        if (!proc.isKernelThread && proc.name.size() >= 15) {
            auto baseNameOf = [](QString s) -> QString {
                if (s.endsWith(QStringLiteral(" (deleted)"))) {
                    s.chop(10);
                }
                const qsizetype slash = s.lastIndexOf(QLatin1Char('/'));
                return (slash >= 0) ? s.mid(slash + 1) : s;
            };
            const QString argv1Base = baseNameOf(QString::fromUtf8(rawArgv1).trimmed());
            const QString argv0Base = baseNameOf(QString::fromUtf8(rawArgv0).trimmed());
            const QString exeBase = baseNameOf(proc.exePath);

            if (!argv1Base.isEmpty() && argv1Base.startsWith(proc.name)) {
                proc.name = argv1Base;
            } else if (!argv0Base.isEmpty() && argv0Base.startsWith(proc.name)) {
                proc.name = argv0Base;
            } else if (!exeBase.isEmpty() && exeBase.startsWith(proc.name)) {
                proc.name = exeBase;
            }
        }

        // Disk G/Ç (/proc/[pid]/io)
        std::snprintf(pathBuf, sizeof(pathBuf), "/proc/%d/io", pid);
        if (readSmallFile(pathBuf, fileContent)) {
            proc.ioReadable = true;
            std::uint64_t rBytes = 0;
            std::uint64_t wBytes = 0;
            std::istringstream iss(fileContent);
            std::string ioLine;
            while (std::getline(iss, ioLine)) {
                if (ioLine.starts_with("read_bytes:")) {
                    std::from_chars(ioLine.data() + 12, ioLine.data() + ioLine.size(), rBytes);
                } else if (ioLine.starts_with("write_bytes:")) {
                    std::from_chars(ioLine.data() + 13, ioLine.data() + ioLine.size(), wBytes);
                }
            }
            nextIoBytes.emplace(key, PrevIo{rBytes, wBytes});
            if (const auto it = m_prevIoBytes.find(key); it != m_prevIoBytes.end()) {
                const std::uint64_t dr = (rBytes >= it->second.readBytes)
                    ? (rBytes - it->second.readBytes)
                    : 0;
                const std::uint64_t dw = (wBytes >= it->second.writeBytes)
                    ? (wBytes - it->second.writeBytes)
                    : 0;
                proc.diskReadBytesPerSec = static_cast<double>(dr) / safeDt;
                proc.diskWriteBytesPerSec = static_cast<double>(dw) / safeDt;
            }
        }

        // cgroup v2 yolu
        std::snprintf(pathBuf, sizeof(pathBuf), "/proc/%d/cgroup", pid);
        if (readSmallFile(pathBuf, fileContent)) {
            std::istringstream iss(fileContent);
            std::string cgLine;
            while (std::getline(iss, cgLine)) {
                const auto lastColon = cgLine.rfind(':');
                if (lastColon != std::string::npos) {
                    proc.cgroupPath = QString::fromUtf8(sysfs::trim(cgLine.substr(lastColon + 1)));
                    if (cgLine.starts_with("0::")) {
                        break;
                    }
                }
            }
        }

        // Tek geçişli /proc/[pid]/fd taraması:
        // 1) socket:[inode] üzerinden Netlink TCP hızları
        // 2) YALNIZCA hedefi /dev/dri/ ile başlayan tanıtıcılar için fdinfo okunur!
        std::snprintf(pathBuf, sizeof(pathBuf), "/proc/%d/fd", pid);
        if (DIR* fdDir = ::opendir(pathBuf)) {
            proc.netReadable = true;
            const int dfd = ::dirfd(fdDir);

            std::unordered_set<std::uint64_t> seenSocketInodes;
            std::unordered_set<std::uint64_t> seenDrmClients;
            std::uint64_t drmEngineNs = 0;
            std::uint64_t drmVramBytes = 0;
            std::uint64_t drmSysMemBytes = 0;
            bool sawDrmFdinfo = false;

            while (const dirent* fdEnt = ::readdir(fdDir)) {
                if (fdEnt->d_name[0] == '.') {
                    continue;
                }
                const ssize_t tLen = ::readlinkat(dfd, fdEnt->d_name, linkBuf, sizeof(linkBuf) - 1);
                if (tLen <= 0) {
                    continue;
                }
                linkBuf[tLen] = '\0';
                const std::string_view target(linkBuf, static_cast<std::size_t>(tLen));

                std::uint64_t inode = 0;
                if (parseSocketInode(target, inode)) {
                    if (seenSocketInodes.insert(inode).second) {
                        if (const auto rIt = tcpRates.find(inode); rIt != tcpRates.end()) {
                            ++proc.tcpSocketCount;
                            proc.netRxBytesPerSec += rIt->second.rxBytesPerSec;
                            proc.netTxBytesPerSec += rIt->second.txBytesPerSec;
                        }
                    }
                    continue;
                }

                // Performans kuralı: fdinfo yalnızca /dev/dri/ aygıtları için açılır!
                if (target.starts_with("/dev/dri/")) {
                    std::snprintf(pathBuf, sizeof(pathBuf), "/proc/%d/fdinfo/%s", pid, fdEnt->d_name);
                    if (!readSmallFile(pathBuf, fileContent)) {
                        continue;
                    }

                    std::uint64_t clientId = 0;
                    std::uint64_t localEngineNs = 0;
                    std::uint64_t localVram = 0;
                    std::uint64_t localSysMem = 0;
                    bool hasClientId = false;

                    std::istringstream iss(fileContent);
                    std::string fdLine;
                    while (std::getline(iss, fdLine)) {
                        const auto colon = fdLine.find(':');
                        if (colon == std::string::npos) {
                            continue;
                        }
                        const std::string_view k(fdLine.data(), colon);
                        const std::string_view v(fdLine.data() + colon + 1,
                                                 fdLine.size() - colon - 1);
                        if (k == "drm-client-id") {
                            const std::string t = sysfs::trim(v);
                            const auto [cp, cec] = std::from_chars(t.data(), t.data() + t.size(), clientId);
                            hasClientId = (cec == std::errc{});
                            (void)cp;
                        } else if (k.starts_with("drm-engine-") && v.find("ns") != std::string_view::npos) {
                            localEngineNs += parseNanosecondsValue(v);
                        } else if (k == "drm-resident-vram" || k == "drm-memory-vram" ||
                                   k == "drm-resident-local0" || k == "drm-resident-local") {
                            localVram += parseMemValueWithUnit(v);
                        } else if (k == "drm-resident-system" || k == "drm-memory-system") {
                            localSysMem += parseMemValueWithUnit(v);
                        }
                    }

                    if (!hasClientId || seenDrmClients.insert(clientId).second) {
                        sawDrmFdinfo = true;
                        drmEngineNs += localEngineNs;
                        drmVramBytes += localVram;
                        drmSysMemBytes += localSysMem;
                    }
                }
            }
            ::closedir(fdDir);

            if (sawDrmFdinfo) {
                proc.hasGpuMetrics = true;
                proc.gpuMemBytes = (drmVramBytes > 0) ? drmVramBytes : drmSysMemBytes;
                nextDrmNs.emplace(key, drmEngineNs);
                if (const auto it = m_prevDrmNs.find(key); it != m_prevDrmNs.end()) {
                    const std::uint64_t dNs = (drmEngineNs >= it->second)
                        ? (drmEngineNs - it->second)
                        : 0;
                    proc.gpuPercent = std::clamp(
                        100.0 * static_cast<double>(dNs) / (safeDt * 1e9),
                        0.0,
                        100.0);
                }
            }
        }

        if (const auto nvIt = nvmlProcs.find(static_cast<std::uint32_t>(pid)); nvIt != nvmlProcs.end()) {
            proc.hasGpuMetrics = true;
            proc.gpuPercent = std::max(proc.gpuPercent, nvIt->second.smUtilPercent);
            proc.gpuMemBytes = std::max(proc.gpuMemBytes, nvIt->second.vramBytes);
        }

        ++snapshot.totalProcessCount;
        snapshot.totalThreadCount += proc.threads;
        snapshot.processes.push_back(std::move(proc));
    }

    ::closedir(procDir);

    m_prevCpuTicks = std::move(nextCpuTicks);
    m_prevIoBytes = std::move(nextIoBytes);
    m_prevDrmNs = std::move(nextDrmNs);

    snapshot.applications = buildApplications(snapshot.processes);

    // Uygulama ikonlarını ilgili alt süreçlere de yansıt; kalan süreçler için ikili/sistem ikonu ata
    QHash<int, QString> pidToIcon;
    for (const ApplicationInfo& app : snapshot.applications) {
        if (!app.iconName.isEmpty() &&
            app.iconName != QStringLiteral("application-x-executable")) {
            for (int pid : app.pids) {
                pidToIcon.insert(pid, app.iconName);
            }
        }
    }

    for (ProcessInfo& proc : snapshot.processes) {
        if (const auto it = pidToIcon.constFind(proc.pid); it != pidToIcon.constEnd()) {
            proc.iconName = *it;
            continue;
        }
        if (proc.isKernelThread) {
            proc.iconName = QStringLiteral("applications-system");
            continue;
        }
        const QString exeBase = QFileInfo(proc.exePath).fileName();
        QString resolved = m_desktopIndex.findIconByProcessName(proc.name, exeBase);
        if (!resolved.isEmpty()) {
            proc.iconName = resolved;
            continue;
        }
        const QString lower = (!exeBase.isEmpty() ? exeBase : proc.name).toLower();
        if (lower == QStringLiteral("bash") || lower == QStringLiteral("zsh") ||
            lower == QStringLiteral("fish") || lower == QStringLiteral("sh") ||
            lower == QStringLiteral("dash") || lower == QStringLiteral("tmux")) {
            proc.iconName = QStringLiteral("utilities-terminal");
        } else if (lower == QStringLiteral("systemd") || lower.startsWith(QStringLiteral("dbus")) ||
                   lower == QStringLiteral("pipewire") || lower == QStringLiteral("wireplumber")) {
            proc.iconName = QStringLiteral("preferences-system-services");
        } else {
            proc.iconName = lower;
        }
    }

    return snapshot;
}

QVector<ApplicationInfo> ProcessCollector::buildApplications(
    const QVector<ProcessInfo>& processes) const {

    QHash<int, int> pidToIndex;
    pidToIndex.reserve(processes.size());
    for (int i = 0; i < processes.size(); ++i) {
        pidToIndex.insert(processes[i].pid, i);
    }

    struct ResolvedApp {
        QString key;
        QString name;
        QString iconName;
        QString desktopId;
    };

    QHash<int, ResolvedApp> pidAppMap;

    auto resolveFromScope = [&](const QString& unitName) -> std::optional<ResolvedApp> {
        const QString cleaned = cleanScopeAppName(unitName);
        if (cleaned.isEmpty() || isSessionHelperUnit(cleaned.toLower())) {
            return std::nullopt;
        }

        // Önce tam temizlenmiş ad ile dene, ardından sondaki -<pid>/<uuid> parçalarını kırparak ara
        QString probe = cleaned;
        while (!probe.isEmpty()) {
            if (const DesktopAppMeta* meta = m_desktopIndex.findByDesktopId(probe)) {
                if (meta->noDisplay) {
                    return std::nullopt;
                }
                return ResolvedApp{
                    QStringLiteral("desktop:%1").arg(meta->desktopId.toLower()),
                    meta->name,
                    meta->iconName,
                    meta->desktopId,
                };
            }
            const int dashPos = probe.lastIndexOf(QLatin1Char('-'));
            if (dashPos <= 0) {
                break;
            }
            probe = probe.left(dashPos);
        }

        // .desktop dosyasını eşleyemezse yalnızca gerçek app- scope'uysa temizlenmiş adla koru
        if (unitName.startsWith(QStringLiteral("app-"))) {
            const int lastDash = cleaned.lastIndexOf(QLatin1Char('-'));
            QString displayName = (lastDash > 0) ? cleaned.left(lastDash) : cleaned;
            return ResolvedApp{
                QStringLiteral("scope:%1").arg(displayName.toLower()),
                displayName,
                QStringLiteral("application-x-executable"),
                {},
            };
        }
        return std::nullopt;
    };

    // Aşama 1: systemd kullanıcı app.slice kapsamları
    // Aşama 2: .desktop Exec / StartupWMClass eşleşmesi
    for (const ProcessInfo& p : processes) {
        if (p.uid != m_currentUid || p.isKernelThread) {
            continue;
        }

        const int appSliceIdx = p.cgroupPath.indexOf(QStringLiteral("/app.slice/"));
        if (appSliceIdx >= 0) {
            QString unitPart = p.cgroupPath.mid(appSliceIdx + 11);
            const int slashPos = unitPart.indexOf(QLatin1Char('/'));
            if (slashPos > 0) {
                unitPart = unitPart.left(slashPos);
            }
            if (const auto resolved = resolveFromScope(unitPart)) {
                pidAppMap.insert(p.pid, *resolved);
                continue;
            }
        }

        if (p.cgroupPath.contains(QStringLiteral("/background.slice/")) ||
            p.cgroupPath.contains(QStringLiteral("/session.slice/"))) {
            continue;
        }

        const QString exeBase = QFileInfo(p.exePath).fileName();
        if (const DesktopAppMeta* meta = m_desktopIndex.findByProcess(p.name, exeBase, p.cmdline)) {
            pidAppMap.insert(p.pid,
                             ResolvedApp{
                                 QStringLiteral("desktop:%1").arg(meta->desktopId.toLower()),
                                 meta->name,
                                 meta->iconName,
                                 meta->desktopId,
                             });
        }
    }

    // Aşama 3: Eşleşmemiş kullanıcı süreçlerini PPID soy ağacındaki masaüstü uygulamasına bağla
    for (const ProcessInfo& p : processes) {
        if (p.uid != m_currentUid || p.isKernelThread || pidAppMap.contains(p.pid)) {
            continue;
        }

        int curPpid = p.ppid;
        QSet<int> visited;
        for (int depth = 0; depth < 16 && curPpid > 1; ++depth) {
            if (!visited.contains(curPpid)) {
                visited.insert(curPpid);
            } else {
                break;
            }
            if (const auto appIt = pidAppMap.constFind(curPpid); appIt != pidAppMap.constEnd()) {
                pidAppMap.insert(p.pid, *appIt);
                break;
            }
            const auto pIdxIt = pidToIndex.constFind(curPpid);
            if (pIdxIt == pidToIndex.constEnd()) {
                break;
            }
            curPpid = processes[*pIdxIt].ppid;
        }
    }

    QHash<QString, int> appKeyToOutIdx;
    QVector<ApplicationInfo> apps;

    for (const ProcessInfo& p : processes) {
        const auto it = pidAppMap.constFind(p.pid);
        if (it == pidAppMap.constEnd()) {
            continue;
        }

        const ResolvedApp& meta = *it;
        int outIdx = -1;
        if (const auto existingIt = appKeyToOutIdx.constFind(meta.key);
            existingIt != appKeyToOutIdx.constEnd()) {
            outIdx = *existingIt;
        } else {
            ApplicationInfo app;
            app.id = meta.key;
            app.name = meta.name;
            app.iconName = meta.iconName;
            app.desktopId = meta.desktopId;
            outIdx = static_cast<int>(apps.size());
            appKeyToOutIdx.insert(meta.key, outIdx);
            apps.push_back(std::move(app));
        }

        ApplicationInfo& target = apps[outIdx];
        target.pids.push_back(p.pid);
        target.cpuPercent += p.cpuPercent;
        target.rssBytes += p.rssBytes;
        target.sharedBytes += p.sharedBytes;
        if (p.ioReadable) {
            target.ioReadable = true;
            target.diskReadBytesPerSec += p.diskReadBytesPerSec;
            target.diskWriteBytesPerSec += p.diskWriteBytesPerSec;
        }
        if (p.netReadable) {
            target.netReadable = true;
            target.netRxBytesPerSec += p.netRxBytesPerSec;
            target.netTxBytesPerSec += p.netTxBytesPerSec;
        }
        if (p.hasGpuMetrics) {
            target.hasGpuMetrics = true;
            target.gpuPercent = std::clamp(target.gpuPercent + p.gpuPercent, 0.0, 100.0);
            target.gpuMemBytes += p.gpuMemBytes;
        }
    }

    return apps;
}

} // namespace sysmon
