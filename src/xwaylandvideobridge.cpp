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
#include <QDBusPendingCallWatcher>
#include <QGuiApplication>
#include <QIcon>
#include <QMenu>
#include <QRandomGenerator>
#include <QSettings>
#include <QStandardPaths>
#include <QTimer>

#include <KLocalizedString>
#include <KSandbox>
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
        return i18nc("@title:window", "Screen Share - Monitor");
    case XwaylandVideoBridge::Window:
        return i18nc("@title:window", "Screen Share - Window");
    case XwaylandVideoBridge::Virtual:
        return i18nc("@title:window", "Screen Share - Virtual");
    }
    return i18nc("@title:window", "Screen Share");
}

static QString configFilePath()
{
    // Inside Flatpak this resolves to ~/.var/app/<app id>/config.
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + QLatin1String("/xwaylandvideobridgerc");
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
    m_trayIcon->setTitle(i18n("Xwayland Video Bridge"));
    m_trayIcon->setToolTip(QStringLiteral("org.kde.xwaylandvideobridge"),
                           i18n("Xwayland Video Bridge"),
                           i18n("Offer XDG Desktop Portals screencast streams to X11 apps"));
    m_trayIcon->setStatus(KStatusNotifierItem::Passive);
    m_trayIcon->setStandardActionsEnabled(false);

    connect(m_trayIcon, &KStatusNotifierItem::activateRequested, this, &XwaylandVideoBridge::resetSession);

    auto *menu = new QMenu;
    auto *resetAction = menu->addAction(QIcon::fromTheme(QStringLiteral("view-refresh")), i18nc("@action:inmenu", "Reset Bridge"));
    connect(resetAction, &QAction::triggered, this, &XwaylandVideoBridge::resetSession);

    // The window is invisible, so the tray menu is the only way to quit from the UI.
    auto *quitAction = menu->addAction(QIcon::fromTheme(QStringLiteral("application-exit")), i18n("Quit"));
    connect(quitAction, &QAction::triggered, qApp, &QCoreApplication::quit);
    m_trayIcon->setContextMenu(menu);

    connect(qApp, &QCoreApplication::aboutToQuit, this, &XwaylandVideoBridge::closeSession);

    // Stays mapped while idle so X11 clients can still enumerate and pick it.
    m_window->show();

    requestAutostart();
}

XwaylandVideoBridge::~XwaylandVideoBridge() = default;

void XwaylandVideoBridge::showRunningMessage()
{
    m_trayIcon->showMessage(i18nc("@title", "Xwayland Video Bridge is running"),
                            i18nc("@info", "Use the system tray icon to reset or quit it."),
                            QStringLiteral("org.kde.xwaylandvideobridge"));
}

void XwaylandVideoBridge::fitItemToWindow()
{
    if (!m_pipeWireItem) {
        return;
    }

    const QSizeF windowSize = m_window->size();
    const QSize streamSize = m_pipeWireItem->streamSize();

    // The window can be smaller than the stream (work area cap, WM constraints), so scale the stream to fit.
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

    // Shrink even if still redirected. A big leftover window would eat input if click-through ever broke.
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
        // We don't need the reply, and after Closed the session is gone anway.
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

    // The request path comes from our bus name and handle_token, so one connection covers every request
    // as long as they don't overlap.
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

    const Stream &stream = streams.constFirst();
    const auto serial = stream.opts.constFind(QLatin1String("pipewire-serial"));
    if (serial != stream.opts.constEnd()) {
        m_pipeWireItem->setObjectSerial(serial->toULongLong());
    } else {
        // Pre-v6 portals only give us the node id.
        QT_WARNING_PUSH
        QT_WARNING_DISABLE_DEPRECATED
        m_pipeWireItem->setNodeId(stream.nodeId);
        QT_WARNING_POP
    }

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
        // The size may not change (refused or already capped), so refit here.
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

void XwaylandVideoBridge::requestAutostart()
{
    if (!KSandbox::isFlatpak()) {
        return;
    }

    QSettings settings(configFilePath(), QSettings::IniFormat);
    if (settings.value(QStringLiteral("AutostartRequested"), false).toBool()) {
        return;
    }

    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected()) {
        return;
    }
    const QString token = QStringLiteral("xwaylandvideobridge_autostart");

    const QString sender = bus.baseService().mid(1).replace(QLatin1Char('.'), QLatin1Char('_'));
    m_backgroundRequestPath = QDBusObjectPath(QStringLiteral("/org/freedesktop/portal/desktop/request/%1/%2").arg(sender, token));
    bus.connect(QString(),
                m_backgroundRequestPath.path(),
                QLatin1String("org.freedesktop.portal.Request"),
                QLatin1String("Response"),
                this,
                SLOT(backgroundResponse(uint, QVariantMap)));

    QDBusMessage message = QDBusMessage::createMethodCall(QLatin1String("org.freedesktop.portal.Desktop"),
                                                          QLatin1String("/org/freedesktop/portal/desktop"),
                                                          QLatin1String("org.freedesktop.portal.Background"),
                                                          QLatin1String("RequestBackground"));
    const QVariantMap options = {
        {QLatin1String("handle_token"), token},
        {QLatin1String("reason"), i18nc("@info", "Start the video bridge on login so X11 apps can share Wayland windows")},
        {QLatin1String("autostart"), true},
        {QLatin1String("commandline"), QStringList{QStringLiteral("xwaylandvideobridge"), QStringLiteral("--autostart")}},
    };
    // No parent window: ours is invisible.
    message << QString() << options;

    auto *watcher = new QDBusPendingCallWatcher(bus.asyncCall(message), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *call) {
        call->deleteLater();
        if (call->isError()) {
            qCWarning(XWAYLANDBRIDGE) << "Could not request autostart from the Background portal" << call->error();
            disconnectBackgroundRequest();
        }
    });
}

void XwaylandVideoBridge::backgroundResponse(uint code, const QVariantMap &results)
{
    disconnectBackgroundRequest();

    if (code != 0 && code != 1) {
        qCWarning(XWAYLANDBRIDGE) << "Background portal request failed:" << code << results;
        return;
    }

    QSettings settings(configFilePath(), QSettings::IniFormat);
    settings.setValue(QStringLiteral("AutostartRequested"), true);

    if (!results.value(QLatin1String("autostart")).toBool()) {
        qCInfo(XWAYLANDBRIDGE) << "Autostart was not enabled by the Background portal";
    }
}

void XwaylandVideoBridge::disconnectBackgroundRequest()
{
    if (m_backgroundRequestPath.path().isEmpty()) {
        return;
    }

    QDBusConnection::sessionBus().disconnect(QString(),
                                             m_backgroundRequestPath.path(),
                                             QLatin1String("org.freedesktop.portal.Request"),
                                             QLatin1String("Response"),
                                             this,
                                             SLOT(backgroundResponse(uint, QVariantMap)));
    m_backgroundRequestPath = {};
}
