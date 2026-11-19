#pragma once

#include "core/TelemetryTypes.h"

#include <QAbstractTableModel>
#include <QHash>
#include <QIcon>
#include <QVector>

namespace sysmon {

/**
 * @brief Kullanıcı masaüstü uygulamalarını artımlı güncelleyen 8 sütunlu tablo modeli.
 *        Tema simgelerini yalnızca UI iş parçacığı üzerinde QIcon::fromTheme ile çözer.
 */
class ApplicationTableModel : public QAbstractTableModel {
    Q_OBJECT

public:
    enum Column {
        ColName = 0,
        ColCpu,
        ColMemoryRss,
        ColMemoryShared,
        ColDiskIo,
        ColNetwork,
        ColGpuPercent,
        ColGpuVram,
        ColCount
    };

    explicit ApplicationTableModel(QObject* parent = nullptr);

    [[nodiscard]] int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    [[nodiscard]] int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    [[nodiscard]] QVariant headerData(int section,
                                      Qt::Orientation orientation,
                                      int role = Qt::DisplayRole) const override;

    void updateApplications(const QVector<ApplicationInfo>& incoming);
    void retranslate();
    [[nodiscard]] const ApplicationInfo* applicationAt(int row) const;

private:
    [[nodiscard]] QIcon resolveIcon(const QString& iconName) const;

    QVector<ApplicationInfo> m_rows;
    QHash<QString, int> m_idToRow;
    mutable QHash<QString, QIcon> m_iconCache;
};

} // namespace sysmon
