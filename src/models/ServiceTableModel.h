#pragma once

#include "core/TelemetryTypes.h"

#include <QAbstractTableModel>
#include <QHash>
#include <QSortFilterProxyModel>
#include <QString>
#include <QVector>

namespace sysmon {

class ServiceTableModel : public QAbstractTableModel {
    Q_OBJECT

public:
    enum Column {
        ColUnit = 0,
        ColLoad,
        ColActive,
        ColSub,
        ColStartup,
        ColDescription,
        ColCount
    };

    explicit ServiceTableModel(QObject* parent = nullptr);

    [[nodiscard]] int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    [[nodiscard]] int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    [[nodiscard]] QVariant headerData(int section,
                                      Qt::Orientation orientation,
                                      int role = Qt::DisplayRole) const override;

    void updateServices(const QVector<ServiceInfo>& incoming);
    void retranslate();
    [[nodiscard]] const ServiceInfo* serviceAt(int row) const;

private:
    QVector<ServiceInfo> m_rows;
    QHash<QString, int> m_unitToRow;
};

class ServiceFilterProxyModel : public QSortFilterProxyModel {
    Q_OBJECT

public:
    enum class StatusFilter {
        All = 0,
        Active,
        Inactive,
        Failed
    };

    explicit ServiceFilterProxyModel(QObject* parent = nullptr);

    void setSourceServiceModel(ServiceTableModel* model);
    void setSearchText(const QString& text);
    void setStatusFilter(StatusFilter filter);

protected:
    [[nodiscard]] bool filterAcceptsRow(int sourceRow,
                                        const QModelIndex& sourceParent) const override;

private:
    ServiceTableModel* m_serviceModel{nullptr};
    QString m_searchText;
    StatusFilter m_statusFilter{StatusFilter::All};
};

} // namespace sysmon
