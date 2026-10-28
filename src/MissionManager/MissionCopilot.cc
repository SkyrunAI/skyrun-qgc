#include "MissionCopilot.h"

#include <cmath>

#include <QtCore/QDir>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QProcess>
#include <QtCore/QStandardPaths>
#include <QtCore/QtMath>

#include "Fact.h"
#include "MissionController.h"
#include "MultiVehicleManager.h"
#include "QGCApplication.h"
#include "SimpleMissionItem.h"
#include "Vehicle.h"

static const char* kSystemPrompt =
    "You plan drone missions for QGroundControl. Reply with nothing but a JSON array of mission "
    "steps, in the order flown. Each step is an object with: \"type\", one of takeoff, waypoint or "
    "land; \"north_m\", metres north of home; \"east_m\", metres east of home; \"altitude_m\", "
    "metres above home. Start with takeoff and finish with land. Land brings the aircraft down "
    "where QGroundControl decides is right for the airframe, so the mission needs no waypoint to "
    "get home first. No prose, no code fences.";

/// QGroundControl launched from Finder does not inherit the shell's PATH.
static QString claudePath()
{
    const QString onPath = QStandardPaths::findExecutable(QStringLiteral("claude"));
    if (!onPath.isEmpty()) {
        return onPath;
    }
    return QStandardPaths::findExecutable(QStringLiteral("claude"),
                                          { QDir::homePath() + QStringLiteral("/.local/bin") });
}

MissionCopilot::MissionCopilot(MissionController* missionController)
    : QObject(missionController)
    , _missionController(missionController)
{
}

void MissionCopilot::ask(const QString& instruction)
{
    const QString claude = claudePath();
    if (claude.isEmpty()) {
        emit said(tr("No claude command found. Install Claude Code and sign in."));
        return;
    }

    _claude = new QProcess(this);
    // A neutral directory keeps some other project's CLAUDE.md out of the answer
    _claude->setWorkingDirectory(QDir::tempPath());
    connect(_claude, &QProcess::finished, this, &MissionCopilot::_handleFinished);
    _claude->start(claude, {
        QStringLiteral("-p"), instruction,
        QStringLiteral("--output-format"), QStringLiteral("text"),
        QStringLiteral("--model"), QStringLiteral("opus"),
        QStringLiteral("--system-prompt"), QString::fromUtf8(kSystemPrompt),
    });
    _claude->closeWriteChannel();
    emit busyChanged();
}

void MissionCopilot::_handleFinished()
{
    QProcess* claude = _claude;
    _claude = nullptr;
    claude->deleteLater();
    emit busyChanged();

    if (claude->exitStatus() != QProcess::NormalExit || claude->exitCode() != 0) {
        const QString failure = QString::fromUtf8(claude->readAllStandardError()).trimmed();
        emit said(failure.isEmpty() ? tr("claude exited with %1.").arg(claude->exitCode()) : failure);
        return;
    }

    const QJsonDocument reply = QJsonDocument::fromJson(claude->readAllStandardOutput().trimmed());
    if (!reply.isArray()) {
        emit said(tr("No mission in the reply."));
        return;
    }
    _build(reply.array());
}

void MissionCopilot::_build(const QJsonArray& steps)
{
    const QGeoCoordinate home = _home();
    if (!home.isValid()) {
        emit said(tr("No home position. Connect a vehicle, or start QGroundControl with --home <lat>,<lon>."));
        return;
    }
    if (steps.isEmpty()) {
        emit said(tr("No mission in the reply."));
        return;
    }

    _missionController->removeAll();
    _missionController->setHomePosition(home);

    int index = 1;
    for (const QJsonValue& value : steps) {
        const QJsonObject step = value.toObject();
        const double north = step.value("north_m").toDouble();
        const double east = step.value("east_m").toDouble();
        const QGeoCoordinate position = home.atDistanceAndAzimuth(
            std::hypot(north, east), qRadiansToDegrees(std::atan2(east, north)));

        const QString type = step.value("type").toString();
        VisualMissionItem* item = nullptr;
        if (type == QLatin1String("takeoff")) {
            item = _missionController->insertTakeoffItem(position, index);
        } else if (type == QLatin1String("land")) {
            item = _missionController->insertLandItem(position, index);
        } else {
            item = _missionController->insertSimpleMissionItem(position, index);
        }

        // Fixed-wing and VTOL landings come back as complex items, which carry their own altitude
        if (SimpleMissionItem* simple = qobject_cast<SimpleMissionItem*>(item)) {
            simple->altitude()->setRawValue(step.value("altitude_m").toDouble());
        }
        index++;
    }

    _missionController->setCurrentPlanViewSeqNum(0, true);
    emit said(tr("Built %1 mission items.").arg(steps.count()));
}

QGeoCoordinate MissionCopilot::_home() const
{
    const QGeoCoordinate override = qgcApp()->homeOverride();
    if (override.isValid()) {
        return override;
    }
    Vehicle* vehicle = MultiVehicleManager::instance()->activeVehicle();
    if (vehicle && vehicle->homePosition().isValid()) {
        return vehicle->homePosition();
    }
    return _missionController->plannedHomePosition();
}
