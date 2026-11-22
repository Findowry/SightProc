#pragma once

#include <QMetaType>
#include <QPair>
#include <QString>
#include <QStringList>
#include <QVector>

#include <cstdint>

namespace sysmon {

struct CoreTelemetry {
    int id{0};
    double usagePercent{0.0};
    double curFreqMHz{0.0};
};

struct CpuTelemetry {
    QString modelName;
    QString vendor;
    QString architecture;
    int physicalCores{0};
    int logicalCores{0};

    double totalUsagePercent{0.0};
    double userPercent{0.0};
    double systemPercent{0.0};
    double iowaitPercent{0.0};

    double avgFreqMHz{0.0};
    double maxCurrentFreqMHz{0.0};
    double baseFreqMHz{0.0};
    double boostFreqMHz{0.0};
    double uptimeSeconds{0.0};

    std::uint64_t l1dBytes{0};
    std::uint64_t l1iBytes{0};
    std::uint64_t l2Bytes{0};
    std::uint64_t l3Bytes{0};

    QString scalingDriver;
    QString governor;
    QString energyPreference;

    std::uint64_t contextSwitchesPerSec{0};
    std::uint64_t processesRunning{0};
    std::uint64_t processesBlocked{0};

    double loadAvg1{0.0};
    double loadAvg5{0.0};
    double loadAvg15{0.0};
    std::uint64_t interruptsPerSec{0};
    std::uint64_t totalForks{0};
    double minScalingFreqMHz{0.0};
    double maxScalingFreqMHz{0.0};
    QString virtualization;
    double bogoMips{0.0};
    QString microcode;
    double coreTempMinC{-100.0};
    double coreTempMaxC{-100.0};

    double packageTempC{-100.0};
    QVector<CoreTelemetry> cores;
};

struct MemoryTelemetry {
    std::uint64_t totalBytes{0};
    std::uint64_t usedBytes{0};
    std::uint64_t availableBytes{0};
    std::uint64_t freeBytes{0};
    std::uint64_t buffersBytes{0};
    std::uint64_t cachedBytes{0};
    std::uint64_t sharedBytes{0};
    std::uint64_t reclaimableBytes{0};

    std::uint64_t dirtyBytes{0};
    std::uint64_t writebackBytes{0};
    std::uint64_t activeBytes{0};
    std::uint64_t inactiveBytes{0};
    std::uint64_t committedAsBytes{0};
    std::uint64_t pageTablesBytes{0};

    std::uint64_t swapTotalBytes{0};
    std::uint64_t swapUsedBytes{0};

    double usagePercent{0.0};
    double swapUsagePercent{0.0};

    QStringList swapDevices;
    QVector<QPair<QString, double>> dimmTempsC;
};

struct DiskTelemetry {
    QString name;
    QString devicePath;
    QString model;
    std::uint64_t capacityBytes{0};
    bool isRotational{false};
    std::uint64_t logicalBlockSize{512};

    double readBytesPerSec{0.0};
    double writeBytesPerSec{0.0};
    std::uint64_t totalReadBytes{0};
    std::uint64_t totalWriteBytes{0};
    std::uint64_t totalReadOps{0};
    std::uint64_t totalWriteOps{0};
    double readIops{0.0};
    double writeIops{0.0};
    double activePercent{0.0};
    double avgResponseMs{0.0};

    QString scheduler;
    double tempC{-100.0};
    QStringList mountPoints;
};

struct NetworkTelemetry {
    QString name;
    bool isWireless{false};
    QString operState;
    int linkSpeedMbps{-1};
    QString macAddress;
    int mtu{0};
    QString driver;
    QString duplex;
    std::uint64_t carrierChanges{0};
    QStringList ipv4Addresses;
    QStringList ipv6Addresses;

    double rxBytesPerSec{0.0};
    double txBytesPerSec{0.0};
    std::uint64_t rxTotalBytes{0};
    std::uint64_t txTotalBytes{0};
    std::uint64_t rxPackets{0};
    std::uint64_t txPackets{0};
    std::uint64_t rxErrors{0};
    std::uint64_t txErrors{0};
    std::uint64_t rxDropped{0};
    std::uint64_t txDropped{0};
};

struct GpuTelemetry {
    QString id;
    QString name;
    QString vendor;
    QString driver;
    QString pciSlot;
    QString vendorHex;
    QString deviceHex;
    QString powerState;
    bool sleeping{false};

    double utilizationPercent{-1.0};
    double memoryControllerPercent{-1.0};
    std::uint64_t vramUsedBytes{0};
    std::uint64_t vramTotalBytes{0};
    double coreClockMHz{-1.0};
    double maxClockMHz{-1.0};
    double memClockMHz{-1.0};
    double tempC{-100.0};
    double powerWatts{-1.0};
    double fanPercent{-1.0};
    int activeProcessCount{-1};
};

struct ThermalSensor {
    QString id;
    QString hwmonName;
    QString label;
    double tempC{-100.0};
    double maxTempC{-100.0};
    double critTempC{-100.0};
};

struct BatteryTelemetry {
    QString id;
    QString modelName;
    QString manufacturer;
    QString technology;
    QString status;
    double capacityPercent{0.0};
    double energyNowWh{-1.0};
    double energyFullWh{-1.0};
    double energyDesignWh{-1.0};
    double powerWatts{0.0};
    double voltageVolts{-1.0};
    double healthPercent{-1.0};
    double remainingHours{-1.0};
    int cycleCount{-1};
    double tempC{-100.0};
};

struct FanTelemetry {
    QString id;
    QString hwmonName;
    QString label;
    int rpm{0};
    int minObservedRpm{0};
    int maxObservedRpm{0};
    double pwmPercent{-1.0};
    QString pwmControlMode;
    QString pairedTempLabel;
    double pairedTempC{-100.0};
};

struct SystemTelemetrySnapshot {
    qint64 timestampMs{0};
    CpuTelemetry cpu;
    MemoryTelemetry memory;
    QVector<DiskTelemetry> disks;
    QVector<NetworkTelemetry> networks;
    QVector<GpuTelemetry> gpus;
    QVector<FanTelemetry> fans;
    QVector<ThermalSensor> thermals;
    QVector<BatteryTelemetry> batteries;
    QString displaySession;
};

struct ProcessInfo {
    int pid{0};
    int ppid{0};
    std::uint32_t uid{0};
    QString name;
    QString cmdline;
    QString exePath;
    QString iconName;
    QString user;
    char stateChar{'?'};
    QString stateText;
    int niceValue{0};
    int threads{1};
    std::uint64_t startTimeTicks{0};

    double cpuPercent{0.0};
    std::uint64_t rssBytes{0};
    std::uint64_t sharedBytes{0};

    bool ioReadable{false};
    double diskReadBytesPerSec{0.0};
    double diskWriteBytesPerSec{0.0};

    bool netReadable{false};
    double netRxBytesPerSec{0.0};
    double netTxBytesPerSec{0.0};
    int tcpSocketCount{0};

    bool hasGpuMetrics{false};
    double gpuPercent{0.0};
    std::uint64_t gpuMemBytes{0};

    QString cgroupPath;
    bool isKernelThread{false};
};

struct ApplicationInfo {
    QString id;
    QString name;
    QString iconName;
    QString desktopId;
    QVector<int> pids;

    double cpuPercent{0.0};
    std::uint64_t rssBytes{0};
    std::uint64_t sharedBytes{0};

    bool ioReadable{false};
    double diskReadBytesPerSec{0.0};
    double diskWriteBytesPerSec{0.0};

    bool netReadable{false};
    double netRxBytesPerSec{0.0};
    double netTxBytesPerSec{0.0};

    bool hasGpuMetrics{false};
    double gpuPercent{0.0};
    std::uint64_t gpuMemBytes{0};
};

struct ProcessSnapshot {
    qint64 timestampMs{0};
    int totalProcessCount{0};
    int totalThreadCount{0};
    std::uint32_t currentUserUid{0};
    QVector<ProcessInfo> processes;
    QVector<ApplicationInfo> applications;
};

struct ServiceInfo {
    QString unitName;
    QString loadState;
    QString activeState;
    QString subState;
    QString unitFileState;
    QString description;
};

} // namespace sysmon

Q_DECLARE_METATYPE(sysmon::SystemTelemetrySnapshot)
Q_DECLARE_METATYPE(sysmon::ProcessSnapshot)
Q_DECLARE_METATYPE(sysmon::ServiceInfo)
Q_DECLARE_METATYPE(QVector<sysmon::ServiceInfo>)
