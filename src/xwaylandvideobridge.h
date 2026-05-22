/*
 * SPDX-License-Identifier: LicenseRef-KDE-Accepted-GPL
 * SPDX-FileCopyrightText: 2023 Aleix Pol <aleixpol@kde.org>
 * SPDX-FileCopyrightText: 2026 Hadi Chokr <hadichokr@icloud.com>
 */

#pragma once

#include <KStatusNotifierItem>
#include <PipeWireRecord>
#include <QDBusObjectPath>
#include <QObject>

class QTimer;
class ContentsWindow;
class PipeWireSourceItem;

struct Stream {
    uint nodeId;
    QVariantMap opts;
};

class OrgFreedesktopPortalScreenCastInterface;

class XwaylandVideoBridge : public QObject {
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
    void startStream(const QDBusObjectPath &path);
    void handleStreams(const QVector<Stream> &streams);
    void start();

    OrgFreedesktopPortalScreenCastInterface *iface;
    QDBusObjectPath m_path;
    QString m_handleToken;

    QTimer *m_quitTimer;
    QScopedPointer<ContentsWindow> m_window;
    PipeWireSourceItem *m_pipeWireItem = nullptr;
    KStatusNotifierItem *m_trayIcon = nullptr;
    bool m_sessionActive = false;
};
