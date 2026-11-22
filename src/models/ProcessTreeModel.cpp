#include "models/ProcessTreeModel.h"

#include "core/FormatUtils.h"
#include "core/Lang.h"

#include <QSet>

#include <algorithm>
#include <unordered_map>
#include <utility>

namespace sysmon {

ProcessTreeModel::ProcessTreeModel(QObject* parent)
    : QAbstractItemModel(parent)
    , m_root(std::make_unique<Node>()) {}

ProcessTreeModel::Node* ProcessTreeModel::nodeFromIndex(const QModelIndex& index) const noexcept {
    if (!index.isValid()) {
        return nullptr;
    }
    return static_cast<Node*>(index.internalPointer());
}

QModelIndex ProcessTreeModel::indexForNode(Node* node, int column) const {
    if (!node || node == m_root.get() || column < 0 || column >= ColCount) {
        return {};
    }
    return createIndex(node->rowInParent, column, node);
}

QModelIndex ProcessTreeModel::index(int row, int column, const QModelIndex& parent) const {
    if (column < 0 || column >= ColCount || row < 0) {
        return {};
    }
    if (parent.isValid() && parent.column() > 0) {
        return {};
    }
    Node* parentNode = parent.isValid() ? nodeFromIndex(parent) : m_root.get();
    if (!parentNode || static_cast<std::size_t>(row) >= parentNode->children.size()) {
        return {};
    }
    return createIndex(row, column, parentNode->children[static_cast<std::size_t>(row)].get());
}

QModelIndex ProcessTreeModel::parent(const QModelIndex& child) const {
    if (!child.isValid()) {
        return {};
    }
    Node* childNode = nodeFromIndex(child);
    if (!childNode || !childNode->parent || childNode->parent == m_root.get()) {
        return {};
    }
    return createIndex(childNode->parent->rowInParent, 0, childNode->parent);
}

int ProcessTreeModel::rowCount(const QModelIndex& parent) const {
    if (parent.column() > 0) {
        return 0;
    }
    Node* parentNode = parent.isValid() ? nodeFromIndex(parent) : m_root.get();
    return parentNode ? static_cast<int>(parentNode->children.size()) : 0;
}

int ProcessTreeModel::columnCount(const QModelIndex&) const {
    return ColCount;
}

QVariant ProcessTreeModel::headerData(int section,
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
        return trText("TCP Network", "TCP Ağı");
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
    Node* node = nodeFromIndex(index);
    if (!node || node == m_root.get()) {
        return {};
    }

    const ProcessInfo& p = node->info;
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
                .arg(trText("(TCP only via rootless NETLINK_SOCK_DIAG; excludes UDP/QUIC)",
                            "(Root yetkisiz NETLINK_SOCK_DIAG üzerinden yalnızca TCP; UDP/QUIC hariç)"));
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
    emitDataChangedRecursive(m_root.get(), QModelIndex());
}

void ProcessTreeModel::retranslate() {
    emit headerDataChanged(Qt::Horizontal, 0, ColCount - 1);
    emitDataChangedRecursive(m_root.get(), QModelIndex());
}

void ProcessTreeModel::emitDataChangedRecursive(Node* parentNode, const QModelIndex& parentIdx) {
    if (!parentNode || parentNode->children.empty()) {
        return;
    }
    const int lastRow = static_cast<int>(parentNode->children.size()) - 1;
    emit dataChanged(index(0, 0, parentIdx), index(lastRow, ColCount - 1, parentIdx));
    for (int r = 0; r <= lastRow; ++r) {
        Node* childNode = parentNode->children[static_cast<std::size_t>(r)].get();
        if (!childNode->children.empty()) {
            emitDataChangedRecursive(childNode, index(r, 0, parentIdx));
        }
    }
}

void ProcessTreeModel::eraseSubtreeFromMap(Node* node) {
    if (!node) {
        return;
    }
    for (const auto& child : node->children) {
        eraseSubtreeFromMap(child.get());
    }
    if (node != m_root.get()) {
        m_pidToNode.remove(node->info.pid);
    }
}

void ProcessTreeModel::updateProcesses(const QVector<ProcessInfo>& incoming) {
    m_lastProcesses = incoming;

    if (m_pidToNode.isEmpty()) {
        rebuildTree(incoming);
        return;
    }

    std::unordered_map<int, const ProcessInfo*> incomingByPid;
    incomingByPid.reserve(static_cast<std::size_t>(incoming.size()));
    for (const ProcessInfo& p : incoming) {
        if (p.pid <= 0 || !incomingByPid.emplace(p.pid, &p).second) {
            rebuildTree(incoming);
            return;
        }
    }

    auto computeDesiredParentPid = [&](const ProcessInfo& p) -> int {
        if (!m_treeMode || p.ppid <= 0 || p.ppid == p.pid) {
            return 0;
        }
        if (incomingByPid.find(p.ppid) == incomingByPid.end()) {
            return 0;
        }
        int probe = p.ppid;
        for (int depth = 0; depth < 64 && probe > 0; ++depth) {
            if (probe == p.pid) {
                return 0;
            }
            const auto it = incomingByPid.find(probe);
            if (it == incomingByPid.end()) {
                break;
            }
            const int nextPpid = it->second->ppid;
            probe = (nextPpid > 0 && nextPpid != probe &&
                     incomingByPid.find(nextPpid) != incomingByPid.end())
                ? nextPpid
                : 0;
        }
        return p.ppid;
    };

    std::unordered_map<int, int> desiredParentMap;
    desiredParentMap.reserve(incomingByPid.size());
    for (const ProcessInfo& p : incoming) {
        desiredParentMap.emplace(p.pid, computeDesiredParentPid(p));
    }

    // Artımlı güvenlik kontrolü: PID yeniden kullanımı veya yaşayan sürecin ebeveyn değişimi
    // varsa QSortFilterProxyModel tutarlılığı için güvenli tam sıfırlamaya düş.
    for (auto it = m_pidToNode.constBegin(); it != m_pidToNode.constEnd(); ++it) {
        const int pid = it.key();
        Node* node = it.value();
        const auto inIt = incomingByPid.find(pid);
        if (inIt == incomingByPid.end()) {
            continue;
        }
        const ProcessInfo* inc = inIt->second;
        if (node->info.startTimeTicks != 0 && inc->startTimeTicks != 0 &&
            node->info.startTimeTicks != inc->startTimeTicks) {
            rebuildTree(incoming);
            return;
        }
        const int curParentPid = (node->parent && node->parent != m_root.get())
            ? node->parent->info.pid
            : 0;
        if (curParentPid != desiredParentMap[pid]) {
            rebuildTree(incoming);
            return;
        }
    }

    // 1. Ölen üst alt-ağaçları (ebeveyni hayatta kalan veya kök olanlar) artımlı sil.
    std::vector<Node*> deadSubtreeRoots;
    for (auto it = m_pidToNode.constBegin(); it != m_pidToNode.constEnd(); ++it) {
        Node* node = it.value();
        if (incomingByPid.find(it.key()) != incomingByPid.end()) {
            continue;
        }
        Node* p = node->parent;
        if (!p || p == m_root.get() || incomingByPid.find(p->info.pid) != incomingByPid.end()) {
            deadSubtreeRoots.push_back(node);
        }
    }

    for (Node* deadNode : deadSubtreeRoots) {
        Node* parentNode = deadNode->parent ? deadNode->parent : m_root.get();
        const int row = deadNode->rowInParent;
        if (row < 0 || static_cast<std::size_t>(row) >= parentNode->children.size() ||
            parentNode->children[static_cast<std::size_t>(row)].get() != deadNode) {
            rebuildTree(incoming);
            return;
        }
        beginRemoveRows(indexForNode(parentNode, 0), row, row);
        eraseSubtreeFromMap(deadNode);
        parentNode->children.erase(parentNode->children.begin() + row);
        for (std::size_t i = static_cast<std::size_t>(row); i < parentNode->children.size(); ++i) {
            parentNode->children[i]->rowInParent = static_cast<int>(i);
        }
        endRemoveRows();
    }

    // 2. Hayatta kalan süreç verilerini yerinde güncelle ve dataChanged yayınla.
    for (auto it = m_pidToNode.constBegin(); it != m_pidToNode.constEnd(); ++it) {
        const auto inIt = incomingByPid.find(it.key());
        if (inIt != incomingByPid.end()) {
            it.value()->info = *(inIt->second);
        }
    }
    emitDataChangedRecursive(m_root.get(), QModelIndex());

    // 3. Yeni doğan süreçleri üstten alta doğru artımlı ekle.
    QVector<const ProcessInfo*> pendingInsert;
    pendingInsert.reserve(incoming.size());
    for (const ProcessInfo& p : incoming) {
        if (!m_pidToNode.contains(p.pid)) {
            pendingInsert.push_back(&p);
        }
    }

    while (!pendingInsert.isEmpty()) {
        QHash<Node*, QVector<const ProcessInfo*>> batches;
        QVector<Node*> batchOrder;
        QVector<const ProcessInfo*> deferred;

        for (const ProcessInfo* p : pendingInsert) {
            const int desParentPid = desiredParentMap[p->pid];
            Node* parentNode = nullptr;
            if (desParentPid == 0) {
                parentNode = m_root.get();
            } else if (const auto pIt = m_pidToNode.constFind(desParentPid);
                       pIt != m_pidToNode.constEnd()) {
                parentNode = *pIt;
            }

            if (parentNode) {
                if (!batches.contains(parentNode)) {
                    batchOrder.push_back(parentNode);
                }
                batches[parentNode].push_back(p);
            } else {
                deferred.push_back(p);
            }
        }

        if (batchOrder.isEmpty()) {
            rebuildTree(incoming);
            return;
        }

        for (Node* parentNode : batchOrder) {
            const QVector<const ProcessInfo*>& batch = batches[parentNode];
            const int first = static_cast<int>(parentNode->children.size());
            const int last = first + static_cast<int>(batch.size()) - 1;
            beginInsertRows(indexForNode(parentNode, 0), first, last);
            parentNode->children.reserve(parentNode->children.size() +
                                         static_cast<std::size_t>(batch.size()));
            for (const ProcessInfo* p : batch) {
                auto node = std::make_unique<Node>();
                node->info = *p;
                node->parent = parentNode;
                node->rowInParent = static_cast<int>(parentNode->children.size());
                Node* rawPtr = node.get();
                parentNode->children.push_back(std::move(node));
                m_pidToNode.insert(p->pid, rawPtr);
            }
            endInsertRows();
        }

        pendingInsert = std::move(deferred);
    }
}

void ProcessTreeModel::rebuildTree(const QVector<ProcessInfo>& processes) {
    emit aboutToRebuild();
    beginResetModel();

    m_pidToNode.clear();
    m_root = std::make_unique<Node>();
    m_pidToNode.reserve(processes.size());

    std::vector<std::unique_ptr<Node>> created;
    created.reserve(static_cast<std::size_t>(processes.size()));
    std::unordered_map<int, Node*> rawByPid;
    rawByPid.reserve(static_cast<std::size_t>(processes.size()));

    for (const auto& p : processes) {
        auto n = std::make_unique<Node>();
        n->info = p;
        Node* raw = n.get();
        rawByPid[p.pid] = raw;
        m_pidToNode.insert(p.pid, raw);
        created.push_back(std::move(n));
    }

    for (auto& n : created) {
        Node* parentNode = m_root.get();
        if (m_treeMode) {
            const int ppid = n->info.ppid;
            if (ppid > 0 && ppid != n->info.pid) {
                if (const auto it = rawByPid.find(ppid); it != rawByPid.end()) {
                    Node* candidate = it->second;
                    Node* probe = candidate;
                    bool cycle = false;
                    for (int depth = 0; depth < 64 && probe; ++depth) {
                        if (probe == n.get()) {
                            cycle = true;
                            break;
                        }
                        const int nextPpid = probe->info.ppid;
                        if (nextPpid > 0 && nextPpid != probe->info.pid) {
                            const auto nextIt = rawByPid.find(nextPpid);
                            probe = (nextIt != rawByPid.end()) ? nextIt->second : nullptr;
                        } else {
                            probe = nullptr;
                        }
                    }
                    if (!cycle) {
                        parentNode = candidate;
                    }
                }
            }
        }
        n->parent = parentNode;
        n->rowInParent = static_cast<int>(parentNode->children.size());
        parentNode->children.push_back(std::move(n));
    }

    endResetModel();
    emit rebuilt();
}

const ProcessInfo* ProcessTreeModel::processForIndex(const QModelIndex& index) const {
    if (!index.isValid()) {
        return nullptr;
    }
    Node* node = nodeFromIndex(index);
    if (!node || node == m_root.get()) {
        return nullptr;
    }
    return &node->info;
}

QModelIndex ProcessTreeModel::indexForPid(int pid) const {
    const auto it = m_pidToNode.constFind(pid);
    if (it == m_pidToNode.constEnd()) {
        return {};
    }
    return indexForNode(*it, 0);
}

} // namespace sysmon
