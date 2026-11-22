#pragma once

#include "core/TelemetryTypes.h"

#include <QPointer>
#include <QSet>
#include <QVector>
#include <QWidget>

#include <cstdint>

class QComboBox;
class QEvent;
class QFrame;
class QLabel;
class QLineEdit;
class QModelIndex;
class QPoint;
class QPushButton;
class QTreeView;

namespace sysmon {

class ProcessDetailsDialog;
class ProcessFilterProxyModel;
class ProcessTreeModel;
class ScaledRowDelegate;

/**
 * @brief Sekme 3 — /proc süreçlerini ağaç veya bağımsız liste olarak gösteren,
 *        ikonlu, tüm POSIX SIG sinyalli ve ayrıntı pencereli sekme.
 */
class ProcessesTab : public QWidget {
    Q_OBJECT

public:
    explicit ProcessesTab(QWidget* parent = nullptr);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

public slots:
    void onProcessSnapshot(const sysmon::ProcessSnapshot& snapshot);
    void setPidFilter(const QString& appName, const QVector<int>& pids);
    void clearPidFilter();
    void applyAppearance();
    void applyProcessPrefs();
    void retranslateUi();
    void expandAll();
    void collapseAll();

private slots:
    void onContextMenuRequested(const QPoint& pos);
    void onAboutToRebuild();
    void onRebuilt();
    void onDoubleClicked(const QModelIndex& proxyIdx);

private:
    void setupUi();
    void updateCountLabel();
    [[nodiscard]] const ProcessInfo* selectedProcess() const;
    void openDetailsForProcess(const ProcessInfo& proc);
    void sendSignalToProcess(int pid,
                             std::uint64_t expectedStartTime,
                             const QString& procName,
                             int sig,
                             const QString& sigLabel);
    void setProcessNice(int pid,
                        std::uint64_t expectedStartTime,
                        const QString& procName,
                        int niceValue);
    void runPkexecAction(const QStringList& args, const QString& actionTitle);
    void collectExpandedPids(const QModelIndex& parent, QSet<int>& outPids) const;
    void restoreExpandedPids(const QModelIndex& parent);

    QLineEdit* m_searchEdit{nullptr};
    QComboBox* m_filterCombo{nullptr};
    QLabel* m_countLabel{nullptr};

    QFrame* m_pidBanner{nullptr};
    QLabel* m_pidBannerLabel{nullptr};
    QPushButton* m_clearPidBtn{nullptr};

    QTreeView* m_treeView{nullptr};
    ProcessTreeModel* m_model{nullptr};
    ProcessFilterProxyModel* m_proxy{nullptr};
    ScaledRowDelegate* m_rowDelegate{nullptr};

    QPointer<ProcessDetailsDialog> m_detailsDialog;

    int m_totalProcesses{0};
    int m_totalThreads{0};
    QString m_pidBannerAppName;
    int m_pidBannerCount{0};

    bool m_contextMenuOpen{false};
    bool m_headerDragging{false};
    bool m_hasPendingSnapshot{false};
    ProcessSnapshot m_pendingSnapshot;

    int m_savedSelectedPid{-1};
    int m_savedScrollVal{0};
    int m_savedHScrollVal{0};
    QSet<int> m_savedExpandedPids;
};

} // namespace sysmon
