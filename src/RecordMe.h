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

#pragma once

#include <QObject>
#include <QDBusObjectPath>
#include <KStatusNotifierItem>
#include <PipeWireRecord>

class QTimer;
class QQmlApplicationEngine;

struct Stream {
    uint nodeId;
    QVariantMap opts;
};

class OrgFreedesktopPortalScreenCastInterface;

class RecordMe : public QObject
{
    Q_OBJECT
public:
    RecordMe(QObject* parent = nullptr);
    ~RecordMe() override;

    enum CursorModes {
        Hidden = 1,
        Embedded = 2,
        Metadata = 4
    };
    Q_ENUM(CursorModes);

    enum SourceTypes {
        Monitor = 1,
        Window = 2
    };
    Q_ENUM(SourceTypes);

    void setDuration(int duration);

public Q_SLOTS:
    void response(uint code, const QVariantMap& results);

private:
    void init(const QDBusObjectPath &path);
    void handleStreams(const QVector<Stream> &streams);
    void start();

    OrgFreedesktopPortalScreenCastInterface *iface;
    QDBusObjectPath m_path;
    const QString m_handleToken;
    KStatusNotifierItem *const m_sni;
    PipeWireRecord *m_record = nullptr;
};
