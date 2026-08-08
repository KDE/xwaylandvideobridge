/*
 * SPDX-License-Identifier: LicenseRef-KDE-Accepted-GPL
 * SPDX-FileCopyrightText: 2023 Aleix Pol <aleixpol@kde.org>
 * SPDX-FileCopyrightText: 2026 Hadi Chokr <hadichokr@icloud.com>
 */

#pragma once

#include <QDBusObjectPath>
#include <QObject>
#include <QScopedPointer>
#include <QVariant>

class QTimer;
class ContentsWindow;
class KStatusNotifierItem;
class PipeWireSourceItem;
class X11RecordingNotifier;
class OrgFreedesktopPortalScreenCastInterface;

struct Stream {
    uint nodeId = 0;
    QVariantMap opts;
};

class XwaylandVideoBridge : public QObject
{
    Q_OBJECT
public:
    explicit XwaylandVideoBridge(QObject *parent = nullptr);
    ~XwaylandVideoBridge() override;

    enum CursorMode { Hidden = 1, Embedded = 2, Metadata = 4 };
    Q_ENUM(CursorMode)
    Q_DECLARE_FLAGS(CursorModes, CursorMode)

    enum SourceTypes { Monitor = 1, Window = 2, Virtual = 4 };
    Q_ENUM(SourceTypes)

public Q_SLOTS:
    void response(uint code, const QVariantMap &results);

private Q_SLOTS:
    void closeSession();

private:
    void init();
    void resetSession();
    void selectSources(const QDBusObjectPath &sessionPath);
    void start();
    void handleStreams(const QVector<Stream> &streams);
    void clearStream();
    void fitItemToWindow();

    OrgFreedesktopPortalScreenCastInterface *m_portal;
    QDBusObjectPath m_sessionPath;
    QDBusObjectPath m_requestPath;
    QString m_handleToken;

    QTimer *m_quitTimer;
    QScopedPointer<ContentsWindow> m_window;
    X11RecordingNotifier *m_recordingNotifier = nullptr;
    PipeWireSourceItem *m_pipeWireItem = nullptr;
    KStatusNotifierItem *m_trayIcon = nullptr;
    bool m_sessionActive = false;
};
