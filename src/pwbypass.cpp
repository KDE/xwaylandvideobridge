/*
 * App to render feeds coming from xdg-desktop-portal
 * Copyright 2020 Aleix Pol Gonzalez <aleixpol@kde.org>
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

#include "xdp_dbus_screencast_interface.h"
#include <KLocalizedString>
#include <KFileUtils>

#include <KPipeWire/pipewiresourceitem.h>

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
    , m_window(new QQuickWindow)
{
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
}

PwBypass::~PwBypass() = default;

void PwBypass::init(const QDBusObjectPath& path)
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
        init(QDBusObjectPath(handleIt->toString()));
        return;
    }

    qDebug() << "params" << results << code;
    if (results.isEmpty()) {
        start();
        return;
    }
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

    auto pipewireSource = new PipeWireSourceItem(m_window->contentItem());
    pipewireSource->setNodeId(streams[0].nodeId);
    pipewireSource->setFd(fd);
    pipewireSource->setVisible(true);

    pipewireSource->setSize(pipewireSource->naturalSize());
    connect(pipewireSource, &PipeWireSourceItem::naturalSizeChanged, this, [pipewireSource]() {
        pipewireSource->setSize(pipewireSource->naturalSize());
    });

    m_window->resize(pipewireSource->size().toSize());
    connect(pipewireSource, &QQuickItem::widthChanged, this, [this, pipewireSource]() {
        m_window->resize(pipewireSource->size().toSize());
    });
    connect(pipewireSource, &QQuickItem::heightChanged, this, [this, pipewireSource]() {
        m_window->resize(pipewireSource->size().toSize());
    });


    m_window->show();
}

void PwBypass::closeSession()
{
    QDBusMessage closeScreencastSession = QDBusMessage::createMethodCall(QLatin1String("org.freedesktop.portal.Desktop"),
                                            m_path.path(),
                                            QLatin1String("org.freedesktop.portal.Session"),
                                            QLatin1String("Close"));
    QDBusConnection::sessionBus().call(closeScreencastSession);
}
