#include "ui/MainWindow.h"

#include "core/AppSettings.h"
#include "core/FormatUtils.h"
#include "core/Lang.h"
#include "core/ProcessWorker.h"
#include "core/TelemetryWorker.h"
#include "ui/ApplicationsTab.h"
#include "ui/PerformanceTab.h"
#include "ui/PreferencesDialog.h"
#include "ui/ProcessesTab.h"
#include "ui/ServicesTab.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QCloseEvent>
#include <QComboBox>
#include <QDesktopServices>
#include <QDir>
#include <QEvent>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QIcon>
#include <QKeySequence>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMetaObject>
#include <QPushButton>
#include <QStatusBar>
#include <QStyle>
#include <QTabWidget>
#include <QUrl>
#include <QWidget>

#include <algorithm>

namespace sysmon {

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent) {
    setupUi();
    setupMenuBar();
    connectSettingsSignals();
    retranslateUi();
    applyAllAppearances();
    updateStyleStatus();
    startWorkers();
}

MainWindow::~MainWindow() {
    shutdownWorkers();
}

void MainWindow::setupUi() {
    resize(1340, 820);
    setMinimumSize(980, 620);

    m_tabs = new QTabWidget(this);
    m_tabs->setDocumentMode(true);

    m_performanceTab = new PerformanceTab(m_tabs);
    m_applicationsTab = new ApplicationsTab(m_tabs);
    m_processesTab = new ProcessesTab(m_tabs);
    m_servicesTab = new ServicesTab(m_tabs);

    m_tabs->addTab(m_performanceTab,
                   QIcon::fromTheme(QStringLiteral("utilities-system-monitor")),
                   QString());
    m_tabs->addTab(m_applicationsTab,
                   QIcon::fromTheme(QStringLiteral("applications-all"),
                                    QIcon::fromTheme(QStringLiteral("applications-other"))),
                   QString());
    m_tabs->addTab(m_processesTab,
                   QIcon::fromTheme(QStringLiteral("system-run")),
                   QString());
    m_tabs->addTab(m_servicesTab,
                   QIcon::fromTheme(QStringLiteral("preferences-system-services"),
                                    QIcon::fromTheme(QStringLiteral("preferences-system"))),
                   QString());

    auto* cornerWidget = new QWidget(m_tabs);
    auto* cornerLayout = new QHBoxLayout(cornerWidget);
    cornerLayout->setContentsMargins(0, 2, 6, 2);
    cornerLayout->setSpacing(6);

    m_intervalLabel = new QLabel(cornerWidget);
    m_intervalCombo = new QComboBox(cornerWidget);
    m_intervalCombo->addItem(QStringLiteral("500 ms"), 500);
    m_intervalCombo->addItem(QStringLiteral("1 s"), 1000);
    m_intervalCombo->addItem(QStringLiteral("2 s"), 2000);
    m_intervalCombo->addItem(QStringLiteral("3 s"), 3000);
    const int initIdx = m_intervalCombo->findData(AppSettings::instance().refreshIntervalMs());
    m_intervalCombo->setCurrentIndex(initIdx >= 0 ? initIdx : 1);

    m_pauseButton = new QPushButton(
        QIcon::fromTheme(QStringLiteral("media-playback-pause")),
        QString(),
        cornerWidget);
    m_pauseButton->setCheckable(true);

    cornerLayout->addWidget(m_intervalLabel);
    cornerLayout->addWidget(m_intervalCombo);
    cornerLayout->addWidget(m_pauseButton);
    m_tabs->setCornerWidget(cornerWidget, Qt::TopRightCorner);

    setCentralWidget(m_tabs);
    statusBar();

    connect(m_intervalCombo, qOverload<int>(&QComboBox::currentIndexChanged),
            this, &MainWindow::onRefreshIntervalComboChanged);
    connect(m_pauseButton, &QPushButton::toggled,
            this, &MainWindow::onPauseToggled);
    connect(m_applicationsTab, &ApplicationsTab::showApplicationProcessesRequested,
            this, [this](const QString& appName, const QVector<int>& pids) {
                m_processesTab->setPidFilter(appName, pids);
                m_tabs->setCurrentWidget(m_processesTab);
            });
}

void MainWindow::setupMenuBar() {
    auto* bar = menuBar();

    m_fileMenu = bar->addMenu(QString());
    m_refreshAct = m_fileMenu->addAction(QIcon::fromTheme(QStringLiteral("view-refresh")), QString());
    m_refreshAct->setShortcut(QKeySequence::Refresh);
    m_pauseAct = m_fileMenu->addAction(QIcon::fromTheme(QStringLiteral("media-playback-pause")), QString());
    m_pauseAct->setCheckable(true);
    m_pauseAct->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_P));
    m_fileMenu->addSeparator();
    m_quitAct = m_fileMenu->addAction(QIcon::fromTheme(QStringLiteral("application-exit")), QString());
    m_quitAct->setShortcut(QKeySequence::Quit);

    m_editMenu = bar->addMenu(QString());
    m_prefsAct = m_editMenu->addAction(QIcon::fromTheme(QStringLiteral("configure")), QString());
    m_prefsAct->setShortcut(QKeySequence::Preferences);
    m_configDirAct = m_editMenu->addAction(QIcon::fromTheme(QStringLiteral("folder-open")), QString());

    m_viewMenu = bar->addMenu(QString());
    for (int i = 0; i < 4; ++i) {
        m_tabActs[i] = m_viewMenu->addAction(QString());
        m_tabActs[i]->setShortcut(QKeySequence(Qt::CTRL | (Qt::Key_1 + i)));
        connect(m_tabActs[i], &QAction::triggered, this, [this, i]() {
            m_tabs->setCurrentIndex(i);
        });
    }
    m_viewMenu->addSeparator();

    m_treeModeAct = m_viewMenu->addAction(QString());
    m_treeModeAct->setCheckable(true);
    m_treeModeAct->setChecked(AppSettings::instance().processTreeMode());
    m_expandAllAct = m_viewMenu->addAction(QString());
    m_collapseAllAct = m_viewMenu->addAction(QString());
    m_viewMenu->addSeparator();

    m_zoomInAct = m_viewMenu->addAction(QIcon::fromTheme(QStringLiteral("zoom-in")), QString());
    m_zoomInAct->setShortcut(QKeySequence::ZoomIn);
    m_zoomOutAct = m_viewMenu->addAction(QIcon::fromTheme(QStringLiteral("zoom-out")), QString());
    m_zoomOutAct->setShortcut(QKeySequence::ZoomOut);
    m_zoomResetAct = m_viewMenu->addAction(QIcon::fromTheme(QStringLiteral("zoom-original")), QString());
    m_zoomResetAct->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_0));
    m_viewMenu->addSeparator();

    m_refreshMenu = m_viewMenu->addMenu(QString());
    auto* rateGroup = new QActionGroup(this);
    rateGroup->setExclusive(true);
    const int rates[] = {500, 1000, 2000, 3000};
    for (int ms : rates) {
        const QString lbl = (ms < 1000)
            ? QStringLiteral("%1 ms").arg(ms)
            : QStringLiteral("%1 s").arg(ms / 1000);
        QAction* rAct = m_refreshMenu->addAction(lbl);
        rAct->setCheckable(true);
        rAct->setChecked(AppSettings::instance().refreshIntervalMs() == ms);
        rAct->setData(ms);
        rateGroup->addAction(rAct);
        connect(rAct, &QAction::triggered, this, [ms]() {
            AppSettings::instance().setRefreshIntervalMs(ms);
        });
    }

    m_helpMenu = bar->addMenu(QString());
    m_shortcutsAct = m_helpMenu->addAction(QIcon::fromTheme(QStringLiteral("help-contents")), QString());
    m_aboutAct = m_helpMenu->addAction(QIcon::fromTheme(QStringLiteral("help-about")), QString());

    connect(m_refreshAct, &QAction::triggered, this, [this]() {
        m_servicesTab->refresh();
    });
    connect(m_pauseAct, &QAction::toggled, m_pauseButton, &QPushButton::setChecked);
    connect(m_quitAct, &QAction::triggered, this, &QWidget::close);
    connect(m_prefsAct, &QAction::triggered, this, &MainWindow::openPreferences);
    connect(m_configDirAct, &QAction::triggered, this, &MainWindow::openConfigDirectory);

    connect(m_treeModeAct, &QAction::toggled, this, [](bool on) {
        AppSettings::instance().setProcessTreeMode(on);
    });
    connect(m_expandAllAct, &QAction::triggered, m_processesTab, &ProcessesTab::expandAll);
    connect(m_collapseAllAct, &QAction::triggered, m_processesTab, &ProcessesTab::collapseAll);

    connect(m_zoomInAct, &QAction::triggered, this, [this]() {
        const int idx = m_tabs->currentIndex();
        auto& s = AppSettings::instance();
        s.setTabScalePercent(idx, s.tabScalePercent(idx) + 10);
    });
    connect(m_zoomOutAct, &QAction::triggered, this, [this]() {
        const int idx = m_tabs->currentIndex();
        auto& s = AppSettings::instance();
        s.setTabScalePercent(idx, s.tabScalePercent(idx) - 10);
    });
    connect(m_zoomResetAct, &QAction::triggered, this, [this]() {
        AppSettings::instance().resetTabScale(m_tabs->currentIndex());
    });

    connect(m_shortcutsAct, &QAction::triggered, this, &MainWindow::showShortcutsHelp);
    connect(m_aboutAct, &QAction::triggered, this, &MainWindow::showAboutDialog);
}

void MainWindow::connectSettingsSignals() {
    auto& s = AppSettings::instance();

    connect(&s, &AppSettings::languageChanged, this, &MainWindow::retranslateUi);
    connect(&s, &AppSettings::tableOpacityChanged, this, [this](int) {
        applyAllAppearances();
    });
    connect(&s, &AppSettings::tabScaleChanged, this, [this](int tabIdx, int) {
        switch (tabIdx) {
        case 0:
            m_performanceTab->applyAppearance();
            break;
        case 1:
            m_applicationsTab->applyAppearance();
            break;
        case 2:
            m_processesTab->applyAppearance();
            break;
        case 3:
            m_servicesTab->applyAppearance();
            break;
        default:
            break;
        }
    });
    connect(&s, &AppSettings::chartAppearanceChanged,
            m_performanceTab, &PerformanceTab::applyAppearance);
    connect(&s, &AppSettings::performanceExtrasChanged,
            m_performanceTab, &PerformanceTab::applyAppearance);

    connect(&s, &AppSettings::processPrefsChanged, this, [this]() {
        m_treeModeAct->setChecked(AppSettings::instance().processTreeMode());
        m_processesTab->applyProcessPrefs();
    });

    connect(&s, &AppSettings::refreshIntervalChanged, this, [this](int ms) {
        const int comboIdx = m_intervalCombo->findData(ms);
        if (comboIdx >= 0 && m_intervalCombo->currentIndex() != comboIdx) {
            m_intervalCombo->setCurrentIndex(comboIdx);
        }
        for (QAction* act : m_refreshMenu->actions()) {
            act->setChecked(act->data().toInt() == ms);
        }
        m_performanceTab->applyAppearance();
        if (m_telemetryWorker) {
            QMetaObject::invokeMethod(m_telemetryWorker, "setIntervalMs",
                                      Qt::QueuedConnection,
                                      Q_ARG(int, ms));
        }
        if (m_processWorker) {
            QMetaObject::invokeMethod(m_processWorker, "setIntervalMs",
                                      Qt::QueuedConnection,
                                      Q_ARG(int, ms));
        }
    });

    connect(&s, &AppSettings::probeSleepingGpusChanged, this, [this](bool enabled) {
        m_performanceTab->applyAppearance();
        if (m_telemetryWorker) {
            QMetaObject::invokeMethod(m_telemetryWorker, "setProbeSleepingGpus",
                                      Qt::QueuedConnection,
                                      Q_ARG(bool, enabled));
        }
        if (m_processWorker) {
            QMetaObject::invokeMethod(m_processWorker, "setProbeSleepingGpus",
                                      Qt::QueuedConnection,
                                      Q_ARG(bool, enabled));
        }
    });
}

void MainWindow::startWorkers() {
    const auto& s = AppSettings::instance();

    m_telemetryWorker = new TelemetryWorker();
    m_telemetryWorker->setQtPlatformName(QGuiApplication::platformName());
    m_telemetryWorker->setIntervalMs(s.refreshIntervalMs());
    m_telemetryWorker->setProbeSleepingGpus(s.probeSleepingGpus());
    m_telemetryWorker->moveToThread(&m_telemetryThread);

    connect(&m_telemetryThread, &QThread::started,
            m_telemetryWorker, &TelemetryWorker::start);
    connect(m_telemetryWorker, &TelemetryWorker::snapshotReady,
            m_performanceTab, &PerformanceTab::onTelemetrySnapshot,
            Qt::QueuedConnection);
    connect(m_telemetryWorker, &TelemetryWorker::snapshotReady,
            this, &MainWindow::onTelemetrySummary,
            Qt::QueuedConnection);
    connect(m_performanceTab, &PerformanceTab::probeSleepingGpusToggled,
            m_telemetryWorker, &TelemetryWorker::setProbeSleepingGpus,
            Qt::QueuedConnection);

    m_processWorker = new ProcessWorker();
    m_processWorker->setIntervalMs(s.refreshIntervalMs());
    m_processWorker->setProbeSleepingGpus(s.probeSleepingGpus());
    m_processWorker->moveToThread(&m_processThread);

    connect(&m_processThread, &QThread::started,
            m_processWorker, &ProcessWorker::start);
    connect(m_processWorker, &ProcessWorker::snapshotReady,
            m_applicationsTab, &ApplicationsTab::onProcessSnapshot,
            Qt::QueuedConnection);
    connect(m_processWorker, &ProcessWorker::snapshotReady,
            m_processesTab, &ProcessesTab::onProcessSnapshot,
            Qt::QueuedConnection);
    connect(m_performanceTab, &PerformanceTab::probeSleepingGpusToggled,
            m_processWorker, &ProcessWorker::setProbeSleepingGpus,
            Qt::QueuedConnection);

    m_telemetryThread.start();
    m_processThread.start();
}

void MainWindow::shutdownWorkers() {
    if (m_workersStopped) {
        return;
    }
    m_workersStopped = true;

    if (m_servicesTab) {
        m_servicesTab->shutdown();
    }

    if (m_telemetryWorker) {
        if (m_telemetryThread.isRunning()) {
            QMetaObject::invokeMethod(m_telemetryWorker, &TelemetryWorker::stop,
                                      Qt::QueuedConnection);
            m_telemetryThread.quit();
            m_telemetryThread.wait();
        }
        delete m_telemetryWorker;
        m_telemetryWorker = nullptr;
    }

    if (m_processWorker) {
        if (m_processThread.isRunning()) {
            QMetaObject::invokeMethod(m_processWorker, &ProcessWorker::stop,
                                      Qt::QueuedConnection);
            m_processThread.quit();
            m_processThread.wait();
        }
        delete m_processWorker;
        m_processWorker = nullptr;
    }
}

void MainWindow::onTelemetrySummary(const SystemTelemetrySnapshot& snapshot) {
    m_lastSnapshot = snapshot;
    m_hasLastSnapshot = true;
    updateStyleStatus();
}

void MainWindow::onRefreshIntervalComboChanged(int index) {
    const int ms = m_intervalCombo->itemData(index).toInt();
    if (ms > 0) {
        AppSettings::instance().setRefreshIntervalMs(ms);
    }
}

void MainWindow::onPauseToggled(bool paused) {
    if (m_pauseAct->isChecked() != paused) {
        m_pauseAct->setChecked(paused);
    }
    m_pauseButton->setText(paused ? trText("Resume", "Devam Et") : trText("Pause", "Duraklat"));
    m_pauseButton->setIcon(QIcon::fromTheme(paused
                                                ? QStringLiteral("media-playback-start")
                                                : QStringLiteral("media-playback-pause")));
    if (m_telemetryWorker) {
        QMetaObject::invokeMethod(m_telemetryWorker, "setPaused",
                                  Qt::QueuedConnection,
                                  Q_ARG(bool, paused));
    }
    if (m_processWorker) {
        QMetaObject::invokeMethod(m_processWorker, "setPaused",
                                  Qt::QueuedConnection,
                                  Q_ARG(bool, paused));
    }
    updateStyleStatus();
}

void MainWindow::openPreferences() {
    if (!m_prefsDialog) {
        m_prefsDialog = new PreferencesDialog(this);
        m_prefsDialog->setAttribute(Qt::WA_DeleteOnClose);
    }
    m_prefsDialog->show();
    m_prefsDialog->raise();
    m_prefsDialog->activateWindow();
}

void MainWindow::openConfigDirectory() {
    const QString dir = AppSettings::configDirPath();
    QDir().mkpath(dir);
    QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
}

void MainWindow::showShortcutsHelp() {
    QMessageBox::information(
        this,
        trText("Shortcuts & Tips", "Kısayollar ve İpuçları"),
        trText("• Ctrl + Mouse Wheel: Zoom the active tab independently\n"
               "• Ctrl + + / Ctrl + - / Ctrl + 0: Zoom in / out / reset active tab\n"
               "• Ctrl + 1..4: Switch between Performance, Applications, Processes, Services\n"
               "• Ctrl + , : Open Preferences (Language, Blur Opacity, Colors, Process Tree)\n"
               "• Double-click a process or right-click -> Details... to inspect /proc & open files",
               "• Ctrl + Fare Tekerleği: Etkin sekmeyi bağımsız boyutlandır\n"
               "• Ctrl + + / Ctrl + - / Ctrl + 0: Etkin sekmeyi büyüt / küçült / sıfırla\n"
               "• Ctrl + 1..4: Sekmeler arasında hızlı geçiş\n"
               "• Ctrl + , : Tercihler penceresini aç (Dil, Şeffaflık, Renkler, Süreç Ağacı)\n"
               "• Süreç ayrıntıları için satıra çift tıklayın veya sağ tık -> Ayrıntılar... seçin"));
}

void MainWindow::showAboutDialog() {
    QMessageBox box(this);
    box.setWindowTitle(trText("About SightProc", "SightProc Hakkında"));
    box.setTextFormat(Qt::RichText);
    box.setTextInteractionFlags(Qt::TextBrowserInteraction);
    box.setIconPixmap(QApplication::windowIcon().pixmap(64, 64));
    const QString desc = trText("Qt6 Widgets C++20 based GNU/Linux system monitor",
                                "Qt6 Widgets C++20 tabanlı GNU/Linux sistem monitörü");
    box.setText(
        QStringLiteral("<h3>SightProc 1.1.0</h3>"
                       "<p>Your sight into /proc.</p>"
                       "<p>%1</p>"
                       "<p><a href=\"https://github.com/Findowry/SightProc\">"
                       "https://github.com/Findowry/SightProc</a></p>")
            .arg(desc));
    box.exec();
}

void MainWindow::applyAllAppearances() {
    m_performanceTab->applyAppearance();
    m_applicationsTab->applyAppearance();
    m_processesTab->applyAppearance();
    m_servicesTab->applyAppearance();
}

void MainWindow::retranslateUi() {
    const QString title = QStringLiteral("SightProc");
    setWindowTitle(title);
    QApplication::setApplicationDisplayName(title);

    m_tabs->setTabText(0, trText("Performance", "Performans"));
    m_tabs->setTabText(1, trText("Applications", "Uygulamalar"));
    m_tabs->setTabText(2, trText("Processes", "Süreçler"));
    m_tabs->setTabText(3, trText("Services", "Servisler"));

    m_intervalLabel->setText(trText("Refresh:", "Yenileme:"));
    m_pauseButton->setText(m_pauseButton->isChecked()
                               ? trText("Resume", "Devam Et")
                               : trText("Pause", "Duraklat"));

    m_fileMenu->setTitle(trText("&File", "&Dosya"));
    m_refreshAct->setText(trText("Refresh Services", "Servisleri Yenile"));
    m_pauseAct->setText(trText("Pause Updates", "Güncellemeleri Duraklat"));
    m_quitAct->setText(trText("Quit", "Çıkış"));

    m_editMenu->setTitle(trText("&Edit", "Dü&zenle"));
    m_prefsAct->setText(trText("Preferences...", "Tercihler..."));
    m_configDirAct->setText(trText("Config Directory", "Yapılandırma Dizini"));

    m_viewMenu->setTitle(trText("&View", "&Görünüm"));
    m_tabActs[0]->setText(trText("Performance Tab", "Performans Sekmesi"));
    m_tabActs[1]->setText(trText("Applications Tab", "Uygulamalar Sekmesi"));
    m_tabActs[2]->setText(trText("Processes Tab", "Süreçler Sekmesi"));
    m_tabActs[3]->setText(trText("Services Tab", "Servisler Sekmesi"));
    m_treeModeAct->setText(trText("Process Tree View", "Süreç Ağacı Görünümü"));
    m_expandAllAct->setText(trText("Expand All Processes", "Tüm Süreçleri Genişlet"));
    m_collapseAllAct->setText(trText("Collapse All Processes", "Tüm Süreçleri Daralt"));
    m_zoomInAct->setText(trText("Zoom In Active Tab", "Etkin Sekmeyi Büyüt"));
    m_zoomOutAct->setText(trText("Zoom Out Active Tab", "Etkin Sekmeyi Küçült"));
    m_zoomResetAct->setText(trText("Reset Active Tab Scale", "Etkin Sekme Ölçeğini Sıfırla"));
    m_refreshMenu->setTitle(trText("Refresh Interval", "Yenileme Aralığı"));

    m_helpMenu->setTitle(trText("&Help", "&Yardım"));
    m_shortcutsAct->setText(trText("Shortcuts & Tips", "Kısayollar ve İpuçları"));
    m_aboutAct->setText(trText("About", "Hakkında"));

    m_performanceTab->retranslateUi();
    m_applicationsTab->retranslateUi();
    m_processesTab->retranslateUi();
    m_servicesTab->retranslateUi();
    updateStyleStatus();
}

void MainWindow::changeEvent(QEvent* event) {
    if (event->type() == QEvent::StyleChange ||
        event->type() == QEvent::ApplicationPaletteChange ||
        event->type() == QEvent::PaletteChange) {
        applyAllAppearances();
        updateStyleStatus();
    }
    QMainWindow::changeEvent(event);
}

void MainWindow::closeEvent(QCloseEvent* event) {
    if (m_performanceTab) {
        m_performanceTab->saveLayoutState();
    }
    shutdownWorkers();
    QMainWindow::closeEvent(event);
}

void MainWindow::updateStyleStatus() {
    const QStyle* activeStyle = QApplication::style();
    const QString styleName = activeStyle ? activeStyle->objectName() : QStringLiteral("unknown");

    if (!m_hasLastSnapshot) {
        statusBar()->showMessage(
            trText("Starting telemetry — Style: %1", "Telemetri başlatılıyor — Stil: %1")
                .arg(styleName));
        return;
    }

    const QString pausedTag = (m_pauseButton && m_pauseButton->isChecked())
        ? trText(" [PAUSED]", " [DURAKLATILDI]")
        : QString{};
    const QString sessionSuffix = m_lastSnapshot.displaySession.isEmpty()
        ? QString{}
        : QStringLiteral("  |  %1").arg(m_lastSnapshot.displaySession);

    statusBar()->showMessage(
        QStringLiteral("CPU: %1  |  %2: %3 / %4 (%5)  |  %6: %7%8%9")
            .arg(fmt::formatPercent(m_lastSnapshot.cpu.totalUsagePercent),
                 trText("RAM", "Bellek"),
                 fmt::formatBytes(m_lastSnapshot.memory.usedBytes),
                 fmt::formatBytes(m_lastSnapshot.memory.totalBytes),
                 fmt::formatPercent(m_lastSnapshot.memory.usagePercent),
                 trText("Style", "Stil"),
                 styleName,
                 sessionSuffix,
                 pausedTag));
}

} // namespace sysmon
