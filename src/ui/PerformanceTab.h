#pragma once

#include "core/AppSettings.h"
#include "core/TelemetryTypes.h"
#include "models/DeviceListModel.h"

#include <QHash>
#include <QPair>
#include <QString>
#include <QVector>
#include <QWidget>

class QCheckBox;
class QFormLayout;
class QLabel;
class QListView;
class QModelIndex;
class QPushButton;
class QScrollArea;
class QSplitter;
class QStackedWidget;
class QTableWidget;
class QTableWidgetItem;

namespace sysmon {

class AreaChartWidget;
class CoreGridWidget;
class DeviceListDelegate;
class MemoryBarWidget;

/**
 * @brief Sekme 1 — Özelleştirilebilir renkli canlı grafikler, ek göstergeler,
 *        serbestçe boyutlandırılabilir dikey bölücüler, çekirdek ızgarası ve teknik künye.
 */
class PerformanceTab : public QWidget {
    Q_OBJECT

public:
    explicit PerformanceTab(QWidget* parent = nullptr);

public slots:
    void onTelemetrySnapshot(const sysmon::SystemTelemetrySnapshot& snapshot);
    void applyAppearance();
    void retranslateUi();

signals:
    void probeSleepingGpusToggled(bool enabled);

private slots:
    void onDeviceSelectionChanged(const QModelIndex& current, const QModelIndex& previous);
    void onCoreGridToggled(bool checked);
    void onAddIndicatorClicked();
    void onCustomizeSpecsClicked();
    void onThermalItemChanged(QTableWidgetItem* item);

private:
    struct IndicatorDef {
        QString id;
        QString label;
    };

    void setupUi();
    void refreshCenterAndSpecs();
    void rebuildExtraChartsIfNeeded(const DeviceEntry& entry);
    void updateExtraCharts(const DeviceEntry& entry, bool appendSample);
    void updateThermalTable(bool allowSelectionCheckboxes);
    void openThermalSensorMenu();
    void updateSpecsForEntry(const DeviceEntry& entry);
    void setSpecsRows(const QString& schemaId,
                      const QVector<QPair<QString, QString>>& rows);
    void updateChartStyle(AreaChartWidget* chart, AppSettings::HardwareCategory cat);
    [[nodiscard]] QVector<IndicatorDef> availableIndicators(AppSettings::HardwareCategory cat) const;
    [[nodiscard]] QVector<IndicatorDef> availableExtraSpecs(AppSettings::HardwareCategory cat) const;

    DeviceListModel* m_deviceModel{nullptr};
    DeviceListDelegate* m_deviceDelegate{nullptr};
    QListView* m_deviceList{nullptr};

    QLabel* m_titleLabel{nullptr};
    QLabel* m_summaryLabel{nullptr};
    QPushButton* m_addIndicatorBtn{nullptr};
    QPushButton* m_coreGridToggle{nullptr};
    QStackedWidget* m_centerStack{nullptr};

    QSplitter* m_verticalSplitter{nullptr};
    AreaChartWidget* m_mainChart{nullptr};
    MemoryBarWidget* m_memoryBar{nullptr};
    QVector<QPair<QString, AreaChartWidget*>> m_extraCharts;
    QHash<QString, QString> m_extraChartSigs;
    QString m_activeExtraSignature;
    QString m_lastMainChartSignature;
    bool m_pendingChartPush{false};

    QTableWidget* m_thermalTable{nullptr};
    CoreGridWidget* m_coreGrid{nullptr};

    QLabel* m_specHeaderLabel{nullptr};
    QPushButton* m_customizeSpecsBtn{nullptr};
    QScrollArea* m_specsScroll{nullptr};
    QWidget* m_specsWidget{nullptr};
    QFormLayout* m_specsLayout{nullptr};
    QCheckBox* m_probeGpuCheck{nullptr};

    QString m_activeSchemaId;
    QVector<QLabel*> m_specValueLabels;

    SystemTelemetrySnapshot m_snapshot;
    bool m_hasSnapshot{false};
    QString m_selectedKey{QStringLiteral("cpu")};
};

} // namespace sysmon
