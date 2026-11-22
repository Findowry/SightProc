#include "models/ApplicationTableModel.h"

#include "core/FormatUtils.h"
#include "core/Lang.h"

#include <QSet>

namespace sysmon {

ApplicationTableModel::ApplicationTableModel(QObject* parent)
    : QAbstractTableModel(parent) {}

int ApplicationTableModel::rowCount(const QModelIndex& parent) const {
    if (parent.isValid()) {
        return 0;
    }
    return static_cast<int>(m_rows.size());
}

int ApplicationTableModel::columnCount(const QModelIndex& parent) const {
    if (parent.isValid()) {
        return 0;
    }
    return ColCount;
}

QVariant ApplicationTableModel::headerData(int section,
                                           Qt::Orientation orientation,
                                           int role) const {
    if (orientation != Qt::Horizontal) {
        return {};
    }
    if (role == Qt::ToolTipRole && section == ColNetwork) {
        return trText("Measures TCP traffic only via rootless NETLINK_SOCK_DIAG (excludes UDP/QUIC).",
                      "Root yetkisi olmadan NETLINK_SOCK_DIAG üzerinden yalnızca TCP trafiği ölçülür (UDP/QUIC hariç).");
    }
    if (role != Qt::DisplayRole) {
        return {};
    }
    switch (section) {
    case ColName:
        return trText("Application", "Uygulama");
    case ColCpu:
        return trText("CPU %", "CPU %");
    case ColMemoryRss:
        return trText("Memory (RAM)", "Bellek (RAM)");
    case ColMemoryShared:
        return trText("Shared Memory", "Paylaşılan Bellek");
    case ColDiskIo:
        return trText("Disk I/O", "Disk G/Ç");
    case ColNetwork:
        return trText("TCP Network", "TCP Ağı");
    case ColGpuPercent:
        return trText("GPU %", "GPU %");
    case ColGpuVram:
        return trText("VRAM", "VRAM");
    default:
        return {};
    }
}

QIcon ApplicationTableModel::resolveIcon(const QString& iconName) const {
    const QString key = iconName.isEmpty()
        ? QStringLiteral("application-x-executable")
        : iconName;

    if (const auto it = m_iconCache.constFind(key); it != m_iconCache.constEnd()) {
        return *it;
    }

    QIcon resolved;
    if (key.startsWith(QLatin1Char('/'))) {
        resolved = QIcon(key);
    } else {
        resolved = QIcon::fromTheme(key);
    }
    if (resolved.isNull()) {
        resolved = QIcon::fromTheme(QStringLiteral("application-x-executable"));
    }
    m_iconCache.insert(key, resolved);
    return resolved;
}

QVariant ApplicationTableModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= m_rows.size()) {
        return {};
    }

    const ApplicationInfo& app = m_rows[index.row()];
    const int col = index.column();

    if (role == Qt::TextAlignmentRole) {
        return (col == ColName)
            ? static_cast<int>(Qt::AlignLeft | Qt::AlignVCenter)
            : static_cast<int>(Qt::AlignRight | Qt::AlignVCenter);
    }

    if (role == Qt::DecorationRole && col == ColName) {
        return resolveIcon(app.iconName);
    }

    if (role == Qt::DisplayRole) {
        switch (col) {
        case ColName:
            return (app.pids.size() > 1)
                ? QStringLiteral("%1 (%2)").arg(app.name).arg(app.pids.size())
                : app.name;
        case ColCpu:
            return fmt::formatPercent(app.cpuPercent);
        case ColMemoryRss:
            return fmt::formatBytes(app.rssBytes);
        case ColMemoryShared:
            return fmt::formatBytes(app.sharedBytes);
        case ColDiskIo:
            return app.ioReadable
                ? fmt::formatRate(app.diskReadBytesPerSec + app.diskWriteBytesPerSec)
                : QStringLiteral("—");
        case ColNetwork:
            return app.netReadable
                ? fmt::formatRate(app.netRxBytesPerSec + app.netTxBytesPerSec)
                : QStringLiteral("—");
        case ColGpuPercent:
            return app.hasGpuMetrics ? fmt::formatPercent(app.gpuPercent) : QStringLiteral("—");
        case ColGpuVram:
            return app.hasGpuMetrics ? fmt::formatBytes(app.gpuMemBytes) : QStringLiteral("—");
        default:
            return {};
        }
    }

    if (role == Qt::ToolTipRole) {
        switch (col) {
        case ColName:
            return QStringLiteral("%1\n%2: %3%4")
                .arg(app.name, trText("Process count", "Süreç sayısı"))
                .arg(app.pids.size())
                .arg(app.desktopId.isEmpty()
                         ? QString{}
                         : QStringLiteral("\n%1: %2")
                               .arg(trText("Desktop ID", "Masaüstü kimliği"), app.desktopId));
        case ColDiskIo:
            return app.ioReadable
                ? QStringLiteral("%1: %2\n%3: %4")
                      .arg(trText("Read", "Okuma"),
                           fmt::formatRate(app.diskReadBytesPerSec),
                           trText("Write", "Yazma"),
                           fmt::formatRate(app.diskWriteBytesPerSec))
                : trText("Disk I/O unreadable", "Disk G/Ç okunamadı");
        case ColNetwork:
            return app.netReadable
                ? QStringLiteral("%1: %2\n%3: %4")
                      .arg(trText("Download (Rx)", "İndirme (Rx)"),
                           fmt::formatRate(app.netRxBytesPerSec),
                           trText("Upload (Tx)", "Yükleme (Tx)"),
                           fmt::formatRate(app.netTxBytesPerSec))
                : trText("Network sockets unreadable", "Ağ soketi okunamadı");
        default:
            return {};
        }
    }

    if (role == Qt::UserRole) {
        switch (col) {
        case ColName:
            return app.name.toLower();
        case ColCpu:
            return app.cpuPercent;
        case ColMemoryRss:
            return static_cast<qulonglong>(app.rssBytes);
        case ColMemoryShared:
            return static_cast<qulonglong>(app.sharedBytes);
        case ColDiskIo:
            return app.ioReadable
                ? (app.diskReadBytesPerSec + app.diskWriteBytesPerSec)
                : -1.0;
        case ColNetwork:
            return app.netReadable
                ? (app.netRxBytesPerSec + app.netTxBytesPerSec)
                : -1.0;
        case ColGpuPercent:
            return app.hasGpuMetrics ? app.gpuPercent : -1.0;
        case ColGpuVram:
            return static_cast<qulonglong>(app.hasGpuMetrics ? app.gpuMemBytes : 0ULL);
        default:
            return {};
        }
    }

    return {};
}

void ApplicationTableModel::updateApplications(const QVector<ApplicationInfo>& incoming) {
    QSet<QString> incomingIds;
    incomingIds.reserve(incoming.size());
    for (const ApplicationInfo& app : incoming) {
        incomingIds.insert(app.id);
    }

    for (int row = static_cast<int>(m_rows.size()) - 1; row >= 0; --row) {
        if (!incomingIds.contains(m_rows[row].id)) {
            beginRemoveRows(QModelIndex(), row, row);
            m_rows.removeAt(row);
            endRemoveRows();
        }
    }

    m_idToRow.clear();
    m_idToRow.reserve(m_rows.size());
    for (int row = 0; row < m_rows.size(); ++row) {
        m_idToRow.insert(m_rows[row].id, row);
    }

    const int survivingCount = static_cast<int>(m_rows.size());
    QVector<ApplicationInfo> toInsert;

    for (const ApplicationInfo& app : incoming) {
        const auto it = m_idToRow.constFind(app.id);
        if (it != m_idToRow.constEnd()) {
            m_rows[*it] = app;
        } else {
            toInsert.push_back(app);
        }
    }

    if (survivingCount > 0) {
        emit dataChanged(index(0, 0), index(survivingCount - 1, ColCount - 1));
    }

    if (!toInsert.isEmpty()) {
        const int first = static_cast<int>(m_rows.size());
        const int last = first + static_cast<int>(toInsert.size()) - 1;
        beginInsertRows(QModelIndex(), first, last);
        for (const ApplicationInfo& app : toInsert) {
            const int row = static_cast<int>(m_rows.size());
            m_idToRow.insert(app.id, row);
            m_rows.push_back(app);
        }
        endInsertRows();
    }
}

void ApplicationTableModel::retranslate() {
    emit headerDataChanged(Qt::Horizontal, 0, ColCount - 1);
}

const ApplicationInfo* ApplicationTableModel::applicationAt(int row) const {
    if (row < 0 || row >= m_rows.size()) {
        return nullptr;
    }
    return &m_rows[row];
}

} // namespace sysmon
