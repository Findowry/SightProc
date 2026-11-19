#include "ui/ProcessesTab.h"

#include "core/AppSettings.h"
#include "core/Lang.h"
#include "models/ProcessFilterProxyModel.h"
#include "models/ProcessTreeModel.h"
#include "ui/ProcessDetailsDialog.h"
#include "widgets/ThemeUtils.h"

#include <QAbstractItemView>
#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QEvent>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QIcon>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QProcess>
#include <QPushButton>
#include <QScrollBar>
#include <QTreeView>
#include <QVBoxLayout>

#include <algorithm>
#include <cerrno>
#include <csignal>
#include <cstring>
#include <sys/resource.h>

namespace sysmon {

namespace {

struct SignalDef {
    int sig;
    const char* name;
    const char* enDesc;
    const char* trDesc;
    bool destructive;
};

QVector<SignalDef> allPosixSignals() {
    QVector<SignalDef> list;
    auto add = [&](int s, const char* n, const char* en, const char* tr, bool dest = false) {
        list.push_back(SignalDef{s, n, en, tr, dest});
    };

#ifdef SIGHUP
    add(SIGHUP, "SIGHUP (1)", "Hangup", "Bağlantıyı Kes / Yenile", false);
#endif
#ifdef SIGINT
    add(SIGINT, "SIGINT (2)", "Interrupt", "Kesme (Ctrl+C)", false);
#endif
#ifdef SIGQUIT
    add(SIGQUIT, "SIGQUIT (3)", "Quit (Core Dump)", "Çıkış (Çekirdek Dökümü)", true);
#endif
#ifdef SIGILL
    add(SIGILL, "SIGILL (4)", "Illegal Instruction", "Geçersiz Yönerge", true);
#endif
#ifdef SIGTRAP
    add(SIGTRAP, "SIGTRAP (5)", "Trace / Breakpoint", "Hata Ayıklama Tuzağı", false);
#endif
#ifdef SIGABRT
    add(SIGABRT, "SIGABRT (6)", "Abort", "Durdur / İptal Et", true);
#endif
#ifdef SIGBUS
    add(SIGBUS, "SIGBUS (7)", "Bus Error", "Veriyolu Hatası", true);
#endif
#ifdef SIGFPE
    add(SIGFPE, "SIGFPE (8)", "Floating-Point Exception", "Kayan Nokta Hatası", true);
#endif
#ifdef SIGKILL
    add(SIGKILL, "SIGKILL (9)", "Force Kill", "Zorla Öldür", true);
#endif
#ifdef SIGUSR1
    add(SIGUSR1, "SIGUSR1 (10)", "User Signal 1", "Kullanıcı Sinyali 1", false);
#endif
#ifdef SIGSEGV
    add(SIGSEGV, "SIGSEGV (11)", "Segmentation Fault", "Bellek Erişim İhlali", true);
#endif
#ifdef SIGUSR2
    add(SIGUSR2, "SIGUSR2 (12)", "User Signal 2", "Kullanıcı Sinyali 2", false);
#endif
#ifdef SIGPIPE
    add(SIGPIPE, "SIGPIPE (13)", "Broken Pipe", "Kırık Boru", false);
#endif
#ifdef SIGALRM
    add(SIGALRM, "SIGALRM (14)", "Alarm Clock", "Zamanlayıcı Alarmı", false);
#endif
#ifdef SIGTERM
    add(SIGTERM, "SIGTERM (15)", "Terminate", "Normal Sonlandır", false);
#endif
#ifdef SIGSTKFLT
    add(SIGSTKFLT, "SIGSTKFLT (16)", "Stack Fault", "Yığın Hatası", true);
#endif
#ifdef SIGCHLD
    add(SIGCHLD, "SIGCHLD (17)", "Child Status Changed", "Alt Süreç Değişti", false);
#endif
#ifdef SIGCONT
    add(SIGCONT, "SIGCONT (18)", "Continue", "Devam Ettir", false);
#endif
#ifdef SIGSTOP
    add(SIGSTOP, "SIGSTOP (19)", "Stop (Pause)", "Duraklat", false);
#endif
#ifdef SIGTSTP
    add(SIGTSTP, "SIGTSTP (20)", "Terminal Stop", "Uçbirim Duraklatması", false);
#endif
#ifdef SIGTTIN
    add(SIGTTIN, "SIGTTIN (21)", "Background Read", "Arka Plan Okuma", false);
#endif
#ifdef SIGTTOU
    add(SIGTTOU, "SIGTTOU (22)", "Background Write", "Arka Plan Yazma", false);
#endif
#ifdef SIGURG
    add(SIGURG, "SIGURG (23)", "Urgent Socket Data", "Acil Soket Verisi", false);
#endif
#ifdef SIGXCPU
    add(SIGXCPU, "SIGXCPU (24)", "CPU Limit Exceeded", "CPU Süre Sınırı Aşıldı", true);
#endif
#ifdef SIGXFSZ
    add(SIGXFSZ, "SIGXFSZ (25)", "File Size Limit", "Dosya Boyut Sınırı", true);
#endif
#ifdef SIGVTALRM
    add(SIGVTALRM, "SIGVTALRM (26)", "Virtual Timer", "Sanal Zamanlayıcı", false);
#endif
#ifdef SIGPROF
    add(SIGPROF, "SIGPROF (27)", "Profiling Timer", "Profil Zamanlayıcı", false);
#endif
#ifdef SIGWINCH
    add(SIGWINCH, "SIGWINCH (28)", "Window Resize", "Pencere Boyut Değişimi", false);
#endif
#ifdef SIGIO
    add(SIGIO, "SIGIO (29)", "I/O Possible", "G/Ç Hazır", false);
#endif
#ifdef SIGPWR
    add(SIGPWR, "SIGPWR (30)", "Power Failure", "Güç Kesintisi", true);
#endif
#ifdef SIGSYS
    add(SIGSYS, "SIGSYS (31)", "Bad System Call", "Geçersiz Sistem Çağrısı", true);
#endif
    return list;
}

} // namespace

ProcessesTab::ProcessesTab(QWidget* parent)
    : QWidget(parent) {
    setupUi();
    applyProcessPrefs();
    applyAppearance();
    retranslateUi();
}

void ProcessesTab::setupUi() {
    auto* rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(8, 8, 8, 8);
    rootLayout->setSpacing(6);

    auto* topBar = new QHBoxLayout();
    m_searchEdit = new QLineEdit(this);
    m_searchEdit->setClearButtonEnabled(true);

    m_filterCombo = new QComboBox(this);
    m_filterCombo->addItem(QString(), static_cast<int>(ProcessFilterProxyModel::QuickFilter::All));
    m_filterCombo->addItem(QString(), static_cast<int>(ProcessFilterProxyModel::QuickFilter::CurrentUser));
    m_filterCombo->addItem(QString(), static_cast<int>(ProcessFilterProxyModel::QuickFilter::HighCpu));
    m_filterCombo->addItem(QString(), static_cast<int>(ProcessFilterProxyModel::QuickFilter::UsingGpu));

    m_countLabel = new QLabel(this);

    topBar->addWidget(m_searchEdit, 1);
    topBar->addWidget(m_filterCombo);
    topBar->addWidget(m_countLabel);
    rootLayout->addLayout(topBar);

    m_pidBanner = new QFrame(this);
    m_pidBanner->setFrameShape(QFrame::StyledPanel);
    m_pidBanner->setVisible(false);
    auto* bannerLayout = new QHBoxLayout(m_pidBanner);
    bannerLayout->setContentsMargins(8, 4, 8, 4);
    m_pidBannerLabel = new QLabel(m_pidBanner);
    m_clearPidBtn = new QPushButton(m_pidBanner);
    bannerLayout->addWidget(m_pidBannerLabel, 1);
    bannerLayout->addWidget(m_clearPidBtn);
    rootLayout->addWidget(m_pidBanner);

    m_model = new ProcessTreeModel(this);
    m_proxy = new ProcessFilterProxyModel(this);
    m_proxy->setSourceProcessModel(m_model);

    m_rowDelegate = new ScaledRowDelegate(26, this);

    m_treeView = new QTreeView(this);
    m_treeView->setModel(m_proxy);
    m_treeView->setItemDelegate(m_rowDelegate);
    m_treeView->setAlternatingRowColors(true);
    m_treeView->setUniformRowHeights(true);
    m_treeView->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_treeView->setSelectionMode(QAbstractItemView::SingleSelection);
    m_treeView->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_treeView->setSortingEnabled(true);
    m_treeView->sortByColumn(ProcessTreeModel::ColCpu, Qt::DescendingOrder);
    m_treeView->setContextMenuPolicy(Qt::CustomContextMenu);
    m_treeView->setIndentation(16);
    m_treeView->setAnimated(false);
    m_treeView->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_treeView->setTextElideMode(Qt::ElideRight);

    // Tüm sütunları Interactive yaparak ters yönlü sütun boyutlandırma hatasını önle
    QHeaderView* hdr = m_treeView->header();
    hdr->setSectionResizeMode(QHeaderView::Interactive);
    hdr->setStretchLastSection(false);
    hdr->setCascadingSectionResizes(false);
    hdr->setMinimumSectionSize(56);
    hdr->setMaximumSectionSize(4000);
    hdr->viewport()->installEventFilter(this);
    m_treeView->setColumnWidth(ProcessTreeModel::ColName, 320);
    m_treeView->setColumnWidth(ProcessTreeModel::ColPid, 72);
    m_treeView->setColumnWidth(ProcessTreeModel::ColUser, 96);
    m_treeView->setColumnWidth(ProcessTreeModel::ColState, 118);
    m_treeView->setColumnWidth(ProcessTreeModel::ColCpu, 76);
    m_treeView->setColumnWidth(ProcessTreeModel::ColMemoryRss, 102);
    m_treeView->setColumnWidth(ProcessTreeModel::ColMemoryShared, 116);
    m_treeView->setColumnWidth(ProcessTreeModel::ColDiskIo, 114);
    m_treeView->setColumnWidth(ProcessTreeModel::ColNetwork, 98);
    m_treeView->setColumnWidth(ProcessTreeModel::ColGpuPercent, 74);
    m_treeView->setColumnWidth(ProcessTreeModel::ColGpuMemory, 98);

    rootLayout->addWidget(m_treeView, 1);

    auto* zoomFilter = new ZoomWheelFilter(2, this);
    installEventFilter(zoomFilter);
    m_treeView->installEventFilter(zoomFilter);
    m_treeView->viewport()->installEventFilter(zoomFilter);

    connect(m_model, &ProcessTreeModel::aboutToRebuild,
            this, &ProcessesTab::onAboutToRebuild);
    connect(m_model, &ProcessTreeModel::rebuilt,
            this, &ProcessesTab::onRebuilt);

    connect(m_searchEdit, &QLineEdit::textChanged, this, [this](const QString& text) {
        m_proxy->setSearchText(text);
        if (!text.trimmed().isEmpty() && m_model->treeMode()) {
            m_treeView->expandAll();
        }
        updateCountLabel();
    });
    connect(m_filterCombo, qOverload<int>(&QComboBox::currentIndexChanged),
            this, [this](int index) {
                const auto filter = static_cast<ProcessFilterProxyModel::QuickFilter>(
                    m_filterCombo->itemData(index).toInt());
                m_proxy->setQuickFilter(filter);
                if (filter != ProcessFilterProxyModel::QuickFilter::All && m_model->treeMode()) {
                    m_treeView->expandAll();
                }
                updateCountLabel();
            });
    connect(m_clearPidBtn, &QPushButton::clicked, this, &ProcessesTab::clearPidFilter);
    connect(m_treeView, &QTreeView::customContextMenuRequested,
            this, &ProcessesTab::onContextMenuRequested);
    connect(m_treeView, &QTreeView::doubleClicked,
            this, &ProcessesTab::onDoubleClicked);
}

void ProcessesTab::applyAppearance() {
    const auto& s = AppSettings::instance();
    const int scale = s.tabScalePercent(2);
    const double factor = static_cast<double>(scale) / 100.0;

    QFont f = QApplication::font();
    const double basePt = f.pointSizeF() > 0 ? f.pointSizeF() : 10.0;
    f.setPointSizeF(std::max(7.5, basePt * factor));
    setFont(f);
    m_treeView->setFont(f);
    m_treeView->header()->setFont(f);

    const int iconPx = std::clamp(static_cast<int>(18.0 * factor), 14, 40);
    m_treeView->setIconSize(QSize(iconPx, iconPx));
    m_rowDelegate->setScalePercent(scale);
    m_treeView->doItemsLayout();

    applyTranslucentViewPalette(m_treeView, s.tableOpacityPercent());
}

void ProcessesTab::applyProcessPrefs() {
    const auto& s = AppSettings::instance();
    const bool tree = s.processTreeMode();
    m_treeView->setRootIsDecorated(tree);
    m_treeView->setItemsExpandable(tree);
    m_model->setTreeMode(tree);
    m_model->setShowIcons(s.showProcessIcons());
}

void ProcessesTab::retranslateUi() {
    m_searchEdit->setPlaceholderText(
        trText("Search process name, PID, user or command...",
               "Süreç adı, PID, kullanıcı veya komut ara..."));

    m_filterCombo->setItemText(0, trText("All", "Tümü"));
    m_filterCombo->setItemText(1, trText("My Processes", "Kullanıcı Süreçlerim"));
    m_filterCombo->setItemText(2, trText("High CPU (>1%)", "Yüksek CPU (>%1)"));
    m_filterCombo->setItemText(3, trText("Using GPU", "GPU Kullananlar"));

    m_clearPidBtn->setText(trText("Clear Filter", "Filtreyi Temizle"));
    if (m_pidBanner->isVisible()) {
        m_pidBannerLabel->setText(
            trText("Application filter active: %1 (%2 processes)",
                   "Uygulama filtresi etkin: %1 (%2 süreç)")
                .arg(m_pidBannerAppName)
                .arg(m_pidBannerCount));
    }

    m_model->retranslate();
    updateCountLabel();
}

void ProcessesTab::expandAll() {
    m_treeView->expandAll();
}

void ProcessesTab::collapseAll() {
    m_treeView->collapseAll();
}

bool ProcessesTab::eventFilter(QObject* watched, QEvent* event) {
    if (watched == m_treeView->header()->viewport()) {
        if (event->type() == QEvent::MouseButtonPress) {
            m_headerDragging = true;
        } else if (event->type() == QEvent::MouseButtonRelease ||
                   event->type() == QEvent::FocusOut) {
            if (m_headerDragging) {
                m_headerDragging = false;
                if (m_hasPendingSnapshot && !m_contextMenuOpen) {
                    m_hasPendingSnapshot = false;
                    onProcessSnapshot(m_pendingSnapshot);
                }
            }
        }
    }
    return QWidget::eventFilter(watched, event);
}

void ProcessesTab::onProcessSnapshot(const ProcessSnapshot& snapshot) {
    if (m_contextMenuOpen || m_headerDragging) {
        m_pendingSnapshot = snapshot;
        m_hasPendingSnapshot = true;
        return;
    }

    m_totalProcesses = snapshot.totalProcessCount;
    m_totalThreads = snapshot.totalThreadCount;
    m_proxy->setCurrentUserUid(snapshot.currentUserUid);
    m_model->updateProcesses(snapshot.processes);
    updateCountLabel();

    if (m_detailsDialog) {
        const QModelIndex idx = m_model->indexForPid(
            selectedProcess() ? selectedProcess()->pid : -1);
        if (idx.isValid()) {
            if (const ProcessInfo* p = m_model->processForIndex(idx)) {
                m_detailsDialog->updateLiveInfo(*p);
            }
        }
    }
}

void ProcessesTab::onAboutToRebuild() {
    m_savedScrollVal = m_treeView->verticalScrollBar()->value();
    m_savedHScrollVal = m_treeView->horizontalScrollBar()->value();
    m_savedSelectedPid = selectedProcess() ? selectedProcess()->pid : -1;
    m_savedExpandedPids.clear();
    if (m_model->treeMode()) {
        collectExpandedPids(QModelIndex(), m_savedExpandedPids);
    }
}

void ProcessesTab::onRebuilt() {
    if (m_model->treeMode()) {
        if (m_proxy->hasActiveFilter()) {
            m_treeView->expandAll();
        } else if (!m_savedExpandedPids.isEmpty()) {
            restoreExpandedPids(QModelIndex());
        }
    }
    if (m_savedSelectedPid > 0) {
        const QModelIndex srcIdx = m_model->indexForPid(m_savedSelectedPid);
        if (srcIdx.isValid()) {
            const QModelIndex proxyIdx = m_proxy->mapFromSource(srcIdx);
            if (proxyIdx.isValid()) {
                m_treeView->setCurrentIndex(proxyIdx);
            }
        }
    }
    m_treeView->verticalScrollBar()->setValue(m_savedScrollVal);
    m_treeView->horizontalScrollBar()->setValue(m_savedHScrollVal);
}

void ProcessesTab::collectExpandedPids(const QModelIndex& parent, QSet<int>& outPids) const {
    const int rows = m_proxy->rowCount(parent);
    for (int r = 0; r < rows; ++r) {
        const QModelIndex proxyIdx = m_proxy->index(r, 0, parent);
        if (m_treeView->isExpanded(proxyIdx)) {
            const QModelIndex srcIdx = m_proxy->mapToSource(proxyIdx);
            if (const ProcessInfo* p = m_model->processForIndex(srcIdx)) {
                outPids.insert(p->pid);
            }
            collectExpandedPids(proxyIdx, outPids);
        }
    }
}

void ProcessesTab::restoreExpandedPids(const QModelIndex& parent) {
    const int rows = m_proxy->rowCount(parent);
    for (int r = 0; r < rows; ++r) {
        const QModelIndex proxyIdx = m_proxy->index(r, 0, parent);
        const QModelIndex srcIdx = m_proxy->mapToSource(proxyIdx);
        if (const ProcessInfo* p = m_model->processForIndex(srcIdx)) {
            if (m_savedExpandedPids.contains(p->pid)) {
                m_treeView->setExpanded(proxyIdx, true);
                restoreExpandedPids(proxyIdx);
            }
        }
    }
}

void ProcessesTab::setPidFilter(const QString& appName, const QVector<int>& pids) {
    QSet<int> pidSet;
    for (int pid : pids) {
        pidSet.insert(pid);
    }
    m_pidBannerAppName = appName;
    m_pidBannerCount = pidSet.size();
    m_proxy->setPidFilter(pidSet);
    if (m_model->treeMode()) {
        m_treeView->expandAll();
    }
    m_pidBannerLabel->setText(
        trText("Application filter active: %1 (%2 processes)",
               "Uygulama filtresi etkin: %1 (%2 süreç)")
            .arg(appName)
            .arg(pidSet.size()));
    m_pidBanner->setVisible(true);
    updateCountLabel();
}

void ProcessesTab::clearPidFilter() {
    m_proxy->clearPidFilter();
    m_pidBanner->setVisible(false);
    updateCountLabel();
}

void ProcessesTab::updateCountLabel() {
    m_countLabel->setText(
        trText("%1 processes · %2 threads", "%1 süreç · %2 iş parçacığı")
            .arg(m_totalProcesses)
            .arg(m_totalThreads));
}

const ProcessInfo* ProcessesTab::selectedProcess() const {
    const QModelIndex proxyIdx = m_treeView->currentIndex();
    if (!proxyIdx.isValid()) {
        return nullptr;
    }
    const QModelIndex srcIdx = m_proxy->mapToSource(proxyIdx);
    return m_model->processForIndex(srcIdx);
}

void ProcessesTab::onDoubleClicked(const QModelIndex& proxyIdx) {
    if (!proxyIdx.isValid()) {
        return;
    }
    const QModelIndex srcIdx = m_proxy->mapToSource(proxyIdx);
    if (const ProcessInfo* proc = m_model->processForIndex(srcIdx)) {
        openDetailsForProcess(*proc);
    }
}

void ProcessesTab::openDetailsForProcess(const ProcessInfo& proc) {
    if (m_detailsDialog) {
        m_detailsDialog->close();
        m_detailsDialog->deleteLater();
    }
    m_detailsDialog = new ProcessDetailsDialog(proc, this);
    m_detailsDialog->setAttribute(Qt::WA_DeleteOnClose);
    m_detailsDialog->show();
}

void ProcessesTab::onContextMenuRequested(const QPoint& pos) {
    const QModelIndex clickedIdx = m_treeView->indexAt(pos);
    if (clickedIdx.isValid()) {
        m_treeView->setCurrentIndex(clickedIdx);
    }

    const ProcessInfo* procPtr = selectedProcess();
    if (!procPtr) {
        return;
    }
    const ProcessInfo proc = *procPtr;

    m_contextMenuOpen = true;

    QMenu menu(this);

    QAction* detailsAct = menu.addAction(
        QIcon::fromTheme(QStringLiteral("dialog-information")),
        trText("Details...", "Ayrıntılar..."));
    menu.addSeparator();

    QAction* termAct = menu.addAction(
        QIcon::fromTheme(QStringLiteral("process-stop")),
        trText("Terminate (SIGTERM)", "Sonlandır (SIGTERM)"));
    QAction* killAct = menu.addAction(
        QIcon::fromTheme(QStringLiteral("edit-delete")),
        trText("Force Kill (SIGKILL)", "Zorla Öldür (SIGKILL)"));
    QAction* stopAct = menu.addAction(
        QIcon::fromTheme(QStringLiteral("media-playback-pause")),
        trText("Suspend (SIGSTOP)", "Duraklat (SIGSTOP)"));
    QAction* contAct = menu.addAction(
        QIcon::fromTheme(QStringLiteral("media-playback-start")),
        trText("Resume (SIGCONT)", "Devam Ettir (SIGCONT)"));

    QMenu* allSignalsMenu = menu.addMenu(
        QIcon::fromTheme(QStringLiteral("system-run")),
        trText("Send Signal (All SIG...)", "Ek Sinyal Gönder (Tüm SIG...)"));
    for (const SignalDef& sd : allPosixSignals()) {
        const QString desc = (currentLanguage() == Language::Turkish)
            ? QString::fromUtf8(sd.trDesc)
            : QString::fromUtf8(sd.enDesc);
        QAction* sigAct = allSignalsMenu->addAction(
            QStringLiteral("%1 — %2").arg(QString::fromLatin1(sd.name), desc));
        connect(sigAct, &QAction::triggered, this, [this, proc, sd]() {
            if (sd.destructive && AppSettings::instance().confirmSigkill()) {
                const auto reply = QMessageBox::warning(
                    this,
                    trText("Send Destructive Signal", "Yıkıcı Sinyal Gönder"),
                    trText("Send %1 to %2 (PID %3)?\nThis may terminate the process immediately.",
                           "%2 (PID %3) sürecine %1 gönderilsin mi?\nSüreç hemen sonlanabilir.")
                        .arg(QString::fromLatin1(sd.name), proc.name)
                        .arg(proc.pid),
                    QMessageBox::Yes | QMessageBox::No,
                    QMessageBox::No);
                if (reply != QMessageBox::Yes) {
                    return;
                }
            }
            sendSignalToProcess(proc.pid, proc.name, sd.sig, QString::fromLatin1(sd.name));
        });
    }

    menu.addSeparator();

    QMenu* niceMenu = menu.addMenu(
        trText("Change Priority (nice: %1)", "Öncelik Değiştir (nice: %1)").arg(proc.niceValue));
    struct NicePreset {
        const char* enLabel;
        const char* trLabel;
        int value;
    };
    const NicePreset presets[] = {
        {"Very High (-10)", "Çok Yüksek (-10)", -10},
        {"High (-5)", "Yüksek (-5)", -5},
        {"Normal (0)", "Normal (0)", 0},
        {"Low (+5)", "Düşük (+5)", 5},
        {"Very Low (+15)", "Çok Düşük (+15)", 15},
    };
    for (const auto& p : presets) {
        QAction* act = niceMenu->addAction(trText(p.enLabel, p.trLabel));
        act->setCheckable(true);
        act->setChecked(proc.niceValue == p.value);
        connect(act, &QAction::triggered, this, [this, proc, val = p.value]() {
            setProcessNice(proc.pid, proc.name, val);
        });
    }
    niceMenu->addSeparator();
    QAction* customNiceAct = niceMenu->addAction(
        trText("Custom Value (-20..19)...", "Özel Değer (-20..19)..."));

    menu.addSeparator();
    QAction* copyPidAct = menu.addAction(trText("Copy PID", "PID Kopyala"));
    QAction* copyCmdAct = menu.addAction(trText("Copy Command Line", "Komut Satırını Kopyala"));

    if (m_model->treeMode()) {
        menu.addSeparator();
        QAction* expBranchAct = menu.addAction(trText("Expand Branch", "Dalı Genişlet"));
        QAction* expAllAct = menu.addAction(trText("Expand All", "Tümünü Genişlet"));
        QAction* colAllAct = menu.addAction(trText("Collapse All", "Tümünü Daralt"));
        connect(expBranchAct, &QAction::triggered, this, [this, clickedIdx]() {
            if (clickedIdx.isValid()) {
                m_treeView->expandRecursively(clickedIdx);
            }
        });
        connect(expAllAct, &QAction::triggered, this, &ProcessesTab::expandAll);
        connect(colAllAct, &QAction::triggered, this, &ProcessesTab::collapseAll);
    }

    connect(detailsAct, &QAction::triggered, this, [this, proc]() {
        openDetailsForProcess(proc);
    });
    connect(termAct, &QAction::triggered, this, [this, proc]() {
        sendSignalToProcess(proc.pid, proc.name, SIGTERM, QStringLiteral("SIGTERM"));
    });
    connect(killAct, &QAction::triggered, this, [this, proc]() {
        if (AppSettings::instance().confirmSigkill()) {
            const auto reply = QMessageBox::warning(
                this,
                trText("Force Kill Process", "Süreci Zorla Öldür"),
                trText("Send SIGKILL to %1 (PID %2)?\nUnsaved data may be lost.",
                       "%1 (PID %2) sürecine SIGKILL gönderilsin mi?\nKaydedilmemiş veriler kaybolabilir.")
                    .arg(proc.name)
                    .arg(proc.pid),
                QMessageBox::Yes | QMessageBox::No,
                QMessageBox::No);
            if (reply != QMessageBox::Yes) {
                return;
            }
        }
        sendSignalToProcess(proc.pid, proc.name, SIGKILL, QStringLiteral("SIGKILL"));
    });
    connect(stopAct, &QAction::triggered, this, [this, proc]() {
        sendSignalToProcess(proc.pid, proc.name, SIGSTOP, QStringLiteral("SIGSTOP"));
    });
    connect(contAct, &QAction::triggered, this, [this, proc]() {
        sendSignalToProcess(proc.pid, proc.name, SIGCONT, QStringLiteral("SIGCONT"));
    });
    connect(customNiceAct, &QAction::triggered, this, [this, proc]() {
        bool ok = false;
        const int val = QInputDialog::getInt(
            this,
            trText("Nice Priority", "Nice Önceliği"),
            trText("Nice value for %1 (PID %2) (-20 highest, 19 lowest):",
                   "%1 (PID %2) için nice değeri (-20 en yüksek, 19 en düşük):")
                .arg(proc.name)
                .arg(proc.pid),
            proc.niceValue,
            -20,
            19,
            1,
            &ok);
        if (ok) {
            setProcessNice(proc.pid, proc.name, val);
        }
    });
    connect(copyPidAct, &QAction::triggered, this, [proc]() {
        QApplication::clipboard()->setText(QString::number(proc.pid));
    });
    connect(copyCmdAct, &QAction::triggered, this, [proc]() {
        QApplication::clipboard()->setText(proc.cmdline);
    });

    menu.exec(m_treeView->viewport()->mapToGlobal(pos));
    m_contextMenuOpen = false;
    if (m_hasPendingSnapshot) {
        m_hasPendingSnapshot = false;
        onProcessSnapshot(m_pendingSnapshot);
    }
}

void ProcessesTab::sendSignalToProcess(int pid,
                                       const QString& procName,
                                       int sig,
                                       const QString& sigLabel) {
    if (::kill(static_cast<pid_t>(pid), sig) == 0) {
        return;
    }

    const int err = errno;
    if (err == ESRCH) {
        return;
    }
    if (err == EPERM || err == EACCES) {
        const auto reply = QMessageBox::question(
            this,
            trText("Permission Required", "Yetki Gerekli"),
            trText("Administrator privileges are required to send %1 to %2 (PID %3).\nRetry elevated via pkexec?",
                   "%2 (PID %3) sürecine %1 göndermek için yönetici yetkisi gerekiyor.\npkexec ile yetki yükseltilerek denensin mi?")
                .arg(sigLabel, procName)
                .arg(pid),
            QMessageBox::Yes | QMessageBox::No,
            QMessageBox::Yes);
        if (reply == QMessageBox::Yes) {
            runPkexecAction(
                {QStringLiteral("kill"), QStringLiteral("-%1").arg(sig), QString::number(pid)},
                trText("Process Signal", "Süreç Sinyali"));
        }
        return;
    }

    QMessageBox::warning(
        this,
        trText("Signal Error", "Sinyal Hatası"),
        trText("Failed to send %1 to %2 (PID %3): %4",
               "%2 (PID %3) için %1 gönderilemedi: %4")
            .arg(sigLabel, procName)
            .arg(pid)
            .arg(QString::fromLocal8Bit(std::strerror(err))));
}

void ProcessesTab::setProcessNice(int pid, const QString& procName, int niceValue) {
    errno = 0;
    if (::setpriority(PRIO_PROCESS, static_cast<id_t>(pid), niceValue) == 0) {
        return;
    }

    const int err = errno;
    if (err == ESRCH) {
        return;
    }
    if (err == EPERM || err == EACCES) {
        const auto reply = QMessageBox::question(
            this,
            trText("Permission Required", "Yetki Gerekli"),
            trText("Administrator privileges are required to set nice %1 on %2 (PID %3).\nRetry elevated via pkexec?",
                   "%2 (PID %3) nice değerini %1 yapmak için yönetici yetkisi gerekiyor.\npkexec renice ile denensin mi?")
                .arg(niceValue)
                .arg(procName)
                .arg(pid),
            QMessageBox::Yes | QMessageBox::No,
            QMessageBox::Yes);
        if (reply == QMessageBox::Yes) {
            runPkexecAction(
                {QStringLiteral("renice"),
                 QStringLiteral("-n"),
                 QString::number(niceValue),
                 QStringLiteral("-p"),
                 QString::number(pid)},
                trText("Nice Priority", "Nice Önceliği"));
        }
        return;
    }

    QMessageBox::warning(
        this,
        trText("Priority Error", "Öncelik Hatası"),
        trText("Failed to change nice value on %1 (PID %2): %3",
               "%1 (PID %2) nice değeri değiştirilemedi: %3")
            .arg(procName)
            .arg(pid)
            .arg(QString::fromLocal8Bit(std::strerror(err))));
}

void ProcessesTab::runPkexecAction(const QStringList& args, const QString& actionTitle) {
    auto* proc = new QProcess(this);
    connect(proc, &QProcess::finished,
            this, [this, proc, actionTitle](int exitCode, QProcess::ExitStatus status) {
                const QString errOut = QString::fromUtf8(proc->readAllStandardError()).trimmed();
                if ((status != QProcess::NormalExit || exitCode != 0) && exitCode != 126 && exitCode != 127) {
                    QMessageBox::warning(
                        this,
                        actionTitle,
                        errOut.isEmpty()
                            ? trText("Action failed (exit code: %1).",
                                     "İşlem başarısız oldu (kod: %1).")
                                  .arg(exitCode)
                            : errOut);
                }
                proc->deleteLater();
            });
    proc->start(QStringLiteral("pkexec"), args);
}

} // namespace sysmon
