#pragma once

#include "core/TelemetryTypes.h"

#include <QAbstractItemModel>
#include <QHash>
#include <QIcon>
#include <QVector>

namespace sysmon {

/**
 * @brief Süreçleri tercihe bağlı olarak dallanan PPID ağacı veya düz liste
 *        biçiminde gösteren, simge önbellekli 11 sütunlu model.
 */
class ProcessTreeModel : public QAbstractItemModel {
    Q_OBJECT

public:
    enum Column {
        ColName = 0,
        ColPid,
        ColUser,
        ColState,
        ColCpu,
        ColMemoryRss,
        ColMemoryShared,
        ColDiskIo,
        ColNetwork,
        ColGpuPercent,
        ColGpuMemory,
        ColCount
    };

    explicit ProcessTreeModel(QObject* parent = nullptr);

    [[nodiscard]] QModelIndex index(int row,
                                    int column,
                                    const QModelIndex& parent = QModelIndex()) const override;
    [[nodiscard]] QModelIndex parent(const QModelIndex& child) const override;
    [[nodiscard]] int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    [[nodiscard]] int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    [[nodiscard]] QVariant headerData(int section,
                                      Qt::Orientation orientation,
                                      int role = Qt::DisplayRole) const override;

    void setTreeMode(bool enabled);
    [[nodiscard]] bool treeMode() const noexcept { return m_treeMode; }

    void setShowIcons(bool enabled);
    [[nodiscard]] bool showIcons() const noexcept { return m_showIcons; }

    void updateProcesses(const QVector<ProcessInfo>& incoming);
    void retranslate();

    [[nodiscard]] const ProcessInfo* processForIndex(const QModelIndex& index) const;
    [[nodiscard]] QModelIndex indexForPid(int pid) const;

signals:
    void aboutToRebuild();
    void rebuilt();

private:
    struct Node {
        ProcessInfo info;
        int parentNode{-1};
        int rowInParent{0};
        QVector<int> children;
    };

    void rebuildTree(const QVector<ProcessInfo>& processes);
    void emitDataChangedRecursive(int parentNodeIdx, const QModelIndex& parentIdx);
    [[nodiscard]] QIcon resolveProcessIcon(const ProcessInfo& proc) const;

    bool m_treeMode{true};
    bool m_showIcons{true};
    QVector<ProcessInfo> m_lastProcesses;
    QVector<Node> m_nodes;
    QHash<int, int> m_pidToNode;
    mutable QHash<QString, QIcon> m_iconCache;
};

} // namespace sysmon
