#pragma once

#include "core/BatteryCollector.h"
#include "core/CpuCollector.h"
#include "core/DiskCollector.h"
#include "core/GpuCollector.h"
#include "core/HwmonCollector.h"
#include "core/MemoryCollector.h"
#include "core/NetworkCollector.h"
#include "core/NvmlBridge.h"
#include "core/TelemetryTypes.h"

#include <QObject>

#include <chrono>

class QTimer;

namespace sysmon {

/**
 * @brief Arka plan QThread üzerinde çalışan donanım telemetrisi toplayıcısı.
 */
class TelemetryWorker : public QObject {
    Q_OBJECT

public:
    explicit TelemetryWorker(NvmlBridge& nvml, QObject* parent = nullptr);
    ~TelemetryWorker() override;

    void setQtPlatformName(const QString& platformName);

public slots:
    void start();
    void stop();
    void setIntervalMs(int ms);
    void setPaused(bool paused);
    void setProbeSleepingGpus(bool enable);

signals:
    void snapshotReady(const sysmon::SystemTelemetrySnapshot& snapshot);

private slots:
    void onTimerTick();

private:
    void collectAndEmit();

    HwmonCollector m_hwmonCollector;
    CpuCollector m_cpuCollector;
    MemoryCollector m_memoryCollector;
    DiskCollector m_diskCollector;
    NetworkCollector m_networkCollector;
    GpuCollector m_gpuCollector;
    BatteryCollector m_batteryCollector;
    QString m_qtPlatformName;
    QString m_cachedDisplaySession;

    QTimer* m_timer{nullptr};
    int m_intervalMs{1000};
    bool m_paused{false};
    bool m_stopped{false};
    bool m_probeSleepingGpus{false};
    bool m_warmupPending{true};
    std::chrono::steady_clock::time_point m_lastTick{};
    bool m_hasTick{false};
};

} // namespace sysmon
