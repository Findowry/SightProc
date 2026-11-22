#pragma once

#include "core/TelemetryTypes.h"

#include <QAbstractItemModel>
#include <QHash>
#include <QIcon>
#include <QVector>

#include <memory>
#include <vector>

namespace sysmon {

/**
 * @brief Süreçleri tercihe bağlı olarak dallanan PPID ağacı veya düz liste biçiminde
 *        gösteren, simge önbellekli 11 sütunlu hibrit artımlı model.
 *
 * Normal ekleme/çıkarma ve veri güncellemeleri beginInsertRows / beginRemoveRows /
 * dataChanged ile artımlı uygulanır; görünüm modu değişimi, PID yeniden kullanımı veya
 * çekirdek tarafından ebeveyn değişimi (reparenting) durumunda tutarlılık için
 * güvenli tam sıfırlamaya (beginResetModel) geri düşer.
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
        Node* parent{nullptr};
        int rowInParent{0};
        std::vector<std::unique_ptr<Node>> children;
    };

    [[nodiscard]] Node* nodeFromIndex(const QModelIndex& index) const noexcept;
    [[nodiscard]] QModelIndex indexForNode(Node* node, int column = 0) const;
    void eraseSubtreeFromMap(Node* node);
    void rebuildTree(const QVector<ProcessInfo>& processes);
    void emitDataChangedRecursive(Node* parentNode, const QModelIndex& parentIdx);
    [[nodiscard]] QIcon resolveProcessIcon(const ProcessInfo& proc) const;

    bool m_treeMode{true};
    bool m_showIcons{true};
    QVector<ProcessInfo> m_lastProcesses;
    std::unique_ptr<Node> m_root;
    QHash<int, Node*> m_pidToNode;
    mutable QHash<QString, QIcon> m_iconCache;
};

} // namespace sysmon
