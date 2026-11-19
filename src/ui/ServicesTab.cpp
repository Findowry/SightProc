#include "ui/ServicesTab.h"

#include "core/AppSettings.h"
#include "core/Lang.h"
#include "models/ServiceTableModel.h"
#include "widgets/ThemeUtils.h"

#include <QAbstractItemView>
#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QIcon>
#include <QItemSelectionModel>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSplitter>
#include <QTableView>
#include <QTextCursor>
#include <QVBoxLayout>

#include <algorithm>

namespace sysmon {

ServicesTab::ServicesTab(QWidget* parent)
    : QWidget(parent) {
    setupUi();
    retranslateUi();
    applyAppearance();
    refresh();
}

ServicesTab::~ServicesTab() {
    shutdown();
}

void ServicesTab::shutdown() {
    if (m_controller) {
        m_controller->stopAll();
    }
}

void ServicesTab::setupUi() {
    m_controller = new SystemdController(this);

    auto* rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(8, 8, 8, 8);
    rootLayout->setSpacing(6);

    auto* topBar = new QHBoxLayout();

    m_scopeCombo = new QComboBox(this);
    m_scopeCombo->addItem(QString(), static_cast<int>(ServiceScope::System));
    m_scopeCombo->addItem(QString(), static_cast<int>(ServiceScope::User));

    m_statusCombo = new QComboBox(this);
    m_statusCombo->addItem(QString(), static_cast<int>(ServiceFilterProxyModel::StatusFilter::All));
    m_statusCombo->addItem(QString(), static_cast<int>(ServiceFilterProxyModel::StatusFilter::Active));
    m_statusCombo->addItem(QString(), static_cast<int>(ServiceFilterProxyModel::StatusFilter::Inactive));
    m_statusCombo->addItem(QString(), static_cast<int>(ServiceFilterProxyModel::StatusFilter::Failed));

    m_searchEdit = new QLineEdit(this);
    m_searchEdit->setClearButtonEnabled(true);

    m_startBtn = new QPushButton(QIcon::fromTheme(QStringLiteral("media-playback-start")), QString(), this);
    m_stopBtn = new QPushButton(QIcon::fromTheme(QStringLiteral("media-playback-stop")), QString(), this);
    m_restartBtn = new QPushButton(QIcon::fromTheme(QStringLiteral("view-refresh")), QString(), this);
    m_reloadBtn = new QPushButton(this);
    m_enableBtn = new QPushButton(this);
    m_disableBtn = new QPushButton(this);
    m_refreshBtn = new QPushButton(QIcon::fromTheme(QStringLiteral("view-refresh")), QString(), this);

    topBar->addWidget(m_scopeCombo);
    topBar->addWidget(m_statusCombo);
    topBar->addWidget(m_searchEdit, 1);
    topBar->addWidget(m_startBtn);
    topBar->addWidget(m_stopBtn);
    topBar->addWidget(m_restartBtn);
    topBar->addWidget(m_reloadBtn);
    topBar->addWidget(m_enableBtn);
    topBar->addWidget(m_disableBtn);
    topBar->addWidget(m_refreshBtn);
    rootLayout->addLayout(topBar);

    m_statusLabel = new QLabel(this);
    m_statusLabel->setWordWrap(true);
    rootLayout->addWidget(m_statusLabel);

    auto* splitter = new QSplitter(Qt::Vertical, this);
    splitter->setChildrenCollapsible(false);

    m_model = new ServiceTableModel(this);
    m_proxy = new ServiceFilterProxyModel(this);
    m_proxy->setSourceServiceModel(m_model);

    m_rowDelegate = new ScaledRowDelegate(26, this);

    m_tableView = new QTableView(splitter);
    m_tableView->setModel(m_proxy);
    m_tableView->setItemDelegate(m_rowDelegate);
    m_tableView->setAlternatingRowColors(true);
    m_tableView->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_tableView->setSelectionMode(QAbstractItemView::SingleSelection);
    m_tableView->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_tableView->setSortingEnabled(true);
    m_tableView->sortByColumn(ServiceTableModel::ColUnit, Qt::AscendingOrder);
    m_tableView->verticalHeader()->setVisible(false);
    m_tableView->setContextMenuPolicy(Qt::CustomContextMenu);

    // Tüm sütunlar Interactive, ters boyutlandırma hatası yok
    QHeaderView* hdr = m_tableView->horizontalHeader();
    hdr->setSectionResizeMode(QHeaderView::Interactive);
    hdr->setStretchLastSection(false);
    hdr->setMinimumSectionSize(56);
    m_tableView->setColumnWidth(ServiceTableModel::ColUnit, 260);
    m_tableView->setColumnWidth(ServiceTableModel::ColLoad, 86);
    m_tableView->setColumnWidth(ServiceTableModel::ColActive, 86);
    m_tableView->setColumnWidth(ServiceTableModel::ColSub, 96);
    m_tableView->setColumnWidth(ServiceTableModel::ColStartup, 96);
    m_tableView->setColumnWidth(ServiceTableModel::ColDescription, 460);

    auto* logPanel = new QWidget(splitter);
    auto* logLayout = new QVBoxLayout(logPanel);
    logLayout->setContentsMargins(0, 4, 0, 0);
    logLayout->setSpacing(4);

    auto* logBar = new QHBoxLayout();
    m_logHeaderLabel = new QLabel(logPanel);
    m_followBtn = new QPushButton(logPanel);
    m_followBtn->setCheckable(true);
    m_clearLogBtn = new QPushButton(logPanel);
    logBar->addWidget(m_logHeaderLabel, 1);
    logBar->addWidget(m_followBtn);
    logBar->addWidget(m_clearLogBtn);
    logLayout->addLayout(logBar);

    m_logView = new QPlainTextEdit(logPanel);
    m_logView->setReadOnly(true);
    m_logView->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    logLayout->addWidget(m_logView, 1);

    splitter->addWidget(m_tableView);
    splitter->addWidget(logPanel);
    splitter->setStretchFactor(0, 3);
    splitter->setStretchFactor(1, 2);

    rootLayout->addWidget(splitter, 1);

    auto* zoomFilter = new ZoomWheelFilter(3, this);
    installEventFilter(zoomFilter);
    m_tableView->installEventFilter(zoomFilter);
    m_tableView->viewport()->installEventFilter(zoomFilter);
    m_logView->viewport()->installEventFilter(zoomFilter);

    connect(m_controller, &SystemdController::unitsLoaded,
            this, &ServicesTab::onUnitsLoaded);
    connect(m_controller, &SystemdController::actionFinished,
            this, &ServicesTab::onActionFinished);
    connect(m_controller, &SystemdController::logsReplaced,
            this, &ServicesTab::onLogsReplaced);
    connect(m_controller, &SystemdController::logsAppended,
            this, &ServicesTab::onLogsAppended);

    connect(m_scopeCombo, qOverload<int>(&QComboBox::currentIndexChanged),
            this, &ServicesTab::refresh);
    connect(m_statusCombo, qOverload<int>(&QComboBox::currentIndexChanged),
            this, [this](int idx) {
                const auto f = static_cast<ServiceFilterProxyModel::StatusFilter>(
                    m_statusCombo->itemData(idx).toInt());
                m_proxy->setStatusFilter(f);
            });
    connect(m_searchEdit, &QLineEdit::textChanged, this, [this](const QString& txt) {
        m_proxy->setSearchText(txt);
    });
    connect(m_refreshBtn, &QPushButton::clicked, this, &ServicesTab::refresh);

    auto wireActionBtn = [this](QPushButton* btn, const QString& verb) {
        connect(btn, &QPushButton::clicked, this, [this, verb]() {
            if (const ServiceInfo* s = selectedService()) {
                m_controller->performAction(currentScope(), s->unitName, verb);
            }
        });
    };
    wireActionBtn(m_startBtn, QStringLiteral("start"));
    wireActionBtn(m_stopBtn, QStringLiteral("stop"));
    wireActionBtn(m_restartBtn, QStringLiteral("restart"));
    wireActionBtn(m_reloadBtn, QStringLiteral("reload"));
    wireActionBtn(m_enableBtn, QStringLiteral("enable"));
    wireActionBtn(m_disableBtn, QStringLiteral("disable"));

    connect(m_tableView->selectionModel(), &QItemSelectionModel::currentChanged,
            this, &ServicesTab::onSelectionChanged);
    connect(m_tableView, &QTableView::customContextMenuRequested,
            this, &ServicesTab::onContextMenuRequested);
    connect(m_followBtn, &QPushButton::toggled, this, [this](bool) {
        fetchLogsForSelected();
    });
    connect(m_clearLogBtn, &QPushButton::clicked, m_logView, &QPlainTextEdit::clear);

    updateActionStates();
}

void ServicesTab::applyAppearance() {
    const auto& s = AppSettings::instance();
    const int scale = s.tabScalePercent(3);
    const double factor = static_cast<double>(scale) / 100.0;

    QFont f = QApplication::font();
    const double basePt = f.pointSizeF() > 0 ? f.pointSizeF() : 10.0;
    f.setPointSizeF(std::max(7.5, basePt * factor));
    setFont(f);
    m_tableView->setFont(f);
    m_tableView->horizontalHeader()->setFont(f);

    QFont mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    const double monoPt = mono.pointSizeF() > 0 ? mono.pointSizeF() : 10.0;
    mono.setPointSizeF(std::max(7.5, monoPt * factor));
    m_logView->setFont(mono);

    m_rowDelegate->setScalePercent(scale);
    m_tableView->doItemsLayout();

    const int opacity = s.tableOpacityPercent();
    applyTranslucentViewPalette(m_tableView, opacity);
    applyTranslucentViewPalette(m_logView, opacity);
}

void ServicesTab::retranslateUi() {
    m_scopeCombo->setItemText(0, trText("System Services", "Sistem Servisleri"));
    m_scopeCombo->setItemText(1, trText("User Services (--user)", "Kullanıcı Servisleri (--user)"));

    m_statusCombo->setItemText(0, trText("All", "Tümü"));
    m_statusCombo->setItemText(1, trText("Active", "Aktif"));
    m_statusCombo->setItemText(2, trText("Inactive", "Pasif"));
    m_statusCombo->setItemText(3, trText("Failed", "Hatalı"));

    m_searchEdit->setPlaceholderText(trText("Search services...", "Servis ara..."));
    m_startBtn->setText(trText("Start", "Başlat"));
    m_stopBtn->setText(trText("Stop", "Durdur"));
    m_restartBtn->setText(trText("Restart", "Yeniden Başlat"));
    m_reloadBtn->setText(trText("Reload", "Yeniden Yükle"));
    m_enableBtn->setText(trText("Enable", "Etkinleştir"));
    m_disableBtn->setText(trText("Disable", "Devre Dışı"));
    m_refreshBtn->setText(trText("Refresh", "Yenile"));

    m_followBtn->setText(trText("Live Follow (-f)", "Canlı Takip (-f)"));
    m_clearLogBtn->setText(trText("Clear", "Temizle"));
    m_logView->setPlaceholderText(
        trText("journalctl logs for the selected service will appear here.",
               "Seçili servisin journalctl günlükleri burada görüntülenecektir."));

    m_logHeaderLabel->setText(
        m_selectedUnit.isEmpty()
            ? trText("Journal Logs", "Journal Günlükleri")
            : QStringLiteral("%1 — %2").arg(trText("Journal Logs", "Journal Günlükleri"),
                                            m_selectedUnit));

    m_model->retranslate();
}

ServiceScope ServicesTab::currentScope() const {
    return static_cast<ServiceScope>(m_scopeCombo->currentData().toInt());
}

void ServicesTab::refresh() {
    m_statusLabel->setText(trText("Loading systemd units...", "systemd birimleri yükleniyor..."));
    m_controller->refreshUnits(currentScope());
}

void ServicesTab::onUnitsLoaded(const QVector<ServiceInfo>& units, const QString& warningMsg) {
    m_model->updateServices(units);

    if (!warningMsg.isEmpty()) {
        m_statusLabel->setText(
            QStringLiteral("%1 (%2 %3)").arg(warningMsg).arg(units.size()).arg(trText("units", "birim")));
    } else {
        m_statusLabel->setText(
            trText("%1 service units loaded.", "%1 servis birimi yüklendi.").arg(units.size()));
    }
    updateActionStates();
}

void ServicesTab::onActionFinished(bool success, const QString& message) {
    m_statusLabel->setText(message);
    if (!success) {
        QMessageBox::warning(
            this,
            trText("Service Action", "Servis İşlemi"),
            message);
    }
}

const ServiceInfo* ServicesTab::selectedService() const {
    const QModelIndex proxyIdx = m_tableView->currentIndex();
    if (!proxyIdx.isValid()) {
        return nullptr;
    }
    const QModelIndex srcIdx = m_proxy->mapToSource(proxyIdx);
    return m_model->serviceAt(srcIdx.row());
}

void ServicesTab::updateActionStates() {
    const bool hasSel = (selectedService() != nullptr);
    m_startBtn->setEnabled(hasSel);
    m_stopBtn->setEnabled(hasSel);
    m_restartBtn->setEnabled(hasSel);
    m_reloadBtn->setEnabled(hasSel);
    m_enableBtn->setEnabled(hasSel);
    m_disableBtn->setEnabled(hasSel);
}

void ServicesTab::onSelectionChanged(const QModelIndex&, const QModelIndex&) {
    updateActionStates();
    fetchLogsForSelected();
}

void ServicesTab::fetchLogsForSelected() {
    const ServiceInfo* s = selectedService();
    if (!s) {
        m_selectedUnit.clear();
        m_logHeaderLabel->setText(trText("Journal Logs", "Journal Günlükleri"));
        m_controller->stopLogs();
        return;
    }
    m_selectedUnit = s->unitName;
    m_logHeaderLabel->setText(
        QStringLiteral("%1 — %2").arg(trText("Journal Logs", "Journal Günlükleri"),
                                        m_selectedUnit));
    m_controller->fetchLogs(currentScope(), m_selectedUnit, 150, m_followBtn->isChecked());
}

void ServicesTab::onLogsReplaced(const QString& text) {
    m_logView->setPlainText(text);
}

void ServicesTab::onLogsAppended(const QString& chunk) {
    m_logView->moveCursor(QTextCursor::End);
    m_logView->insertPlainText(chunk);
    m_logView->moveCursor(QTextCursor::End);
}

void ServicesTab::onContextMenuRequested(const QPoint& pos) {
    const QModelIndex idx = m_tableView->indexAt(pos);
    if (idx.isValid()) {
        m_tableView->setCurrentIndex(idx);
    }
    const ServiceInfo* s = selectedService();
    if (!s) {
        return;
    }
    const QString unit = s->unitName;
    const ServiceScope scope = currentScope();

    QMenu menu(this);
    auto addCmd = [&](const QString& label, const QString& verb) {
        QAction* act = menu.addAction(label);
        connect(act, &QAction::triggered, this, [this, scope, unit, verb]() {
            m_controller->performAction(scope, unit, verb);
        });
    };

    addCmd(trText("Start", "Başlat"), QStringLiteral("start"));
    addCmd(trText("Stop", "Durdur"), QStringLiteral("stop"));
    addCmd(trText("Restart", "Yeniden Başlat"), QStringLiteral("restart"));
    addCmd(trText("Reload", "Yeniden Yükle"), QStringLiteral("reload"));
    menu.addSeparator();
    addCmd(trText("Enable at Boot", "Açılışta Etkinleştir"), QStringLiteral("enable"));
    addCmd(trText("Disable at Boot", "Açılışta Devre Dışı Bırak"), QStringLiteral("disable"));
    menu.addSeparator();
    QAction* copyAct = menu.addAction(trText("Copy Unit Name", "Birim Adını Kopyala"));
    connect(copyAct, &QAction::triggered, this, [unit]() {
        QApplication::clipboard()->setText(unit);
    });

    menu.exec(m_tableView->viewport()->mapToGlobal(pos));
}

} // namespace sysmon
