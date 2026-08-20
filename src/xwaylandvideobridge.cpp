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
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QGuiApplication>
#include <QIcon>
#include <QMenu>
#include <QRandomGenerator>
#include <QTimer>

#include <KLocalizedString>
#include <KStatusNotifierItem>

#include <PipeWireSourceItem>

#include "contentswindow.h"
#include "x11recordingnotifier.h"
#include "xdp_dbus_screencast_interface.h"
#include "xwaylandvideobridge_debug.h"

Q_DECLARE_METATYPE(Stream)

const QDBusArgument &operator<<(const QDBusArgument &argument, const Stream & /*stream*/)
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

const QDBusArgument &operator>>(const QDBusArgument &argument, QVector<Stream> &streams)
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

static QString newHandleToken()
{
    return QStringLiteral("xwaylandvideobridge%1").arg(QRandomGenerator::global()->generate());
}

static QString streamTitle(const Stream &stream)
{
    const auto sourceType = static_cast<XwaylandVideoBridge::SourceTypes>(stream.opts.value(QLatin1String("source_type"), 0u).toUInt());
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
    , m_portal(new OrgFreedesktopPortalScreenCastInterface(QLatin1String("org.freedesktop.portal.Desktop"),
                                                           QLatin1String("/org/freedesktop/portal/desktop"),
                                                           QDBusConnection::sessionBus(),
                                                           this))
    , m_handleToken(newHandleToken())
    , m_quitTimer(new QTimer(this))
    , m_window(new ContentsWindow)
{
    qDBusRegisterMetaType<Stream>();
    qDBusRegisterMetaType<QVector<Stream>>();

    m_quitTimer->setInterval(5000);
    m_quitTimer->setSingleShot(true);
    connect(m_quitTimer, &QTimer::timeout, this, &XwaylandVideoBridge::closeSession);

    m_recordingNotifier = new X11RecordingNotifier(m_window->winId(), this);
    connect(m_recordingNotifier, &X11RecordingNotifier::isRedirectedChanged, this, [this] {
        if (m_recordingNotifier->isRedirected()) {
            m_quitTimer->stop();
            init();
        } else {
            m_quitTimer->start();
        }
    });

    connect(m_recordingNotifier, &X11RecordingNotifier::redirectRequested, this, &XwaylandVideoBridge::init);

    connect(m_window.data(), &ContentsWindow::windowIdChanged, this, [this](WId windowId) {
        m_recordingNotifier->setWindowId(windowId);
    });

    connect(m_window.data(), &ContentsWindow::mirrorWindowClosed, this, &XwaylandVideoBridge::closeSession);
    connect(m_window.data(), &QWindow::widthChanged, this, &XwaylandVideoBridge::fitItemToWindow);
    connect(m_window.data(), &QWindow::heightChanged, this, &XwaylandVideoBridge::fitItemToWindow);

    m_trayIcon = new KStatusNotifierItem(this);
    m_trayIcon->setIconByName(QStringLiteral("org.kde.xwaylandvideobridge"));
    m_trayIcon->setTitle(i18n("Wayland to X11 Video Bridge"));
    m_trayIcon->setToolTip(QStringLiteral("org.kde.xwaylandvideobridge"),
                           i18n("Wayland to X11 Video Bridge"),
                           i18n("Utility to allow streaming Wayland windows to X applications"));
    m_trayIcon->setStatus(KStatusNotifierItem::Passive);
    m_trayIcon->setStandardActionsEnabled(false);

    connect(m_trayIcon, &KStatusNotifierItem::activateRequested, this, &XwaylandVideoBridge::resetSession);

    auto *menu = new QMenu;
    auto *resetAction = menu->addAction(QIcon::fromTheme(QStringLiteral("view-refresh")), i18n("Reset Bridge"));
    connect(resetAction, &QAction::triggered, this, &XwaylandVideoBridge::resetSession);

    // The window is deliberately invisible, so the tray menu is the only way for a user to get rid of it.
    auto *quitAction = menu->addAction(QIcon::fromTheme(QStringLiteral("application-exit")), i18n("Quit"));
    connect(quitAction, &QAction::triggered, qApp, &QCoreApplication::quit);
    m_trayIcon->setContextMenu(menu);

    connect(qApp, &QCoreApplication::aboutToQuit, this, &XwaylandVideoBridge::closeSession);

    // Stays mapped while idle so X11 clients can still enumerate and pick it.
    m_window->show();
}

XwaylandVideoBridge::~XwaylandVideoBridge() = default;

void XwaylandVideoBridge::fitItemToWindow()
{
    if (!m_pipeWireItem) {
        return;
    }

    const QSizeF windowSize = m_window->size();
    const QSize streamSize = m_pipeWireItem->streamSize();

    // The window manager clamps the window to the work area, so the stream is scaled into what was granted
    // rather than being left to fall outside the window.
    const QSizeF target = streamSize.isEmpty() ? windowSize : QSizeF(streamSize).scaled(windowSize, Qt::KeepAspectRatio);

    m_pipeWireItem->setSize(target);
    m_pipeWireItem->setPosition(QPointF((windowSize.width() - target.width()) / 2, (windowSize.height() - target.height()) / 2));
}

void XwaylandVideoBridge::clearStream()
{
    if (!m_pipeWireItem) {
        return;
    }

    disconnect(m_pipeWireItem, nullptr, this, nullptr);
    m_pipeWireItem->deleteLater();
    m_pipeWireItem = nullptr;
}

void XwaylandVideoBridge::resetSession()
{
    closeSession();
    init();
}

void XwaylandVideoBridge::closeSession()
{
    m_sessionActive = false;
    m_trayIcon->setStatus(KStatusNotifierItem::Passive);
    m_quitTimer->stop();

    clearStream();

    // An oversized window left mapped without a stream swallows pointer input across a whole output, so it
    // shrinks even if a client still has it redirected.
    m_window->goIdle();

    QDBusConnection bus = QDBusConnection::sessionBus();

    if (!m_requestPath.path().isEmpty()) {
        bus.disconnect(QString(),
                       m_requestPath.path(),
                       QLatin1String("org.freedesktop.portal.Request"),
                       QLatin1String("Response"),
                       this,
                       SLOT(response(uint, QVariantMap)));
        m_requestPath = {};
    }

    if (!m_sessionPath.path().isEmpty()) {
        bus.disconnect(QString(), m_sessionPath.path(), QLatin1String("org.freedesktop.portal.Session"), QLatin1String("Closed"), this, SLOT(closeSession()));

        QDBusMessage closeMsg = QDBusMessage::createMethodCall(QLatin1String("org.freedesktop.portal.Desktop"),
                                                               m_sessionPath.path(),
                                                               QLatin1String("org.freedesktop.portal.Session"),
                                                               QLatin1String("Close"));
        // Closed can bring us here from inside a D-Bus callback, and the reply is of no use anyway.
        bus.call(closeMsg, QDBus::NoBlock);
        m_sessionPath = {};
    }

    m_handleToken = newHandleToken();
}

void XwaylandVideoBridge::init()
{
    if (m_sessionActive) {
        return;
    }

    const QVariantMap sessionParameters = {
        {QLatin1String("session_handle_token"), m_handleToken},
        {QLatin1String("handle_token"), m_handleToken},
    };

    auto reply = m_portal->CreateSession(sessionParameters);
    reply.waitForFinished();
    if (!reply.isValid()) {
        qCWarning(XWAYLANDBRIDGE) << "Could not initialize the screencast session" << reply.error();
        return;
    }

    // The portal derives the request path from handle_token, so this one connection covers the responses to
    // every request made with this token.
    m_requestPath = reply.value();

    const bool connected = QDBusConnection::sessionBus().connect(QString(),
                                                                 m_requestPath.path(),
                                                                 QLatin1String("org.freedesktop.portal.Request"),
                                                                 QLatin1String("Response"),
                                                                 this,
                                                                 SLOT(response(uint, QVariantMap)));
    if (!connected) {
        qCWarning(XWAYLANDBRIDGE) << "Failed to connect to the portal response signal on" << m_requestPath.path();
        m_requestPath = {};
        return;
    }

    m_sessionActive = true;
    m_trayIcon->setStatus(KStatusNotifierItem::Active);
}

void XwaylandVideoBridge::response(uint code, const QVariantMap &results)
{
    if (code != 0) {
        if (code != 1) {
            qCWarning(XWAYLANDBRIDGE) << "XDG session failed:" << results << code;
        }
        closeSession();
        return;
    }

    const auto streamsIt = results.constFind(QLatin1String("streams"));
    if (streamsIt != results.constEnd()) {
        QVector<Stream> streams;
        streamsIt->value<QDBusArgument>() >> streams;
        handleStreams(streams);
        return;
    }

    const auto handleIt = results.constFind(QLatin1String("session_handle"));
    if (handleIt != results.constEnd()) {
        selectSources(QDBusObjectPath(handleIt->toString()));
        return;
    }

    if (results.isEmpty()) {
        start();
    }
}

void XwaylandVideoBridge::selectSources(const QDBusObjectPath &sessionPath)
{
    m_sessionPath = sessionPath;

    QDBusConnection::sessionBus()
        .connect(QString(), m_sessionPath.path(), QLatin1String("org.freedesktop.portal.Session"), QLatin1String("Closed"), this, SLOT(closeSession()));

    const CursorModes availableCursorModes = static_cast<CursorModes>(m_portal->availableCursorModes());
    CursorMode cursorMode = CursorMode::Hidden;
    if (availableCursorModes.testFlag(CursorMode::Metadata)) {
        cursorMode = CursorMode::Metadata;
    } else if (availableCursorModes.testFlag(CursorMode::Embedded)) {
        cursorMode = CursorMode::Embedded;
    } else {
        qCWarning(XWAYLANDBRIDGE) << "Portal does not support any cursor modes. Cursors will be hidden";
    }

    const QVariantMap sourcesParameters = {
        {QLatin1String("handle_token"), m_handleToken},
        {QLatin1String("types"), m_portal->availableSourceTypes()},
        {QLatin1String("multiple"), false},
        {QLatin1String("cursor_mode"), static_cast<uint>(cursorMode)},
    };

    auto reply = m_portal->SelectSources(m_sessionPath, sourcesParameters);
    reply.waitForFinished();

    if (reply.isError()) {
        qCWarning(XWAYLANDBRIDGE) << "Could not select sources" << reply.error();
        closeSession();
    }
}

void XwaylandVideoBridge::start()
{
    const QVariantMap startParameters = {
        {QLatin1String("handle_token"), m_handleToken},
    };

    auto reply = m_portal->Start(m_sessionPath, QStringLiteral("x11:%1").arg(QString::number(m_window->winId(), 16)), startParameters);
    reply.waitForFinished();

    if (reply.isError()) {
        qCWarning(XWAYLANDBRIDGE) << "Could not start stream" << reply.error();
        closeSession();
    }
}

void XwaylandVideoBridge::handleStreams(const QVector<Stream> &streams)
{
    if (streams.isEmpty()) {
        qCWarning(XWAYLANDBRIDGE) << "No streams available";
        closeSession();
        return;
    }

    const QVariantMap startParameters = {
        {QLatin1String("handle_token"), m_handleToken},
    };

    auto reply = m_portal->OpenPipeWireRemote(m_sessionPath, startParameters);
    reply.waitForFinished();

    if (reply.isError()) {
        qCWarning(XWAYLANDBRIDGE) << "Could not open PipeWire remote:" << reply.error();
        closeSession();
        return;
    }

    clearStream();
    m_window->setTitle(streamTitle(streams.constFirst()));

    m_pipeWireItem = new PipeWireSourceItem(m_window->contentItem());
    m_pipeWireItem->setFd(reply.value().takeFileDescriptor());
    m_pipeWireItem->setNodeId(streams.constFirst().nodeId);
    m_pipeWireItem->setVisible(true);
    fitItemToWindow();

    auto matchStreamSize = [this] {
        if (!m_pipeWireItem) {
            return;
        }
        const QSize size = m_pipeWireItem->streamSize();
        if (size.isEmpty()) {
            return;
        }
        m_window->showForStream(size);
        // A request the window manager refuses changes nothing, so there is no size change to react to.
        fitItemToWindow();
    };
    connect(m_pipeWireItem, &PipeWireSourceItem::streamSizeChanged, this, matchStreamSize);
    matchStreamSize();

    connect(m_pipeWireItem, &PipeWireSourceItem::stateChanged, this, [this] {
        if (m_pipeWireItem && m_pipeWireItem->state() == PipeWireSourceItem::StreamState::Unconnected) {
            closeSession();
        }
    });
}
