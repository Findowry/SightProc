#include "ui/PerformanceTab.h"

#include "core/FormatUtils.h"
#include "core/Lang.h"
#include "widgets/AreaChartWidget.h"
#include "widgets/CoreGridWidget.h"
#include "widgets/DeviceListDelegate.h"
#include "widgets/MemoryBarWidget.h"
#include "widgets/ThemeUtils.h"

#include <QAbstractItemView>
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QFont>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QLabel>
#include <QListView>
#include <QMenu>
#include <QPalette>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSplitter>
#include <QStackedWidget>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <vector>

namespace sysmon {

namespace {

template <std::size_t N>
std::vector<double> toVector(const RingBuffer<double, N>& buf, double scale = 1.0) {
    std::vector<double> out;
    out.reserve(buf.size());
    for (std::size_t i = 0; i < buf.size(); ++i) {
        out.push_back(buf[i] * scale);
    }
    return out;
}

QColor thermalSensorColor(int orderIndex) {
    const QColor win = QApplication::palette().color(QPalette::Window);
    const bool isDark = (win.lightnessF() < 0.5f);
    const float baseHue = 0.02f;
    const float hue = std::fmod(baseHue + static_cast<float>(orderIndex) * 0.16f, 1.0f);
    return QColor::fromHslF(hue, isDark ? 0.85f : 0.78f, isDark ? 0.63f : 0.45f);
}

QString translateBatteryStatus(const QString& raw) {
    if (raw == QStringLiteral("Charging")) {
        return trText("Charging", "Şarj Oluyor");
    }
    if (raw == QStringLiteral("Discharging")) {
        return trText("Discharging", "Deşarj Oluyor");
    }
    if (raw == QStringLiteral("Full")) {
        return trText("Full", "Tam Şarj");
    }
    if (raw == QStringLiteral("Not charging")) {
        return trText("Not Charging", "Şarj Olmuyor");
    }
    return raw.isEmpty() ? QStringLiteral("—") : raw;
}

} // namespace

PerformanceTab::PerformanceTab(QWidget* parent)
    : QWidget(parent) {
    setupUi();
    retranslateUi();
    applyAppearance();

    connect(&AppSettings::instance(), &AppSettings::selectedThermalSensorsChanged,
            this, [this]() {
                m_lastMainChartSignature.clear();
                if (m_hasSnapshot) {
                    refreshCenterAndSpecs();
                }
            });
}

void PerformanceTab::setupUi() {
    auto* rootLayout = new QHBoxLayout(this);
    rootLayout->setContentsMargins(8, 8, 8, 8);

    auto* splitter = new QSplitter(Qt::Horizontal, this);
    splitter->setChildrenCollapsible(false);

    // Sol panel: Donanım listesi
    m_deviceModel = new DeviceListModel(this);
    m_deviceDelegate = new DeviceListDelegate(this);
    m_deviceList = new QListView(splitter);
    m_deviceList->setModel(m_deviceModel);
    m_deviceList->setItemDelegate(m_deviceDelegate);
    m_deviceList->setUniformItemSizes(true);
    m_deviceList->setSelectionMode(QAbstractItemView::SingleSelection);
    m_deviceList->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_deviceList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_deviceList->setMinimumWidth(240);

    // Orta panel: Başlık, + Gösterge Ekle, Çekirdek Izgarası ve Serbest Dikey Splitter Grafikleri
    auto* centerPanel = new QWidget(splitter);
    auto* centerLayout = new QVBoxLayout(centerPanel);
    centerLayout->setContentsMargins(8, 0, 8, 0);
    centerLayout->setSpacing(8);

    auto* headerRow = new QHBoxLayout();
    auto* titleCol = new QVBoxLayout();
    titleCol->setSpacing(2);

    m_titleLabel = new QLabel(centerPanel);
    m_summaryLabel = new QLabel(centerPanel);
    m_summaryLabel->setWordWrap(true);

    titleCol->addWidget(m_titleLabel);
    titleCol->addWidget(m_summaryLabel);

    m_addIndicatorBtn = new QPushButton(centerPanel);
    m_coreGridToggle = new QPushButton(centerPanel);
    m_coreGridToggle->setCheckable(true);

    headerRow->addLayout(titleCol, 1);
    headerRow->addWidget(m_addIndicatorBtn, 0, Qt::AlignTop);
    headerRow->addWidget(m_coreGridToggle, 0, Qt::AlignTop);
    centerLayout->addLayout(headerRow);

    m_centerStack = new QStackedWidget(centerPanel);

    auto* chartPage = new QWidget(m_centerStack);
    auto* chartPageLayout = new QVBoxLayout(chartPage);
    chartPageLayout->setContentsMargins(0, 0, 0, 0);
    chartPageLayout->setSpacing(6);

    m_verticalSplitter = new QSplitter(Qt::Vertical, chartPage);
    m_verticalSplitter->setChildrenCollapsible(false);

    m_mainChart = new AreaChartWidget(m_verticalSplitter);
    m_mainChart->setDisplayMode(AreaChartWidget::DisplayMode::Detailed);
    m_mainChart->setMinimumHeight(140);
    m_verticalSplitter->addWidget(m_mainChart);
    m_verticalSplitter->setStretchFactor(0, 2);

    m_thermalTable = new QTableWidget(0, 4, m_verticalSplitter);
    m_thermalTable->setAlternatingRowColors(true);
    m_thermalTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_thermalTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_thermalTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_thermalTable->verticalHeader()->setVisible(false);
    m_thermalTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    m_thermalTable->horizontalHeader()->setStretchLastSection(false);
    m_thermalTable->horizontalHeader()->setCascadingSectionResizes(false);
    m_thermalTable->setColumnWidth(0, 150);
    m_thermalTable->setColumnWidth(1, 220);
    m_thermalTable->setColumnWidth(2, 110);
    m_thermalTable->setColumnWidth(3, 110);
    m_thermalTable->setMinimumHeight(110);
    m_thermalTable->setVisible(false);
    m_verticalSplitter->addWidget(m_thermalTable);

    chartPageLayout->addWidget(m_verticalSplitter, 1);

    m_memoryBar = new MemoryBarWidget(chartPage);
    m_memoryBar->setVisible(false);
    chartPageLayout->addWidget(m_memoryBar);

    m_coreGrid = new CoreGridWidget(m_centerStack);

    m_centerStack->addWidget(chartPage);
    m_centerStack->addWidget(m_coreGrid);
    centerLayout->addWidget(m_centerStack, 1);

    // Sağ panel: Teknik Künye ve Özelleştir
    auto* rightPanel = new QWidget(splitter);
    auto* rightLayout = new QVBoxLayout(rightPanel);
    rightLayout->setContentsMargins(4, 0, 4, 0);
    rightLayout->setSpacing(6);

    auto* specTopRow = new QHBoxLayout();
    m_specHeaderLabel = new QLabel(rightPanel);
    m_customizeSpecsBtn = new QPushButton(rightPanel);
    specTopRow->addWidget(m_specHeaderLabel, 1);
    specTopRow->addWidget(m_customizeSpecsBtn);
    rightLayout->addLayout(specTopRow);

    m_specsScroll = new QScrollArea(rightPanel);
    m_specsScroll->setWidgetResizable(true);
    m_specsScroll->setFrameShape(QFrame::NoFrame);

    m_specsWidget = new QWidget(m_specsScroll);
    m_specsLayout = new QFormLayout(m_specsWidget);
    m_specsLayout->setContentsMargins(0, 4, 4, 4);
    m_specsLayout->setHorizontalSpacing(14);
    m_specsLayout->setVerticalSpacing(7);
    m_specsScroll->setWidget(m_specsWidget);
    rightLayout->addWidget(m_specsScroll, 1);

    m_probeGpuCheck = new QCheckBox(rightPanel);
    m_probeGpuCheck->setChecked(AppSettings::instance().probeSleepingGpus());
    m_probeGpuCheck->setVisible(false);
    rightLayout->addWidget(m_probeGpuCheck);

    splitter->addWidget(m_deviceList);
    splitter->addWidget(centerPanel);
    splitter->addWidget(rightPanel);
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 3);
    splitter->setStretchFactor(2, 1);
    splitter->setSizes({260, 720, 320});

    rootLayout->addWidget(splitter);

    auto* zoomFilter = new ZoomWheelFilter(0, this);
    installEventFilter(zoomFilter);
    m_deviceList->viewport()->installEventFilter(zoomFilter);
    m_mainChart->installEventFilter(zoomFilter);
    m_specsScroll->viewport()->installEventFilter(zoomFilter);

    connect(m_deviceList->selectionModel(), &QItemSelectionModel::currentChanged,
            this, &PerformanceTab::onDeviceSelectionChanged);
    connect(m_coreGridToggle, &QPushButton::toggled,
            this, &PerformanceTab::onCoreGridToggled);
    connect(m_addIndicatorBtn, &QPushButton::clicked,
            this, &PerformanceTab::onAddIndicatorClicked);
    connect(m_customizeSpecsBtn, &QPushButton::clicked,
            this, &PerformanceTab::onCustomizeSpecsClicked);
    connect(m_thermalTable, &QTableWidget::itemChanged,
            this, &PerformanceTab::onThermalItemChanged);
    connect(m_probeGpuCheck, &QCheckBox::toggled, this, [this](bool on) {
        AppSettings::instance().setProbeSleepingGpus(on);
        emit probeSleepingGpusToggled(on);
    });
}

void PerformanceTab::applyAppearance() {
    const auto& s = AppSettings::instance();
    const int scale = s.tabScalePercent(0);
    const double factor = static_cast<double>(scale) / 100.0;

    QFont baseFont = QApplication::font();
    const double basePt = baseFont.pointSizeF() > 0 ? baseFont.pointSizeF() : 10.0;
    baseFont.setPointSizeF(std::max(7.5, basePt * factor));
    setFont(baseFont);

    QFont titleFont = baseFont;
    titleFont.setBold(true);
    titleFont.setPointSizeF(titleFont.pointSizeF() + 2.5);
    m_titleLabel->setFont(titleFont);

    QFont specFont = baseFont;
    specFont.setBold(true);
    m_specHeaderLabel->setFont(specFont);

    m_deviceDelegate->setScalePercent(scale);
    m_deviceList->doItemsLayout();

    const int opacity = s.tableOpacityPercent();
    applyTranslucentViewPalette(m_deviceList, opacity);
    applyTranslucentViewPalette(m_thermalTable, opacity);

    m_probeGpuCheck->setChecked(s.probeSleepingGpus());
    m_coreGrid->applyVisualSettings();
    m_memoryBar->update();

    if (m_hasSnapshot) {
        if (const DeviceEntry* entry = m_deviceModel->entryAt(m_deviceList->currentIndex().row())) {
            updateChartStyle(m_mainChart, entry->category);
            for (const auto& pair : m_extraCharts) {
                updateChartStyle(pair.second, entry->category);
            }
        }
        refreshCenterAndSpecs();
    }
}

void PerformanceTab::retranslateUi() {
    m_coreGridToggle->setText(trText("Logical Cores (Grid)", "Mantıksal Çekirdekler (Izgara)"));
    m_specHeaderLabel->setText(trText("Technical Specs", "Teknik Künye"));
    m_customizeSpecsBtn->setText(trText("Customize...", "Özelleştir..."));
    m_probeGpuCheck->setText(trText("Probe sleeping NVIDIA GPU (D3cold)",
                                    "Uyuyan NVIDIA GPU'yu yokla (D3cold)"));
    m_probeGpuCheck->setToolTip(
        trText("When enabled, sleeping dGPU is queried via NVML (may increase battery draw).",
               "Etkinleştirildiğinde uyku durumundaki harici GPU NVML ile yoklanır (pil tüketimini artırabilir)."));

    m_thermalTable->setHorizontalHeaderLabels({
        trText("Source / Chart", "Kaynak / Grafik"),
        trText("Sensor", "Sensör"),
        trText("Temperature", "Sıcaklık"),
        trText("Critical Limit", "Kritik Sınır"),
    });

    if (!m_hasSnapshot) {
        m_addIndicatorBtn->setText(trText("+ Add Indicator", "+ Gösterge Ekle"));
        m_titleLabel->setText(trText("Performance", "Performans"));
        m_summaryLabel->setText(trText("Waiting for hardware telemetry...",
                                       "Donanım telemetrisi bekleniyor..."));
    }

    m_deviceModel->retranslate();
    if (m_hasSnapshot) {
        refreshCenterAndSpecs();
    }
}

void PerformanceTab::onTelemetrySnapshot(const SystemTelemetrySnapshot& snapshot) {
    m_snapshot = snapshot;
    m_hasSnapshot = true;

    m_deviceModel->updateFromSnapshot(snapshot);
    m_coreGrid->updateCores(snapshot.cpu.cores);

    int targetRow = m_deviceModel->rowForKey(m_selectedKey);
    if (targetRow < 0 && m_deviceModel->rowCount() > 0) {
        targetRow = 0;
        if (const DeviceEntry* first = m_deviceModel->entryAt(0)) {
            m_selectedKey = first->key;
        }
    }

    if (targetRow >= 0) {
        const QModelIndex idx = m_deviceModel->index(targetRow);
        if (m_deviceList->currentIndex() != idx) {
            m_deviceList->setCurrentIndex(idx);
        }
    }

    m_pendingChartPush = true;
    refreshCenterAndSpecs();
}

void PerformanceTab::onDeviceSelectionChanged(const QModelIndex& current,
                                              const QModelIndex&) {
    if (!current.isValid()) {
        return;
    }
    if (const DeviceEntry* entry = m_deviceModel->entryAt(current.row())) {
        m_selectedKey = entry->key;
        m_lastMainChartSignature.clear();
        m_pendingChartPush = false;
        refreshCenterAndSpecs();
    }
}

void PerformanceTab::onCoreGridToggled(bool checked) {
    if (const DeviceEntry* entry = m_deviceModel->entryAt(m_deviceList->currentIndex().row())) {
        if (entry->kind == DeviceKind::Cpu) {
            m_centerStack->setCurrentIndex(checked ? 1 : 0);
        }
    }
}

QVector<PerformanceTab::IndicatorDef> PerformanceTab::availableIndicators(
    AppSettings::HardwareCategory cat) const {
    switch (cat) {
    case AppSettings::HardwareCategory::Cpu:
        return {
            {QStringLiteral("cpu_freq"), trText("Average Frequency (MHz)", "Ortalama Frekans (MHz)")},
            {QStringLiteral("cpu_temp"), trText("Package Temperature (°C)", "Paket Sıcaklığı (°C)")},
            {QStringLiteral("cpu_split"), trText("User vs System (%)", "Kullanıcı vs Sistem (%)")},
            {QStringLiteral("cpu_ctxt"), trText("Context Switches (k/s)", "Bağlam Anahtarı (k/sn)")},
        };
    case AppSettings::HardwareCategory::Memory:
        return {
            {QStringLiteral("mem_swap"), trText("Swap / ZRAM Usage (%)", "Takas / ZRAM Kullanımı (%)")},
            {QStringLiteral("mem_cache"), trText("Cache & Buffers (GiB)", "Önbellek ve Tampon (GiB)")},
        };
    case AppSettings::HardwareCategory::Disk:
        return {
            {QStringLiteral("disk_active"), trText("Active I/O Time (%)", "Disk Etkinlik Süresi (%)")},
            {QStringLiteral("disk_iops"), trText("Read / Write IOPS", "Okuma / Yazma IOPS")},
            {QStringLiteral("disk_temp"), trText("Drive Temperature (°C)", "Disk Sıcaklığı (°C)")},
        };
    case AppSettings::HardwareCategory::Network:
        return {
            {QStringLiteral("net_err"), trText("Errors & Dropped Packets", "Hata ve Kayıp Paketler")},
        };
    case AppSettings::HardwareCategory::Gpu:
        return {
            {QStringLiteral("gpu_vram"), trText("VRAM Usage (MiB)", "VRAM Kullanımı (MiB)")},
            {QStringLiteral("gpu_clock"), trText("Core Clock (MHz)", "Çekirdek Saat Hızı (MHz)")},
            {QStringLiteral("gpu_temp"), trText("GPU Temperature (°C)", "GPU Sıcaklığı (°C)")},
            {QStringLiteral("gpu_power"), trText("Power Draw (W)", "Güç Tüketimi (W)")},
        };
    case AppSettings::HardwareCategory::Fan:
        return {
            {QStringLiteral("fan_temp"), trText("Paired Temperature (°C)", "Eşleşen Sıcaklık (°C)")},
            {QStringLiteral("fan_pwm"), trText("PWM Duty Cycle (%)", "PWM Görev Döngüsü (%)")},
        };
    case AppSettings::HardwareCategory::Battery:
        return {
            {QStringLiteral("bat_power"), trText("Power Rate (W)", "Güç Hızı (W)")},
            {QStringLiteral("bat_voltage"), trText("Terminal Voltage (V)", "Uç Gerilimi (V)")},
        };
    default:
        return {};
    }
}

QVector<PerformanceTab::IndicatorDef> PerformanceTab::availableExtraSpecs(
    AppSettings::HardwareCategory cat) const {
    switch (cat) {
    case AppSettings::HardwareCategory::Cpu:
        return {
            {QStringLiteral("load_avg"), trText("Load Average (1/5/15m)", "Yük Ortalaması (1/5/15 dk)")},
            {QStringLiteral("interrupts"), trText("Interrupts / s", "Kesme Sayısı / sn")},
            {QStringLiteral("forks"), trText("Processes Created", "Oluşturulan Süreçler")},
            {QStringLiteral("scaling_bounds"), trText("Scaling Min / Max", "Ölçekleme Min / Maks")},
            {QStringLiteral("virtualization"), trText("Virtualization", "Sanallaştırma")},
            {QStringLiteral("bogo_ucode"), trText("BogoMIPS / Microcode", "BogoMIPS / Mikrokod")},
            {QStringLiteral("core_temp_range"), trText("Core Temp Min / Max", "Çekirdek Sıcaklığı Min / Maks")},
        };
    case AppSettings::HardwareCategory::Memory:
        return {
            {QStringLiteral("dirty_wb"), trText("Dirty / Writeback", "Kirli / Geri Yazılan")},
            {QStringLiteral("active_inactive"), trText("Active / Inactive", "Aktif / Pasif")},
            {QStringLiteral("committed_pt"), trText("Committed / PageTables", "Taahhüt / Sayfa Tablosu")},
        };
    case AppSettings::HardwareCategory::Disk:
        return {
            {QStringLiteral("media_type"), trText("Media Type / Block Size", "Ortam Türü / Blok Boyutu")},
            {QStringLiteral("total_ops"), trText("Total Read / Write Ops", "Toplam Okuma / Yazma İşlemi")},
        };
    case AppSettings::HardwareCategory::Network:
        return {
            {QStringLiteral("driver_duplex"), trText("Driver / Duplex", "Sürücü / Çift Yönlü")},
            {QStringLiteral("packets_carrier"), trText("Packets / Carrier Changes", "Paketler / Taşıyıcı Değişimi")},
        };
    case AppSettings::HardwareCategory::Gpu:
        return {
            {QStringLiteral("pci_ids"), trText("PCI Vendor / Device ID", "PCI Satıcı / Aygıt Kimliği")},
            {QStringLiteral("mem_ctrl_util"), trText("Memory Controller Load", "Bellek Denetleyici Yükü")},
        };
    case AppSettings::HardwareCategory::Battery:
        return {
            {QStringLiteral("bat_temp"), trText("Battery Temperature", "Batarya Sıcaklığı")},
            {QStringLiteral("bat_wear"), trText("Capacity Wear Loss", "Kapasite Aşınma Kaybı")},
        };
    default:
        return {};
    }
}

void PerformanceTab::openThermalSensorMenu() {
    if (m_snapshot.thermals.isEmpty()) {
        return;
    }

    QStringList selected = AppSettings::instance().selectedThermalSensors();
    QMenu menu(this);

    QAction* clearAct = menu.addAction(trText("Show Default Overview (CPU + Peak)",
                                              "Varsayılan Özeti Göster (CPU + Tepe)"));
    connect(clearAct, &QAction::triggered, this, []() {
        AppSettings::instance().setSelectedThermalSensors({});
    });
    menu.addSeparator();

    for (const ThermalSensor& t : m_snapshot.thermals) {
        const QString title = QStringLiteral("%1 — %2 (%3)")
                                  .arg(t.hwmonName, t.label, fmt::formatTemp(t.tempC));
        QAction* act = menu.addAction(title);
        act->setCheckable(true);
        act->setChecked(selected.contains(t.id));
        connect(act, &QAction::toggled, this, [id = t.id](bool checked) {
            QStringList list = AppSettings::instance().selectedThermalSensors();
            if (checked && !list.contains(id)) {
                list.push_back(id);
            } else if (!checked) {
                list.removeAll(id);
            }
            AppSettings::instance().setSelectedThermalSensors(list);
        });
    }

    menu.exec(m_addIndicatorBtn->mapToGlobal(QPoint(0, m_addIndicatorBtn->height())));
}

void PerformanceTab::onAddIndicatorClicked() {
    const DeviceEntry* entry = m_deviceModel->entryAt(m_deviceList->currentIndex().row());
    if (!entry) {
        return;
    }

    if (entry->kind == DeviceKind::ThermalOverview) {
        openThermalSensorMenu();
        return;
    }

    const auto defs = availableIndicators(entry->category);
    if (defs.isEmpty()) {
        return;
    }

    QStringList enabled = AppSettings::instance().enabledIndicators(entry->category);
    QMenu menu(this);
    for (const auto& def : defs) {
        QAction* act = menu.addAction(def.label);
        act->setCheckable(true);
        act->setChecked(enabled.contains(def.id));
        connect(act, &QAction::toggled, this, [cat = entry->category, id = def.id](bool checked) {
            QStringList list = AppSettings::instance().enabledIndicators(cat);
            if (checked && !list.contains(id)) {
                list.push_back(id);
            } else if (!checked) {
                list.removeAll(id);
            }
            AppSettings::instance().setEnabledIndicators(cat, list);
        });
    }
    menu.exec(m_addIndicatorBtn->mapToGlobal(QPoint(0, m_addIndicatorBtn->height())));
}

void PerformanceTab::onCustomizeSpecsClicked() {
    const DeviceEntry* entry = m_deviceModel->entryAt(m_deviceList->currentIndex().row());
    if (!entry) {
        return;
    }

    const auto defs = availableExtraSpecs(entry->category);
    if (defs.isEmpty()) {
        return;
    }

    QStringList enabled = AppSettings::instance().enabledExtraSpecs(entry->category);
    QMenu menu(this);
    for (const auto& def : defs) {
        QAction* act = menu.addAction(def.label);
        act->setCheckable(true);
        act->setChecked(enabled.contains(def.id));
        connect(act, &QAction::toggled, this, [cat = entry->category, id = def.id](bool checked) {
            QStringList list = AppSettings::instance().enabledExtraSpecs(cat);
            if (checked && !list.contains(id)) {
                list.push_back(id);
            } else if (!checked) {
                list.removeAll(id);
            }
            AppSettings::instance().setEnabledExtraSpecs(cat, list);
        });
    }
    menu.exec(m_customizeSpecsBtn->mapToGlobal(QPoint(0, m_customizeSpecsBtn->height())));
}

void PerformanceTab::onThermalItemChanged(QTableWidgetItem* item) {
    if (!item || item->column() != 0) {
        return;
    }
    const QString id = item->data(Qt::UserRole).toString();
    if (id.isEmpty()) {
        return;
    }
    const bool checked = (item->checkState() == Qt::Checked);
    QStringList list = AppSettings::instance().selectedThermalSensors();
    if (checked && !list.contains(id)) {
        list.push_back(id);
    } else if (!checked) {
        list.removeAll(id);
    }
    AppSettings::instance().setSelectedThermalSensors(list);
}

void PerformanceTab::updateChartStyle(AreaChartWidget* chart,
                                      AppSettings::HardwareCategory cat) {
    if (!chart) {
        return;
    }
    const auto& s = AppSettings::instance();
    chart->setCustomPrimaryColor(s.categoryColor(cat));
    chart->setLineWidth(s.chartLineWidth());
    chart->setFillOpacityPercent(s.chartFillOpacityPercent());
    chart->setGridVisible(s.chartShowGrid());
    chart->setSmoothScrollEnabled(s.chartSmoothScroll());
    chart->setScrollDurationMs(s.refreshIntervalMs());
    chart->setHoverValueVisible(s.chartShowHoverValue());
}

void PerformanceTab::rebuildExtraChartsIfNeeded(const DeviceEntry& entry) {
    const QStringList ids = AppSettings::instance().enabledIndicators(entry.category);
    const QString signature = entry.key + QLatin1Char('|') + ids.join(QLatin1Char(','));

    if (m_activeExtraSignature == signature) {
        return;
    }
    m_activeExtraSignature = signature;
    m_extraChartSigs.clear();

    for (const auto& pair : m_extraCharts) {
        if (AreaChartWidget* chart = pair.second) {
            chart->hide();
            chart->setParent(nullptr);
            chart->deleteLater();
        }
    }
    m_extraCharts.clear();

    const auto defs = availableIndicators(entry.category);
    for (const QString& id : ids) {
        bool validId = false;
        for (const auto& d : defs) {
            if (d.id == id) {
                validId = true;
                break;
            }
        }
        if (!validId) {
            continue;
        }
        auto* chart = new AreaChartWidget(m_verticalSplitter);
        chart->setDisplayMode(AreaChartWidget::DisplayMode::Detailed);
        chart->setMinimumHeight(96);
        const int tableIdx = m_verticalSplitter->indexOf(m_thermalTable);
        if (tableIdx >= 0) {
            m_verticalSplitter->insertWidget(tableIdx, chart);
        } else {
            m_verticalSplitter->addWidget(chart);
        }
        m_extraCharts.push_back(qMakePair(id, chart));
    }

    // Splitter boyutlarını görünür öğelere göre dengeli şekilde başlat
    QList<int> sizes;
    const int count = m_verticalSplitter->count();
    for (int i = 0; i < count; ++i) {
        QWidget* w = m_verticalSplitter->widget(i);
        if (w == m_mainChart) {
            sizes.push_back(320);
        } else if (w == m_thermalTable) {
            sizes.push_back(w->isVisible() ? 190 : 0);
        } else {
            sizes.push_back(150);
        }
    }
    if (!sizes.isEmpty()) {
        m_verticalSplitter->setSizes(sizes);
    }
}

void PerformanceTab::updateExtraCharts(const DeviceEntry& entry, bool appendSample) {
    rebuildExtraChartsIfNeeded(entry);

    for (const auto& [id, chart] : m_extraCharts) {
        updateChartStyle(chart, entry.category);

        auto feedSingle = [&](const QString& sig, const RingBuffer<double, 64>& buf) {
            if (m_extraChartSigs.value(id) != sig) {
                m_extraChartSigs.insert(id, sig);
                chart->setHistory(toVector(buf));
            } else if (appendSample && !buf.empty()) {
                chart->pushSample(buf.latest());
            }
        };

        auto feedDual = [&](const QString& sig,
                            const RingBuffer<double, 64>& bufA,
                            const RingBuffer<double, 64>& bufB) {
            if (m_extraChartSigs.value(id) != sig) {
                m_extraChartSigs.insert(id, sig);
                chart->setHistory(toVector(bufA), toVector(bufB));
            } else if (appendSample && !bufA.empty()) {
                chart->pushSamples(bufA.latest(), bufB.empty() ? 0.0 : bufB.latest());
            }
        };

        if (id == QStringLiteral("cpu_freq")) {
            chart->setScaleMode(AreaChartWidget::ScaleMode::FixedRange);
            chart->setFixedRange(0.0, std::max(m_snapshot.cpu.boostFreqMHz, 5000.0));
            chart->setUnitSuffix(QStringLiteral("MHz"));
            chart->setSeriesLabels(trText("Avg Frequency", "Ort. Frekans"));
            chart->setSecondarySeriesEnabled(false);
            feedSingle(entry.key + id, entry.extraHistories.value(id));
        } else if (id == QStringLiteral("cpu_temp")) {
            chart->setScaleMode(AreaChartWidget::ScaleMode::FixedRange);
            chart->setFixedRange(0.0, 110.0);
            chart->setUnitSuffix(QStringLiteral("°C"));
            chart->setSeriesLabels(trText("Package Temp", "Paket Sıcaklığı"));
            chart->setSecondarySeriesEnabled(false);
            feedSingle(entry.key + id, entry.extraHistories.value(id));
        } else if (id == QStringLiteral("cpu_split")) {
            chart->setScaleMode(AreaChartWidget::ScaleMode::FixedRange);
            chart->setFixedRange(0.0, 100.0);
            chart->setUnitSuffix(QStringLiteral("%"));
            chart->setSeriesLabels(trText("User", "Kullanıcı"), trText("System", "Sistem"));
            chart->setSecondarySeriesEnabled(true);
            feedDual(entry.key + id,
                     entry.extraHistories.value(QStringLiteral("cpu_user")),
                     entry.extraHistories.value(QStringLiteral("cpu_sys")));
        } else if (id == QStringLiteral("cpu_ctxt")) {
            chart->setScaleMode(AreaChartWidget::ScaleMode::AutoScale);
            chart->setAutoScaleMinCeiling(10.0);
            chart->setUnitSuffix(QStringLiteral("k/s"));
            chart->setSeriesLabels(trText("Context Switches", "Bağlam Anahtarı"));
            chart->setSecondarySeriesEnabled(false);
            feedSingle(entry.key + id, entry.extraHistories.value(id));
        } else if (id == QStringLiteral("mem_swap")) {
            chart->setScaleMode(AreaChartWidget::ScaleMode::FixedRange);
            chart->setFixedRange(0.0, 100.0);
            chart->setUnitSuffix(QStringLiteral("%"));
            chart->setSeriesLabels(trText("Swap / ZRAM", "Takas / ZRAM"));
            chart->setSecondarySeriesEnabled(false);
            feedSingle(entry.key + id, entry.extraHistories.value(id));
        } else if (id == QStringLiteral("mem_cache")) {
            const double totalGiB =
                static_cast<double>(std::max<std::uint64_t>(m_snapshot.memory.totalBytes, 1)) /
                (1024.0 * 1024.0 * 1024.0);
            chart->setScaleMode(AreaChartWidget::ScaleMode::FixedRange);
            chart->setFixedRange(0.0, totalGiB);
            chart->setUnitSuffix(QStringLiteral("GiB"));
            chart->setSeriesLabels(trText("Cache & Buffers", "Önbellek ve Tampon"));
            chart->setSecondarySeriesEnabled(false);
            feedSingle(entry.key + id, entry.extraHistories.value(id));
        } else if (id == QStringLiteral("disk_active")) {
            chart->setScaleMode(AreaChartWidget::ScaleMode::FixedRange);
            chart->setFixedRange(0.0, 100.0);
            chart->setUnitSuffix(QStringLiteral("%"));
            chart->setSeriesLabels(trText("Active Time", "Etkinlik"));
            chart->setSecondarySeriesEnabled(false);
            feedSingle(entry.key + id, entry.extraHistories.value(id));
        } else if (id == QStringLiteral("disk_iops")) {
            chart->setScaleMode(AreaChartWidget::ScaleMode::AutoScale);
            chart->setAutoScaleMinCeiling(50.0);
            chart->setUnitSuffix(QStringLiteral("IOPS"));
            chart->setSeriesLabels(trText("Read IOPS", "Okuma IOPS"),
                                   trText("Write IOPS", "Yazma IOPS"));
            chart->setSecondarySeriesEnabled(true);
            feedDual(entry.key + id,
                     entry.extraHistories.value(QStringLiteral("disk_riops")),
                     entry.extraHistories.value(QStringLiteral("disk_wiops")));
        } else if (id == QStringLiteral("disk_temp")) {
            chart->setScaleMode(AreaChartWidget::ScaleMode::FixedRange);
            chart->setFixedRange(0.0, 100.0);
            chart->setUnitSuffix(QStringLiteral("°C"));
            chart->setSeriesLabels(trText("Drive Temp", "Disk Sıcaklığı"));
            chart->setSecondarySeriesEnabled(false);
            feedSingle(entry.key + id, entry.extraHistories.value(id));
        } else if (id == QStringLiteral("net_err")) {
            chart->setScaleMode(AreaChartWidget::ScaleMode::AutoScale);
            chart->setAutoScaleMinCeiling(10.0);
            chart->setUnitSuffix(QString());
            chart->setSeriesLabels(trText("Errors & Drops", "Hata ve Kayıplar"));
            chart->setSecondarySeriesEnabled(false);
            feedSingle(entry.key + id, entry.extraHistories.value(id));
        } else if (id == QStringLiteral("gpu_vram")) {
            double maxVramMiB = 512.0;
            if (entry.deviceIndex >= 0 && entry.deviceIndex < m_snapshot.gpus.size()) {
                const auto total = m_snapshot.gpus[entry.deviceIndex].vramTotalBytes;
                if (total > 0) {
                    maxVramMiB = static_cast<double>(total) / (1024.0 * 1024.0);
                }
            }
            chart->setScaleMode(AreaChartWidget::ScaleMode::FixedRange);
            chart->setFixedRange(0.0, std::max(maxVramMiB, 1.0));
            chart->setUnitSuffix(QStringLiteral("MiB"));
            chart->setSeriesLabels(trText("VRAM Usage", "VRAM Kullanımı"));
            chart->setSecondarySeriesEnabled(false);
            feedSingle(entry.key + id, entry.extraHistories.value(id));
        } else if (id == QStringLiteral("gpu_clock")) {
            double maxClk = 2500.0;
            if (entry.deviceIndex >= 0 && entry.deviceIndex < m_snapshot.gpus.size()) {
                const double m = m_snapshot.gpus[entry.deviceIndex].maxClockMHz;
                if (m > 0.0) {
                    maxClk = m;
                }
            }
            chart->setScaleMode(AreaChartWidget::ScaleMode::FixedRange);
            chart->setFixedRange(0.0, maxClk);
            chart->setUnitSuffix(QStringLiteral("MHz"));
            chart->setSeriesLabels(trText("Core Clock", "Çekirdek Saat Hızı"));
            chart->setSecondarySeriesEnabled(false);
            feedSingle(entry.key + id, entry.extraHistories.value(id));
        } else if (id == QStringLiteral("gpu_temp")) {
            chart->setScaleMode(AreaChartWidget::ScaleMode::FixedRange);
            chart->setFixedRange(0.0, 110.0);
            chart->setUnitSuffix(QStringLiteral("°C"));
            chart->setSeriesLabels(trText("GPU Temp", "GPU Sıcaklığı"));
            chart->setSecondarySeriesEnabled(false);
            feedSingle(entry.key + id, entry.extraHistories.value(id));
        } else if (id == QStringLiteral("gpu_power")) {
            chart->setScaleMode(AreaChartWidget::ScaleMode::AutoScale);
            chart->setAutoScaleMinCeiling(25.0);
            chart->setUnitSuffix(QStringLiteral("W"));
            chart->setSeriesLabels(trText("Power Draw", "Güç Tüketimi"));
            chart->setSecondarySeriesEnabled(false);
            feedSingle(entry.key + id, entry.extraHistories.value(id));
        } else if (id == QStringLiteral("fan_temp")) {
            chart->setScaleMode(AreaChartWidget::ScaleMode::FixedRange);
            chart->setFixedRange(0.0, 110.0);
            chart->setUnitSuffix(QStringLiteral("°C"));
            chart->setSeriesLabels(trText("Paired Temp", "Eşleşen Sıcaklık"));
            chart->setSecondarySeriesEnabled(false);
            feedSingle(entry.key + id, entry.extraHistories.value(id));
        } else if (id == QStringLiteral("fan_pwm")) {
            chart->setScaleMode(AreaChartWidget::ScaleMode::FixedRange);
            chart->setFixedRange(0.0, 100.0);
            chart->setUnitSuffix(QStringLiteral("%"));
            chart->setSeriesLabels(trText("PWM Duty", "PWM Yüzdesi"));
            chart->setSecondarySeriesEnabled(false);
            feedSingle(entry.key + id, entry.extraHistories.value(id));
        } else if (id == QStringLiteral("bat_power")) {
            chart->setScaleMode(AreaChartWidget::ScaleMode::AutoScale);
            chart->setAutoScaleMinCeiling(15.0);
            chart->setUnitSuffix(QStringLiteral("W"));
            chart->setSeriesLabels(trText("Power Rate", "Güç Hızı"));
            chart->setSecondarySeriesEnabled(false);
            feedSingle(entry.key + id, entry.extraHistories.value(id));
        } else if (id == QStringLiteral("bat_voltage")) {
            chart->setScaleMode(AreaChartWidget::ScaleMode::AutoScale);
            chart->setAutoScaleMinCeiling(5.0);
            chart->setUnitSuffix(QStringLiteral("V"));
            chart->setSeriesLabels(trText("Voltage", "Gerilim"));
            chart->setSecondarySeriesEnabled(false);
            feedSingle(entry.key + id, entry.extraHistories.value(id));
        }
    }
}

void PerformanceTab::refreshCenterAndSpecs() {
    if (!m_hasSnapshot) {
        return;
    }
    const int row = m_deviceList->currentIndex().row();
    const DeviceEntry* entry = m_deviceModel->entryAt(row);
    if (!entry) {
        return;
    }

    const bool appendSample = m_pendingChartPush;
    m_pendingChartPush = false;

    const bool isCpu = (entry->kind == DeviceKind::Cpu);
    const bool isThermal = (entry->kind == DeviceKind::ThermalOverview);
    m_coreGridToggle->setVisible(isCpu);
    m_centerStack->setCurrentIndex((isCpu && m_coreGridToggle->isChecked()) ? 1 : 0);

    if (isThermal) {
        m_addIndicatorBtn->setText(trText("+ Select Sensors", "+ Sensör Seç"));
        m_addIndicatorBtn->setEnabled(!m_snapshot.thermals.isEmpty());
    } else {
        m_addIndicatorBtn->setText(trText("+ Add Indicator", "+ Gösterge Ekle"));
        m_addIndicatorBtn->setEnabled(!availableIndicators(entry->category).isEmpty());
    }
    m_customizeSpecsBtn->setEnabled(!availableExtraSpecs(entry->category).isEmpty());

    m_memoryBar->setVisible(entry->kind == DeviceKind::Memory);
    const bool showThermal = (entry->kind == DeviceKind::Fan || isThermal);
    m_thermalTable->setVisible(showThermal);
    if (showThermal) {
        const int tblIdx = m_verticalSplitter->indexOf(m_thermalTable);
        QList<int> sz = m_verticalSplitter->sizes();
        if (tblIdx >= 0 && tblIdx < sz.size() && sz[tblIdx] < 80) {
            sz[0] = std::max(180, sz[0] - 190);
            sz[tblIdx] = 190;
            m_verticalSplitter->setSizes(sz);
        }
    }
    m_probeGpuCheck->setVisible(false);

    updateChartStyle(m_mainChart, entry->category);

    auto feedMainSingle = [&](const QString& sig, double scale = 1.0) {
        if (m_lastMainChartSignature != sig) {
            m_lastMainChartSignature = sig;
            m_mainChart->setHistory(toVector(entry->primaryHistory, scale));
        } else if (appendSample && !entry->primaryHistory.empty()) {
            m_mainChart->pushSample(entry->primaryHistory.latest() * scale);
        }
    };

    auto feedMainDual = [&](const QString& sig, double scale = 1.0) {
        if (m_lastMainChartSignature != sig) {
            m_lastMainChartSignature = sig;
            m_mainChart->setHistory(toVector(entry->primaryHistory, scale),
                                    toVector(entry->secondaryHistory, scale));
        } else if (appendSample && !entry->primaryHistory.empty()) {
            const double sec = entry->secondaryHistory.empty()
                ? 0.0
                : entry->secondaryHistory.latest();
            m_mainChart->pushSamples(entry->primaryHistory.latest() * scale, sec * scale);
        }
    };

    switch (entry->kind) {
    case DeviceKind::Cpu: {
        m_titleLabel->setText(m_snapshot.cpu.modelName.isEmpty()
                                  ? trText("Processor (CPU)", "İşlemci (CPU)")
                                  : m_snapshot.cpu.modelName);
        m_summaryLabel->setText(
            QStringLiteral("%1: %2  ·  %3: %4  ·  %5: %6")
                .arg(trText("Utilization", "Kullanım"),
                     fmt::formatPercent(m_snapshot.cpu.totalUsagePercent),
                     trText("Avg Clock", "Ortalama Saat"),
                     fmt::formatFrequencyMHz(m_snapshot.cpu.avgFreqMHz),
                     trText("Temp", "Sıcaklık"),
                     fmt::formatTemp(m_snapshot.cpu.packageTempC)));

        m_mainChart->setScaleMode(AreaChartWidget::ScaleMode::FixedRange);
        m_mainChart->setFixedRange(0.0, 100.0);
        m_mainChart->setUnitSuffix(QStringLiteral("%"));
        m_mainChart->setSeriesLabels(trText("Total CPU", "Toplam CPU"));
        m_mainChart->setSecondarySeriesEnabled(false);
        feedMainSingle(entry->key);
        break;
    }
    case DeviceKind::Memory: {
        m_titleLabel->setText(trText("System Memory (RAM & Swap)", "Sistem Belleği (RAM & Takas)"));
        m_summaryLabel->setText(
            QStringLiteral("%1: %2 / %3 (%4)  ·  %5: %6 / %7")
                .arg(trText("Used", "Kullanılan"),
                     fmt::formatBytes(m_snapshot.memory.usedBytes),
                     fmt::formatBytes(m_snapshot.memory.totalBytes),
                     fmt::formatPercent(m_snapshot.memory.usagePercent),
                     trText("Swap", "Takas"),
                     fmt::formatBytes(m_snapshot.memory.swapUsedBytes),
                     fmt::formatBytes(m_snapshot.memory.swapTotalBytes)));

        m_mainChart->setScaleMode(AreaChartWidget::ScaleMode::FixedRange);
        m_mainChart->setFixedRange(0.0, 100.0);
        m_mainChart->setUnitSuffix(QStringLiteral("%"));
        m_mainChart->setSeriesLabels(trText("Memory Usage", "Bellek Kullanımı"));
        m_mainChart->setSecondarySeriesEnabled(false);
        feedMainSingle(entry->key);
        m_memoryBar->setMemory(m_snapshot.memory);
        break;
    }
    case DeviceKind::Disk: {
        if (entry->deviceIndex >= 0 && entry->deviceIndex < m_snapshot.disks.size()) {
            const DiskTelemetry& d = m_snapshot.disks[entry->deviceIndex];
            m_titleLabel->setText(QStringLiteral("%1 (%2)").arg(d.model, d.name));
            m_summaryLabel->setText(
                QStringLiteral("%1: %2  ·  %3: %4  ·  %5: %6  ·  %7: %8")
                    .arg(trText("Read", "Okuma"),
                         fmt::formatRate(d.readBytesPerSec),
                         trText("Write", "Yazma"),
                         fmt::formatRate(d.writeBytesPerSec),
                         trText("Active", "Etkinlik"),
                         fmt::formatPercent(d.activePercent),
                         trText("Temp", "Sıcaklık"),
                         fmt::formatTemp(d.tempC)));

            const double peakBps = std::max(entry->primaryHistory.maxValue(0.0),
                                            entry->secondaryHistory.maxValue(0.0));
            const bool useMiB = (peakBps >= 1024.0 * 1024.0);
            const double divisor = useMiB ? (1024.0 * 1024.0) : 1024.0;
            const QString unit = useMiB ? QStringLiteral("MiB/s") : QStringLiteral("KiB/s");

            m_mainChart->setScaleMode(AreaChartWidget::ScaleMode::AutoScale);
            m_mainChart->setAutoScaleMinCeiling(useMiB ? 1.0 : 10.0);
            m_mainChart->setUnitSuffix(unit);
            m_mainChart->setSeriesLabels(trText("Read", "Okuma"), trText("Write", "Yazma"));
            m_mainChart->setSecondarySeriesEnabled(true);
            feedMainDual(entry->key + QLatin1Char('|') + unit, 1.0 / divisor);
        }
        break;
    }
    case DeviceKind::Network: {
        if (entry->deviceIndex >= 0 && entry->deviceIndex < m_snapshot.networks.size()) {
            const NetworkTelemetry& n = m_snapshot.networks[entry->deviceIndex];
            const QString typeStr = n.isWireless ? QStringLiteral("Wi-Fi") : QStringLiteral("Ethernet");
            m_titleLabel->setText(QStringLiteral("%1 — %2").arg(typeStr, n.name));
            m_summaryLabel->setText(
                QStringLiteral("%1: %2  ·  %3: %4  ·  %5: %6")
                    .arg(trText("Download", "İndirme"),
                         fmt::formatRate(n.rxBytesPerSec),
                         trText("Upload", "Yükleme"),
                         fmt::formatRate(n.txBytesPerSec),
                         trText("State", "Durum"),
                         n.operState));

            const double peakBps = std::max(entry->primaryHistory.maxValue(0.0),
                                            entry->secondaryHistory.maxValue(0.0));
            const bool useMiB = (peakBps >= 1024.0 * 1024.0);
            const double divisor = useMiB ? (1024.0 * 1024.0) : 1024.0;
            const QString unit = useMiB ? QStringLiteral("MiB/s") : QStringLiteral("KiB/s");

            m_mainChart->setScaleMode(AreaChartWidget::ScaleMode::AutoScale);
            m_mainChart->setAutoScaleMinCeiling(useMiB ? 1.0 : 10.0);
            m_mainChart->setUnitSuffix(unit);
            m_mainChart->setSeriesLabels(trText("Download (Rx)", "İndirme (Rx)"),
                                         trText("Upload (Tx)", "Yükleme (Tx)"));
            m_mainChart->setSecondarySeriesEnabled(true);
            feedMainDual(entry->key + QLatin1Char('|') + unit, 1.0 / divisor);
        }
        break;
    }
    case DeviceKind::Gpu: {
        if (entry->deviceIndex >= 0 && entry->deviceIndex < m_snapshot.gpus.size()) {
            const GpuTelemetry& g = m_snapshot.gpus[entry->deviceIndex];
            m_titleLabel->setText(g.name);
            if (g.sleeping) {
                m_summaryLabel->setText(
                    QStringLiteral("%1: %2 (%3)  ·  %4: %5")
                        .arg(trText("State", "Durum"),
                             trText("Power Saving / Sleep", "Güç Tasarrufu / Uyku"),
                             g.powerState,
                             trText("Driver", "Sürücü"),
                             g.driver));
            } else {
                m_summaryLabel->setText(
                    QStringLiteral("%1: %2  ·  %3: %4  ·  %5: %6  ·  %7: %8")
                        .arg(trText("Utilization", "Kullanım"),
                             fmt::formatPercent(g.utilizationPercent),
                             trText("Clock", "Saat"),
                             fmt::formatFrequencyMHz(g.coreClockMHz),
                             trText("Temp", "Sıcaklık"),
                             fmt::formatTemp(g.tempC),
                             trText("Power", "Güç"),
                             g.powerWatts >= 0.0
                                 ? QStringLiteral("%1 W").arg(QString::number(g.powerWatts, 'f', 1))
                                 : QStringLiteral("—")));
            }

            m_probeGpuCheck->setVisible(g.driver == QStringLiteral("nvidia"));

            m_mainChart->setScaleMode(AreaChartWidget::ScaleMode::FixedRange);
            m_mainChart->setFixedRange(0.0, 100.0);
            m_mainChart->setUnitSuffix(QStringLiteral("%"));
            m_mainChart->setSeriesLabels(trText("GPU Utilization", "GPU Kullanımı"));
            m_mainChart->setSecondarySeriesEnabled(false);
            feedMainSingle(entry->key);
        }
        break;
    }
    case DeviceKind::Fan: {
        if (entry->deviceIndex >= 0 && entry->deviceIndex < m_snapshot.fans.size()) {
            const FanTelemetry& f = m_snapshot.fans[entry->deviceIndex];
            m_titleLabel->setText(f.label);
            m_summaryLabel->setText(
                QStringLiteral("%1: %2 RPM  ·  PWM: %3 (%4)  ·  %5: %6")
                    .arg(trText("Speed", "Devir"))
                    .arg(f.rpm)
                    .arg(fmt::formatPercent(f.pwmPercent, 0),
                         f.pwmControlMode,
                         trText("Paired Temp", "Eşleşen Sıcaklık"),
                         fmt::formatTemp(f.pairedTempC)));

            m_mainChart->setScaleMode(AreaChartWidget::ScaleMode::AutoScale);
            m_mainChart->setAutoScaleMinCeiling(
                std::max(3000.0, static_cast<double>(f.maxObservedRpm) * 1.1));
            m_mainChart->setUnitSuffix(QStringLiteral("RPM"));
            m_mainChart->setSeriesLabels(trText("Fan Speed", "Fan Devri"));
            m_mainChart->setSecondarySeriesEnabled(false);
            feedMainSingle(entry->key);
            updateThermalTable(false);
        }
        break;
    }
    case DeviceKind::Battery: {
        if (entry->deviceIndex >= 0 && entry->deviceIndex < m_snapshot.batteries.size()) {
            const BatteryTelemetry& b = m_snapshot.batteries[entry->deviceIndex];
            m_titleLabel->setText(
                b.modelName.isEmpty()
                    ? QStringLiteral("%1 (%2)").arg(trText("Battery", "Batarya"), b.id)
                    : QStringLiteral("%1 — %2 (%3)").arg(trText("Battery", "Batarya"), b.modelName, b.id));
            m_summaryLabel->setText(
                QStringLiteral("%1: %2  ·  %3: %4  ·  %5: %6 W  ·  %7: %8")
                    .arg(trText("Charge", "Şarj"),
                         fmt::formatPercent(b.capacityPercent),
                         trText("Status", "Durum"),
                         translateBatteryStatus(b.status),
                         trText("Power", "Güç"),
                         QString::number(b.powerWatts, 'f', 1),
                         trText("Health", "Sağlık"),
                         b.healthPercent > 0.0 ? fmt::formatPercent(b.healthPercent) : QStringLiteral("—")));

            m_mainChart->setScaleMode(AreaChartWidget::ScaleMode::FixedRange);
            m_mainChart->setFixedRange(0.0, 100.0);
            m_mainChart->setUnitSuffix(QStringLiteral("%"));
            m_mainChart->setSeriesLabels(trText("Battery Level", "Batarya Seviyesi"));
            m_mainChart->setSecondarySeriesEnabled(false);
            feedMainSingle(entry->key);
        }
        break;
    }
    case DeviceKind::ThermalOverview: {
        const QStringList selectedIds = AppSettings::instance().selectedThermalSensors();
        QVector<const ThermalSensor*> activeSensors;
        for (const QString& selId : selectedIds) {
            for (const auto& t : m_snapshot.thermals) {
                if (t.id == selId) {
                    activeSensors.push_back(&t);
                    break;
                }
            }
        }

        m_titleLabel->setText(trText("System Thermal Sensors (hwmon)",
                                     "Sistem Sıcaklık Sensörleri (hwmon)"));
        if (activeSensors.isEmpty()) {
            m_summaryLabel->setText(
                trText("Monitoring %1 thermal sensors and %2 fans",
                       "Toplam %1 termal sensör ve %2 fan izleniyor")
                    .arg(m_snapshot.thermals.size())
                    .arg(m_snapshot.fans.size()));
        } else {
            m_summaryLabel->setText(
                trText("Plotting %1 selected sensor(s) out of %2 — tick sensors below or via '+ Select Sensors'",
                       "%2 sensörden %1 tanesi grafikte çiziliyor — alttan veya '+ Sensör Seç' menüsünden seçin")
                    .arg(activeSensors.size())
                    .arg(m_snapshot.thermals.size()));
        }

        m_mainChart->setScaleMode(AreaChartWidget::ScaleMode::FixedRange);
        m_mainChart->setFixedRange(0.0, 110.0);
        m_mainChart->setUnitSuffix(QStringLiteral("°C"));

        if (!activeSensors.isEmpty()) {
            QStringList matchedIds;
            matchedIds.reserve(activeSensors.size());
            for (const auto* sPtr : activeSensors) {
                matchedIds.push_back(sPtr->id);
            }
            const QString multiSig = QStringLiteral("thermal:multi:%1:%2")
                                         .arg(matchedIds.join(QLatin1Char(',')))
                                         .arg(static_cast<int>(currentLanguage()));
            if (m_lastMainChartSignature != multiSig) {
                m_lastMainChartSignature = multiSig;
                QVector<AreaChartWidget::MultiSeriesInput> multi;
                multi.reserve(activeSensors.size());
                for (int i = 0; i < activeSensors.size(); ++i) {
                    const ThermalSensor* t = activeSensors[i];
                    AreaChartWidget::MultiSeriesInput item;
                    item.id = t->id;
                    item.label = QStringLiteral("%1 · %2").arg(t->hwmonName, t->label);
                    item.color = thermalSensorColor(i);
                    const auto histKey = QStringLiteral("therm:%1").arg(t->id);
                    if (entry->extraHistories.contains(histKey)) {
                        item.values = toVector(entry->extraHistories.value(histKey));
                    } else {
                        item.values.push_back(std::max(0.0, t->tempC));
                    }
                    multi.push_back(std::move(item));
                }
                m_mainChart->setMultiSeries(multi);
            } else if (appendSample) {
                QVector<double> latest;
                latest.reserve(activeSensors.size());
                for (const auto* t : activeSensors) {
                    latest.push_back(std::max(0.0, t->tempC));
                }
                m_mainChart->pushMultiSample(latest);
            }
        } else {
            m_mainChart->setSeriesLabels(trText("CPU Package", "CPU Paketi"),
                                         trText("Hottest Sensor", "En Yüksek Sensör"));
            m_mainChart->setSecondarySeriesEnabled(true);
            feedMainDual(QStringLiteral("thermal:default"));
        }

        updateThermalTable(true);
        break;
    }
    }

    updateExtraCharts(*entry, appendSample);
    updateSpecsForEntry(*entry);
}

void PerformanceTab::updateThermalTable(bool allowSelectionCheckboxes) {
    QSignalBlocker blocker(m_thermalTable);
    const QStringList selectedIds = AppSettings::instance().selectedThermalSensors();

    m_thermalTable->setRowCount(static_cast<int>(m_snapshot.thermals.size()));
    for (int i = 0; i < m_snapshot.thermals.size(); ++i) {
        const ThermalSensor& t = m_snapshot.thermals[i];
        auto ensureItem = [&](int col) -> QTableWidgetItem* {
            QTableWidgetItem* item = m_thermalTable->item(i, col);
            if (!item) {
                item = new QTableWidgetItem();
                m_thermalTable->setItem(i, col, item);
            }
            return item;
        };

        QTableWidgetItem* srcItem = ensureItem(0);
        srcItem->setText(t.hwmonName);
        if (allowSelectionCheckboxes) {
            srcItem->setData(Qt::UserRole, t.id);
            srcItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsUserCheckable);
            srcItem->setCheckState(selectedIds.contains(t.id) ? Qt::Checked : Qt::Unchecked);
        } else {
            srcItem->setData(Qt::UserRole, QVariant());
            srcItem->setData(Qt::CheckStateRole, QVariant());
            srcItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        }

        ensureItem(1)->setText(t.label);
        ensureItem(2)->setText(fmt::formatTemp(t.tempC));
        ensureItem(3)->setText(fmt::formatTemp(t.critTempC));
    }
}

void PerformanceTab::updateSpecsForEntry(const DeviceEntry& entry) {
    QVector<QPair<QString, QString>> rows;
    const QStringList extras = AppSettings::instance().enabledExtraSpecs(entry.category);

    switch (entry.kind) {
    case DeviceKind::Cpu: {
        const CpuTelemetry& c = m_snapshot.cpu;
        rows = {
            {trText("Model", "Model"), c.modelName},
            {trText("Architecture", "Mimari"), c.architecture},
            {trText("Cores / Threads", "Fiziksel / Mantıksal"),
             QStringLiteral("%1 / %2").arg(c.physicalCores).arg(c.logicalCores)},
            {trText("Avg Clock", "Anlık Ort. Frekans"), fmt::formatFrequencyMHz(c.avgFreqMHz)},
            {trText("Peak Core Clock", "En Yüksek Çekirdek"), fmt::formatFrequencyMHz(c.maxCurrentFreqMHz)},
            {trText("Base / Max Clock", "Taban / Maks Frekans"),
             QStringLiteral("%1 / %2")
                 .arg(fmt::formatFrequencyMHz(c.baseFreqMHz),
                      fmt::formatFrequencyMHz(c.boostFreqMHz))},
            {trText("Cache L1d / L1i", "Önbellek L1d / L1i"),
             QStringLiteral("%1 / %2")
                 .arg(fmt::formatBytes(c.l1dBytes), fmt::formatBytes(c.l1iBytes))},
            {trText("Cache L2 / L3", "Önbellek L2 / L3"),
             QStringLiteral("%1 / %2")
                 .arg(fmt::formatBytes(c.l2Bytes), fmt::formatBytes(c.l3Bytes))},
            {trText("Scaling Driver", "Ölçekleme Sürücüsü"),
             c.scalingDriver.isEmpty() ? QStringLiteral("—") : c.scalingDriver},
            {trText("Governor / EPP", "Governor / EPP"),
             QStringLiteral("%1 / %2")
                 .arg(c.governor.isEmpty() ? QStringLiteral("—") : c.governor,
                      c.energyPreference.isEmpty() ? QStringLiteral("—") : c.energyPreference)},
            {trText("User / Sys / IOWait", "Kullanıcı / Sistem / IOWait"),
             QStringLiteral("%1 / %2 / %3")
                 .arg(fmt::formatPercent(c.userPercent),
                      fmt::formatPercent(c.systemPercent),
                      fmt::formatPercent(c.iowaitPercent))},
            {trText("Context Switches / s", "Bağlam Anahtarı / sn"),
             QString::number(c.contextSwitchesPerSec)},
            {trText("Running / Blocked", "Çalışan / Bekleyen"),
             QStringLiteral("%1 / %2").arg(c.processesRunning).arg(c.processesBlocked)},
            {trText("Package Temp", "Paket Sıcaklığı"), fmt::formatTemp(c.packageTempC)},
            {trText("Uptime", "Çalışma Süresi"), fmt::formatUptime(c.uptimeSeconds)},
        };
        if (extras.contains(QStringLiteral("load_avg"))) {
            rows.push_back({trText("Load Avg (1/5/15m)", "Yük Ortalaması (1/5/15 dk)"),
                            QStringLiteral("%1, %2, %3")
                                .arg(QString::number(c.loadAvg1, 'f', 2),
                                     QString::number(c.loadAvg5, 'f', 2),
                                     QString::number(c.loadAvg15, 'f', 2))});
        }
        if (extras.contains(QStringLiteral("interrupts"))) {
            rows.push_back({trText("Interrupts / s", "Kesme / sn"),
                            QString::number(c.interruptsPerSec)});
        }
        if (extras.contains(QStringLiteral("forks"))) {
            rows.push_back({trText("Processes Created", "Oluşturulan Süreçler"),
                            QString::number(c.totalForks)});
        }
        if (extras.contains(QStringLiteral("scaling_bounds"))) {
            rows.push_back({trText("Scaling Min / Max", "Ölçekleme Min / Maks"),
                            QStringLiteral("%1 / %2")
                                .arg(fmt::formatFrequencyMHz(c.minScalingFreqMHz),
                                     fmt::formatFrequencyMHz(c.maxScalingFreqMHz))});
        }
        if (extras.contains(QStringLiteral("virtualization"))) {
            rows.push_back({trText("Virtualization", "Sanallaştırma"),
                            c.virtualization.isEmpty() ? QStringLiteral("—") : c.virtualization});
        }
        if (extras.contains(QStringLiteral("bogo_ucode"))) {
            rows.push_back({trText("BogoMIPS / Microcode", "BogoMIPS / Mikrokod"),
                            QStringLiteral("%1 / %2")
                                .arg(QString::number(c.bogoMips, 'f', 1),
                                     c.microcode.isEmpty() ? QStringLiteral("—") : c.microcode)});
        }
        if (extras.contains(QStringLiteral("core_temp_range"))) {
            rows.push_back({trText("Core Temp Min / Max", "Çekirdek Sıcaklığı Min / Maks"),
                            QStringLiteral("%1 / %2")
                                .arg(fmt::formatTemp(c.coreTempMinC),
                                     fmt::formatTemp(c.coreTempMaxC))});
        }
        setSpecsRows(QStringLiteral("cpu:%1:%2")
                         .arg(static_cast<int>(currentLanguage()))
                         .arg(extras.join(QLatin1Char(','))),
                     rows);
        break;
    }
    case DeviceKind::Memory: {
        const MemoryTelemetry& m = m_snapshot.memory;
        rows = {
            {trText("Total RAM", "Toplam RAM"), fmt::formatBytes(m.totalBytes)},
            {trText("Used", "Kullanılan"), fmt::formatBytes(m.usedBytes)},
            {trText("Available", "Kullanılabilir"), fmt::formatBytes(m.availableBytes)},
            {trText("Free", "Serbest (Free)"), fmt::formatBytes(m.freeBytes)},
            {trText("Cached", "Önbellek (Cached)"), fmt::formatBytes(m.cachedBytes)},
            {trText("Buffers", "Tampon (Buffers)"), fmt::formatBytes(m.buffersBytes)},
            {trText("Shared (Shmem)", "Paylaşılan (Shmem)"), fmt::formatBytes(m.sharedBytes)},
            {trText("Reclaimable", "Geri Kazanılabilir"), fmt::formatBytes(m.reclaimableBytes)},
            {trText("Swap / ZRAM", "Takas (Swap / ZRAM)"),
             QStringLiteral("%1 / %2 (%3)")
                 .arg(fmt::formatBytes(m.swapUsedBytes),
                      fmt::formatBytes(m.swapTotalBytes),
                      fmt::formatPercent(m.swapUsagePercent))},
            {trText("Swap Devices", "Takas Aygıtları"),
             m.swapDevices.isEmpty() ? trText("None", "Yok") : m.swapDevices.join(QLatin1Char('\n'))},
        };
        if (extras.contains(QStringLiteral("dirty_wb"))) {
            rows.push_back({trText("Dirty / Writeback", "Kirli / Geri Yazılan"),
                            QStringLiteral("%1 / %2")
                                .arg(fmt::formatBytes(m.dirtyBytes),
                                     fmt::formatBytes(m.writebackBytes))});
        }
        if (extras.contains(QStringLiteral("active_inactive"))) {
            rows.push_back({trText("Active / Inactive", "Aktif / Pasif"),
                            QStringLiteral("%1 / %2")
                                .arg(fmt::formatBytes(m.activeBytes),
                                     fmt::formatBytes(m.inactiveBytes))});
        }
        if (extras.contains(QStringLiteral("committed_pt"))) {
            rows.push_back({trText("Committed / PageTables", "Taahhüt / Sayfa Tablosu"),
                            QStringLiteral("%1 / %2")
                                .arg(fmt::formatBytes(m.committedAsBytes),
                                     fmt::formatBytes(m.pageTablesBytes))});
        }
        for (const auto& dimm : m.dimmTempsC) {
            rows.push_back({dimm.first, fmt::formatTemp(dimm.second)});
        }
        setSpecsRows(QStringLiteral("memory:%1:%2:%3")
                         .arg(static_cast<int>(currentLanguage()))
                         .arg(m.dimmTempsC.size())
                         .arg(extras.join(QLatin1Char(','))),
                     rows);
        break;
    }
    case DeviceKind::Disk: {
        if (entry.deviceIndex >= 0 && entry.deviceIndex < m_snapshot.disks.size()) {
            const DiskTelemetry& d = m_snapshot.disks[entry.deviceIndex];
            rows = {
                {trText("Device Path", "Aygıt Yolu"), d.devicePath},
                {trText("Model", "Model"), d.model},
                {trText("Capacity", "Kapasite"), fmt::formatBytes(d.capacityBytes)},
                {trText("Read Speed", "Okuma Hızı"), fmt::formatRate(d.readBytesPerSec)},
                {trText("Write Speed", "Yazma Hızı"), fmt::formatRate(d.writeBytesPerSec)},
                {trText("Read / Write IOPS", "Okuma / Yazma IOPS"),
                 QStringLiteral("%1 / %2")
                     .arg(QString::number(d.readIops, 'f', 0),
                          QString::number(d.writeIops, 'f', 0))},
                {trText("Active Time", "Etkinlik Süresi"), fmt::formatPercent(d.activePercent)},
                {trText("Avg Response", "Ort. Yanıt Süresi"),
                 QStringLiteral("%1 ms").arg(QString::number(d.avgResponseMs, 'f', 2))},
                {trText("Total Read", "Toplam Okunan"), fmt::formatBytes(d.totalReadBytes)},
                {trText("Total Written", "Toplam Yazılan"), fmt::formatBytes(d.totalWriteBytes)},
                {trText("I/O Scheduler", "I/O Zamanlayıcı"), d.scheduler},
                {trText("Temperature", "Sıcaklık"), fmt::formatTemp(d.tempC)},
                {trText("Mount Points", "Bağlama Noktaları"),
                 d.mountPoints.isEmpty()
                     ? trText("Not mounted", "Bağlı değil")
                     : d.mountPoints.join(QLatin1Char('\n'))},
            };
            if (extras.contains(QStringLiteral("media_type"))) {
                rows.push_back({trText("Media / Block Size", "Ortam / Blok Boyutu"),
                                QStringLiteral("%1 / %2 B")
                                    .arg(d.isRotational ? QStringLiteral("HDD") : QStringLiteral("SSD / NVMe"))
                                    .arg(d.logicalBlockSize)});
            }
            if (extras.contains(QStringLiteral("total_ops"))) {
                rows.push_back({trText("Total Read / Write Ops", "Toplam Okuma / Yazma"),
                                QStringLiteral("%1 / %2")
                                    .arg(d.totalReadOps)
                                    .arg(d.totalWriteOps)});
            }
            setSpecsRows(QStringLiteral("%1:%2:%3")
                             .arg(entry.key)
                             .arg(static_cast<int>(currentLanguage()))
                             .arg(extras.join(QLatin1Char(','))),
                         rows);
        }
        break;
    }
    case DeviceKind::Network: {
        if (entry.deviceIndex >= 0 && entry.deviceIndex < m_snapshot.networks.size()) {
            const NetworkTelemetry& n = m_snapshot.networks[entry.deviceIndex];
            rows = {
                {trText("Interface", "Arayüz"), n.name},
                {trText("Type", "Tür"),
                 n.isWireless
                     ? trText("Wireless (Wi-Fi)", "Kablosuz (Wi-Fi)")
                     : trText("Wired (Ethernet)", "Kablolu (Ethernet)")},
                {trText("State", "Durum"), n.operState},
                {trText("Link Speed", "Bağlantı Hızı"),
                 n.linkSpeedMbps > 0
                     ? QStringLiteral("%1 Mbps").arg(n.linkSpeedMbps)
                     : QStringLiteral("—")},
                {trText("Download (Rx)", "İndirme (Rx)"), fmt::formatRate(n.rxBytesPerSec)},
                {trText("Upload (Tx)", "Yükleme (Tx)"), fmt::formatRate(n.txBytesPerSec)},
                {trText("Total Received", "Toplam İndirilen"), fmt::formatBytes(n.rxTotalBytes)},
                {trText("Total Sent", "Toplam Yüklenen"), fmt::formatBytes(n.txTotalBytes)},
                {trText("IPv4 Addresses", "IPv4 Adresleri"),
                 n.ipv4Addresses.isEmpty() ? QStringLiteral("—") : n.ipv4Addresses.join(QLatin1Char('\n'))},
                {trText("IPv6 Addresses", "IPv6 Adresleri"),
                 n.ipv6Addresses.isEmpty() ? QStringLiteral("—") : n.ipv6Addresses.join(QLatin1Char('\n'))},
                {trText("MAC Address", "MAC Adresi"),
                 n.macAddress.isEmpty() ? QStringLiteral("—") : n.macAddress},
                {trText("MTU", "MTU"), n.mtu > 0 ? QString::number(n.mtu) : QStringLiteral("—")},
                {trText("Errors (Rx/Tx)", "Hatalar (Rx/Tx)"),
                 QStringLiteral("%1 / %2").arg(n.rxErrors).arg(n.txErrors)},
                {trText("Drops (Rx/Tx)", "Kayıplar (Rx/Tx)"),
                 QStringLiteral("%1 / %2").arg(n.rxDropped).arg(n.txDropped)},
            };
            if (extras.contains(QStringLiteral("driver_duplex"))) {
                rows.push_back({trText("Driver / Duplex", "Sürücü / Çift Yönlü"),
                                QStringLiteral("%1 / %2")
                                    .arg(n.driver.isEmpty() ? QStringLiteral("—") : n.driver,
                                         n.duplex.isEmpty() ? QStringLiteral("—") : n.duplex)});
            }
            if (extras.contains(QStringLiteral("packets_carrier"))) {
                rows.push_back({trText("Packets (Rx/Tx) / Carrier", "Paketler (Rx/Tx) / Taşıyıcı"),
                                QStringLiteral("%1 / %2 (%3)")
                                    .arg(n.rxPackets)
                                    .arg(n.txPackets)
                                    .arg(n.carrierChanges)});
            }
            setSpecsRows(QStringLiteral("%1:%2:%3")
                             .arg(entry.key)
                             .arg(static_cast<int>(currentLanguage()))
                             .arg(extras.join(QLatin1Char(','))),
                         rows);
        }
        break;
    }
    case DeviceKind::Gpu: {
        if (entry.deviceIndex >= 0 && entry.deviceIndex < m_snapshot.gpus.size()) {
            const GpuTelemetry& g = m_snapshot.gpus[entry.deviceIndex];
            rows = {
                {trText("Model", "Model"), g.name},
                {trText("Vendor", "Üretici"), g.vendor},
                {trText("Kernel Driver", "Çekirdek Sürücüsü"), g.driver},
                {trText("PCI Bus", "PCI Yolu"), g.pciSlot.isEmpty() ? QStringLiteral("—") : g.pciSlot},
                {trText("Power State", "Güç Durumu"), g.powerState},
                {trText("GPU Utilization", "GPU Kullanımı"),
                 g.sleeping ? trText("Sleeping (D3cold)", "Uykuda (D3cold)") : fmt::formatPercent(g.utilizationPercent)},
                {trText("VRAM (Used / Total)", "VRAM (Kullanılan / Toplam)"),
                 g.vramTotalBytes > 0
                     ? QStringLiteral("%1 / %2")
                           .arg(fmt::formatBytes(g.vramUsedBytes),
                                fmt::formatBytes(g.vramTotalBytes))
                     : trText("Shared System Memory", "Paylaşımlı Sistem Belleği")},
                {trText("Core Clock", "Çekirdek Saat Hızı"), fmt::formatFrequencyMHz(g.coreClockMHz)},
                {trText("Memory Clock", "Bellek Saat Hızı"), fmt::formatFrequencyMHz(g.memClockMHz)},
                {trText("Temperature", "Sıcaklık"), fmt::formatTemp(g.tempC)},
                {trText("Power Draw", "Güç Tüketimi"),
                 g.powerWatts >= 0.0
                     ? QStringLiteral("%1 W").arg(QString::number(g.powerWatts, 'f', 1))
                     : QStringLiteral("—")},
                {trText("GPU Fan", "GPU Fanı"), fmt::formatPercent(g.fanPercent, 0)},
                {trText("Active GPU Processes", "Aktif GPU Süreçleri"),
                 g.activeProcessCount >= 0 ? QString::number(g.activeProcessCount) : QStringLiteral("—")},
            };
            if (extras.contains(QStringLiteral("pci_ids"))) {
                rows.push_back({trText("PCI Vendor / Device ID", "PCI Satıcı / Aygıt ID"),
                                QStringLiteral("0x%1 / 0x%2").arg(g.vendorHex, g.deviceHex)});
            }
            if (extras.contains(QStringLiteral("mem_ctrl_util"))) {
                rows.push_back({trText("Memory Controller", "Bellek Denetleyicisi"),
                                fmt::formatPercent(g.memoryControllerPercent)});
            }
            setSpecsRows(QStringLiteral("%1:%2:%3")
                             .arg(entry.key)
                             .arg(static_cast<int>(currentLanguage()))
                             .arg(extras.join(QLatin1Char(','))),
                         rows);
        }
        break;
    }
    case DeviceKind::Fan: {
        if (entry.deviceIndex >= 0 && entry.deviceIndex < m_snapshot.fans.size()) {
            const FanTelemetry& f = m_snapshot.fans[entry.deviceIndex];
            rows = {
                {trText("Hwmon Source", "Hwmon Kaynağı"), f.hwmonName},
                {trText("Fan Label", "Fan Etiketi"), f.label},
                {trText("Current Speed", "Anlık Devir"), QStringLiteral("%1 RPM").arg(f.rpm)},
                {trText("Observed Min / Max", "Gözlenen Min / Maks"),
                 QStringLiteral("%1 / %2 RPM").arg(f.minObservedRpm).arg(f.maxObservedRpm)},
                {trText("PWM Duty Cycle", "PWM Görev Döngüsü"), fmt::formatPercent(f.pwmPercent, 0)},
                {trText("Control Mode", "Kontrol Modu"), f.pwmControlMode},
                {trText("Paired Sensor", "Eşleşen Sensör"),
                 f.pairedTempLabel.isEmpty() ? QStringLiteral("—") : f.pairedTempLabel},
                {trText("Paired Temp", "Eşleşen Sıcaklık"), fmt::formatTemp(f.pairedTempC)},
            };
            setSpecsRows(QStringLiteral("%1:%2").arg(entry.key).arg(static_cast<int>(currentLanguage())),
                         rows);
        }
        break;
    }
    case DeviceKind::Battery: {
        if (entry.deviceIndex >= 0 && entry.deviceIndex < m_snapshot.batteries.size()) {
            const BatteryTelemetry& b = m_snapshot.batteries[entry.deviceIndex];
            rows = {
                {trText("Supply ID", "Kaynak Kimliği"), b.id},
                {trText("Model Name", "Model Adı"),
                 b.modelName.isEmpty() ? QStringLiteral("—") : b.modelName},
                {trText("Manufacturer", "Üretici"),
                 b.manufacturer.isEmpty() ? QStringLiteral("—") : b.manufacturer},
                {trText("Technology", "Teknoloji"),
                 b.technology.isEmpty() ? QStringLiteral("—") : b.technology},
                {trText("Status", "Durum"), translateBatteryStatus(b.status)},
                {trText("Charge Level", "Şarj Seviyesi"), fmt::formatPercent(b.capacityPercent)},
                {trText("Energy (Now / Full)", "Enerji (Anlık / Tam)"),
                 (b.energyNowWh >= 0.0 && b.energyFullWh > 0.0)
                     ? QStringLiteral("%1 / %2 Wh")
                           .arg(QString::number(b.energyNowWh, 'f', 2),
                                QString::number(b.energyFullWh, 'f', 2))
                     : QStringLiteral("—")},
                {trText("Design Capacity", "Tasarım Kapasitesi"),
                 b.energyDesignWh > 0.0
                     ? QStringLiteral("%1 Wh").arg(QString::number(b.energyDesignWh, 'f', 2))
                     : QStringLiteral("—")},
                {trText("Battery Health", "Batarya Sağlığı"),
                 b.healthPercent > 0.0 ? fmt::formatPercent(b.healthPercent) : QStringLiteral("—")},
                {trText("Power Draw / Charge", "Güç Tüketimi / Şarj"),
                 QStringLiteral("%1 W").arg(QString::number(b.powerWatts, 'f', 2))},
                {trText("Voltage", "Gerilim"),
                 b.voltageVolts > 0.0
                     ? QStringLiteral("%1 V").arg(QString::number(b.voltageVolts, 'f', 3))
                     : QStringLiteral("—")},
                {trText("Time Remaining", "Kalan Süre"),
                 b.remainingHours > 0.0
                     ? fmt::formatUptime(b.remainingHours * 3600.0)
                     : QStringLiteral("—")},
                {trText("Cycle Count", "Döngü Sayısı"),
                 b.cycleCount >= 0 ? QString::number(b.cycleCount) : QStringLiteral("—")},
            };
            if (extras.contains(QStringLiteral("bat_temp"))) {
                rows.push_back({trText("Battery Temp", "Batarya Sıcaklığı"), fmt::formatTemp(b.tempC)});
            }
            if (extras.contains(QStringLiteral("bat_wear"))) {
                const double lossWh = (b.energyDesignWh > b.energyFullWh && b.energyFullWh > 0.0)
                    ? (b.energyDesignWh - b.energyFullWh)
                    : 0.0;
                rows.push_back({trText("Capacity Loss", "Kapasite Kaybı"),
                                QStringLiteral("%1 Wh").arg(QString::number(lossWh, 'f', 2))});
            }
            setSpecsRows(QStringLiteral("%1:%2:%3")
                             .arg(entry.key)
                             .arg(static_cast<int>(currentLanguage()))
                             .arg(extras.join(QLatin1Char(','))),
                         rows);
        }
        break;
    }
    case DeviceKind::ThermalOverview: {
        double maxTemp = -100.0;
        QString hottest;
        for (const auto& t : m_snapshot.thermals) {
            if (t.tempC > maxTemp) {
                maxTemp = t.tempC;
                hottest = QStringLiteral("%1 (%2)").arg(t.hwmonName, t.label);
            }
        }
        rows = {
            {trText("Total Sensors", "Toplam Sensör"), QString::number(m_snapshot.thermals.size())},
            {trText("Selected On Chart", "Grafikte Seçili"),
             QString::number(AppSettings::instance().selectedThermalSensors().size())},
            {trText("Total Fans", "Toplam Fan"), QString::number(m_snapshot.fans.size())},
            {trText("CPU Package", "CPU Paketi"), fmt::formatTemp(m_snapshot.cpu.packageTempC)},
            {trText("Hottest Sensor", "En Sıcak Nokta"),
             hottest.isEmpty()
                 ? QStringLiteral("—")
                 : QStringLiteral("%1 — %2").arg(hottest, fmt::formatTemp(maxTemp))},
        };
        setSpecsRows(QStringLiteral("%1:%2").arg(entry.key).arg(static_cast<int>(currentLanguage())),
                     rows);
        break;
    }
    }
}

void PerformanceTab::setSpecsRows(const QString& schemaId,
                                  const QVector<QPair<QString, QString>>& rows) {
    if (m_activeSchemaId == schemaId && m_specValueLabels.size() == rows.size()) {
        for (int i = 0; i < rows.size(); ++i) {
            m_specValueLabels[i]->setText(rows[i].second);
        }
        return;
    }

    while (m_specsLayout->rowCount() > 0) {
        const auto taken = m_specsLayout->takeRow(0);
        delete taken.labelItem;
        delete taken.fieldItem;
    }
    const QList<QWidget*> oldWidgets = m_specsWidget->findChildren<QWidget*>(
        QString(), Qt::FindDirectChildrenOnly);
    for (QWidget* w : oldWidgets) {
        delete w;
    }
    m_specValueLabels.clear();
    m_specValueLabels.reserve(rows.size());

    for (const auto& [key, val] : rows) {
        auto* keyLabel = new QLabel(key + QLatin1Char(':'), m_specsWidget);
        auto* valLabel = new QLabel(val, m_specsWidget);
        valLabel->setWordWrap(true);
        valLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
        m_specsLayout->addRow(keyLabel, valLabel);
        m_specValueLabels.push_back(valLabel);
    }

    m_activeSchemaId = schemaId;
}

} // namespace sysmon
