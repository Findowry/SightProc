#pragma once

#include "core/TelemetryTypes.h"

#include <QMainWindow>
#include <QPointer>
#include <QThread>

class QAction;
class QCloseEvent;
class QComboBox;
class QEvent;
class QLabel;
class QMenu;
class QPushButton;
class QTabWidget;

namespace sysmon {

class ApplicationsTab;
class PerformanceTab;
class PreferencesDialog;
class ProcessWorker;
class ProcessesTab;
class ServicesTab;
class TelemetryWorker;

/**
 * @brief Dosya/Düzenle/Görünüm/Yardım menü çubuğu, 4 sekme, Tercihler iletişim kutusu
 *        ve arka plan iş parçacıklarını yöneten ana pencere.
 */
class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

protected:
    void changeEvent(QEvent* event) override;
    void closeEvent(QCloseEvent* event) override;

private slots:
    void onTelemetrySummary(const sysmon::SystemTelemetrySnapshot& snapshot);
    void onRefreshIntervalComboChanged(int index);
    void onPauseToggled(bool paused);
    void openPreferences();
    void openConfigDirectory();
    void showShortcutsHelp();
    void showAboutDialog();
    void applyAllAppearances();
    void retranslateUi();

private:
    void setupUi();
    void setupMenuBar();
    void connectSettingsSignals();
    void startWorkers();
    void shutdownWorkers();
    void updateStyleStatus();

    QTabWidget* m_tabs{nullptr};
    QLabel* m_intervalLabel{nullptr};
    QComboBox* m_intervalCombo{nullptr};
    QPushButton* m_pauseButton{nullptr};

    PerformanceTab* m_performanceTab{nullptr};
    ApplicationsTab* m_applicationsTab{nullptr};
    ProcessesTab* m_processesTab{nullptr};
    ServicesTab* m_servicesTab{nullptr};
    QPointer<PreferencesDialog> m_prefsDialog;

    // Menü çubuğu öğeleri
    QMenu* m_fileMenu{nullptr};
    QMenu* m_editMenu{nullptr};
    QMenu* m_viewMenu{nullptr};
    QMenu* m_refreshMenu{nullptr};
    QMenu* m_helpMenu{nullptr};

    QAction* m_refreshAct{nullptr};
    QAction* m_pauseAct{nullptr};
    QAction* m_quitAct{nullptr};
    QAction* m_prefsAct{nullptr};
    QAction* m_configDirAct{nullptr};
    QAction* m_tabActs[4]{};
    QAction* m_treeModeAct{nullptr};
    QAction* m_expandAllAct{nullptr};
    QAction* m_collapseAllAct{nullptr};
    QAction* m_zoomInAct{nullptr};
    QAction* m_zoomOutAct{nullptr};
    QAction* m_zoomResetAct{nullptr};
    QAction* m_shortcutsAct{nullptr};
    QAction* m_aboutAct{nullptr};

    QThread m_telemetryThread;
    TelemetryWorker* m_telemetryWorker{nullptr};
    QThread m_processThread;
    ProcessWorker* m_processWorker{nullptr};
    bool m_workersStopped{false};

    SystemTelemetrySnapshot m_lastSnapshot;
    bool m_hasLastSnapshot{false};
};

} // namespace sysmon
