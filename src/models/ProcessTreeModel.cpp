#include "models/ProcessTreeModel.h"

#include "core/FormatUtils.h"
#include "core/Lang.h"

#include <QSet>

#include <algorithm>
#include <unordered_map>

namespace sysmon {

ProcessTreeModel::ProcessTreeModel(QObject* parent)
    : QAbstractItemModel(parent) {
    m_nodes.push_back(Node{}); // Görünmez kök düğüm (indeks 0)
}

QModelIndex ProcessTreeModel::index(int row, int column, const QModelIndex& parent) const {
    if (column < 0 || column >= ColCount || row < 0) {
        return {};
    }
    const int parentIdx = parent.isValid() ? static_cast<int>(parent.internalId()) : 0;
    if (parentIdx < 0 || parentIdx >= m_nodes.size()) {
        return {};
    }
    const QVector<int>& children = m_nodes[parentIdx].children;
    if (row >= children.size()) {
        return {};
    }
    return createIndex(row, column, static_cast<quintptr>(children[row]));
}

QModelIndex ProcessTreeModel::parent(const QModelIndex& child) const {
    if (!child.isValid()) {
        return {};
    }
    const int nodeIdx = static_cast<int>(child.internalId());
    if (nodeIdx <= 0 || nodeIdx >= m_nodes.size()) {
        return {};
    }
    const int parentIdx = m_nodes[nodeIdx].parentNode;
    if (parentIdx <= 0 || parentIdx >= m_nodes.size()) {
        return {};
    }
    return createIndex(m_nodes[parentIdx].rowInParent, 0, static_cast<quintptr>(parentIdx));
}

int ProcessTreeModel::rowCount(const QModelIndex& parent) const {
    if (parent.column() > 0) {
        return 0;
    }
    const int parentIdx = parent.isValid() ? static_cast<int>(parent.internalId()) : 0;
    if (parentIdx < 0 || parentIdx >= m_nodes.size()) {
        return 0;
    }
    return static_cast<int>(m_nodes[parentIdx].children.size());
}

int ProcessTreeModel::columnCount(const QModelIndex&) const {
    return ColCount;
}

QVariant ProcessTreeModel::headerData(int section,
                                      Qt::Orientation orientation,
                                      int role) const {
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole) {
        return {};
    }
    switch (section) {
    case ColName:
        return trText("Process Name", "Süreç Adı");
    case ColPid:
        return trText("PID", "PID");
    case ColUser:
        return trText("User", "Kullanıcı");
    case ColState:
        return trText("State", "Durum");
    case ColCpu:
        return trText("CPU %", "CPU %");
    case ColMemoryRss:
        return trText("Memory (RSS)", "Bellek (RSS)");
    case ColMemoryShared:
        return trText("Shared Memory", "Paylaşılan Bellek");
    case ColDiskIo:
        return trText("Disk Read/Write", "Disk Okuma/Yazma");
    case ColNetwork:
        return trText("Network", "Ağ");
    case ColGpuPercent:
        return trText("GPU %", "GPU %");
    case ColGpuMemory:
        return trText("GPU Memory", "GPU Belleği");
    default:
        return {};
    }
}

QIcon ProcessTreeModel::resolveProcessIcon(const ProcessInfo& proc) const {
    const QString key = !proc.iconName.isEmpty()
        ? proc.iconName
        : proc.name.toLower();

    if (const auto it = m_iconCache.constFind(key); it != m_iconCache.constEnd()) {
        return *it;
    }

    QIcon resolved;
    if (key.startsWith(QLatin1Char('/'))) {
        resolved = QIcon(key);
    } else if (!key.isEmpty()) {
        resolved = QIcon::fromTheme(key);
    }
    if (resolved.isNull() && !proc.name.isEmpty()) {
        resolved = QIcon::fromTheme(proc.name.toLower());
    }
    if (resolved.isNull()) {
        resolved = QIcon::fromTheme(QStringLiteral("application-x-executable"));
    }

    m_iconCache.insert(key, resolved);
    return resolved;
}

QVariant ProcessTreeModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid()) {
        return {};
    }
    const int nodeIdx = static_cast<int>(index.internalId());
    if (nodeIdx <= 0 || nodeIdx >= m_nodes.size()) {
        return {};
    }

    const ProcessInfo& p = m_nodes[nodeIdx].info;
    const int col = index.column();

    if (role == Qt::TextAlignmentRole) {
        if (col == ColName || col == ColUser || col == ColState) {
            return static_cast<int>(Qt::AlignLeft | Qt::AlignVCenter);
        }
        return static_cast<int>(Qt::AlignRight | Qt::AlignVCenter);
    }

    if (role == Qt::DecorationRole && col == ColName && m_showIcons) {
        return resolveProcessIcon(p);
    }

    if (role == Qt::DisplayRole) {
        switch (col) {
        case ColName:
            return p.name;
        case ColPid:
            return p.pid;
        case ColUser:
            return p.user;
        case ColState:
            return processStateText(p.stateChar);
        case ColCpu:
            return fmt::formatPercent(p.cpuPercent);
        case ColMemoryRss:
            return fmt::formatBytes(p.rssBytes);
        case ColMemoryShared:
            return fmt::formatBytes(p.sharedBytes);
        case ColDiskIo:
            return p.ioReadable
                ? fmt::formatRate(p.diskReadBytesPerSec + p.diskWriteBytesPerSec)
                : QStringLiteral("—");
        case ColNetwork:
            return p.netReadable
                ? fmt::formatRate(p.netRxBytesPerSec + p.netTxBytesPerSec)
                : QStringLiteral("—");
        case ColGpuPercent:
            return p.hasGpuMetrics ? fmt::formatPercent(p.gpuPercent) : QStringLiteral("—");
        case ColGpuMemory:
            return p.hasGpuMetrics ? fmt::formatBytes(p.gpuMemBytes) : QStringLiteral("—");
        default:
            return {};
        }
    }

    if (role == Qt::ToolTipRole) {
        switch (col) {
        case ColName:
            return QStringLiteral("%1\nPID: %2 · PPID: %3 · %4: %5 · Nice: %6")
                .arg(p.cmdline)
                .arg(p.pid)
                .arg(p.ppid)
                .arg(trText("Threads", "İş parçacığı"))
                .arg(p.threads)
                .arg(p.niceValue);
        case ColDiskIo:
            if (!p.ioReadable) {
                return trText("Permission denied (EACCES): Cannot read /proc/[pid]/io.",
                              "Yetki sınırı (EACCES): Başka kullanıcıya ait /proc/[pid]/io okunamadı.");
            }
            return QStringLiteral("%1: %2\n%3: %4")
                .arg(trText("Read", "Okuma"),
                     fmt::formatRate(p.diskReadBytesPerSec),
                     trText("Write", "Yazma"),
                     fmt::formatRate(p.diskWriteBytesPerSec));
        case ColNetwork:
            if (!p.netReadable) {
                return trText("Permission denied (EACCES): Cannot inspect /proc/[pid]/fd.",
                              "Yetki sınırı (EACCES): /proc/[pid]/fd soketleri okunamadı.");
            }
            return QStringLiteral("%1: %2\n%3: %4\n%5: %6\n%7")
                .arg(trText("Download (Rx)", "İndirme (Rx)"),
                     fmt::formatRate(p.netRxBytesPerSec),
                     trText("Upload (Tx)", "Yükleme (Tx)"),
                     fmt::formatRate(p.netTxBytesPerSec),
                     trText("Active TCP sockets", "Aktif TCP soket"))
                .arg(p.tcpSocketCount)
                .arg(trText("(Rootless NETLINK_SOCK_DIAG TCP accounting)",
                            "(Kök yetkisiz NETLINK_SOCK_DIAG TCP ölçümü)"));
        case ColGpuPercent:
        case ColGpuMemory:
            if (!p.hasGpuMetrics) {
                return trText("No active DRM handle or NVML session for this process.",
                              "Bu süreç için etkin GPU/DRM tanıtıcısı veya NVML oturumu yok.");
            }
            return QStringLiteral("%1: %2\n%3: %4")
                .arg(trText("GPU Utilization", "GPU Kullanımı"),
                     fmt::formatPercent(p.gpuPercent),
                     trText("GPU Memory", "GPU Belleği"),
                     fmt::formatBytes(p.gpuMemBytes));
        default:
            return {};
        }
    }

    if (role == Qt::UserRole) {
        switch (col) {
        case ColName:
            return p.name.toLower();
        case ColPid:
            return p.pid;
        case ColUser:
            return p.user.toLower();
        case ColState:
            return processStateText(p.stateChar).toLower();
        case ColCpu:
            return p.cpuPercent;
        case ColMemoryRss:
            return static_cast<qulonglong>(p.rssBytes);
        case ColMemoryShared:
            return static_cast<qulonglong>(p.sharedBytes);
        case ColDiskIo:
            return p.ioReadable ? (p.diskReadBytesPerSec + p.diskWriteBytesPerSec) : -1.0;
        case ColNetwork:
            return p.netReadable ? (p.netRxBytesPerSec + p.netTxBytesPerSec) : -1.0;
        case ColGpuPercent:
            return p.hasGpuMetrics ? p.gpuPercent : -1.0;
        case ColGpuMemory:
            return static_cast<qulonglong>(p.hasGpuMetrics ? p.gpuMemBytes : 0ULL);
        default:
            return {};
        }
    }

    return {};
}

void ProcessTreeModel::setTreeMode(bool enabled) {
    if (m_treeMode == enabled) {
        return;
    }
    m_treeMode = enabled;
    rebuildTree(m_lastProcesses);
}

void ProcessTreeModel::setShowIcons(bool enabled) {
    if (m_showIcons == enabled) {
        return;
    }
    m_showIcons = enabled;
    emitDataChangedRecursive(0, QModelIndex());
}

void ProcessTreeModel::retranslate() {
    emit headerDataChanged(Qt::Horizontal, 0, ColCount - 1);
    emitDataChangedRecursive(0, QModelIndex());
}

void ProcessTreeModel::emitDataChangedRecursive(int parentNodeIdx, const QModelIndex& parentIdx) {
    if (parentNodeIdx < 0 || parentNodeIdx >= m_nodes.size()) {
        return;
    }
    const QVector<int>& children = m_nodes[parentNodeIdx].children;
    if (children.isEmpty()) {
        return;
    }
    emit dataChanged(index(0, 0, parentIdx),
                     index(static_cast<int>(children.size()) - 1, ColCount - 1, parentIdx));
    for (int r = 0; r < children.size(); ++r) {
        const int childNodeIdx = children[r];
        if (!m_nodes[childNodeIdx].children.isEmpty()) {
            emitDataChangedRecursive(childNodeIdx, index(r, 0, parentIdx));
        }
    }
}

void ProcessTreeModel::updateProcesses(const QVector<ProcessInfo>& incoming) {
    m_lastProcesses = incoming;

    // Hızlı yol: PID seti ve ebeveyn topolojisi birebir aynıysa yalnızca veri güncelle
    if (incoming.size() == (m_nodes.size() - 1)) {
        QSet<int> incomingPids;
        incomingPids.reserve(incoming.size());
        for (const auto& p : incoming) {
            incomingPids.insert(p.pid);
        }

        bool topologyMatch = (incomingPids.size() == incoming.size());
        if (topologyMatch) {
            for (const auto& p : incoming) {
                const auto it = m_pidToNode.constFind(p.pid);
                if (it == m_pidToNode.constEnd()) {
                    topologyMatch = false;
                    break;
                }
                const int nodeIdx = *it;
                const int expectedParentPid =
                    (m_treeMode && incomingPids.contains(p.ppid) && p.ppid != p.pid && p.ppid > 0)
                    ? p.ppid
                    : 0;
                const int curParentNode = m_nodes[nodeIdx].parentNode;
                const int curParentPid = (curParentNode > 0) ? m_nodes[curParentNode].info.pid : 0;
                if (expectedParentPid != curParentPid) {
                    topologyMatch = false;
                    break;
                }
            }
        }

        if (topologyMatch) {
            for (const auto& p : incoming) {
                const int nodeIdx = m_pidToNode.value(p.pid);
                m_nodes[nodeIdx].info = p;
            }
            emitDataChangedRecursive(0, QModelIndex());
            return;
        }
    }

    rebuildTree(incoming);
}

void ProcessTreeModel::rebuildTree(const QVector<ProcessInfo>& processes) {
    emit aboutToRebuild();
    beginResetModel();

    m_nodes.clear();
    m_pidToNode.clear();
    m_nodes.push_back(Node{}); // Görünmez kök

    m_nodes.reserve(processes.size() + 1);
    m_pidToNode.reserve(processes.size());

    for (const auto& p : processes) {
        Node n;
        n.info = p;
        const int idx = static_cast<int>(m_nodes.size());
        m_pidToNode.insert(p.pid, idx);
        m_nodes.push_back(std::move(n));
    }

    for (int idx = 1; idx < m_nodes.size(); ++idx) {
        int parentNodeIdx = 0;
        if (m_treeMode) {
            const int ppid = m_nodes[idx].info.ppid;
            if (ppid > 0 && ppid != m_nodes[idx].info.pid) {
                if (const auto it = m_pidToNode.constFind(ppid); it != m_pidToNode.constEnd()) {
                    // Döngü koruması
                    int probe = *it;
                    bool cycle = false;
                    for (int depth = 0; depth < 64 && probe > 0; ++depth) {
                        if (probe == idx) {
                            cycle = true;
                            break;
                        }
                        const int nextPpid = m_nodes[probe].info.ppid;
                        probe = (nextPpid > 0 && m_pidToNode.contains(nextPpid))
                            ? m_pidToNode.value(nextPpid)
                            : 0;
                    }
                    if (!cycle) {
                        parentNodeIdx = *it;
                    }
                }
            }
        }
        m_nodes[idx].parentNode = parentNodeIdx;
        m_nodes[idx].rowInParent = static_cast<int>(m_nodes[parentNodeIdx].children.size());
        m_nodes[parentNodeIdx].children.push_back(idx);
    }

    endResetModel();
    emit rebuilt();
}

const ProcessInfo* ProcessTreeModel::processForIndex(const QModelIndex& index) const {
    if (!index.isValid()) {
        return nullptr;
    }
    const int nodeIdx = static_cast<int>(index.internalId());
    if (nodeIdx <= 0 || nodeIdx >= m_nodes.size()) {
        return nullptr;
    }
    return &m_nodes[nodeIdx].info;
}

QModelIndex ProcessTreeModel::indexForPid(int pid) const {
    const auto it = m_pidToNode.constFind(pid);
    if (it == m_pidToNode.constEnd()) {
        return {};
    }
    const int nodeIdx = *it;
    if (nodeIdx <= 0 || nodeIdx >= m_nodes.size()) {
        return {};
    }
    return createIndex(m_nodes[nodeIdx].rowInParent, 0, static_cast<quintptr>(nodeIdx));
}

} // namespace sysmon
