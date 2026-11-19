#pragma once

#include <QSet>
#include <QSortFilterProxyModel>
#include <QString>

#include <cstdint>

namespace sysmon {

class ProcessTreeModel;

/**
 * @brief Süreç ağacı/listesi için özyinelemeli metin arama, hızlı filtre ve PID alt kümesi süzgeci.
 */
class ProcessFilterProxyModel : public QSortFilterProxyModel {
    Q_OBJECT

public:
    enum class QuickFilter {
        All = 0,
        CurrentUser,
        HighCpu,
        UsingGpu
    };

    explicit ProcessFilterProxyModel(QObject* parent = nullptr);

    void setSourceProcessModel(ProcessTreeModel* model);
    void setSearchText(const QString& text);
    void setQuickFilter(QuickFilter filter);
    void setCurrentUserUid(std::uint32_t uid);
    void setPidFilter(const QSet<int>& pids);
    void clearPidFilter();

    [[nodiscard]] bool hasActiveFilter() const noexcept;

protected:
    [[nodiscard]] bool filterAcceptsRow(int sourceRow,
                                        const QModelIndex& sourceParent) const override;

private:
    ProcessTreeModel* m_processModel{nullptr};
    QString m_searchText;
    QuickFilter m_quickFilter{QuickFilter::All};
    std::uint32_t m_currentUserUid{0};
    bool m_pidFilterActive{false};
    QSet<int> m_allowedPids;
};

} // namespace sysmon
