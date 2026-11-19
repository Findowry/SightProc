#include "models/ProcessFilterProxyModel.h"

#include "models/ProcessTreeModel.h"

namespace sysmon {

ProcessFilterProxyModel::ProcessFilterProxyModel(QObject* parent)
    : QSortFilterProxyModel(parent) {
    setSortRole(Qt::UserRole);
    setDynamicSortFilter(true);
    setRecursiveFilteringEnabled(true);
}

void ProcessFilterProxyModel::setSourceProcessModel(ProcessTreeModel* model) {
    m_processModel = model;
    setSourceModel(model);
}

void ProcessFilterProxyModel::setSearchText(const QString& text) {
    const QString trimmed = text.trimmed();
    if (m_searchText == trimmed) {
        return;
    }
    beginFilterChange();
    m_searchText = trimmed;
    endFilterChange(QSortFilterProxyModel::Direction::Rows);
}

void ProcessFilterProxyModel::setQuickFilter(QuickFilter filter) {
    if (m_quickFilter == filter) {
        return;
    }
    beginFilterChange();
    m_quickFilter = filter;
    endFilterChange(QSortFilterProxyModel::Direction::Rows);
}

void ProcessFilterProxyModel::setCurrentUserUid(std::uint32_t uid) {
    if (m_currentUserUid == uid) {
        return;
    }
    if (m_quickFilter == QuickFilter::CurrentUser) {
        beginFilterChange();
        m_currentUserUid = uid;
        endFilterChange(QSortFilterProxyModel::Direction::Rows);
    } else {
        m_currentUserUid = uid;
    }
}

void ProcessFilterProxyModel::setPidFilter(const QSet<int>& pids) {
    beginFilterChange();
    m_pidFilterActive = true;
    m_allowedPids = pids;
    endFilterChange(QSortFilterProxyModel::Direction::Rows);
}

void ProcessFilterProxyModel::clearPidFilter() {
    if (!m_pidFilterActive) {
        return;
    }
    beginFilterChange();
    m_pidFilterActive = false;
    m_allowedPids.clear();
    endFilterChange(QSortFilterProxyModel::Direction::Rows);
}

bool ProcessFilterProxyModel::hasActiveFilter() const noexcept {
    return !m_searchText.isEmpty() ||
           m_quickFilter != QuickFilter::All ||
           m_pidFilterActive;
}

bool ProcessFilterProxyModel::filterAcceptsRow(int sourceRow,
                                               const QModelIndex& sourceParent) const {
    if (!m_processModel) {
        return true;
    }
    const QModelIndex idx = m_processModel->index(sourceRow, 0, sourceParent);
    const ProcessInfo* p = m_processModel->processForIndex(idx);
    if (!p) {
        return false;
    }

    if (m_pidFilterActive && !m_allowedPids.contains(p->pid)) {
        return false;
    }

    switch (m_quickFilter) {
    case QuickFilter::All:
        break;
    case QuickFilter::CurrentUser:
        if (p->uid != m_currentUserUid) {
            return false;
        }
        break;
    case QuickFilter::HighCpu:
        if (p->cpuPercent < 1.0) {
            return false;
        }
        break;
    case QuickFilter::UsingGpu:
        if (!p->hasGpuMetrics || (p->gpuPercent <= 0.0 && p->gpuMemBytes == 0)) {
            return false;
        }
        break;
    }

    if (!m_searchText.isEmpty()) {
        const bool matchName = p->name.contains(m_searchText, Qt::CaseInsensitive);
        const bool matchCmd = p->cmdline.contains(m_searchText, Qt::CaseInsensitive);
        const bool matchUser = p->user.contains(m_searchText, Qt::CaseInsensitive);
        const bool matchPid = QString::number(p->pid).contains(m_searchText);
        if (!matchName && !matchCmd && !matchUser && !matchPid) {
            return false;
        }
    }

    return true;
}

} // namespace sysmon
