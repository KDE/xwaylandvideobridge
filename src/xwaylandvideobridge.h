/*
 * App to render feeds coming from xdg-desktop-portal
 *
 * SPDX-License-Identifier: LicenseRef-KDE-Accepted-GPL
 * SPDX-FileCopyrightText: 2023 Aleix Pol <aleixpol@kde.org>
 */

#pragma once

#include <QObject>
#include <QDBusObjectPath>
#include <PipeWireRecord>

class QTimer;
class ContentsWindow;
class PipeWireSourceItem;

class KStatusNotifierItem;

struct Stream {
    uint nodeId;
    QVariantMap opts;
};

class OrgFreedesktopPortalScreenCastInterface;

class XwaylandVideoBridge : public QObject
{
    Q_OBJECT
public:
    XwaylandVideoBridge(QObject* parent = nullptr);
    ~XwaylandVideoBridge() override;

    enum CursorMode {
        Hidden = 1,
        Embedded = 2,
        Metadata = 4
    };
    Q_ENUM(CursorMode);
    Q_DECLARE_FLAGS(CursorModes, CursorMode)

    enum SourceTypes {
        Monitor = 1,
        Window = 2
    };
    Q_ENUM(SourceTypes);

    void setDuration(int duration);

public Q_SLOTS:
    void response(uint code, const QVariantMap& results);

private:
    void init();
    void startStream(const QDBusObjectPath &path);
    void handleStreams(const QVector<Stream> &streams);
    void start();
    void closeSession();
    void closed();

    OrgFreedesktopPortalScreenCastInterface *iface;
    QDBusObjectPath m_path;
    QString m_handleToken;

    QTimer *m_quitTimer;
    QScopedPointer<ContentsWindow> m_window;
    PipeWireSourceItem *m_pipeWireItem = nullptr;
    KStatusNotifierItem *m_sni;
};
