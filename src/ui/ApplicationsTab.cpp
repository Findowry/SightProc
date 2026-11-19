#include "ui/ApplicationsTab.h"

#include "core/AppSettings.h"
#include "core/Lang.h"
#include "models/ApplicationTableModel.h"
#include "widgets/ThemeUtils.h"

#include <QAbstractItemView>
#include <QAction>
#include <QApplication>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QSortFilterProxyModel>
#include <QTableView>
#include <QVBoxLayout>

#include <algorithm>
#include <cerrno>
#include <csignal>
#include <cstring>

namespace sysmon {

ApplicationsTab::ApplicationsTab(QWidget* parent)
    : QWidget(parent) {
    setupUi();
    retranslateUi();
    applyAppearance();
}

void ApplicationsTab::setupUi() {
    auto* rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(8, 8, 8, 8);
    rootLayout->setSpacing(6);

    auto* topBar = new QHBoxLayout();
    m_searchEdit = new QLineEdit(this);
    m_searchEdit->setClearButtonEnabled(true);

    m_countLabel = new QLabel(this);

    topBar->addWidget(m_searchEdit, 1);
    topBar->addWidget(m_countLabel);
    rootLayout->addLayout(topBar);

    m_model = new ApplicationTableModel(this);
    m_proxy = new QSortFilterProxyModel(this);
    m_proxy->setSourceModel(m_model);
    m_proxy->setSortRole(Qt::UserRole);
    m_proxy->setFilterCaseSensitivity(Qt::CaseInsensitive);
    m_proxy->setFilterKeyColumn(ApplicationTableModel::ColName);
    m_proxy->setDynamicSortFilter(true);

    m_rowDelegate = new ScaledRowDelegate(32, this);

    m_tableView = new QTableView(this);
    m_tableView->setModel(m_proxy);
    m_tableView->setItemDelegate(m_rowDelegate);
    m_tableView->setAlternatingRowColors(true);
    m_tableView->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_tableView->setSelectionMode(QAbstractItemView::SingleSelection);
    m_tableView->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_tableView->setSortingEnabled(true);
    m_tableView->sortByColumn(ApplicationTableModel::ColCpu, Qt::DescendingOrder);
    m_tableView->verticalHeader()->setVisible(false);
    m_tableView->setContextMenuPolicy(Qt::CustomContextMenu);

    // Tüm sütunları Interactive yaparak ters yönlü yeniden boyutlandırma hatasını önle
    QHeaderView* hdr = m_tableView->horizontalHeader();
    hdr->setSectionResizeMode(QHeaderView::Interactive);
    hdr->setStretchLastSection(false);
    hdr->setMinimumSectionSize(64);
    m_tableView->setColumnWidth(ApplicationTableModel::ColName, 290);
    m_tableView->setColumnWidth(ApplicationTableModel::ColCpu, 96);
    m_tableView->setColumnWidth(ApplicationTableModel::ColMemoryRss, 132);
    m_tableView->setColumnWidth(ApplicationTableModel::ColMemoryShared, 148);
    m_tableView->setColumnWidth(ApplicationTableModel::ColDiskIo, 130);
    m_tableView->setColumnWidth(ApplicationTableModel::ColNetwork, 120);
    m_tableView->setColumnWidth(ApplicationTableModel::ColGpuPercent, 92);
    m_tableView->setColumnWidth(ApplicationTableModel::ColGpuVram, 118);

    rootLayout->addWidget(m_tableView, 1);

    auto* zoomFilter = new ZoomWheelFilter(1, this);
    installEventFilter(zoomFilter);
    m_tableView->installEventFilter(zoomFilter);
    m_tableView->viewport()->installEventFilter(zoomFilter);

    connect(m_searchEdit, &QLineEdit::textChanged, this, [this](const QString& text) {
        m_proxy->setFilterFixedString(text.trimmed());
        updateCountLabel();
    });
    connect(m_tableView, &QTableView::customContextMenuRequested,
            this, &ApplicationsTab::onContextMenuRequested);
}

void ApplicationsTab::applyAppearance() {
    const auto& s = AppSettings::instance();
    const int scale = s.tabScalePercent(1);
    const double factor = static_cast<double>(scale) / 100.0;

    QFont f = QApplication::font();
    const double basePt = f.pointSizeF() > 0 ? f.pointSizeF() : 10.0;
    f.setPointSizeF(std::max(7.5, basePt * factor));
    setFont(f);
    m_tableView->setFont(f);
    m_tableView->horizontalHeader()->setFont(f);

    const int iconPx = std::clamp(static_cast<int>(22.0 * factor), 16, 48);
    m_tableView->setIconSize(QSize(iconPx, iconPx));
    m_rowDelegate->setScalePercent(scale);
    m_tableView->doItemsLayout();

    applyTranslucentViewPalette(m_tableView, s.tableOpacityPercent());
}

void ApplicationsTab::retranslateUi() {
    m_searchEdit->setPlaceholderText(trText("Search applications...", "Uygulama ara..."));
    m_model->retranslate();
    updateCountLabel();
}

void ApplicationsTab::onProcessSnapshot(const ProcessSnapshot& snapshot) {
    m_model->updateApplications(snapshot.applications);
    updateCountLabel();
}

void ApplicationsTab::updateCountLabel() {
    const int shown = m_proxy->rowCount();
    const int total = m_model->rowCount();
    if (shown == total) {
        m_countLabel->setText(
            QStringLiteral("%1 %2").arg(total).arg(trText("applications", "uygulama")));
    } else {
        m_countLabel->setText(
            trText("Showing: %1 / %2 applications", "Gösterilen: %1 / %2 uygulama")
                .arg(shown)
                .arg(total));
    }
}

const ApplicationInfo* ApplicationsTab::selectedApplication() const {
    const QModelIndex proxyIdx = m_tableView->currentIndex();
    if (!proxyIdx.isValid()) {
        return nullptr;
    }
    const QModelIndex srcIdx = m_proxy->mapToSource(proxyIdx);
    return m_model->applicationAt(srcIdx.row());
}

void ApplicationsTab::onContextMenuRequested(const QPoint& pos) {
    const QModelIndex clickedIdx = m_tableView->indexAt(pos);
    if (clickedIdx.isValid()) {
        m_tableView->setCurrentIndex(clickedIdx);
    }

    const ApplicationInfo* appPtr = selectedApplication();
    if (!appPtr) {
        return;
    }
    const ApplicationInfo app = *appPtr;

    QMenu menu(this);
    QAction* closeAct = menu.addAction(
        QIcon::fromTheme(QStringLiteral("window-close")),
        trText("Close Application (SIGTERM)", "Uygulamayı Kapat (SIGTERM)"));
    QAction* killAct = menu.addAction(
        QIcon::fromTheme(QStringLiteral("edit-delete")),
        trText("Force Quit (SIGKILL)", "Zorla Sonlandır (SIGKILL)"));
    menu.addSeparator();
    QAction* showProcsAct = menu.addAction(
        QIcon::fromTheme(QStringLiteral("system-run")),
        trText("Show Child Processes in \"Processes\" Tab",
               "Alt Süreçleri \"Süreçler\" Sekmesinde Göster"));

    connect(closeAct, &QAction::triggered, this, [this, app]() {
        sendSignalToApplication(app, SIGTERM);
    });
    connect(killAct, &QAction::triggered, this, [this, app]() {
        if (AppSettings::instance().confirmSigkill()) {
            const auto reply = QMessageBox::warning(
                this,
                trText("Force Quit Application", "Uygulamayı Zorla Sonlandır"),
                trText("Send SIGKILL to all %1 processes belonging to %2?\nUnsaved data may be lost.",
                       "%2 uygulamasına ait %1 sürecin tamamına SIGKILL gönderilsin mi?\nKaydedilmemiş veriler kaybolabilir.")
                    .arg(app.pids.size())
                    .arg(app.name),
                QMessageBox::Yes | QMessageBox::No,
                QMessageBox::No);
            if (reply != QMessageBox::Yes) {
                return;
            }
        }
        sendSignalToApplication(app, SIGKILL);
    });
    connect(showProcsAct, &QAction::triggered, this, [this, app]() {
        emit showApplicationProcessesRequested(app.name, app.pids);
    });

    menu.exec(m_tableView->viewport()->mapToGlobal(pos));
}

void ApplicationsTab::sendSignalToApplication(const ApplicationInfo& app, int sig) {
    int lastErr = 0;
    int failCount = 0;
    for (int pid : app.pids) {
        if (::kill(static_cast<pid_t>(pid), sig) != 0) {
            if (errno != ESRCH) {
                lastErr = errno;
                ++failCount;
            }
        }
    }
    if (failCount > 0) {
        QMessageBox::warning(
            this,
            trText("Application Signal Error", "Uygulama Sinyal Hatası"),
            trText("Could not signal %1 child processes of %2: %3",
                   "%2 uygulamasının %1 alt sürecine sinyal gönderilemedi: %3")
                .arg(failCount)
                .arg(app.name, QString::fromLocal8Bit(std::strerror(lastErr))));
    }
}

} // namespace sysmon
