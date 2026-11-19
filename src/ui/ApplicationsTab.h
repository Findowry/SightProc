#pragma once

#include "core/TelemetryTypes.h"

#include <QVector>
#include <QWidget>

class QLabel;
class QLineEdit;
class QPoint;
class QSortFilterProxyModel;
class QTableView;

namespace sysmon {

class ApplicationTableModel;
class ScaledRowDelegate;

/**
 * @brief Sekme 2 — Kullanıcı masaüstü uygulamalarını büyüklüğü ölçeklenebilir
 *        ve arka planı yarı şeffaf tablo olarak gösteren sekme.
 */
class ApplicationsTab : public QWidget {
    Q_OBJECT

public:
    explicit ApplicationsTab(QWidget* parent = nullptr);

public slots:
    void onProcessSnapshot(const sysmon::ProcessSnapshot& snapshot);
    void applyAppearance();
    void retranslateUi();

signals:
    void showApplicationProcessesRequested(const QString& appName, const QVector<int>& pids);

private slots:
    void onContextMenuRequested(const QPoint& pos);

private:
    void setupUi();
    void updateCountLabel();
    [[nodiscard]] const ApplicationInfo* selectedApplication() const;
    void sendSignalToApplication(const ApplicationInfo& app, int sig);

    QLineEdit* m_searchEdit{nullptr};
    QLabel* m_countLabel{nullptr};
    QTableView* m_tableView{nullptr};
    ApplicationTableModel* m_model{nullptr};
    QSortFilterProxyModel* m_proxy{nullptr};
    ScaledRowDelegate* m_rowDelegate{nullptr};
};

} // namespace sysmon
