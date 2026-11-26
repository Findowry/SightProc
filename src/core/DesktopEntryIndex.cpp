#include "core/DesktopEntryIndex.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSet>
#include <QTextStream>

#include <filesystem>
#include <system_error>

namespace sysmon {

namespace {

QStringList splitExecTokens(const QString& execLine) {
    QStringList tokens;
    QString current;
    bool inSingle = false;
    bool inDouble = false;
    bool escaped = false;

    for (const QChar ch : execLine) {
        if (escaped) {
            current.append(ch);
            escaped = false;
            continue;
        }
        if (ch == QLatin1Char('\\') && !inSingle) {
            escaped = true;
            continue;
        }
        if (ch == QLatin1Char('\'') && !inDouble) {
            inSingle = !inSingle;
            continue;
        }
        if (ch == QLatin1Char('"') && !inSingle) {
            inDouble = !inDouble;
            continue;
        }
        if (ch.isSpace() && !inSingle && !inDouble) {
            if (!current.isEmpty()) {
                tokens.push_back(current);
                current.clear();
            }
            continue;
        }
        current.append(ch);
    }
    if (!current.isEmpty()) {
        tokens.push_back(current);
    }
    return tokens;
}

bool isWrapperCommand(const QString& binLower) {
    return binLower == QStringLiteral("env") ||
           binLower == QStringLiteral("prime-run") ||
           binLower == QStringLiteral("gamemoderun") ||
           binLower == QStringLiteral("mangohud") ||
           binLower == QStringLiteral("obs-gamecapture") ||
           binLower == QStringLiteral("nice") ||
           binLower == QStringLiteral("ionice") ||
           binLower == QStringLiteral("dbus-launch");
}

bool isGenericBinary(const QString& binLower) {
    static const QSet<QString> kGeneric = {
        QStringLiteral("sh"),
        QStringLiteral("bash"),
        QStringLiteral("zsh"),
        QStringLiteral("fish"),
        QStringLiteral("dash"),
        QStringLiteral("python"),
        QStringLiteral("python3"),
        QStringLiteral("perl"),
        QStringLiteral("ruby"),
        QStringLiteral("node"),
        QStringLiteral("java"),
        QStringLiteral("javaw"),
        QStringLiteral("env"),
        QStringLiteral("flatpak"),
        QStringLiteral("xdg-open"),
        QStringLiteral("dbus-daemon"),
        QStringLiteral("systemd"),
        QStringLiteral("sudo"),
        QStringLiteral("pkexec"),
        QStringLiteral("su"),
    };
    return kGeneric.contains(binLower);
}

void parseExecLine(const QString& execLine, QString& outExecBin, QString& outFlatpakId) {
    const QStringList tokens = splitExecTokens(execLine);
    int idx = 0;
    while (idx < tokens.size()) {
        const QString& tok = tokens[idx];
        if (tok.contains(QLatin1Char('=')) && !tok.startsWith(QLatin1Char('/'))) {
            ++idx;
            continue;
        }
        const QString base = QFileInfo(tok).fileName().toLower();
        if (isWrapperCommand(base)) {
            ++idx;
            continue;
        }
        if (base == QStringLiteral("flatpak")) {
            for (int j = idx + 1; j < tokens.size(); ++j) {
                const QString& arg = tokens[j];
                if (arg == QStringLiteral("run") || arg.startsWith(QLatin1Char('-'))) {
                    continue;
                }
                if (!arg.startsWith(QLatin1Char('%'))) {
                    outFlatpakId = arg;
                    outExecBin = base;
                    return;
                }
            }
        }
        outExecBin = QFileInfo(tok).fileName();
        return;
    }
}

} // namespace

DesktopEntryIndex::DesktopEntryIndex() {
    reload();
}

void DesktopEntryIndex::reload() {
    m_entries.clear();
    m_byDesktopIdLower.clear();
    m_byFlatpakIdLower.clear();
    m_byExecLower.clear();
    m_byWmClassLower.clear();
    m_anyIconByBinLower.clear();

    const QString home = QDir::homePath();
    const QStringList dirs = {
        QStringLiteral("/usr/share/applications"),
        QStringLiteral("/usr/local/share/applications"),
        QStringLiteral("/var/lib/flatpak/exports/share/applications"),
        home + QStringLiteral("/.local/share/flatpak/exports/share/applications"),
        home + QStringLiteral("/.local/share/applications"),
    };

    for (const QString& dir : dirs) {
        scanDirectory(dir);
    }
}

void DesktopEntryIndex::scanDirectory(const QString& rootDir) {
    std::error_code ec;
    const std::filesystem::path rootPath(rootDir.toStdString());
    if (!std::filesystem::exists(rootPath, ec)) {
        return;
    }

    std::error_code iterEc;
    std::filesystem::recursive_directory_iterator it(
        rootPath, std::filesystem::directory_options::skip_permission_denied, iterEc);
    const std::filesystem::recursive_directory_iterator end;
    for (; !iterEc && it != end; it.increment(iterEc)) {
        std::error_code entryEc;
        const auto& entry = *it;
        if (!entry.is_regular_file(entryEc) || entryEc) {
            continue;
        }
        const std::filesystem::path p = entry.path();
        if (p.extension() != ".desktop") {
            continue;
        }

        const std::filesystem::path rel = std::filesystem::relative(p, rootPath, entryEc);
        if (entryEc) {
            continue;
        }
        QString desktopId = QString::fromStdString(rel.string());
        desktopId.replace(QLatin1Char('/'), QLatin1Char('-'));
        if (desktopId.endsWith(QStringLiteral(".desktop"), Qt::CaseInsensitive)) {
            desktopId.chop(8);
        }

        QFile file(QString::fromStdString(p.string()));
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            continue;
        }

        QTextStream in(&file);
        bool inDesktopEntry = false;
        QString typeStr;
        QString nameDefault;
        QString nameTr;
        QString iconName;
        QString execLine;
        QString wmClass;
        bool hidden = false;
        bool noDisplay = false;

        while (!in.atEnd()) {
            const QString line = in.readLine().trimmed();
            if (line.isEmpty() || line.startsWith(QLatin1Char('#'))) {
                continue;
            }
            if (line.startsWith(QLatin1Char('['))) {
                if (inDesktopEntry) {
                    break;
                }
                inDesktopEntry = (line == QStringLiteral("[Desktop Entry]"));
                continue;
            }
            if (!inDesktopEntry) {
                continue;
            }

            const int eqPos = line.indexOf(QLatin1Char('='));
            if (eqPos <= 0) {
                continue;
            }
            const QString key = line.left(eqPos).trimmed();
            const QString val = line.mid(eqPos + 1).trimmed();

            if (key == QStringLiteral("Type")) {
                typeStr = val;
            } else if (key == QStringLiteral("Name")) {
                nameDefault = val;
            } else if (key == QStringLiteral("Name[tr]") || key == QStringLiteral("Name[tr_TR]")) {
                if (nameTr.isEmpty() || key == QStringLiteral("Name[tr]")) {
                    nameTr = val;
                }
            } else if (key == QStringLiteral("Icon")) {
                iconName = val;
            } else if (key == QStringLiteral("Exec")) {
                execLine = val;
            } else if (key == QStringLiteral("StartupWMClass")) {
                wmClass = val;
            } else if (key == QStringLiteral("Hidden")) {
                hidden = (val.compare(QStringLiteral("true"), Qt::CaseInsensitive) == 0);
            } else if (key == QStringLiteral("NoDisplay")) {
                noDisplay = (val.compare(QStringLiteral("true"), Qt::CaseInsensitive) == 0);
            }
        }

        if (hidden || typeStr != QStringLiteral("Application")) {
            continue;
        }

        DesktopAppMeta meta;
        meta.desktopId = desktopId;
        meta.name = !nameTr.isEmpty() ? nameTr : (!nameDefault.isEmpty() ? nameDefault : desktopId);
        meta.iconName = iconName;
        meta.startupWmClass = wmClass;
        meta.noDisplay = noDisplay;
        parseExecLine(execLine, meta.execBinary, meta.flatpakId);

        const int idx = static_cast<int>(m_entries.size());
        m_entries.push_back(meta);

        m_byDesktopIdLower.insert(meta.desktopId.toLower(), idx);
        if (!meta.flatpakId.isEmpty()) {
            m_byFlatpakIdLower.insert(meta.flatpakId.toLower(), idx);
        }
        const QString execLower = meta.execBinary.toLower();
        const QString wmLower = meta.startupWmClass.toLower();
        if (!meta.iconName.isEmpty()) {
            if (!execLower.isEmpty() && !isGenericBinary(execLower)) {
                m_anyIconByBinLower.insert(execLower, meta.iconName);
            }
            if (!wmLower.isEmpty() && !isGenericBinary(wmLower)) {
                m_anyIconByBinLower.insert(wmLower, meta.iconName);
            }
            m_anyIconByBinLower.insert(meta.desktopId.toLower(), meta.iconName);
        }
        if (!noDisplay) {
            if (!execLower.isEmpty() && !isGenericBinary(execLower)) {
                m_byExecLower.insert(execLower, idx);
            }
            if (!wmLower.isEmpty() && !isGenericBinary(wmLower)) {
                m_byWmClassLower.insert(wmLower, idx);
            }
        }
    }
}

const DesktopAppMeta* DesktopEntryIndex::findByDesktopId(const QString& id) const {
    QString key = id.trimmed().toLower();
    if (key.endsWith(QStringLiteral(".desktop"))) {
        key.chop(8);
    }
    if (const auto it = m_byDesktopIdLower.find(key); it != m_byDesktopIdLower.end()) {
        return &m_entries[*it];
    }
    if (const auto it = m_byFlatpakIdLower.find(key); it != m_byFlatpakIdLower.end()) {
        return &m_entries[*it];
    }
    return nullptr;
}

const DesktopAppMeta* DesktopEntryIndex::findByProcess(const QString& comm,
                                                       const QString& exeBasename,
                                                       const QString& cmdline) const {
    const QString exeLower = exeBasename.toLower();
    const QString commLower = comm.toLower();

    if (!exeLower.isEmpty() && !isGenericBinary(exeLower)) {
        if (const auto it = m_byExecLower.find(exeLower); it != m_byExecLower.end()) {
            return &m_entries[*it];
        }
        if (const auto it = m_byWmClassLower.find(exeLower); it != m_byWmClassLower.end()) {
            return &m_entries[*it];
        }
    }

    if (!commLower.isEmpty() && !isGenericBinary(commLower)) {
        if (const auto it = m_byExecLower.find(commLower); it != m_byExecLower.end()) {
            return &m_entries[*it];
        }
        if (const auto it = m_byWmClassLower.find(commLower); it != m_byWmClassLower.end()) {
            return &m_entries[*it];
        }
    }

    if (!cmdline.isEmpty() && (exeLower == QStringLiteral("flatpak") ||
                               cmdline.contains(QStringLiteral("/app/"), Qt::CaseInsensitive))) {
        for (auto it = m_byFlatpakIdLower.cbegin(); it != m_byFlatpakIdLower.cend(); ++it) {
            if (cmdline.contains(it.key(), Qt::CaseInsensitive)) {
                return &m_entries[*it];
            }
        }
    }

    return nullptr;
}

QString DesktopEntryIndex::findIconByProcessName(const QString& comm,
                                                 const QString& exeBasename) const {
    const QString exeLower = exeBasename.toLower();
    if (!exeLower.isEmpty()) {
        if (const auto it = m_anyIconByBinLower.constFind(exeLower);
            it != m_anyIconByBinLower.constEnd()) {
            return *it;
        }
    }
    const QString commLower = comm.toLower();
    if (!commLower.isEmpty()) {
        if (const auto it = m_anyIconByBinLower.constFind(commLower);
            it != m_anyIconByBinLower.constEnd()) {
            return *it;
        }
    }
    return {};
}

} // namespace sysmon
