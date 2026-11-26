#include "core/TelemetryWorker.h"
#include "core/DisplaySessionDetector.h"

#include <QDateTime>
#include <QTimer>

#include <algorithm>

namespace sysmon {

TelemetryWorker::TelemetryWorker(NvmlBridge& nvml, QObject* parent)
    : QObject(parent)
    , m_gpuCollector(nvml) {}

TelemetryWorker::~TelemetryWorker() {
    stop();
}

void TelemetryWorker::setQtPlatformName(const QString& platformName) {
    m_qtPlatformName = platformName;
    m_cachedDisplaySession.clear();
}

void TelemetryWorker::start() {
    if (m_timer) {
        return;
    }
    m_stopped = false;
    m_warmupPending = true;

    m_timer = new QTimer(this);
    m_timer->setTimerType(Qt::PreciseTimer);
    connect(m_timer, &QTimer::timeout, this, &TelemetryWorker::onTimerTick);

    collectAndEmit();

    if (!m_paused && !m_stopped) {
        // İlk delta değerlerinin hemen anlamlı görünmesi için 300 ms ısınma adımı
        m_timer->start(300);
    }
}

void TelemetryWorker::stop() {
    m_stopped = true;
    if (m_timer) {
        m_timer->stop();
    }
}

void TelemetryWorker::setIntervalMs(int ms) {
    m_intervalMs = std::clamp(ms, 250, 5000);
    if (m_timer && m_timer->isActive() && !m_warmupPending) {
        m_timer->setInterval(m_intervalMs);
    }
}

void TelemetryWorker::setPaused(bool paused) {
    m_paused = paused;
    if (!m_timer || m_stopped) {
        return;
    }
    if (paused) {
        m_timer->stop();
    } else {
        m_lastTick = std::chrono::steady_clock::now();
        m_timer->start(m_warmupPending ? 300 : m_intervalMs);
    }
}

void TelemetryWorker::setProbeSleepingGpus(bool enable) {
    m_probeSleepingGpus = enable;
}

void TelemetryWorker::onTimerTick() {
    if (m_stopped || m_paused) {
        return;
    }
    if (m_warmupPending) {
        m_warmupPending = false;
        if (m_timer) {
            m_timer->setInterval(m_intervalMs);
        }
    }
    collectAndEmit();
}

void TelemetryWorker::collectAndEmit() {
    const auto now = std::chrono::steady_clock::now();
    double elapsedSec = 1.0;
    if (m_hasTick) {
        elapsedSec = std::chrono::duration<double>(now - m_lastTick).count();
        elapsedSec = std::max(elapsedSec, 0.001);
    }
    m_lastTick = now;
    m_hasTick = true;

    const HwmonReadings hw = m_hwmonCollector.collect();
    const double pkgTemp = HwmonCollector::findCpuPackageTemp(hw.thermals);

    SystemTelemetrySnapshot snapshot;
    snapshot.timestampMs = QDateTime::currentMSecsSinceEpoch();
    snapshot.thermals = hw.thermals;
    snapshot.fans = hw.fans;
    snapshot.cpu = m_cpuCollector.collect(elapsedSec, pkgTemp, hw.thermals);
    snapshot.memory = m_memoryCollector.collect(hw.thermals);
    snapshot.disks = m_diskCollector.collect(elapsedSec, hw.thermals);
    snapshot.networks = m_networkCollector.collect(elapsedSec);
    snapshot.gpus = m_gpuCollector.collect(elapsedSec, m_probeSleepingGpus);
    snapshot.batteries = m_batteryCollector.collect();
    if (m_cachedDisplaySession.isEmpty()) {
        m_cachedDisplaySession = detectActiveDisplaySession(m_qtPlatformName);
    }
    snapshot.displaySession = m_cachedDisplaySession;

    emit snapshotReady(snapshot);
}

} // namespace sysmon
