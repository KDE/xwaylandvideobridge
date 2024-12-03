/*
 * App to render feeds coming from xdg-desktop-portal
 *
 * SPDX-License-Identifier: LicenseRef-KDE-Accepted-GPL
 * SPDX-FileCopyrightText: 2023 David Edmundson <kde@davidedmundson.co.uk>
 * SPDX-FileCopyrightText: 2023 Aleix Pol <aleixpol@kde.org>
 */

#include "xwaylandvideobridge.h"
#include <QGuiApplication>
#include <QLoggingCategory>
#include <QTimer>
#include <QQuickWindow>
#include <QAction>

#include <KLocalizedString>
#include <KFileUtils>

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
#include <PipeWireSourceItem>
#else
#include <KPipeWire/pipewiresourceitem.h>
#endif

#include "xdp_dbus_screencast_interface.h"
#include "contentswindow.h"
#include "x11recordingnotifier.h"
#include "xwaylandvideobridge_debug.h"

Q_DECLARE_METATYPE(Stream)

QDebug operator<<(QDebug debug, const Stream& stream)
{
    QDebugStateSaver saver(debug);
    debug.nospace() << "Stream(id: " << stream.nodeId << ", opts: " << stream.opts << ')';
    return debug;
}


const QDBusArgument &operator<<(const QDBusArgument &argument, const Stream &/*stream*/)
{
    argument.beginStructure();
    //     argument << stream.id << stream.opts;
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

const QDBusArgument &operator>>(const QDBusArgument &argument, QVector<Stream> &stream)
{
    argument.beginArray();
    while ( !argument.atEnd() ) {
        Stream element;
        argument >> element;
        stream.append( element );
    }
    argument.endArray();
    return argument;
}

XwaylandVideoBridge::XwaylandVideoBridge(QObject* parent)
    : QObject(parent)
    , iface(new OrgFreedesktopPortalScreenCastInterface(
                QLatin1String("org.freedesktop.portal.Desktop"), QLatin1String("/org/freedesktop/portal/desktop"), QDBusConnection::sessionBus(), this))
    , m_handleToken(QStringLiteral("xwaylandvideobridge%1").arg(QRandomGenerator::global()->generate()))
    , m_quitTimer(new QTimer(this))
    , m_window(new ContentsWindow)
{
    m_quitTimer->setInterval(5000);
    m_quitTimer->setSingleShot(true);
    connect(m_quitTimer, &QTimer::timeout, this, &XwaylandVideoBridge::closeSession);




    auto notifier = new X11RecordingNotifier(m_window->winId(), this);

    connect(notifier, &X11RecordingNotifier::isRedirectedChanged, this, [this, notifier]() {
        if (notifier->isRedirected()) {
            m_quitTimer->stop();
            // this is a bit racey, there's a point where we wait for a reply from the portal
            if (m_path.path().isEmpty()) {
                init();
            }
        } else {
            m_quitTimer->start();
        }
    });

    m_window->show();
}

XwaylandVideoBridge::~XwaylandVideoBridge() = default;

void XwaylandVideoBridge::startStream(const QDBusObjectPath& path)
{
    m_path = path;

    CursorModes availableCursorModes = static_cast<CursorModes>(iface->availableCursorModes());
    CursorMode cursorMode = CursorMode::Hidden;
    if (availableCursorModes.testFlag(CursorMode::Metadata)) {
        cursorMode = CursorMode::Metadata;
    } else if (availableCursorModes.testFlag(CursorMode::Embedded)) {
        cursorMode = CursorMode::Embedded;
    } else {
        qCWarning(XWAYLANDBRIDGE) << "Portal does not support any cursor modes. Cursors will be hidden";
    }

    const QVariantMap sourcesParameters = {
        { QLatin1String("handle_token"), m_handleToken },
        { QLatin1String("types"), iface->availableSourceTypes() },
        { QLatin1String("multiple"), false }, //for now?
        { QLatin1String("cursor_mode"), static_cast<uint>(cursorMode) }
    };

    auto reply = iface->SelectSources(m_path, sourcesParameters);
    reply.waitForFinished();

    if (reply.isError()) {
        qCWarning(XWAYLANDBRIDGE) << "Could not select sources" << reply.error();
        exit(1);
        return;
    }
    qCDebug(XWAYLANDBRIDGE) << "select sources done" << reply.value().path();
}

void XwaylandVideoBridge::response(uint code, const QVariantMap& results)
{
    if (code == 1) {
        qCDebug(XWAYLANDBRIDGE) << "XDG session cancelled";
        closeSession();
        return;
    } else if (code > 0) {
        qCWarning(XWAYLANDBRIDGE) << "XDG session failed:" << results << code;
        exit(1);
        return;
    }

    const auto streamsIt = results.constFind("streams");
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

    qCDebug(XWAYLANDBRIDGE) << "params" << results << code;
    if (results.isEmpty()) {
        start();
        return;
    }
}

void XwaylandVideoBridge::init()
{
    const QVariantMap sessionParameters = {
        { QLatin1String("session_handle_token"), m_handleToken },
        { QLatin1String("handle_token"), m_handleToken }
    };
    auto sessionReply = iface->CreateSession(sessionParameters);
    sessionReply.waitForFinished();
    if (!sessionReply.isValid()) {
        qCWarning(XWAYLANDBRIDGE) << "Couldn't initialize the remote control session";
        exit(1);
        return;
    }

    const bool ret = QDBusConnection::sessionBus().connect(QString(),
                                                           sessionReply.value().path(),
                                                           QLatin1String("org.freedesktop.portal.Request"),
                                                           QLatin1String("Response"),
                                                           this,
                                                           SLOT(response(uint, QVariantMap)));
    if (!ret) {
        qCWarning(XWAYLANDBRIDGE) << "failed to create session";
        exit(2);
        return;
    }

    qDBusRegisterMetaType<Stream>();
    qDBusRegisterMetaType<QVector<Stream>>();
}

void XwaylandVideoBridge::start()
{
    const QVariantMap startParameters = {
        { QLatin1String("handle_token"), m_handleToken }
    };

    auto reply = iface->Start(m_path, QStringLiteral("org.kde.xwaylandvideobridge"), startParameters);
    reply.waitForFinished();

    if (reply.isError()) {
        qCWarning(XWAYLANDBRIDGE) << "Could not start stream" << reply.error();
        exit(1);
        return;
    }
}

void XwaylandVideoBridge::handleStreams(const QVector<Stream> &streams)
{
    const QVariantMap startParameters = {
        { QLatin1String("handle_token"), m_handleToken }
    };

    auto reply = iface->OpenPipeWireRemote(m_path, startParameters);
    reply.waitForFinished();

    if (reply.isError()) {
        qCWarning(XWAYLANDBRIDGE) << "Could not start stream" << reply.error();
        exit(1);
        return;
    }
    const int fd = reply.value().takeFileDescriptor();

    if (streams.count() < 1) {
        qCWarning(XWAYLANDBRIDGE) << "No streams available";
        exit(1);
    }

    m_pipeWireItem = new PipeWireSourceItem(m_window->contentItem());
    m_pipeWireItem->setFd(fd);
    m_pipeWireItem->setNodeId(streams[0].nodeId);
    m_pipeWireItem->setVisible(true);

    if (m_pipeWireItem->state() == PipeWireSourceItem::StreamState::Streaming) {
        m_pipeWireItem->setSize(m_pipeWireItem->streamSize());
        m_window->resize(m_pipeWireItem->size().toSize());
    }

    connect(m_pipeWireItem, &PipeWireSourceItem::streamSizeChanged, this, [this]() {
        m_pipeWireItem->setSize(m_pipeWireItem->streamSize());
    });


    connect(m_pipeWireItem, &QQuickItem::widthChanged, this, [this]() {
        m_window->resize(m_pipeWireItem->size().toSize());
    });
    connect(m_pipeWireItem, &QQuickItem::heightChanged, this, [this]() {
        m_window->resize(m_pipeWireItem->size().toSize());
    });

    connect(m_pipeWireItem, &PipeWireSourceItem::stateChanged, this, [this]{
        if (m_pipeWireItem->state() == PipeWireSourceItem::StreamState::Unconnected) {
            closeSession();
        }
    });
}

void XwaylandVideoBridge::closeSession()
{

    m_handleToken = QStringLiteral("xwaylandvideobridge%1").arg(QRandomGenerator::global()->generate());
    m_quitTimer->stop();

    if (m_path.path().isEmpty())
        return;
    QDBusMessage closeScreencastSession = QDBusMessage::createMethodCall(QLatin1String("org.freedesktop.portal.Desktop"),
                                                                         m_path.path(),
                                                                         QLatin1String("org.freedesktop.portal.Session"),
                                                                         QLatin1String("Close"));
    m_path = {};

    if (m_pipeWireItem) {
        disconnect(m_pipeWireItem, nullptr, this, nullptr);
        m_pipeWireItem->deleteLater();
        m_pipeWireItem = nullptr;
    }
    QDBusConnection::sessionBus().call(closeScreencastSession);
}
