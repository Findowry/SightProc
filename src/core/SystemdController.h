#pragma once

#include "core/TelemetryTypes.h"

#include <QObject>
#include <QPointer>
#include <QVector>

#include <cstdint>

class QProcess;

namespace sysmon {

enum class ServiceScope {
    System = 0,
    User = 1
};

/**
 * @brief Asenkron QProcess üzerinden systemd servislerini (sistem & kullanıcı)
 *        listeleyen, kontrol eden (pkexec / --user) ve journalctl günlüklerini okuyan denetleyici.
 */
class SystemdController : public QObject {
    Q_OBJECT

public:
    explicit SystemdController(QObject* parent = nullptr);
    ~SystemdController() override;

    void refreshUnits(ServiceScope scope);
    void performAction(ServiceScope scope, const QString& unitName, const QString& action);
    void fetchLogs(ServiceScope scope,
                   const QString& unitName,
                   int lineCount = 150,
                   bool follow = false);
    void stopLogs();
    void stopAll();

    [[nodiscard]] static QVector<ServiceInfo> parseUnitFilesOutput(const QString& output);

signals:
    void unitsLoaded(const QVector<sysmon::ServiceInfo>& units, const QString& warningMessage);
    void actionFinished(bool success, const QString& message);
    void logsReplaced(const QString& initialText);
    void logsAppended(const QString& chunk);

private:
    void finalizeRefresh(std::uint64_t gen);

    struct PendingRefresh {
        std::uint64_t generation{0};
        ServiceScope scope{ServiceScope::System};
        bool unitsDone{false};
        bool unitFilesDone{false};
        int unitsExitCode{0};
        int unitFilesExitCode{0};
        QString unitsStdout;
        QString unitsStderr;
        QString unitFilesStdout;
        QString unitFilesStderr;
    };

    std::uint64_t m_refreshGen{0};
    PendingRefresh m_pending;
    QPointer<QProcess> m_unitsProc;
    QPointer<QProcess> m_unitFilesProc;
    QPointer<QProcess> m_logProc;
    QVector<QPointer<QProcess>> m_actionProcs;
};

} // namespace sysmon
