#include "MissionCopilot.h"

#include <cmath>

#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QtMath>
#include <QtNetwork/QNetworkAccessManager>
#include <QtNetwork/QNetworkReply>
#include <QtNetwork/QNetworkRequest>

#include "Fact.h"
#include "MissionController.h"
#include "MissionCopilotSettings.h"
#include "QGCApplication.h"
#include "SettingsManager.h"
#include "SimpleMissionItem.h"

static const char* kToolName = "build_mission";

static const char* kSystemPrompt =
    "You plan drone missions for QGroundControl. Call the build_mission tool with the mission the "
    "operator asked for, in order. A mission is built from three steps only: takeoff, waypoint and "
    "land. Positions are metres from home, north_m positive northwards and east_m positive "
    "eastwards; altitude_m is metres above home. Start with takeoff and finish with land. Land "
    "brings the aircraft down where QGroundControl decides is right for the airframe, so the "
    "mission needs no waypoint to get home first.";

static QJsonObject buildMissionTool()
{
    const QJsonObject step {
        { "type", "object" },
        { "additionalProperties", false },
        { "required", QJsonArray { "type", "north_m", "east_m", "altitude_m" } },
        { "properties", QJsonObject {
            { "type", QJsonObject { { "type", "string" }, { "enum", QJsonArray { "takeoff", "waypoint", "land" } } } },
            { "north_m", QJsonObject { { "type", "number" }, { "description", "Metres north of home." } } },
            { "east_m", QJsonObject { { "type", "number" }, { "description", "Metres east of home." } } },
            { "altitude_m", QJsonObject { { "type", "number" }, { "description", "Metres above home." } } },
        } },
    };

    return QJsonObject {
        { "name", kToolName },
        { "description", "Build the mission the operator described." },
        { "strict", true },
        { "input_schema", QJsonObject {
            { "type", "object" },
            { "additionalProperties", false },
            { "required", QJsonArray { "steps" } },
            { "properties", QJsonObject { { "steps", QJsonObject {
                { "type", "array" },
                { "description", "The mission, in the order it is flown." },
                { "items", step },
            } } } },
        } },
    };
}

MissionCopilot::MissionCopilot(MissionController* missionController)
    : QObject(missionController)
    , _missionController(missionController)
    , _network(new QNetworkAccessManager(this))
{
}

void MissionCopilot::ask(const QString& instruction)
{
    const QString key = SettingsManager::instance()->missionCopilotSettings()->apiKey()->rawValue().toString();
    if (key.isEmpty()) {
        emit said(tr("Set the Anthropic API key in Application Settings first."));
        return;
    }

    const QJsonObject body {
        { "model", "claude-opus-5" },
        { "max_tokens", 16000 },
        { "system", kSystemPrompt },
        { "tools", QJsonArray { buildMissionTool() } },
        { "tool_choice", QJsonObject { { "type", "tool" }, { "name", kToolName } } },
        { "messages", QJsonArray { QJsonObject { { "role", "user" }, { "content", instruction } } } },
    };

    QNetworkRequest request(QUrl(QStringLiteral("https://api.anthropic.com/v1/messages")));
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setRawHeader("x-api-key", key.toUtf8());
    request.setRawHeader("anthropic-version", "2023-06-01");

    _reply = _network->post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(_reply, &QNetworkReply::finished, this, &MissionCopilot::_handleReply);
    emit busyChanged();
}

void MissionCopilot::_handleReply()
{
    QNetworkReply* reply = _reply;
    _reply = nullptr;
    reply->deleteLater();
    emit busyChanged();

    const QJsonObject response = QJsonDocument::fromJson(reply->readAll()).object();
    if (response.contains("error")) {
        emit said(response.value("error").toObject().value("message").toString());
        return;
    }
    if (response.value("stop_reason").toString() == QLatin1String("refusal")) {
        emit said(tr("The model declined to plan this mission."));
        return;
    }

    const QJsonArray content = response.value("content").toArray();
    for (const QJsonValue& block : content) {
        const QJsonObject object = block.toObject();
        if (object.value("type").toString() == QLatin1String("tool_use")) {
            _build(object.value("input").toObject().value("steps").toArray());
            return;
        }
    }
    emit said(tr("The model sent back no mission."));
}

void MissionCopilot::_build(const QJsonArray& steps)
{
    const QGeoCoordinate home = _home();
    if (!home.isValid()) {
        emit said(tr("No home position. Connect a vehicle, or start QGroundControl with --home <lat>,<lon>."));
        return;
    }
    if (steps.isEmpty()) {
        emit said(tr("The model sent back no mission."));
        return;
    }

    _missionController->removeAll();

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
    return override.isValid() ? override : _missionController->plannedHomePosition();
}
