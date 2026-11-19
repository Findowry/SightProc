#pragma once

#include "core/AppSettings.h"
#include "core/RingBuffer.h"
#include "core/TelemetryTypes.h"

#include <QAbstractListModel>
#include <QHash>
#include <QIcon>
#include <QString>
#include <QVector>

namespace sysmon {

enum class DeviceKind {
    Cpu,
    Memory,
    Disk,
    Network,
    Gpu,
    Fan,
    Battery,
    ThermalOverview
};

struct DeviceEntry {
    QString key;
    DeviceKind kind{DeviceKind::Cpu};
    AppSettings::HardwareCategory category{AppSettings::HardwareCategory::Cpu};
    int deviceIndex{0};
    QString title;
    QString subtitle;
    QIcon icon;
    RingBuffer<double, 64> primaryHistory;
    RingBuffer<double, 64> secondaryHistory;
    QHash<QString, RingBuffer<double, 64>> extraHistories;
    bool autoScale{false};
    double fixedMax{100.0};
};

/**
 * @brief Performans sekmesinin sol panelindeki dinamik donanım cihaz listesi modeli.
 */
class DeviceListModel : public QAbstractListModel {
    Q_OBJECT

public:
    explicit DeviceListModel(QObject* parent = nullptr);

    [[nodiscard]] int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;

    void updateFromSnapshot(const SystemTelemetrySnapshot& snapshot);
    void retranslate();

    [[nodiscard]] const DeviceEntry* entryAt(int row) const;
    [[nodiscard]] int rowForKey(const QString& key) const;

private:
    QVector<DeviceEntry> m_entries;
    SystemTelemetrySnapshot m_lastSnapshot;
    bool m_hasSnapshot{false};
};

} // namespace sysmon
