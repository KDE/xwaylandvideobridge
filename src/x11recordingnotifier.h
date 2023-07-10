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
    ~X11RecordingNotifier();

    bool isRedirected() const;
Q_SIGNALS:
    void isRedirectedChanged();
private:
    void handleNewRecord(xcb_record_enable_context_reply_t *reply);

    xcb_connection_t *m_connection = nullptr;
    xcb_record_context_t m_recordingContext = 0;
    WId m_windowId = 0; // my xterm
    QHash<uint32_t /**called XID*/, int /*count*/> m_redirectionCount;
};
