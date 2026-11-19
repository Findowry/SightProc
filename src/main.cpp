#include "core/AppSettings.h"
#include "core/BatteryCollector.h"
#include "core/CpuCollector.h"
#include "core/DiskCollector.h"
#include "core/DisplaySessionDetector.h"
#include "core/FormatUtils.h"
#include "core/GpuCollector.h"
#include "core/HwmonCollector.h"
#include "core/MemoryCollector.h"
#include "core/NetworkCollector.h"
#include "core/NvmlBridge.h"
#include "core/ProcessCollector.h"
#include "core/SystemdController.h"
#include "core/TelemetryTypes.h"
#include "ui/MainWindow.h"

#include <QApplication>
#include <QFile>
#include <QGuiApplication>
#include <QIcon>
#include <QMetaType>
#include <QProcess>
#include <QProcessEnvironment>

#include <algorithm>
#include <chrono>
#include <iostream>
#include <string_view>
#include <thread>

namespace {

int runDumpTelemetry() {
    sysmon::NvmlBridge nvml;
    sysmon::HwmonCollector hwmonCollector;
    sysmon::CpuCollector cpuCollector;
    sysmon::MemoryCollector memoryCollector;
    sysmon::DiskCollector diskCollector;
    sysmon::NetworkCollector networkCollector;
    sysmon::GpuCollector gpuCollector(nvml);
    sysmon::BatteryCollector batteryCollector;

    auto hw = hwmonCollector.collect();
    double pkgTemp = sysmon::HwmonCollector::findCpuPackageTemp(hw.thermals);
    (void)cpuCollector.collect(0.25, pkgTemp, hw.thermals);
    (void)diskCollector.collect(0.25, hw.thermals);
    (void)networkCollector.collect(0.25);
    (void)gpuCollector.collect(0.25, false);

    std::this_thread::sleep_for(std::chrono::milliseconds(250));

    hw = hwmonCollector.collect();
    pkgTemp = sysmon::HwmonCollector::findCpuPackageTemp(hw.thermals);
    const auto cpu = cpuCollector.collect(0.25, pkgTemp, hw.thermals);
    const auto mem = memoryCollector.collect(hw.thermals);
    const auto disks = diskCollector.collect(0.25, hw.thermals);
    const auto nets = networkCollector.collect(0.25);
    const auto gpus = gpuCollector.collect(0.25, false);
    const auto batteries = batteryCollector.collect();

    std::cout << "=== CPU ===\n"
              << "Model: " << cpu.modelName.toStdString()
              << " | Cores/Threads: " << cpu.physicalCores << "/" << cpu.logicalCores << "\n"
              << "Usage: " << sysmon::fmt::formatPercent(cpu.totalUsagePercent).toStdString()
              << " | Avg Clock: " << sysmon::fmt::formatFrequencyMHz(cpu.avgFreqMHz).toStdString()
              << " | Package Temp: " << sysmon::fmt::formatTemp(cpu.packageTempC).toStdString() << "\n"
              << "Cache L1d/L1i/L2/L3: "
              << sysmon::fmt::formatBytes(cpu.l1dBytes).toStdString() << " / "
              << sysmon::fmt::formatBytes(cpu.l1iBytes).toStdString() << " / "
              << sysmon::fmt::formatBytes(cpu.l2Bytes).toStdString() << " / "
              << sysmon::fmt::formatBytes(cpu.l3Bytes).toStdString() << "\n"
              << "Driver/Governor: " << cpu.scalingDriver.toStdString()
              << " / " << cpu.governor.toStdString()
              << " | Virt: " << cpu.virtualization.toStdString() << "\n";

    std::cout << "\n=== MEMORY ===\n"
              << "Used / Total: " << sysmon::fmt::formatBytes(mem.usedBytes).toStdString()
              << " / " << sysmon::fmt::formatBytes(mem.totalBytes).toStdString()
              << " (" << sysmon::fmt::formatPercent(mem.usagePercent).toStdString() << ")\n"
              << "Swap: " << sysmon::fmt::formatBytes(mem.swapUsedBytes).toStdString()
              << " / " << sysmon::fmt::formatBytes(mem.swapTotalBytes).toStdString() << "\n";
    for (const auto& dimm : mem.dimmTempsC) {
        std::cout << "  " << dimm.first.toStdString() << ": "
                  << sysmon::fmt::formatTemp(dimm.second).toStdString() << "\n";
    }

    std::cout << "\n=== DISKS (" << disks.size() << ") ===\n";
    for (const auto& d : disks) {
        std::cout << "  " << d.name.toStdString() << " (" << d.model.toStdString() << ") "
                  << sysmon::fmt::formatBytes(d.capacityBytes).toStdString()
                  << " | R: " << sysmon::fmt::formatRate(d.readBytesPerSec).toStdString()
                  << " | W: " << sysmon::fmt::formatRate(d.writeBytesPerSec).toStdString()
                  << " | Scheduler: " << d.scheduler.toStdString()
                  << " | Temp: " << sysmon::fmt::formatTemp(d.tempC).toStdString() << "\n";
    }

    std::cout << "\n=== NETWORK (" << nets.size() << ") ===\n";
    for (const auto& n : nets) {
        std::cout << "  " << n.name.toStdString()
                  << " [" << (n.isWireless ? "Wi-Fi" : "Ethernet") << ", "
                  << n.operState.toStdString() << "]"
                  << " | Rx: " << sysmon::fmt::formatRate(n.rxBytesPerSec).toStdString()
                  << " | Tx: " << sysmon::fmt::formatRate(n.txBytesPerSec).toStdString() << "\n";
    }

    std::cout << "\n=== GPU (" << gpus.size() << ") ===\n";
    for (const auto& g : gpus) {
        std::cout << "  " << g.id.toStdString() << ": " << g.name.toStdString()
                  << " | Driver: " << g.driver.toStdString()
                  << " | Power: " << g.powerState.toStdString()
                  << " | Util: " << (g.sleeping ? "Sleep" : sysmon::fmt::formatPercent(g.utilizationPercent).toStdString())
                  << " | VRAM: " << sysmon::fmt::formatBytes(g.vramUsedBytes).toStdString()
                  << " / " << sysmon::fmt::formatBytes(g.vramTotalBytes).toStdString()
                  << " | Clock: " << sysmon::fmt::formatFrequencyMHz(g.coreClockMHz).toStdString() << "\n";
    }

    std::cout << "\n=== FANS (" << hw.fans.size() << ") & THERMALS (" << hw.thermals.size() << ") ===\n";
    for (const auto& f : hw.fans) {
        std::cout << "  " << f.label.toStdString() << ": " << f.rpm << " RPM"
                  << " | PWM: " << sysmon::fmt::formatPercent(f.pwmPercent, 0).toStdString()
                  << " (" << f.pwmControlMode.toStdString() << ")"
                  << " | Paired: " << f.pairedTempLabel.toStdString()
                  << " " << sysmon::fmt::formatTemp(f.pairedTempC).toStdString() << "\n";
    }

    std::cout << "\n=== BATTERY (" << batteries.size() << ") ===\n";
    for (const auto& b : batteries) {
        std::cout << "  " << b.id.toStdString()
                  << " (" << b.modelName.toStdString() << ")"
                  << " | Status: " << b.status.toStdString()
                  << " | Charge: " << sysmon::fmt::formatPercent(b.capacityPercent).toStdString()
                  << " | Power: " << b.powerWatts << " W"
                  << " | Voltage: " << b.voltageVolts << " V"
                  << " | Energy: " << b.energyNowWh << "/" << b.energyFullWh << " Wh"
                  << " | Health: " << sysmon::fmt::formatPercent(b.healthPercent).toStdString()
                  << " | Cycles: " << b.cycleCount << "\n";
    }

    const QString cfgPath = sysmon::AppSettings::configFilePath();
    const auto& settings = sysmon::AppSettings::instance();
    const QString dispSession = sysmon::detectActiveDisplaySession(QString());
    const QFile embeddedIcon(QStringLiteral(":/icons/sightproc.svg"));
    std::cout << "\n=== SESSION & CONFIG ===\n"
              << "Display Session: " << dispSession.toStdString() << "\n"
              << "Config File: " << cfgPath.toStdString() << "\n"
              << "Language: "
              << (settings.language() == sysmon::Language::Turkish ? "Turkish" : "English")
              << " | Refresh: " << settings.refreshIntervalMs() << " ms\n"
              << "Embedded Icon: "
              << (embeddedIcon.exists() ? "OK" : "MISSING")
              << " (" << embeddedIcon.size() << " bytes)\n";

    nvml.shutdown();
    return 0;
}

int runDumpProcesses() {
    sysmon::NvmlBridge nvml;
    sysmon::ProcessCollector collector(nvml);

    (void)collector.collect(0.30, false);
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    auto snap = collector.collect(0.30, false);

    std::cout << "=== PROCESSES ===\n"
              << "Total Processes: " << snap.totalProcessCount
              << " | Total Threads: " << snap.totalThreadCount
              << " | .desktop Entries: " << collector.desktopEntryCount() << "\n";

    std::sort(snap.processes.begin(), snap.processes.end(),
              [](const sysmon::ProcessInfo& a, const sysmon::ProcessInfo& b) {
                  return a.rssBytes > b.rssBytes;
              });

    const int topP = std::min<int>(10, static_cast<int>(snap.processes.size()));
    std::cout << "\nTop " << topP << " Processes by RSS:\n";
    for (int i = 0; i < topP; ++i) {
        const auto& p = snap.processes[i];
        std::cout << "  PID " << p.pid << " (PPID " << p.ppid << ") [" << p.user.toStdString() << "] "
                  << p.name.toStdString()
                  << " | Icon: " << p.iconName.toStdString()
                  << " | CPU: " << sysmon::fmt::formatPercent(p.cpuPercent).toStdString()
                  << " | RSS: " << sysmon::fmt::formatBytes(p.rssBytes).toStdString()
                  << "\n";
    }

    std::cout << "\n=== DESKTOP APPS (" << snap.applications.size() << ") ===\n";
    for (const auto& app : snap.applications) {
        std::cout << "  " << app.name.toStdString()
                  << " (" << app.pids.size() << " procs)"
                  << " | RAM: " << sysmon::fmt::formatBytes(app.rssBytes).toStdString()
                  << " | Icon: " << app.iconName.toStdString() << "\n";
    }

    nvml.shutdown();
    return 0;
}

int runDumpServices() {
    QProcess proc;
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("LC_ALL"), QStringLiteral("C"));
    env.insert(QStringLiteral("LANG"), QStringLiteral("C"));
    proc.setProcessEnvironment(env);
    proc.start(QStringLiteral("systemctl"),
               {QStringLiteral("list-unit-files"),
                QStringLiteral("--type=service"),
                QStringLiteral("--no-pager"),
                QStringLiteral("--no-legend")});
    proc.waitForFinished(5000);
    const QString out = QString::fromUtf8(proc.readAllStandardOutput());
    const auto services = sysmon::SystemdController::parseUnitFilesOutput(out);

    std::cout << "=== SYSTEMD UNIT FILES (" << services.size() << ") ===\n";
    const int limit = std::min<int>(10, static_cast<int>(services.size()));
    for (int i = 0; i < limit; ++i) {
        std::cout << "  " << services[i].unitName.toStdString()
                  << " [" << services[i].unitFileState.toStdString() << "]\n";
    }
    return 0;
}

} // namespace

int main(int argc, char* argv[]) {
    if (argc > 1) {
        const std::string_view arg(argv[1]);
        if (arg == "--dump-telemetry") {
            return runDumpTelemetry();
        }
        if (arg == "--dump-processes") {
            return runDumpProcesses();
        }
        if (arg == "--dump-services") {
            return runDumpServices();
        }
    }

    qRegisterMetaType<sysmon::SystemTelemetrySnapshot>("sysmon::SystemTelemetrySnapshot");
    qRegisterMetaType<sysmon::ProcessSnapshot>("sysmon::ProcessSnapshot");
    qRegisterMetaType<sysmon::ServiceInfo>("sysmon::ServiceInfo");
    qRegisterMetaType<QVector<sysmon::ServiceInfo>>("QVector<sysmon::ServiceInfo>");

    QApplication app(argc, argv);

    QApplication::setOrganizationName(QStringLiteral("sightproc"));
    QApplication::setApplicationName(QStringLiteral("sightproc"));
    QApplication::setApplicationDisplayName(QStringLiteral("SightProc"));
    QApplication::setApplicationVersion(QStringLiteral("1.0.0"));
    QGuiApplication::setDesktopFileName(QStringLiteral("sightproc"));

    QIcon appIcon(QStringLiteral(":/icons/sightproc.svg"));
    if (QIcon::hasThemeIcon(QStringLiteral("sightproc"))) {
        appIcon = QIcon::fromTheme(QStringLiteral("sightproc"));
    }
    QApplication::setWindowIcon(appIcon);

    // Başlangıçta kayıtlı tercihleri (dil, yenileme aralığı ve ölçekler dahil) yükle
    (void)sysmon::AppSettings::instance();

    sysmon::MainWindow window;
    window.setWindowIcon(appIcon);
    window.show();

    return app.exec();
}
