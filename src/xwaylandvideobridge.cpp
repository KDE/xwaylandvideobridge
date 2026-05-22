/*
 * App to render feeds coming from xdg-desktop-portal
 *
 * SPDX-License-Identifier: LicenseRef-KDE-Accepted-GPL
 * SPDX-FileCopyrightText: 2023 David Edmundson <kde@davidedmundson.co.uk>
 * SPDX-FileCopyrightText: 2023 Aleix Pol <aleixpol@kde.org>
 * SPDX-FileCopyrightText: 2026 Hadi Chokr <hadichokr@icloud.com>
 */

#include "xwaylandvideobridge.h"

#include <QAction>
#include <QGuiApplication>
#include <QLoggingCategory>
#include <QMenu>
#include <QQuickWindow>
#include <QTimer>

#include <KLocalizedString>
#include <KStatusNotifierItem>

#include <PipeWireSourceItem>

#include "contentswindow.h"
#include "x11recordingnotifier.h"
#include "xdp_dbus_screencast_interface.h"
#include "xwaylandvideobridge_debug.h"

Q_DECLARE_METATYPE(Stream)

QDebug operator<<(QDebug debug, const Stream &stream)
{
    QDebugStateSaver saver(debug);
    debug.nospace() << "Stream(id: " << stream.nodeId << ", opts: " << stream.opts
    << ')';
    return debug;
}

const QDBusArgument &operator<<(const QDBusArgument &argument,
                                const Stream & /*stream*/)
{
    argument.beginStructure();
    argument.endStructure();
    return argument;
}

const QDBusArgument &operator>>(const QDBusArgument &argument, Stream &stream)
{
    argument.beginStructure();
    argument >> stream.nodeId >> stream.opts;
    argument.endStructure();
    return argument;
}

const QDBusArgument &operator>>(const QDBusArgument &argument,
                                QVector<Stream> &streams)
{
    argument.beginArray();
    while (!argument.atEnd()) {
        Stream element;
        argument >> element;
        streams.append(element);
    }
    argument.endArray();
    return argument;
}

static QString streamTitle(const Stream &stream)
{
    const auto sourceType =
    static_cast<XwaylandVideoBridge::SourceTypes>(
        stream.opts.value(QLatin1String("source_type"), 0u).toUInt());
    switch (sourceType) {
        case XwaylandVideoBridge::Monitor:
            return i18n("Screen Share - Monitor");
        case XwaylandVideoBridge::Window:
            return i18n("Screen Share - Window");
        case XwaylandVideoBridge::Virtual:
            return i18n("Screen Share - Virtual");
    }
    return i18n("Screen Share");
}

XwaylandVideoBridge::XwaylandVideoBridge(QObject *parent)
: QObject(parent)
, iface(new OrgFreedesktopPortalScreenCastInterface(
    QLatin1String("org.freedesktop.portal.Desktop"),
                                                    QLatin1String("/org/freedesktop/portal/desktop"),
                                                    QDBusConnection::sessionBus(), this))
, m_handleToken(QStringLiteral("xwaylandvideobridge%1")
.arg(QRandomGenerator::global()->generate()))
, m_quitTimer(new QTimer(this))
, m_window(new ContentsWindow)
{
    m_quitTimer->setInterval(5000);
    m_quitTimer->setSingleShot(true);
    connect(m_quitTimer, &QTimer::timeout, this,
            &XwaylandVideoBridge::closeSession);

    auto *notifier = new X11RecordingNotifier(m_window->winId(), this);
    connect(notifier, &X11RecordingNotifier::isRedirectedChanged, this,
            [this, notifier]() {
                if (notifier->isRedirected()) {
                    m_quitTimer->stop();
                    if (m_path.path().isEmpty())
                        init();
                } else {
                    m_quitTimer->start();
                }
            });

    connect(m_window.data(), &ContentsWindow::mirrorWindowClosed,
            this, &XwaylandVideoBridge::closeSession);

    m_trayIcon = new KStatusNotifierItem(this);
    m_trayIcon->setIconByName(QStringLiteral("xwaylandvideobridge"));
    m_trayIcon->setTitle(i18n("Wayland to X11 Video Bridge"));
    m_trayIcon->setToolTip(
        QStringLiteral("xwaylandvideobridge"),
                           i18n("Wayland to X11 Video Bridge"),
                           i18n("Utility to allow streaming Wayland windows to X applications"));
    m_trayIcon->setStatus(KStatusNotifierItem::Passive);

    connect(m_trayIcon, &KStatusNotifierItem::activateRequested, this, [this]() {
        closeSession();
        init();
    });

    auto *menu = new QMenu;
    auto *resetAction = menu->addAction(
        QIcon::fromTheme(QStringLiteral("view-refresh")), i18n("Reset Bridge"));
    connect(resetAction, &QAction::triggered, this, [this]() {
        closeSession();
        init();
    });
    m_trayIcon->setContextMenu(menu);

    connect(qApp, &QCoreApplication::aboutToQuit,
            this, &XwaylandVideoBridge::closeSession);

    m_window->show();
}

XwaylandVideoBridge::~XwaylandVideoBridge() = default;

void XwaylandVideoBridge::closeSession()
{
    m_sessionActive = false;
    m_trayIcon->setStatus(KStatusNotifierItem::Passive);

    if (m_pipeWireItem) {
        disconnect(m_pipeWireItem, nullptr, this, nullptr);
        m_pipeWireItem->deleteLater();
        m_pipeWireItem = nullptr;
    }

    m_quitTimer->stop();

    if (!m_path.path().isEmpty()) {
        QDBusConnection::sessionBus().disconnect(
            QString(), m_path.path(),
                                                 QLatin1String("org.freedesktop.portal.Request"),
                                                 QLatin1String("Response"), this, SLOT(response(uint, QVariantMap)));

        QDBusMessage closeMsg = QDBusMessage::createMethodCall(
            QLatin1String("org.freedesktop.portal.Desktop"), m_path.path(),
                                                               QLatin1String("org.freedesktop.portal.Session"),
                                                               QLatin1String("Close"));
        QDBusConnection::sessionBus().call(closeMsg);
        m_path = {};
    }

    m_handleToken = QStringLiteral("xwaylandvideobridge%1")
    .arg(QRandomGenerator::global()->generate());
}

void XwaylandVideoBridge::startStream(const QDBusObjectPath &path)
{
    m_path = path;

    QDBusConnection::sessionBus().connect(
        QString(), m_path.path(),
                                          QLatin1String("org.freedesktop.portal.Session"),
                                          QLatin1String("Closed"), this, SLOT(closeSession()));

    CursorModes availableCursorModes =
    static_cast<CursorModes>(iface->availableCursorModes());
    CursorMode cursorMode = CursorMode::Hidden;
    if (availableCursorModes.testFlag(CursorMode::Metadata)) {
        cursorMode = CursorMode::Metadata;
    } else if (availableCursorModes.testFlag(CursorMode::Embedded)) {
        cursorMode = CursorMode::Embedded;
    } else {
        qCWarning(XWAYLANDBRIDGE)
        << "Portal does not support any cursor modes. Cursors will be hidden";
    }

    const QVariantMap sourcesParameters = {
        {QLatin1String("handle_token"), m_handleToken},
        {QLatin1String("types"), iface->availableSourceTypes()},
        {QLatin1String("multiple"), false},
        {QLatin1String("cursor_mode"), static_cast<uint>(cursorMode)}};

        auto reply = iface->SelectSources(m_path, sourcesParameters);
        reply.waitForFinished();

        if (reply.isError()) {
            qCWarning(XWAYLANDBRIDGE) << "Could not select sources" << reply.error();
            exit(1);
        }
}

void XwaylandVideoBridge::response(uint code, const QVariantMap &results)
{
    if (code == 1) {
        closeSession();
        return;
    } else if (code > 0) {
        qCWarning(XWAYLANDBRIDGE) << "XDG session failed:" << results << code;
        exit(1);
        return;
    }

    const auto streamsIt = results.constFind(QLatin1String("streams"));
    if (streamsIt != results.constEnd()) {
        QVector<Stream> streams;
        streamsIt->value<QDBusArgument>() >> streams;
        handleStreams(streams);
        return;
    }

    const auto handleIt = results.constFind(QStringLiteral("session_handle"));
    if (handleIt != results.constEnd()) {
        startStream(QDBusObjectPath(handleIt->toString()));
        return;
    }

    if (results.isEmpty())
        start();
}

void XwaylandVideoBridge::init()
{
    m_sessionActive = true;
    m_trayIcon->setStatus(KStatusNotifierItem::Active);

    const QVariantMap sessionParameters = {
        {QLatin1String("session_handle_token"), m_handleToken},
        {QLatin1String("handle_token"), m_handleToken}};

        auto sessionReply = iface->CreateSession(sessionParameters);
        sessionReply.waitForFinished();
        if (!sessionReply.isValid()) {
            qCWarning(XWAYLANDBRIDGE) << "Couldn't initialize the screencast session";
            exit(1);
            return;
        }

        const bool ret = QDBusConnection::sessionBus().connect(
            QString(), sessionReply.value().path(),
                                                               QLatin1String("org.freedesktop.portal.Request"),
                                                               QLatin1String("Response"), this, SLOT(response(uint, QVariantMap)));
        if (!ret) {
            qCWarning(XWAYLANDBRIDGE) << "Failed to connect to session response signal";
            exit(2);
            return;
        }

        qDBusRegisterMetaType<Stream>();
        qDBusRegisterMetaType<QVector<Stream>>();
}

void XwaylandVideoBridge::start()
{
    const QVariantMap startParameters = {
        {QLatin1String("handle_token"), m_handleToken}};

        auto reply = iface->Start(
            m_path,
            QStringLiteral("x11:%1").arg(QString::number(m_window->winId(), 16)),
                                  startParameters);
        reply.waitForFinished();

        if (reply.isError()) {
            qCWarning(XWAYLANDBRIDGE) << "Could not start stream" << reply.error();
            exit(1);
        }
}

void XwaylandVideoBridge::handleStreams(const QVector<Stream> &streams)
{
    if (streams.isEmpty()) {
        qCWarning(XWAYLANDBRIDGE) << "No streams available";
        exit(1);
        return;
    }

    const QVariantMap startParameters = {
        {QLatin1String("handle_token"), m_handleToken}};

        auto reply = iface->OpenPipeWireRemote(m_path, startParameters);
        reply.waitForFinished();

        if (reply.isError()) {
            qCWarning(XWAYLANDBRIDGE)
            << "Could not open PipeWire remote:" << reply.error();
            exit(1);
            return;
        }
        const int fd = reply.value().takeFileDescriptor();

        m_window->setTitle(streamTitle(streams[0]));

        m_pipeWireItem = new PipeWireSourceItem(m_window->contentItem());
        m_pipeWireItem->setFd(fd);
        m_pipeWireItem->setNodeId(streams[0].nodeId);
        m_pipeWireItem->setVisible(true);
        m_pipeWireItem->setPosition(QPointF(0, 0));

        connect(m_pipeWireItem, &PipeWireSourceItem::streamSizeChanged, this, [this]() {
            if (!m_pipeWireItem)
                return;
            const QSize s = m_pipeWireItem->streamSize();
            if (!s.isEmpty())
                m_pipeWireItem->setSize(QSizeF(s));
        });
        // Set initial size in case streamSize is already known
        const QSize initial = m_pipeWireItem->streamSize();
        if (!initial.isEmpty())
            m_pipeWireItem->setSize(QSizeF(initial));

    connect(m_pipeWireItem, &PipeWireSourceItem::stateChanged, this, [this]() {
        if (!m_pipeWireItem)
            return;

        const auto state = m_pipeWireItem->state();

        if (state == PipeWireSourceItem::StreamState::Streaming)
            return;

        if (state == PipeWireSourceItem::StreamState::Unconnected)
            closeSession();
    });
}
