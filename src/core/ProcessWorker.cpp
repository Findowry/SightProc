#include "core/ProcessWorker.h"

#include <QTimer>

#include <algorithm>

namespace sysmon {

ProcessWorker::ProcessWorker(QObject* parent)
    : QObject(parent)
    , m_collector(m_nvml) {}

ProcessWorker::~ProcessWorker() {
    stop();
}

void ProcessWorker::start() {
    if (m_timer) {
        return;
    }
    m_stopped = false;
    m_warmupPending = true;

    m_timer = new QTimer(this);
    connect(m_timer, &QTimer::timeout, this, &ProcessWorker::onTimerTick);

    collectAndEmit();

    if (!m_paused && !m_stopped) {
        m_timer->start(400);
    }
}

void ProcessWorker::stop() {
    m_stopped = true;
    if (m_timer) {
        m_timer->stop();
    }
    m_nvml.shutdown();
}

void ProcessWorker::setIntervalMs(int ms) {
    m_intervalMs = std::clamp(ms, 500, 5000);
    if (m_timer && m_timer->isActive() && !m_warmupPending) {
        m_timer->setInterval(m_intervalMs);
    }
}

void ProcessWorker::setPaused(bool paused) {
    m_paused = paused;
    if (!m_timer || m_stopped) {
        return;
    }
    if (paused) {
        m_timer->stop();
    } else {
        m_lastTick = std::chrono::steady_clock::now();
        m_timer->start(m_warmupPending ? 400 : m_intervalMs);
    }
}

void ProcessWorker::setProbeSleepingGpus(bool enable) {
    m_probeSleepingGpus = enable;
}

void ProcessWorker::onTimerTick() {
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

void ProcessWorker::collectAndEmit() {
    const auto now = std::chrono::steady_clock::now();
    double elapsedSec = 1.5;
    if (m_hasTick) {
        elapsedSec = std::chrono::duration<double>(now - m_lastTick).count();
        elapsedSec = std::max(elapsedSec, 0.001);
    }
    m_lastTick = now;
    m_hasTick = true;

    const ProcessSnapshot snap = m_collector.collect(elapsedSec, m_probeSleepingGpus);
    emit snapshotReady(snap);
}

} // namespace sysmon
