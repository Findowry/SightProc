#pragma once

#include <QHash>
#include <QString>
#include <QVector>

namespace sysmon {

struct DesktopAppMeta {
    QString desktopId;
    QString name;
    QString iconName;
    QString execBinary;
    QString startupWmClass;
    QString flatpakId;
    bool noDisplay{false};
};

/**
 * @brief Sistem ve kullanıcı .desktop dosyalarını önbelleğe alan salt veri dizini.
 *        İş parçacığı güvenliği için yalnızca iconName (QString) saklar, QIcon üretmez.
 */
class DesktopEntryIndex {
public:
    DesktopEntryIndex();

    void reload();

    [[nodiscard]] int entryCount() const noexcept { return static_cast<int>(m_entries.size()); }
    [[nodiscard]] const DesktopAppMeta* findByDesktopId(const QString& id) const;
    [[nodiscard]] const DesktopAppMeta* findByProcess(const QString& comm,
                                                      const QString& exeBasename,
                                                      const QString& cmdline) const;
    [[nodiscard]] QString findIconByProcessName(const QString& comm,
                                                const QString& exeBasename) const;

private:
    void scanDirectory(const QString& rootDir);

    QVector<DesktopAppMeta> m_entries;
    QHash<QString, int> m_byDesktopIdLower;
    QHash<QString, int> m_byFlatpakIdLower;
    QHash<QString, int> m_byExecLower;
    QHash<QString, int> m_byWmClassLower;
    QHash<QString, QString> m_anyIconByBinLower;
};

} // namespace sysmon
