#pragma once

#include <QtCore/QObject>
#include <QtPositioning/QGeoCoordinate>
#include <QtQmlIntegration/QtQmlIntegration>

class MissionController;
class QJsonArray;
class QProcess;

/// Turns a typed instruction into takeoff, waypoint and land items in the Plan view.
class MissionCopilot : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("")
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)

public:
    explicit MissionCopilot(MissionController* missionController);

    bool busy() const { return _claude != nullptr; }

    /// Replaces the plan with the mission the model builds for this instruction.
    Q_INVOKABLE void ask(const QString& instruction);

signals:
    void busyChanged();

    /// Progress and failures, shown in the copilot panel.
    void said(const QString& text);

private:
    void _handleFinished();
    void _build(const QJsonArray& steps);
    QGeoCoordinate _home() const;

    MissionController* _missionController = nullptr;
    QProcess* _claude = nullptr;
};
