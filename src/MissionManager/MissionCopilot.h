#pragma once

#include <QtCore/QList>
#include <QtCore/QObject>
#include <QtCore/QPair>
#include <QtPositioning/QGeoCoordinate>
#include <QtQmlIntegration/QtQmlIntegration>

class MissionController;
class QJsonArray;
class QProcess;

/// Turns a typed instruction into mission items in the Plan view, for one aircraft or several.
class MissionCopilot : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("")
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(int pending READ pending NOTIFY pendingChanged)

public:
    explicit MissionCopilot(MissionController* missionController);

    bool busy() const { return _claude != nullptr; }

    /// Plans built for other aircraft, waiting to be sent.
    int pending() const { return _pending.count(); }

    /// Replaces the plan with the mission the model builds for this instruction.
    Q_INVOKABLE void ask(const QString& instruction);

    /// Uploads the plans built for the other aircraft.
    Q_INVOKABLE void send();

signals:
    void busyChanged();
    void pendingChanged();

    /// Progress and failures, shown in the copilot panel.
    void said(const QString& text);

private:
    void _handleFinished();
    void _build(const QJsonArray& missions);
    void _buildOne(const QJsonArray& steps, const QGeoCoordinate& home);
    QGeoCoordinate _home() const;

    MissionController* _missionController = nullptr;
    QProcess* _claude = nullptr;
    QList<QPair<quint32, QString>> _pending;
};
