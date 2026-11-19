#include "models/ServiceTableModel.h"

#include "core/Lang.h"

#include <QSet>

namespace sysmon {

ServiceTableModel::ServiceTableModel(QObject* parent)
    : QAbstractTableModel(parent) {}

int ServiceTableModel::rowCount(const QModelIndex& parent) const {
    if (parent.isValid()) {
        return 0;
    }
    return static_cast<int>(m_rows.size());
}

int ServiceTableModel::columnCount(const QModelIndex& parent) const {
    if (parent.isValid()) {
        return 0;
    }
    return ColCount;
}

QVariant ServiceTableModel::headerData(int section,
                                       Qt::Orientation orientation,
                                       int role) const {
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole) {
        return {};
    }
    switch (section) {
    case ColUnit:
        return trText("Unit Name", "Birim Adı");
    case ColLoad:
        return trText("Load", "Yükleme");
    case ColActive:
        return trText("Active", "Aktiflik");
    case ColSub:
        return trText("Sub", "Alt Durum");
    case ColStartup:
        return trText("Preset / State", "Başlangıç");
    case ColDescription:
        return trText("Description", "Açıklama");
    default:
        return {};
    }
}

QVariant ServiceTableModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= m_rows.size()) {
        return {};
    }
    const ServiceInfo& s = m_rows[index.row()];
    const int col = index.column();

    if (role == Qt::TextAlignmentRole) {
        return static_cast<int>(Qt::AlignLeft | Qt::AlignVCenter);
    }

    if (role == Qt::DisplayRole || role == Qt::ToolTipRole) {
        switch (col) {
        case ColUnit:
            return s.unitName;
        case ColLoad:
            return s.loadState;
        case ColActive:
            return s.activeState;
        case ColSub:
            return s.subState;
        case ColStartup:
            return s.unitFileState.isEmpty() ? QStringLiteral("—") : s.unitFileState;
        case ColDescription:
            return s.description.isEmpty() ? QStringLiteral("—") : s.description;
        default:
            return {};
        }
    }

    if (role == Qt::UserRole) {
        switch (col) {
        case ColUnit:
            return s.unitName.toLower();
        case ColLoad:
            return s.loadState.toLower();
        case ColActive:
            return s.activeState.toLower();
        case ColSub:
            return s.subState.toLower();
        case ColStartup:
            return s.unitFileState.toLower();
        case ColDescription:
            return s.description.toLower();
        default:
            return {};
        }
    }

    return {};
}

void ServiceTableModel::updateServices(const QVector<ServiceInfo>& incoming) {
    QSet<QString> incomingUnits;
    incomingUnits.reserve(incoming.size());
    for (const auto& s : incoming) {
        incomingUnits.insert(s.unitName);
    }

    for (int row = static_cast<int>(m_rows.size()) - 1; row >= 0; --row) {
        if (!incomingUnits.contains(m_rows[row].unitName)) {
            beginRemoveRows(QModelIndex(), row, row);
            m_rows.removeAt(row);
            endRemoveRows();
        }
    }

    m_unitToRow.clear();
    m_unitToRow.reserve(m_rows.size());
    for (int row = 0; row < m_rows.size(); ++row) {
        m_unitToRow.insert(m_rows[row].unitName, row);
    }

    const int survivingCount = static_cast<int>(m_rows.size());
    QVector<ServiceInfo> toInsert;

    for (const auto& s : incoming) {
        const auto it = m_unitToRow.constFind(s.unitName);
        if (it != m_unitToRow.constEnd()) {
            m_rows[*it] = s;
        } else {
            toInsert.push_back(s);
        }
    }

    if (survivingCount > 0) {
        emit dataChanged(index(0, 0), index(survivingCount - 1, ColCount - 1));
    }

    if (!toInsert.isEmpty()) {
        const int first = static_cast<int>(m_rows.size());
        const int last = first + static_cast<int>(toInsert.size()) - 1;
        beginInsertRows(QModelIndex(), first, last);
        for (const auto& s : toInsert) {
            const int row = static_cast<int>(m_rows.size());
            m_unitToRow.insert(s.unitName, row);
            m_rows.push_back(s);
        }
        endInsertRows();
    }
}

void ServiceTableModel::retranslate() {
    emit headerDataChanged(Qt::Horizontal, 0, ColCount - 1);
}

const ServiceInfo* ServiceTableModel::serviceAt(int row) const {
    if (row < 0 || row >= m_rows.size()) {
        return nullptr;
    }
    return &m_rows[row];
}

ServiceFilterProxyModel::ServiceFilterProxyModel(QObject* parent)
    : QSortFilterProxyModel(parent) {
    setSortRole(Qt::UserRole);
    setDynamicSortFilter(true);
}

void ServiceFilterProxyModel::setSourceServiceModel(ServiceTableModel* model) {
    m_serviceModel = model;
    setSourceModel(model);
}

void ServiceFilterProxyModel::setSearchText(const QString& text) {
    const QString trimmed = text.trimmed();
    if (m_searchText == trimmed) {
        return;
    }
    beginFilterChange();
    m_searchText = trimmed;
    endFilterChange(QSortFilterProxyModel::Direction::Rows);
}

void ServiceFilterProxyModel::setStatusFilter(StatusFilter filter) {
    if (m_statusFilter == filter) {
        return;
    }
    beginFilterChange();
    m_statusFilter = filter;
    endFilterChange(QSortFilterProxyModel::Direction::Rows);
}

bool ServiceFilterProxyModel::filterAcceptsRow(int sourceRow, const QModelIndex&) const {
    if (!m_serviceModel) {
        return true;
    }
    const ServiceInfo* s = m_serviceModel->serviceAt(sourceRow);
    if (!s) {
        return false;
    }

    switch (m_statusFilter) {
    case StatusFilter::All:
        break;
    case StatusFilter::Active:
        if (s->activeState != QStringLiteral("active")) {
            return false;
        }
        break;
    case StatusFilter::Inactive:
        if (s->activeState != QStringLiteral("inactive")) {
            return false;
        }
        break;
    case StatusFilter::Failed:
        if (s->activeState != QStringLiteral("failed")) {
            return false;
        }
        break;
    }

    if (!m_searchText.isEmpty()) {
        if (!s->unitName.contains(m_searchText, Qt::CaseInsensitive) &&
            !s->description.contains(m_searchText, Qt::CaseInsensitive)) {
            return false;
        }
    }

    return true;
}

} // namespace sysmon
