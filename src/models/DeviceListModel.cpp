#include "models/DeviceListModel.h"

#include "core/FormatUtils.h"
#include "core/Lang.h"

#include <algorithm>
#include <unordered_map>

namespace sysmon {

DeviceListModel::DeviceListModel(QObject* parent)
    : QAbstractListModel(parent) {}

int DeviceListModel::rowCount(const QModelIndex& parent) const {
    if (parent.isValid()) {
        return 0;
    }
    return static_cast<int>(m_entries.size());
}

QVariant DeviceListModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= m_entries.size()) {
        return {};
    }
    const DeviceEntry& entry = m_entries[index.row()];
    if (role == Qt::DisplayRole) {
        return entry.title;
    }
    if (role == Qt::DecorationRole) {
        return entry.icon;
    }
    return {};
}

void DeviceListModel::retranslate() {
    if (m_hasSnapshot) {
        updateFromSnapshot(m_lastSnapshot);
    }
}

void DeviceListModel::updateFromSnapshot(const SystemTelemetrySnapshot& snapshot) {
    m_lastSnapshot = snapshot;
    m_hasSnapshot = true;

    struct DesiredItem {
        QString key;
        DeviceKind kind;
        AppSettings::HardwareCategory category;
        int deviceIndex;
        QString title;
        QString subtitle;
        QIcon icon;
        bool autoScale;
        double fixedMax;
        double primarySample;
        double secondarySample;
        QVector<QPair<QString, double>> extras;
    };

    QVector<DesiredItem> desired;

    // 1. CPU
    {
        const QString sub = QStringLiteral("%1 · %2")
                                .arg(fmt::formatPercent(snapshot.cpu.totalUsagePercent),
                                     fmt::formatFrequencyMHz(snapshot.cpu.avgFreqMHz));
        desired.push_back(DesiredItem{
            QStringLiteral("cpu"),
            DeviceKind::Cpu,
            AppSettings::HardwareCategory::Cpu,
            0,
            trText("CPU", "İşlemci (CPU)"),
            sub,
            QIcon::fromTheme(QStringLiteral("cpu"),
                             QIcon::fromTheme(QStringLiteral("utilities-system-monitor"))),
            false,
            100.0,
            snapshot.cpu.totalUsagePercent,
            0.0,
            {
                {QStringLiteral("cpu_freq"), snapshot.cpu.avgFreqMHz},
                {QStringLiteral("cpu_temp"), std::max(0.0, snapshot.cpu.packageTempC)},
                {QStringLiteral("cpu_user"), snapshot.cpu.userPercent},
                {QStringLiteral("cpu_sys"), snapshot.cpu.systemPercent},
                {QStringLiteral("cpu_ctxt"), static_cast<double>(snapshot.cpu.contextSwitchesPerSec) / 1000.0},
            },
        });
    }

    // 2. Bellek
    {
        const QString sub = QStringLiteral("%1 / %2 (%3)")
                                .arg(fmt::formatBytes(snapshot.memory.usedBytes),
                                     fmt::formatBytes(snapshot.memory.totalBytes),
                                     fmt::formatPercent(snapshot.memory.usagePercent));
        const double cacheGiB =
            static_cast<double>(snapshot.memory.buffersBytes + snapshot.memory.cachedBytes) /
            (1024.0 * 1024.0 * 1024.0);
        desired.push_back(DesiredItem{
            QStringLiteral("memory"),
            DeviceKind::Memory,
            AppSettings::HardwareCategory::Memory,
            0,
            trText("Memory", "Bellek"),
            sub,
            QIcon::fromTheme(QStringLiteral("media-flash"),
                             QIcon::fromTheme(QStringLiteral("utilities-system-monitor"))),
            false,
            100.0,
            snapshot.memory.usagePercent,
            0.0,
            {
                {QStringLiteral("mem_swap"), snapshot.memory.swapUsagePercent},
                {QStringLiteral("mem_cache"), cacheGiB},
            },
        });
    }

    // 3. Diskler
    for (int i = 0; i < snapshot.disks.size(); ++i) {
        const DiskTelemetry& d = snapshot.disks[i];
        const QString sub = QStringLiteral("%1 %2 · %3 %4")
                                .arg(trText("R:", "O:"),
                                     fmt::formatRate(d.readBytesPerSec),
                                     trText("W:", "Y:"),
                                     fmt::formatRate(d.writeBytesPerSec));
        desired.push_back(DesiredItem{
            QStringLiteral("disk:%1").arg(d.name),
            DeviceKind::Disk,
            AppSettings::HardwareCategory::Disk,
            i,
            QStringLiteral("%1 (%2)").arg(trText("Disk", "Disk"), d.name),
            sub,
            QIcon::fromTheme(QStringLiteral("drive-harddisk")),
            true,
            1024.0,
            d.readBytesPerSec,
            d.writeBytesPerSec,
            {
                {QStringLiteral("disk_active"), d.activePercent},
                {QStringLiteral("disk_riops"), d.readIops},
                {QStringLiteral("disk_wiops"), d.writeIops},
                {QStringLiteral("disk_temp"), std::max(0.0, d.tempC)},
            },
        });
    }

    // 4. Ağ Arayüzleri
    for (int i = 0; i < snapshot.networks.size(); ++i) {
        const NetworkTelemetry& n = snapshot.networks[i];
        const QString prefix = n.isWireless ? QStringLiteral("Wi-Fi") : QStringLiteral("Ethernet");
        const QString sub = QStringLiteral("↓ %1 · ↑ %2")
                                .arg(fmt::formatRate(n.rxBytesPerSec),
                                     fmt::formatRate(n.txBytesPerSec));
        desired.push_back(DesiredItem{
            QStringLiteral("net:%1").arg(n.name),
            DeviceKind::Network,
            AppSettings::HardwareCategory::Network,
            i,
            QStringLiteral("%1 (%2)").arg(prefix, n.name),
            sub,
            QIcon::fromTheme(n.isWireless
                                 ? QStringLiteral("network-wireless")
                                 : QStringLiteral("network-wired")),
            true,
            1024.0,
            n.rxBytesPerSec,
            n.txBytesPerSec,
            {
                {QStringLiteral("net_err"),
                 static_cast<double>(n.rxErrors + n.txErrors + n.rxDropped + n.txDropped)},
            },
        });
    }

    // 5. GPU'lar
    for (int i = 0; i < snapshot.gpus.size(); ++i) {
        const GpuTelemetry& g = snapshot.gpus[i];
        QString sub;
        if (g.sleeping) {
            sub = QStringLiteral("%1 (%2)").arg(trText("Sleep", "Uyku"), g.powerState);
        } else if (g.vramTotalBytes > 0) {
            sub = QStringLiteral("%1 · %2 / %3")
                      .arg(fmt::formatPercent(g.utilizationPercent),
                           fmt::formatBytes(g.vramUsedBytes),
                           fmt::formatBytes(g.vramTotalBytes));
        } else {
            sub = QStringLiteral("%1 · %2")
                      .arg(fmt::formatPercent(g.utilizationPercent),
                           fmt::formatFrequencyMHz(g.coreClockMHz));
        }
        const double utilSample = std::max(0.0, g.utilizationPercent);
        const double vramMiB = static_cast<double>(g.vramUsedBytes) / (1024.0 * 1024.0);
        const double clockMHz = std::max(0.0, g.coreClockMHz);
        const double secSample = (g.vramTotalBytes > 0) ? vramMiB : clockMHz;

        desired.push_back(DesiredItem{
            QStringLiteral("gpu:%1").arg(g.id),
            DeviceKind::Gpu,
            AppSettings::HardwareCategory::Gpu,
            i,
            QStringLiteral("GPU %1 (%2)").arg(i).arg(g.driver),
            sub,
            QIcon::fromTheme(QStringLiteral("video-display")),
            false,
            100.0,
            utilSample,
            secSample,
            {
                {QStringLiteral("gpu_vram"), vramMiB},
                {QStringLiteral("gpu_clock"), clockMHz},
                {QStringLiteral("gpu_temp"), std::max(0.0, g.tempC)},
                {QStringLiteral("gpu_power"), std::max(0.0, g.powerWatts)},
            },
        });
    }

    // 6. Fanlar
    double maxObservedFanRpm = 6000.0;
    for (const auto& f : snapshot.fans) {
        maxObservedFanRpm = std::max(maxObservedFanRpm, static_cast<double>(f.maxObservedRpm));
    }
    for (int i = 0; i < snapshot.fans.size(); ++i) {
        const FanTelemetry& f = snapshot.fans[i];
        const QString tempText = (f.pairedTempC > -50.0)
            ? QStringLiteral(" · %1").arg(fmt::formatTemp(f.pairedTempC))
            : QString{};
        const QString sub = QStringLiteral("%1 RPM%2").arg(f.rpm).arg(tempText);
        desired.push_back(DesiredItem{
            QStringLiteral("fan:%1").arg(f.id),
            DeviceKind::Fan,
            AppSettings::HardwareCategory::Fan,
            i,
            f.label,
            sub,
            QIcon::fromTheme(QStringLiteral("sensors-fan-symbolic"),
                             QIcon::fromTheme(QStringLiteral("sensors-temperature-symbolic"))),
            false,
            maxObservedFanRpm,
            static_cast<double>(f.rpm),
            std::max(0.0, f.pairedTempC),
            {
                {QStringLiteral("fan_temp"), std::max(0.0, f.pairedTempC)},
                {QStringLiteral("fan_pwm"), std::max(0.0, f.pwmPercent)},
            },
        });
    }

    // 7. Bataryalar (sistemde varsa gösterilir)
    for (int i = 0; i < snapshot.batteries.size(); ++i) {
        const BatteryTelemetry& b = snapshot.batteries[i];
        QString stateWord = b.status;
        if (b.status == QStringLiteral("Charging")) {
            stateWord = trText("Charging", "Şarj Oluyor");
        } else if (b.status == QStringLiteral("Discharging")) {
            stateWord = trText("Discharging", "Deşarj Oluyor");
        } else if (b.status == QStringLiteral("Full")) {
            stateWord = trText("Full", "Tam");
        }
        const QString sub = (b.powerWatts > 0.05)
            ? QStringLiteral("%1 · %2 W · %3")
                  .arg(fmt::formatPercent(b.capacityPercent),
                       QString::number(b.powerWatts, 'f', 1),
                       stateWord)
            : QStringLiteral("%1 · %2").arg(fmt::formatPercent(b.capacityPercent), stateWord);

        desired.push_back(DesiredItem{
            QStringLiteral("bat:%1").arg(b.id),
            DeviceKind::Battery,
            AppSettings::HardwareCategory::Battery,
            i,
            QStringLiteral("%1 (%2)").arg(trText("Battery", "Batarya"), b.id),
            sub,
            QIcon::fromTheme(QStringLiteral("battery-good-symbolic"),
                             QIcon::fromTheme(QStringLiteral("battery"))),
            false,
            100.0,
            b.capacityPercent,
            b.powerWatts,
            {
                {QStringLiteral("bat_power"), b.powerWatts},
                {QStringLiteral("bat_voltage"), std::max(0.0, b.voltageVolts)},
            },
        });
    }

    // 8. Genel Termal Özeti
    if (!snapshot.thermals.isEmpty()) {
        double maxTemp = -100.0;
        QVector<QPair<QString, double>> thermalExtras;
        thermalExtras.reserve(snapshot.thermals.size());
        for (const auto& t : snapshot.thermals) {
            maxTemp = std::max(maxTemp, t.tempC);
            thermalExtras.push_back({QStringLiteral("therm:%1").arg(t.id), std::max(0.0, t.tempC)});
        }
        const double cpuOrMax = (snapshot.cpu.packageTempC > -50.0)
            ? snapshot.cpu.packageTempC
            : maxTemp;
        desired.push_back(DesiredItem{
            QStringLiteral("thermal:overview"),
            DeviceKind::ThermalOverview,
            AppSettings::HardwareCategory::Thermal,
            0,
            trText("Thermal Sensors", "Termal Sensörler"),
            QStringLiteral("CPU: %1 · %2 %3")
                .arg(fmt::formatTemp(cpuOrMax),
                     trText("Peak:", "Tepe:"),
                     fmt::formatTemp(maxTemp)),
            QIcon::fromTheme(QStringLiteral("sensors-temperature-symbolic"),
                             QIcon::fromTheme(QStringLiteral("utilities-system-monitor"))),
            false,
            110.0,
            std::max(0.0, cpuOrMax),
            std::max(0.0, maxTemp),
            thermalExtras,
        });
    }

    bool structureSame = (m_entries.size() == desired.size());
    if (structureSame) {
        for (int i = 0; i < m_entries.size(); ++i) {
            if (m_entries[i].key != desired[i].key) {
                structureSame = false;
                break;
            }
        }
    }

    if (structureSame) {
        for (int i = 0; i < m_entries.size(); ++i) {
            DeviceEntry& e = m_entries[i];
            const DesiredItem& d = desired[i];
            e.deviceIndex = d.deviceIndex;
            e.title = d.title;
            e.subtitle = d.subtitle;
            e.fixedMax = d.fixedMax;
            e.primaryHistory.push(d.primarySample);
            e.secondaryHistory.push(d.secondarySample);
            for (const auto& ex : d.extras) {
                e.extraHistories[ex.first].push(ex.second);
            }
        }
        if (!m_entries.isEmpty()) {
            emit dataChanged(index(0), index(static_cast<int>(m_entries.size()) - 1));
        }
        return;
    }

    struct OldHist {
        RingBuffer<double, 64> p;
        RingBuffer<double, 64> s;
        QHash<QString, RingBuffer<double, 64>> ex;
    };
    std::unordered_map<QString, OldHist> oldHistories;
    for (const auto& e : m_entries) {
        oldHistories.emplace(e.key, OldHist{e.primaryHistory, e.secondaryHistory, e.extraHistories});
    }

    beginResetModel();
    m_entries.clear();
    m_entries.reserve(desired.size());
    for (const auto& d : desired) {
        DeviceEntry e;
        e.key = d.key;
        e.kind = d.kind;
        e.category = d.category;
        e.deviceIndex = d.deviceIndex;
        e.title = d.title;
        e.subtitle = d.subtitle;
        e.icon = d.icon;
        e.autoScale = d.autoScale;
        e.fixedMax = d.fixedMax;
        if (const auto it = oldHistories.find(d.key); it != oldHistories.end()) {
            e.primaryHistory = it->second.p;
            e.secondaryHistory = it->second.s;
            e.extraHistories = it->second.ex;
        }
        e.primaryHistory.push(d.primarySample);
        e.secondaryHistory.push(d.secondarySample);
        for (const auto& ex : d.extras) {
            e.extraHistories[ex.first].push(ex.second);
        }
        m_entries.push_back(std::move(e));
    }
    endResetModel();
}

const DeviceEntry* DeviceListModel::entryAt(int row) const {
    if (row < 0 || row >= m_entries.size()) {
        return nullptr;
    }
    return &m_entries[row];
}

int DeviceListModel::rowForKey(const QString& key) const {
    for (int i = 0; i < m_entries.size(); ++i) {
        if (m_entries[i].key == key) {
            return i;
        }
    }
    return -1;
}

} // namespace sysmon
