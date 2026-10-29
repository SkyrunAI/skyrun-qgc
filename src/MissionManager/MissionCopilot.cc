#include "MissionCopilot.h"

#include <cmath>

#include <QtCore/QDir>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QProcess>
#include <QtCore/QStandardPaths>
#include <QtCore/QTemporaryDir>
#include <QtCore/QtMath>

#include "Fact.h"
#include "MissionController.h"
#include "MultiVehicleManager.h"
#include "PlanMasterController.h"
#include "QGCApplication.h"
#include "QmlObjectListModel.h"
#include "SimpleMissionItem.h"
#include "Vehicle.h"

static const char* kSystemPrompt =
    "You plan drone missions for QGroundControl. Reply with nothing but a JSON array of missions, "
    "one per aircraft. Each mission is an object with \"vehicle\", the aircraft's number, or null "
    "when the operator named no aircraft; and \"steps\", the mission in the order it is flown. "
    "Each step is an object with: \"type\", one of takeoff, waypoint, loiter or land; \"north_m\", "
    "metres north of home; \"east_m\", metres east of home; \"altitude_m\", metres above home. A "
    "loiter step also takes \"radius_m\", how far from the point to circle, and \"seconds\", how "
    "long to stay. Orbiting or circling a point is a loiter. Start each mission with takeoff and "
    "finish with land. Land brings the aircraft down where QGroundControl decides is right for the "
    "airframe, so a mission needs no waypoint to get home first. No prose, no code fences.";

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

/// The connected aircraft, in the order QGroundControl lists them.
static QStringList vehicleIds()
{
    QStringList ids;
    QmlObjectListModel* vehicles = MultiVehicleManager::instance()->vehicles();
    for (int i = 0; i < vehicles->count(); i++) {
        Vehicle* vehicle = vehicles->value<Vehicle*>(i);
        if (vehicle) {
            ids.append(QString::number(vehicle->id()));
        }
    }
    return ids;
}

static Vehicle* vehicleById(quint32 id)
{
    QmlObjectListModel* vehicles = MultiVehicleManager::instance()->vehicles();
    for (int i = 0; i < vehicles->count(); i++) {
        Vehicle* vehicle = vehicles->value<Vehicle*>(i);
        if (vehicle && vehicle->id() == id) {
            return vehicle;
        }
    }
    return nullptr;
}

/// Why this aircraft must not be given a mission right now, empty if it may have one.
/// The operator can see the state of the aircraft on screen; these are the others.
static QString refusal(Vehicle* vehicle, Vehicle* controller)
{
    if (vehicle->armed() && (vehicle->flightMode() == vehicle->missionFlightMode())) {
        return MissionCopilot::tr("already flying a mission");
    }
    if ((controller->firmwareType() != vehicle->firmwareType())
        || (QGCMAVLink::vehicleClass(controller->vehicleType())
            != QGCMAVLink::vehicleClass(vehicle->vehicleType()))) {
        return MissionCopilot::tr("not the kind of aircraft this plan was built for");
    }
    if (vehicle->readyToFlyAvailable() && !vehicle->readyToFly()) {
        return MissionCopilot::tr("not ready to fly");
    }
    if (!vehicle->allSensorsHealthy()) {
        return MissionCopilot::tr("reporting an unhealthy sensor");
    }
    return QString();
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

    // The operator says "drone 1"; the aircraft are numbered by MAVLink system id, which is
    // rarely 1. Give the model the real numbers so it can map between them.
    QString prompt = QString::fromUtf8(kSystemPrompt);
    const QStringList ids = vehicleIds();
    if (!ids.isEmpty()) {
        prompt += tr(" The aircraft connected now, in order, are %1. Use those numbers for "
                     "\"vehicle\"; the operator's \"drone 1\" means the first of them.")
                      .arg(ids.join(QStringLiteral(", ")));
    }

    _claude = new QProcess(this);
    // A neutral directory keeps some other project's CLAUDE.md out of the answer
    _claude->setWorkingDirectory(QDir::tempPath());
    connect(_claude, &QProcess::finished, this, &MissionCopilot::_handleFinished);
    _claude->start(claude, {
        QStringLiteral("-p"), instruction,
        QStringLiteral("--output-format"), QStringLiteral("text"),
        QStringLiteral("--model"), QStringLiteral("opus"),
        QStringLiteral("--system-prompt"), prompt,
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

void MissionCopilot::_build(const QJsonArray& missions)
{
    if (missions.isEmpty()) {
        emit said(tr("No mission in the reply."));
        return;
    }

    _pending.clear();
    emit pendingChanged();

    // The Plan view holds one aircraft's mission at a time, so each other aircraft's mission is
    // built here in turn and saved out. The last one built is the one left on screen to review.
    const QGeoCoordinate fallback = _home();
    PlanMasterController* master = _missionController->masterController();
    Vehicle* shown = master->managerVehicle();
    QJsonArray shownSteps;
    QGeoCoordinate shownHome = fallback;
    QStringList planned;

    for (const QJsonValue& value : missions) {
        const QJsonObject mission = value.toObject();
        const QJsonValue id = mission.value("vehicle");
        const QJsonArray steps = mission.value("steps").toArray();
        Vehicle* vehicle = id.isDouble() ? vehicleById(id.toInt()) : nullptr;

        // Each aircraft plans around where it is standing, not where another one is
        const QGeoCoordinate from = (vehicle && vehicle->homePosition().isValid())
                                        ? vehicle->homePosition()
                                        : fallback;
        if (!from.isValid()) {
            emit said(tr("No home position. Connect a vehicle, or start QGroundControl with --home <lat>,<lon>."));
            return;
        }

        if (!vehicle || vehicle == shown) {
            shownSteps = steps;
            shownHome = from;
            continue;
        }
        _buildOne(steps, from);
        const QString file = QDir::tempPath() + QStringLiteral("/qgc-copilot-%1.plan").arg(vehicle->id());
        if (master->saveToFile(file)) {
            _pending.append({ vehicle->id(), file });
            planned.append(QString::number(vehicle->id()));
        }
    }

    _buildOne(shownSteps, shownHome);
    _missionController->setCurrentPlanViewSeqNum(0, true);

    // The aircraft on screen is sent with the rest, so one press covers the fleet
    if (shown && !shown->isOfflineEditingVehicle()) {
        const QString file = QDir::tempPath() + QStringLiteral("/qgc-copilot-%1.plan").arg(shown->id());
        if (master->saveToFile(file)) {
            _pending.append({ shown->id(), file });
            planned.append(QString::number(shown->id()));
        }
    }
    emit pendingChanged();

    if (_pending.isEmpty()) {
        emit said(tr("Built %1 mission items.").arg(shownSteps.count()));
    } else {
        emit said(tr("Planned for %1: %2. This one is on screen; Send uploads them all.")
                      .arg(_pending.count()).arg(planned.join(QStringLiteral(", "))));
    }
}

void MissionCopilot::_buildOne(const QJsonArray& steps, const QGeoCoordinate& home)
{
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
        SimpleMissionItem* simple = qobject_cast<SimpleMissionItem*>(item);
        if (simple) {
            if (type == QLatin1String("loiter")) {
                simple->setCommand(MAV_CMD_NAV_LOITER_TIME);
                simple->setRadius(step.value("radius_m").toDouble());
                simple->missionItem().setParam1(step.value("seconds").toDouble());
            }
            simple->altitude()->setRawValue(step.value("altitude_m").toDouble());
        }
        index++;
    }
}

void MissionCopilot::send()
{
    Vehicle* controller = _missionController->masterController()->controllerVehicle();
    QStringList sent;
    QStringList held;
    QList<QPair<quint32, QString>> keep;

    for (const QPair<quint32, QString>& plan : _pending) {
        Vehicle* vehicle = vehicleById(plan.first);
        if (!vehicle) {
            held.append(tr("%1 has gone").arg(plan.first));
            continue;
        }
        // A refused aircraft keeps its plan, so fixing the problem and pressing Send is enough
        const QString why = refusal(vehicle, controller);
        if (!why.isEmpty()) {
            held.append(tr("%1 is %2").arg(plan.first).arg(why));
            keep.append(plan);
            continue;
        }
        PlanMasterController::sendPlanToVehicle(vehicle, plan.second);
        sent.append(QString::number(plan.first));
    }

    _pending = keep;
    emit pendingChanged();

    if (held.isEmpty()) {
        emit said(tr("Sent to %1.").arg(sent.join(QStringLiteral(", "))));
    } else if (sent.isEmpty()) {
        emit said(tr("Sent to nothing. %1.").arg(held.join(QStringLiteral("; "))));
    } else {
        emit said(tr("Sent to %1. Held back: %2.")
                      .arg(sent.join(QStringLiteral(", "))).arg(held.join(QStringLiteral("; "))));
    }
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
