/*
 * SPDX-License-Identifier: LicenseRef-KDE-Accepted-GPL
 * SPDX-FileCopyrightText: 2023 David Edmundson <kde@davidedmundson.co.uk>
 * SPDX-FileCopyrightText: 2023 Aleix Pol <aleixpol@kde.org>
 * SPDX-FileCopyrightText: 2026 Hadi Chokr <hadichokr@icloud.com>
 */

#pragma once

#include <QHash>
#include <QObject>
#include <QWindow>

#include <xcb/record.h>

class X11RecordingNotifier : public QObject
{
    Q_OBJECT
public:
    X11RecordingNotifier(WId window, QObject *parent);
    ~X11RecordingNotifier() override;

    void setWindowId(WId window);
    bool isRedirected() const;

Q_SIGNALS:
    void isRedirectedChanged();
    void redirectRequested();

private:
    void handleNewRecord(xcb_record_enable_context_reply_t &reply);

    xcb_connection_t *m_connection = nullptr;
    xcb_record_context_t m_recordingContext = 0;
    WId m_windowId = 0;
    QHash<uint32_t /*client XID base*/, int /*count*/> m_redirectionCount;
};
