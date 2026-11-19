#pragma once

#include "core/SystemdController.h"
#include "core/TelemetryTypes.h"

#include <QWidget>

class QComboBox;
class QLabel;
class QLineEdit;
class QModelIndex;
class QPlainTextEdit;
class QPoint;
class QPushButton;
class QTableView;

namespace sysmon {

class ScaledRowDelegate;
class ServiceFilterProxyModel;
class ServiceTableModel;

/**
 * @brief Sekme 4 — Çalışan systemd servislerini listeleyen, pkexec/--user ile
 *        yöneten ve seçili birimin journalctl günlüklerini akışla gösteren sekme.
 */
class ServicesTab : public QWidget {
    Q_OBJECT

public:
    explicit ServicesTab(QWidget* parent = nullptr);
    ~ServicesTab() override;

    void shutdown();

public slots:
    void refresh();
    void applyAppearance();
    void retranslateUi();

private slots:
    void onUnitsLoaded(const QVector<sysmon::ServiceInfo>& units, const QString& warningMsg);
    void onActionFinished(bool success, const QString& message);
    void onSelectionChanged(const QModelIndex& current, const QModelIndex& previous);
    void onContextMenuRequested(const QPoint& pos);
    void onLogsReplaced(const QString& text);
    void onLogsAppended(const QString& chunk);

private:
    void setupUi();
    void updateActionStates();
    [[nodiscard]] const ServiceInfo* selectedService() const;
    [[nodiscard]] ServiceScope currentScope() const;
    void fetchLogsForSelected();

    SystemdController* m_controller{nullptr};
    ServiceTableModel* m_model{nullptr};
    ServiceFilterProxyModel* m_proxy{nullptr};
    ScaledRowDelegate* m_rowDelegate{nullptr};

    QComboBox* m_scopeCombo{nullptr};
    QComboBox* m_statusCombo{nullptr};
    QLineEdit* m_searchEdit{nullptr};
    QPushButton* m_startBtn{nullptr};
    QPushButton* m_stopBtn{nullptr};
    QPushButton* m_restartBtn{nullptr};
    QPushButton* m_reloadBtn{nullptr};
    QPushButton* m_enableBtn{nullptr};
    QPushButton* m_disableBtn{nullptr};
    QPushButton* m_refreshBtn{nullptr};

    QLabel* m_statusLabel{nullptr};
    QTableView* m_tableView{nullptr};

    QLabel* m_logHeaderLabel{nullptr};
    QPushButton* m_followBtn{nullptr};
    QPushButton* m_clearLogBtn{nullptr};
    QPlainTextEdit* m_logView{nullptr};

    QString m_selectedUnit;
};

} // namespace sysmon
