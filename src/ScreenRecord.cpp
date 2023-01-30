/*
 * App To Record systems using xdg-desktop-portal
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

#include "ScreenRecord.h"
#include <QLoggingCategory>
#include <QTimer>

#include "xdp_dbus_screencast_interface.h"
#include <KLocalizedString>
#include <KFileUtils>
#include <KNotification>

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

ScreenRecord::ScreenRecord(QObject* parent)
    : QObject(parent)
    , iface(new OrgFreedesktopPortalScreenCastInterface(
        QLatin1String("org.freedesktop.portal.Desktop"), QLatin1String("/org/freedesktop/portal/desktop"), QDBusConnection::sessionBus(), this))
    , m_handleToken(QStringLiteral("ScreenRecord%1").arg(QRandomGenerator::global()->generate()))
    , m_sni(new KStatusNotifierItem(this))
{
    m_sni->setIconByName("media-record");
    m_sni->setTitle(i18n("Screen Record"));
    m_sni->setToolTip("media-record", i18n("Screen Record"), i18n("Figuring out what to record"));
    m_sni->setStandardActionsEnabled(false);
    m_sni->setStatus(KStatusNotifierItem::Active);

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

ScreenRecord::~ScreenRecord() = default;

void ScreenRecord::init(const QDBusObjectPath& path)
{
    m_path = path;
    uint32_t cursor_mode;
    if (iface->availableCursorModes() & Metadata) {
        cursor_mode = Metadata;
    } else {
        cursor_mode = Hidden;
    }
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

void ScreenRecord::response(uint code, const QVariantMap& results)
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

void ScreenRecord::start()
{
    const QVariantMap startParameters = {
        { QLatin1String("handle_token"), m_handleToken }
    };

    auto reply = iface->Start(m_path, QStringLiteral("org.kde.screenrecord"), startParameters);
    reply.waitForFinished();

    if (reply.isError()) {
        qWarning() << "Could not start stream" << reply.error();
        exit(1);
        return;
    }
    qDebug() << "started!" << reply.value().path();
}

void ScreenRecord::handleStreams(const QVector<Stream> &streams)
{
    Q_ASSERT(streams.count() == 1);
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

    const int fd = reply.value().fileDescriptor();

    const auto now = QDateTime::currentDateTime();
    const QString name = i18n("Video_%1.%2", now.toString(Qt::ISODate), m_record->extension());
    QString moviesPath = QStandardPaths::writableLocation(QStandardPaths::MoviesLocation);
    QString newPath(moviesPath + '/' + name);
    if (QFile::exists(newPath)) {
        newPath = moviesPath + '/' + KFileUtils::suggestName(QUrl::fromLocalFile(newPath), name);
    }

    qDebug() << "recording" << fd << streams.constFirst().nodeId << "into" << newPath;

    m_record = new PipeWireRecord(this);
    m_record->setOutput(newPath);
    if (QFileInfo::exists("/.flatpak-info")) {
        qDebug() << "We are in flatpak, pass the fd" << fd;
        m_record->setFd(fd);
    }
    m_record->setNodeId(streams.constFirst().nodeId);
    m_record->setActive(true);

    connect(m_sni, &KStatusNotifierItem::activateRequested, this, [this] {
        qDebug() << "stop the recoding!" << m_record->output();
        m_record->setActive(false);
    });

    connect(m_record, &PipeWireRecord::errorFound, this, [this] (const QString &error) {
        closeSession();
        qDebug() << "recording error!" << error;
        KNotification *notif = new KNotification("error");
        notif->setComponentName(QStringLiteral("screenrecord"));
        notif->setTitle(i18n("Recording failed"));
        notif->setText(i18n("Could not start recording because: %1", error));
        notif->sendEvent();
        connect(notif, &KNotification::closed, QCoreApplication::instance(), &QCoreApplication::quit);
    });
    connect(m_record, &PipeWireRecord::stateChanged, this, [this] {
        auto state = m_record->state();
        qDebug() << "state changed" << state;
        switch(state) {
            case PipeWireRecord::Idle:
                m_sni->setToolTip("media-record", i18n("Screen Record"), i18n("Setting up..."));
                {
                    closeSession();

                    KNotification *notif = new KNotification("captured");
                    notif->setComponentName(QStringLiteral("screenrecord"));
                    notif->setTitle(i18n("Screen Record"));
                    notif->setText(i18n("New Recording saved in %1", m_record->output()));
                    notif->setUrls({QUrl::fromLocalFile(m_record->output())});
                    notif->sendEvent();
                    connect(notif, &KNotification::closed, QCoreApplication::instance(), &QCoreApplication::quit);
                }
                break;
            case PipeWireRecord::Recording:
                m_sni->setToolTip("media-record", i18n("Screen Record"), i18n("Recording..."));
                break;
            case PipeWireRecord::Rendering:
                m_sni->setToolTip("media-record", i18n("Screen Record"), i18n("Writing file..."));
                break;
        }
    });
}

void ScreenRecord::closeSession()
{
    QDBusMessage closeScreencastSession = QDBusMessage::createMethodCall(QLatin1String("org.freedesktop.portal.Desktop"),
                                            m_path.path(),
                                            QLatin1String("org.freedesktop.portal.Session"),
                                            QLatin1String("Close"));
    QDBusConnection::sessionBus().call(closeScreencastSession);
}
