#include "core/SystemdController.h"

#include "core/Lang.h"

#include <QHash>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRegularExpression>

#include <algorithm>

namespace sysmon {

namespace {

QProcessEnvironment cLocaleEnv() {
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("LC_ALL"), QStringLiteral("C"));
    env.insert(QStringLiteral("LANG"), QStringLiteral("C"));
    return env;
}

QVector<ServiceInfo> parseListUnitsOutput(const QString& output) {
    QVector<ServiceInfo> list;
    const QStringList lines = output.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    static const QRegularExpression wsRe(QStringLiteral("\\s+"));

    for (QString line : lines) {
        line = line.trimmed();
        if (line.isEmpty()) {
            continue;
        }
        if (!line[0].isLetterOrNumber() && !line[0].isPunct()) {
            line = line.mid(1).trimmed();
        }

        const QStringList parts = line.split(wsRe, Qt::SkipEmptyParts);
        if (parts.size() < 4) {
            continue;
        }
        if (!parts[0].endsWith(QStringLiteral(".service"))) {
            continue;
        }

        ServiceInfo info;
        info.unitName = parts[0];
        info.loadState = parts[1];
        info.activeState = parts[2];
        info.subState = parts[3];
        if (parts.size() > 4) {
            info.description = parts.mid(4).join(QLatin1Char(' '));
        }
        list.push_back(std::move(info));
    }
    return list;
}

} // namespace

SystemdController::SystemdController(QObject* parent)
    : QObject(parent) {}

SystemdController::~SystemdController() {
    stopAll();
}

QVector<ServiceInfo> SystemdController::parseUnitFilesOutput(const QString& output) {
    QVector<ServiceInfo> list;
    const QStringList lines = output.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    static const QRegularExpression wsRe(QStringLiteral("\\s+"));

    for (QString line : lines) {
        line = line.trimmed();
        if (line.isEmpty()) {
            continue;
        }
        const QStringList parts = line.split(wsRe, Qt::SkipEmptyParts);
        if (parts.size() < 2 || !parts[0].endsWith(QStringLiteral(".service"))) {
            continue;
        }

        ServiceInfo info;
        info.unitName = parts[0];
        info.loadState = QStringLiteral("unit-file");
        info.activeState = QStringLiteral("inactive");
        info.subState = QStringLiteral("dead");
        info.unitFileState = parts[1];
        list.push_back(std::move(info));
    }
    return list;
}

void SystemdController::refreshUnits(ServiceScope scope) {
    if (m_unitsProc) {
        m_unitsProc->disconnect();
        m_unitsProc->kill();
        m_unitsProc->deleteLater();
        m_unitsProc = nullptr;
    }
    if (m_unitFilesProc) {
        m_unitFilesProc->disconnect();
        m_unitFilesProc->kill();
        m_unitFilesProc->deleteLater();
        m_unitFilesProc = nullptr;
    }

    const std::uint64_t gen = ++m_refreshGen;
    m_pending = PendingRefresh{};
    m_pending.generation = gen;
    m_pending.scope = scope;

    QStringList scopeArgs;
    if (scope == ServiceScope::User) {
        scopeArgs << QStringLiteral("--user");
    }

    const QProcessEnvironment env = cLocaleEnv();

    m_unitsProc = new QProcess(this);
    m_unitsProc->setProcessEnvironment(env);
    connect(m_unitsProc, &QProcess::finished,
            this, [this, gen, proc = m_unitsProc](int exitCode, QProcess::ExitStatus) {
                if (gen == m_refreshGen && proc) {
                    m_pending.unitsDone = true;
                    m_pending.unitsExitCode = exitCode;
                    m_pending.unitsStdout = QString::fromUtf8(proc->readAllStandardOutput());
                    m_pending.unitsStderr = QString::fromUtf8(proc->readAllStandardError()).trimmed();
                    finalizeRefresh(gen);
                }
                if (proc) {
                    proc->deleteLater();
                }
            });
    connect(m_unitsProc, &QProcess::errorOccurred,
            this, [this, gen, proc = m_unitsProc](QProcess::ProcessError) {
                if (gen == m_refreshGen && proc) {
                    m_pending.unitsDone = true;
                    m_pending.unitsExitCode = -1;
                    m_pending.unitsStderr = proc->errorString();
                    finalizeRefresh(gen);
                }
                if (proc) {
                    proc->deleteLater();
                }
            });

    QStringList unitsArgs = scopeArgs;
    unitsArgs << QStringLiteral("list-units")
              << QStringLiteral("--type=service")
              << QStringLiteral("--all")
              << QStringLiteral("--no-pager")
              << QStringLiteral("--no-legend")
              << QStringLiteral("--plain");
    m_unitsProc->start(QStringLiteral("systemctl"), unitsArgs);

    m_unitFilesProc = new QProcess(this);
    m_unitFilesProc->setProcessEnvironment(env);
    connect(m_unitFilesProc, &QProcess::finished,
            this, [this, gen, proc = m_unitFilesProc](int exitCode, QProcess::ExitStatus) {
                if (gen == m_refreshGen && proc) {
                    m_pending.unitFilesDone = true;
                    m_pending.unitFilesExitCode = exitCode;
                    m_pending.unitFilesStdout = QString::fromUtf8(proc->readAllStandardOutput());
                    m_pending.unitFilesStderr = QString::fromUtf8(proc->readAllStandardError()).trimmed();
                    finalizeRefresh(gen);
                }
                if (proc) {
                    proc->deleteLater();
                }
            });
    connect(m_unitFilesProc, &QProcess::errorOccurred,
            this, [this, gen, proc = m_unitFilesProc](QProcess::ProcessError) {
                if (gen == m_refreshGen && proc) {
                    m_pending.unitFilesDone = true;
                    m_pending.unitFilesExitCode = -1;
                    m_pending.unitFilesStderr = proc->errorString();
                    finalizeRefresh(gen);
                }
                if (proc) {
                    proc->deleteLater();
                }
            });

    QStringList filesArgs = scopeArgs;
    filesArgs << QStringLiteral("list-unit-files")
              << QStringLiteral("--type=service")
              << QStringLiteral("--no-pager")
              << QStringLiteral("--no-legend");
    m_unitFilesProc->start(QStringLiteral("systemctl"), filesArgs);
}

void SystemdController::finalizeRefresh(std::uint64_t gen) {
    if (gen != m_refreshGen || !m_pending.unitsDone || !m_pending.unitFilesDone) {
        return;
    }

    QVector<ServiceInfo> units = parseListUnitsOutput(m_pending.unitsStdout);
    const QVector<ServiceInfo> unitFiles = parseUnitFilesOutput(m_pending.unitFilesStdout);

    QHash<QString, QString> fileStateMap;
    fileStateMap.reserve(unitFiles.size());
    for (const auto& uf : unitFiles) {
        fileStateMap.insert(uf.unitName, uf.unitFileState);
    }

    QHash<QString, int> loadedIndex;
    loadedIndex.reserve(units.size());
    for (int i = 0; i < units.size(); ++i) {
        loadedIndex.insert(units[i].unitName, i);
        if (const auto it = fileStateMap.constFind(units[i].unitName);
            it != fileStateMap.constEnd()) {
            units[i].unitFileState = *it;
        } else {
            units[i].unitFileState = QStringLiteral("transient");
        }
    }

    // Yüklü olmayan şablon dışı birim dosyalarını da listeye ekle
    for (const auto& uf : unitFiles) {
        if (!loadedIndex.contains(uf.unitName) && !uf.unitName.endsWith(QStringLiteral("@.service"))) {
            units.push_back(uf);
        }
    }

    QString warning;
    if (m_pending.unitsExitCode != 0) {
        warning = m_pending.unitsStderr;
        if (warning.isEmpty()) {
            warning = trText("Could not connect to systemd manager; showing installed unit files.",
                             "systemd yöneticisine bağlanılamadı; kurulu birim dosyaları gösteriliyor.");
        }
    }

    emit unitsLoaded(units, warning);
}

void SystemdController::performAction(ServiceScope scope,
                                      const QString& unitName,
                                      const QString& action) {
    if (unitName.isEmpty() || action.isEmpty()) {
        return;
    }

    auto* proc = new QProcess(this);
    proc->setProcessEnvironment(cLocaleEnv());
    m_actionProcs.push_back(proc);

    connect(proc, &QProcess::finished,
            this, [this, proc, scope, unitName, action](int exitCode, QProcess::ExitStatus status) {
                const QString err = QString::fromUtf8(proc->readAllStandardError()).trimmed();
                const QString out = QString::fromUtf8(proc->readAllStandardOutput()).trimmed();
                const bool ok = (status == QProcess::NormalExit && exitCode == 0);

                if (ok) {
                    emit actionFinished(
                        true,
                        trText("%1 succeeded for %2.", "%2 için %1 komutu tamamlandı.")
                            .arg(action, unitName));
                    refreshUnits(scope);
                } else if (exitCode == 126 || exitCode == 127) {
                    emit actionFinished(
                        false,
                        trText("Authentication canceled or pkexec unavailable.",
                               "Kimlik doğrulama iptal edildi veya pkexec kullanılamıyor."));
                } else {
                    const QString detail = !err.isEmpty() ? err : out;
                    emit actionFinished(
                        false,
                        detail.isEmpty()
                            ? trText("Action failed (exit %1).", "İşlem başarısız oldu (kod %1).")
                                  .arg(exitCode)
                            : detail);
                }
                m_actionProcs.removeAll(proc);
                proc->deleteLater();
            });

    if (scope == ServiceScope::User) {
        proc->start(QStringLiteral("systemctl"),
                    {QStringLiteral("--user"), action, unitName});
    } else {
        proc->start(QStringLiteral("pkexec"),
                    {QStringLiteral("systemctl"), action, unitName});
    }
}

void SystemdController::fetchLogs(ServiceScope scope,
                                  const QString& unitName,
                                  int lineCount,
                                  bool follow) {
    stopLogs();

    if (unitName.isEmpty()) {
        emit logsReplaced(QString());
        return;
    }

    emit logsReplaced(QString());

    m_logProc = new QProcess(this);
    QStringList args;
    if (scope == ServiceScope::User) {
        args << QStringLiteral("--user")
             << QStringLiteral("--user-unit=%1").arg(unitName);
    } else {
        args << QStringLiteral("-u") << unitName;
    }
    args << QStringLiteral("-n") << QString::number(std::clamp(lineCount, 20, 1000))
         << QStringLiteral("--no-pager")
         << QStringLiteral("--output=short-iso");
    if (follow) {
        args << QStringLiteral("-f");
    }

    connect(m_logProc, &QProcess::readyReadStandardOutput,
            this, [this, proc = m_logProc]() {
                if (!proc) {
                    return;
                }
                const QString chunk = QString::fromUtf8(proc->readAllStandardOutput());
                if (!chunk.isEmpty()) {
                    emit logsAppended(chunk);
                }
            });

    connect(m_logProc, &QProcess::finished,
            this, [this, proc = m_logProc, follow](int exitCode, QProcess::ExitStatus) {
                if (!proc) {
                    return;
                }
                const QString remaining = QString::fromUtf8(proc->readAllStandardOutput());
                if (!remaining.isEmpty()) {
                    emit logsAppended(remaining);
                }
                const QString err = QString::fromUtf8(proc->readAllStandardError()).trimmed();
                if (!err.isEmpty() && exitCode != 0 && !follow) {
                    emit logsAppended(err + QLatin1Char('\n'));
                }
                proc->deleteLater();
                if (m_logProc == proc) {
                    m_logProc = nullptr;
                }
            });

    m_logProc->start(QStringLiteral("journalctl"), args);
}

void SystemdController::stopLogs() {
    if (m_logProc) {
        m_logProc->disconnect();
        m_logProc->terminate();
        if (!m_logProc->waitForFinished(200)) {
            m_logProc->kill();
            m_logProc->waitForFinished(200);
        }
        m_logProc->deleteLater();
        m_logProc = nullptr;
    }
}

void SystemdController::stopAll() {
    if (m_unitsProc) {
        m_unitsProc->disconnect();
        m_unitsProc->kill();
        m_unitsProc->waitForFinished(200);
        m_unitsProc->deleteLater();
        m_unitsProc = nullptr;
    }
    if (m_unitFilesProc) {
        m_unitFilesProc->disconnect();
        m_unitFilesProc->kill();
        m_unitFilesProc->waitForFinished(200);
        m_unitFilesProc->deleteLater();
        m_unitFilesProc = nullptr;
    }
    stopLogs();
    for (const QPointer<QProcess>& p : m_actionProcs) {
        if (p) {
            p->disconnect();
            p->terminate();
            if (!p->waitForFinished(200)) {
                p->kill();
            }
            p->deleteLater();
        }
    }
    m_actionProcs.clear();
}

} // namespace sysmon
