/*
 * App to render feeds coming from xdg-desktop-portal
 * Copyright 2020 Aleix Pol Gonzalez <aleixpol@kde.org>
 * Copyright 2023 David Edmundson <davidedmundson@kde.org>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License or (at your option) version 3 or any later version
 * accepted by the membership of KDE e.V. (or its successor approved
 * by the membership of KDE e.V.), which shall act as a proxy
 * defined in Section 14 of version 3 of the license.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "pwbypass.h"
#include <QGuiApplication>
#include <QLoggingCategory>
#include <QTimer>
#include <QQuickWindow>
#include <QAction>

#include "contentswindow.h"
#include "xdp_dbus_screencast_interface.h"
#include <KLocalizedString>
#include <KFileUtils>
#include <KStatusNotifierItem>

#include <KPipeWire/pipewiresourceitem.h>
#include "x11recordingnotifier.h"

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

PwBypass::PwBypass(QObject* parent)
    : QObject(parent)
    , iface(new OrgFreedesktopPortalScreenCastInterface(
                QLatin1String("org.freedesktop.portal.Desktop"), QLatin1String("/org/freedesktop/portal/desktop"), QDBusConnection::sessionBus(), this))
    , m_handleToken(QStringLiteral("pwbypass%1").arg(QRandomGenerator::global()->generate()))
    , m_quitTimer(new QTimer(this))
    , m_window(new ContentsWindow)
    , m_sni(new KStatusNotifierItem("pipewireToXProxy", this))
{
    m_quitTimer->setInterval(5000);
    connect(m_quitTimer, &QTimer::timeout, this, &PwBypass::closeSession);

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

PwBypass::~PwBypass() = default;

void PwBypass::startStream(const QDBusObjectPath& path)
{
    m_path = path;
    uint32_t cursor_mode = Hidden;
    const QVariantMap sourcesParameters = {
        { QLatin1String("handle_token"), m_handleToken },
        { QLatin1String("types"), iface->availableSourceTypes() },
        { QLatin1String("multiple"), false }, //for now?
        { QLatin1String("cursor_mode"), uint(cursor_mode) }
    };

    auto reply = iface->SelectSources(m_path, sourcesParameters);
    reply.waitForFinished();

    if (reply.isError()) {
        qWarning() << "Could not select sources" << reply.error();
        exit(1);
        return;
    }
    qDebug() << "select sources done" << reply.value().path();
}

void PwBypass::response(uint code, const QVariantMap& results)
{
    if (code == 1) {
        qDebug() << "XDG session cancelled";
        exit(0);
        return;
    } else if (code > 0) {
        qWarning() << "error!!!" << results << code;
        exit(666);
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

    qDebug() << "params" << results << code;
    if (results.isEmpty()) {
        start();
        return;
    }
}

void PwBypass::init()
{
    qDebug();
    const QVariantMap sessionParameters = {
        { QLatin1String("session_handle_token"), m_handleToken },
        { QLatin1String("handle_token"), m_handleToken }
    };
    auto sessionReply = iface->CreateSession(sessionParameters);
    sessionReply.waitForFinished();
    if (!sessionReply.isValid()) {
        qWarning("Couldn't initialize the remote control session");
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
        qWarning() << "failed to create session";
        exit(2);
        return;
    }

    qDBusRegisterMetaType<Stream>();
    qDBusRegisterMetaType<QVector<Stream>>();

    m_sni->setTitle("Exposing application to X11");
    m_sni->setIconByName("video-display");
    auto closeAction = new QAction(i18n("Close"), this);
    connect(closeAction, &QAction::triggered, this, &PwBypass::closeSession);
    m_sni->addAction("closeAction", closeAction);
    m_sni->setStatus(KStatusNotifierItem::Active);
}

void PwBypass::start()
{
    const QVariantMap startParameters = {
        { QLatin1String("handle_token"), m_handleToken }
    };

    auto reply = iface->Start(m_path, QStringLiteral("org.kde.pwbypass"), startParameters);
    reply.waitForFinished();

    if (reply.isError()) {
        qWarning() << "Could not start stream" << reply.error();
        exit(1);
        return;
    }
    qDebug() << "started!" << reply.value().path();
}

void PwBypass::handleStreams(const QVector<Stream> &streams)
{
    const QVariantMap startParameters = {
        { QLatin1String("handle_token"), m_handleToken }
    };

    auto reply = iface->OpenPipeWireRemote(m_path, startParameters);
    reply.waitForFinished();

    if (reply.isError()) {
        qWarning() << "Could not start stream" << reply.error();
        exit(1);
        return;
    }
    const int fd = reply.value().takeFileDescriptor();

    if (streams.count() < 1) {
        qWarning() << "No streams available";
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

void PwBypass::closeSession()
{
    qDebug() << "close";
    if (m_path.path().isEmpty())
        return;
    QDBusMessage closeScreencastSession = QDBusMessage::createMethodCall(QLatin1String("org.freedesktop.portal.Desktop"),
                                                                         m_path.path(),
                                                                         QLatin1String("org.freedesktop.portal.Session"),
                                                                         QLatin1String("Close"));
    m_path = {};
    disconnect(m_pipeWireItem, nullptr, this, nullptr);
    m_pipeWireItem->deleteLater();
    m_pipeWireItem = nullptr;
    QDBusConnection::sessionBus().call(closeScreencastSession);
}
