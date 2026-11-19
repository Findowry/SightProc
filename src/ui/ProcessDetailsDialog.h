#pragma once

#include "core/TelemetryTypes.h"

#include <QDialog>

class QFormLayout;
class QLabel;
class QLineEdit;
class QPushButton;
class QTabWidget;
class QTableWidget;

namespace sysmon {

/**
 * @brief Seçili sürecin genel bilgilerini, bellek/status detaylarını,
 *        ortam değişkenlerini ve açık dosya/soketlerini gösteren pencere.
 */
class ProcessDetailsDialog : public QDialog {
    Q_OBJECT

public:
    explicit ProcessDetailsDialog(const ProcessInfo& info, QWidget* parent = nullptr);

    void updateLiveInfo(const ProcessInfo& info);

private slots:
    void refreshProcFiles();

private:
    void setupUi();
    void populateOverview();
    void populateStatusTab();
    void populateEnvironTab();
    void populateFdTab();
    void filterEnvironTable(const QString& text);

    ProcessInfo m_info;
    QTabWidget* m_tabs{nullptr};
    QFormLayout* m_overviewForm{nullptr};
    QLabel* m_headerLabel{nullptr};
    QTableWidget* m_statusTable{nullptr};
    QLineEdit* m_envSearch{nullptr};
    QTableWidget* m_envTable{nullptr};
    QTableWidget* m_fdTable{nullptr};
    QPushButton* m_refreshBtn{nullptr};
    QPushButton* m_closeBtn{nullptr};
};

} // namespace sysmon
