#include "ui/ProcessDetailsDialog.h"

#include "core/AppSettings.h"
#include "core/FormatUtils.h"
#include "core/Lang.h"
#include "widgets/ThemeUtils.h"

#include <QAbstractItemView>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QTabWidget>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

namespace sysmon {

namespace {

QString readProcSymlink(int pid, const char* leaf) {
    char path[128];
    char target[4096];
    std::snprintf(path, sizeof(path), "/proc/%d/%s", pid, leaf);
    const ssize_t len = ::readlink(path, target, sizeof(target) - 1);
    if (len <= 0) {
        return QStringLiteral("—");
    }
    target[len] = '\0';
    return QString::fromUtf8(target, static_cast<qsizetype>(len));
}

void initReadOnlyTable(QTableWidget* table, const QStringList& headers, int w0, int w1) {
    table->setColumnCount(headers.size());
    table->setHorizontalHeaderLabels(headers);
    table->setAlternatingRowColors(true);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->verticalHeader()->setVisible(false);
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    table->horizontalHeader()->setStretchLastSection(false);
    table->setColumnWidth(0, w0);
    table->setColumnWidth(1, w1);
    applyTranslucentViewPalette(table, AppSettings::instance().tableOpacityPercent());
}

} // namespace

ProcessDetailsDialog::ProcessDetailsDialog(const ProcessInfo& info, QWidget* parent)
    : QDialog(parent)
    , m_info(info) {
    setupUi();
    populateOverview();
    refreshProcFiles();
}

void ProcessDetailsDialog::setupUi() {
    setWindowTitle(QStringLiteral("%1 — %2 (PID %3)")
                       .arg(trText("Process Details", "Süreç Ayrıntıları"), m_info.name)
                       .arg(m_info.pid));
    resize(700, 540);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(10, 10, 10, 10);
    root->setSpacing(8);

    m_headerLabel = new QLabel(this);
    QFont hf = m_headerLabel->font();
    hf.setBold(true);
    hf.setPointSizeF(hf.pointSizeF() + 1.5);
    m_headerLabel->setFont(hf);
    m_headerLabel->setText(
        QStringLiteral("%1  (PID %2 · %3)")
            .arg(m_info.name)
            .arg(m_info.pid)
            .arg(m_info.user));
    root->addWidget(m_headerLabel);

    m_tabs = new QTabWidget(this);

    // 1. Genel Bakış
    auto* overviewScroll = new QScrollArea(m_tabs);
    overviewScroll->setWidgetResizable(true);
    overviewScroll->setFrameShape(QFrame::NoFrame);
    auto* overviewWidget = new QWidget(overviewScroll);
    m_overviewForm = new QFormLayout(overviewWidget);
    m_overviewForm->setContentsMargins(8, 8, 8, 8);
    m_overviewForm->setHorizontalSpacing(16);
    m_overviewForm->setVerticalSpacing(7);
    overviewScroll->setWidget(overviewWidget);
    m_tabs->addTab(overviewScroll, trText("Overview", "Genel Bakış"));

    // 2. Bellek ve Durum
    m_statusTable = new QTableWidget(m_tabs);
    initReadOnlyTable(m_statusTable,
                      {trText("Property", "Özellik"), trText("Value", "Değer")},
                      240,
                      380);
    m_tabs->addTab(m_statusTable, trText("Memory & Status", "Bellek ve Durum"));

    // 3. Ortam Değişkenleri
    auto* envPage = new QWidget(m_tabs);
    auto* envLayout = new QVBoxLayout(envPage);
    envLayout->setContentsMargins(6, 6, 6, 6);
    m_envSearch = new QLineEdit(envPage);
    m_envSearch->setPlaceholderText(trText("Filter environment...", "Ortam değişkeni ara..."));
    m_envSearch->setClearButtonEnabled(true);
    m_envTable = new QTableWidget(envPage);
    initReadOnlyTable(m_envTable,
                      {trText("Variable", "Değişken"), trText("Value", "Değer")},
                      210,
                      410);
    envLayout->addWidget(m_envSearch);
    envLayout->addWidget(m_envTable, 1);
    m_tabs->addTab(envPage, trText("Environment", "Ortam"));

    // 4. Açık Dosyalar ve Soketler
    m_fdTable = new QTableWidget(m_tabs);
    initReadOnlyTable(m_fdTable,
                      {trText("FD", "FD"), trText("Target", "Hedef")},
                      80,
                      540);
    m_tabs->addTab(m_fdTable, trText("Open Files & Sockets", "Açık Dosyalar ve Soketler"));

    root->addWidget(m_tabs, 1);

    auto* btnRow = new QHBoxLayout();
    m_refreshBtn = new QPushButton(trText("Refresh", "Yenile"), this);
    m_closeBtn = new QPushButton(trText("Close", "Kapat"), this);
    btnRow->addStretch(1);
    btnRow->addWidget(m_refreshBtn);
    btnRow->addWidget(m_closeBtn);
    root->addLayout(btnRow);

    connect(m_refreshBtn, &QPushButton::clicked, this, &ProcessDetailsDialog::refreshProcFiles);
    connect(m_closeBtn, &QPushButton::clicked, this, &QDialog::accept);
    connect(m_envSearch, &QLineEdit::textChanged, this, &ProcessDetailsDialog::filterEnvironTable);
}

void ProcessDetailsDialog::updateLiveInfo(const ProcessInfo& info) {
    if (info.pid == m_info.pid) {
        m_info = info;
        populateOverview();
    }
}

void ProcessDetailsDialog::populateOverview() {
    while (m_overviewForm->rowCount() > 0) {
        const auto taken = m_overviewForm->takeRow(0);
        delete taken.labelItem;
        delete taken.fieldItem;
    }
    const QList<QWidget*> oldWidgets =
        m_overviewForm->parentWidget()->findChildren<QWidget*>(
            QString(), Qt::FindDirectChildrenOnly);
    for (QWidget* w : oldWidgets) {
        delete w;
    }

    const QString cwd = readProcSymlink(m_info.pid, "cwd");
    const QString exe = m_info.exePath.isEmpty() ? readProcSymlink(m_info.pid, "exe") : m_info.exePath;

    const QPair<QString, QString> rows[] = {
        {trText("Process Name", "Süreç Adı"), m_info.name},
        {trText("PID / Parent PID", "PID / Üst PID"),
         QStringLiteral("%1 / %2").arg(m_info.pid).arg(m_info.ppid)},
        {trText("User (UID)", "Kullanıcı (UID)"),
         QStringLiteral("%1 (%2)").arg(m_info.user).arg(m_info.uid)},
        {trText("State", "Durum"), processStateText(m_info.stateChar)},
        {trText("Nice Priority", "Nice Önceliği"), QString::number(m_info.niceValue)},
        {trText("Threads", "İş Parçacığı"), QString::number(m_info.threads)},
        {trText("CPU Usage", "CPU Kullanımı"), fmt::formatPercent(m_info.cpuPercent)},
        {trText("Resident Memory (RSS)", "Fiziksel Bellek (RSS)"), fmt::formatBytes(m_info.rssBytes)},
        {trText("Shared Memory", "Paylaşılan Bellek"), fmt::formatBytes(m_info.sharedBytes)},
        {trText("Disk Read / Write", "Disk Okuma / Yazma"),
         m_info.ioReadable
             ? QStringLiteral("%1 / %2")
                   .arg(fmt::formatRate(m_info.diskReadBytesPerSec),
                        fmt::formatRate(m_info.diskWriteBytesPerSec))
             : QStringLiteral("—")},
        {trText("Network Rx / Tx", "Ağ İndirme / Yükleme"),
         m_info.netReadable
             ? QStringLiteral("%1 / %2 (%3 TCP)")
                   .arg(fmt::formatRate(m_info.netRxBytesPerSec),
                        fmt::formatRate(m_info.netTxBytesPerSec))
                   .arg(m_info.tcpSocketCount)
             : QStringLiteral("—")},
        {trText("GPU Usage / Memory", "GPU Kullanımı / Belleği"),
         m_info.hasGpuMetrics
             ? QStringLiteral("%1 / %2")
                   .arg(fmt::formatPercent(m_info.gpuPercent),
                        fmt::formatBytes(m_info.gpuMemBytes))
             : QStringLiteral("—")},
        {trText("Executable", "Yürütülebilir Dosya"), exe},
        {trText("Working Directory", "Çalışma Dizini"), cwd},
        {trText("Command Line", "Komut Satırı"), m_info.cmdline},
        {trText("Cgroup Path", "Cgroup Yolu"),
         m_info.cgroupPath.isEmpty() ? QStringLiteral("—") : m_info.cgroupPath},
    };

    for (const auto& [k, v] : rows) {
        auto* kLbl = new QLabel(k + QLatin1Char(':'), m_overviewForm->parentWidget());
        auto* vLbl = new QLabel(v, m_overviewForm->parentWidget());
        vLbl->setWordWrap(true);
        vLbl->setTextInteractionFlags(Qt::TextSelectableByMouse);
        m_overviewForm->addRow(kLbl, vLbl);
    }
}

void ProcessDetailsDialog::refreshProcFiles() {
    populateStatusTab();
    populateEnvironTab();
    populateFdTab();
}

void ProcessDetailsDialog::populateStatusTab() {
    m_statusTable->setRowCount(0);

    auto appendKv = [&](const QString& k, const QString& v) {
        const int r = m_statusTable->rowCount();
        m_statusTable->insertRow(r);
        m_statusTable->setItem(r, 0, new QTableWidgetItem(k));
        m_statusTable->setItem(r, 1, new QTableWidgetItem(v));
    };

    char path[128];
    std::snprintf(path, sizeof(path), "/proc/%d/status", m_info.pid);
    std::ifstream statusFile(path);
    std::string line;
    while (std::getline(statusFile, line)) {
        const auto colon = line.find(':');
        if (colon == std::string::npos) {
            continue;
        }
        const QString key = QString::fromStdString(line.substr(0, colon)).trimmed();
        const QString val = QString::fromStdString(line.substr(colon + 1)).trimmed();
        if (key == QStringLiteral("VmSize") || key == QStringLiteral("VmRSS") ||
            key == QStringLiteral("RssAnon") || key == QStringLiteral("RssFile") ||
            key == QStringLiteral("RssShmem") || key == QStringLiteral("VmSwap") ||
            key == QStringLiteral("Threads") || key == QStringLiteral("voluntary_ctxt_switches") ||
            key == QStringLiteral("nonvoluntary_ctxt_switches") ||
            key == QStringLiteral("Seccomp") || key == QStringLiteral("CapEff")) {
            appendKv(key, val);
        }
    }

    std::snprintf(path, sizeof(path), "/proc/%d/smaps_rollup", m_info.pid);
    std::ifstream smapsFile(path);
    while (std::getline(smapsFile, line)) {
        const auto colon = line.find(':');
        if (colon == std::string::npos) {
            continue;
        }
        const QString key = QString::fromStdString(line.substr(0, colon)).trimmed();
        const QString val = QString::fromStdString(line.substr(colon + 1)).trimmed();
        if (key == QStringLiteral("Pss") || key == QStringLiteral("Private_Dirty") ||
            key == QStringLiteral("SwapPss")) {
            appendKv(key + QStringLiteral(" (smaps_rollup)"), val);
        }
    }
}

void ProcessDetailsDialog::populateEnvironTab() {
    m_envTable->setRowCount(0);

    char path[128];
    std::snprintf(path, sizeof(path), "/proc/%d/environ", m_info.pid);
    const int fd = ::open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        m_envTable->insertRow(0);
        m_envTable->setItem(0, 0, new QTableWidgetItem(trText("Permission", "Yetki")));
        m_envTable->setItem(
            0,
            1,
            new QTableWidgetItem(trText("Cannot read /proc/[pid]/environ (EACCES).",
                                        "/proc/[pid]/environ okunamadı (EACCES).")));
        return;
    }

    std::string raw;
    char buf[4096];
    for (;;) {
        const ssize_t n = ::read(fd, buf, sizeof(buf));
        if (n <= 0) {
            break;
        }
        raw.append(buf, static_cast<std::size_t>(n));
        if (raw.size() >= 262144) {
            break;
        }
    }
    ::close(fd);

    std::size_t start = 0;
    while (start < raw.size()) {
        std::size_t end = raw.find('\0', start);
        if (end == std::string::npos) {
            end = raw.size();
        }
        const std::string entry = raw.substr(start, end - start);
        start = end + 1;
        if (entry.empty()) {
            continue;
        }
        const auto eq = entry.find('=');
        const QString k = (eq == std::string::npos)
            ? QString::fromUtf8(entry)
            : QString::fromUtf8(entry.substr(0, eq));
        const QString v = (eq == std::string::npos)
            ? QString()
            : QString::fromUtf8(entry.substr(eq + 1));

        const int r = m_envTable->rowCount();
        m_envTable->insertRow(r);
        m_envTable->setItem(r, 0, new QTableWidgetItem(k));
        m_envTable->setItem(r, 1, new QTableWidgetItem(v));
    }
    filterEnvironTable(m_envSearch->text());
}

void ProcessDetailsDialog::filterEnvironTable(const QString& text) {
    const QString needle = text.trimmed();
    for (int r = 0; r < m_envTable->rowCount(); ++r) {
        const QString k = m_envTable->item(r, 0) ? m_envTable->item(r, 0)->text() : QString();
        const QString v = m_envTable->item(r, 1) ? m_envTable->item(r, 1)->text() : QString();
        const bool match = needle.isEmpty() ||
            k.contains(needle, Qt::CaseInsensitive) ||
            v.contains(needle, Qt::CaseInsensitive);
        m_envTable->setRowHidden(r, !match);
    }
}

void ProcessDetailsDialog::populateFdTab() {
    m_fdTable->setRowCount(0);

    char path[128];
    std::snprintf(path, sizeof(path), "/proc/%d/fd", m_info.pid);
    DIR* dir = ::opendir(path);
    if (!dir) {
        m_fdTable->insertRow(0);
        m_fdTable->setItem(0, 0, new QTableWidgetItem(QStringLiteral("—")));
        m_fdTable->setItem(
            0,
            1,
            new QTableWidgetItem(trText("Cannot open /proc/[pid]/fd (EACCES).",
                                        "/proc/[pid]/fd açılamadı (EACCES).")));
        return;
    }

    const int dfd = ::dirfd(dir);
    char linkBuf[4096];
    int count = 0;
    while (const dirent* ent = ::readdir(dir)) {
        if (ent->d_name[0] == '.') {
            continue;
        }
        const ssize_t len = ::readlinkat(dfd, ent->d_name, linkBuf, sizeof(linkBuf) - 1);
        if (len <= 0) {
            continue;
        }
        linkBuf[len] = '\0';
        const int r = m_fdTable->rowCount();
        m_fdTable->insertRow(r);
        m_fdTable->setItem(r, 0, new QTableWidgetItem(QString::fromLatin1(ent->d_name)));
        m_fdTable->setItem(r, 1, new QTableWidgetItem(QString::fromUtf8(linkBuf, static_cast<qsizetype>(len))));
        if (++count >= 500) {
            break;
        }
    }
    ::closedir(dir);
}

} // namespace sysmon
