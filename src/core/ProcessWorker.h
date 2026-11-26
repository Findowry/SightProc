#pragma once

#include "core/NvmlBridge.h"
#include "core/ProcessCollector.h"
#include "core/TelemetryTypes.h"

#include <QObject>

#include <chrono>

class QTimer;

namespace sysmon {

/**
 * @brief Ayrı QThread üzerinde çalışan süreç ve uygulama telemetrisi işçisi.
 */
class ProcessWorker : public QObject {
    Q_OBJECT

public:
    explicit ProcessWorker(NvmlBridge& nvml, QObject* parent = nullptr);
    ~ProcessWorker() override;

public slots:
    void start();
    void stop();
    void setIntervalMs(int ms);
    void setPaused(bool paused);
    void setProbeSleepingGpus(bool enable);

signals:
    void snapshotReady(const sysmon::ProcessSnapshot& snapshot);

private slots:
    void onTimerTick();

private:
    void collectAndEmit();

    ProcessCollector m_collector;
    QTimer* m_timer{nullptr};
    int m_intervalMs{1500};
    bool m_paused{false};
    bool m_stopped{false};
    bool m_probeSleepingGpus{false};
    bool m_warmupPending{true};
    std::chrono::steady_clock::time_point m_lastTick{};
    bool m_hasTick{false};
};

} // namespace sysmon
