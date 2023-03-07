#include "x11recordingnotifier.h"

#include <stdio.h>
#include <stdlib.h>
#include <xcb/xcb.h>
#include <xcb/xproto.h>
#include <xcb/composite.h>
#include <xcb/xcb_event.h>
#include <xcb/xcbext.h>

#include <xcb/record.h>
#include <string.h>

#include <QScopedPointer>
#include <QDebug>
#include <QSocketNotifier>
#include <QScopeGuard>

// next steps:
// - merge with our other code
// - introduce some state logic there
// - ship it!


X11RecordingNotifier::X11RecordingNotifier(WId window, QObject *parent)
    : QObject(parent)
    , m_windowId(window)
{
    // we use a separate connection as the X11 recording API blocks is super weird
    // and we get multiple replies to a request rather than events
    m_connection = xcb_connect(NULL, NULL);
    auto c = m_connection;
    xcb_generic_error_t *error;

    if (!c) {
        qWarning("Error to open local display. Auto activation will fail!\n");
        return;
    }

    int compositeExtensionOpCode = -1;
    {
        xcb_query_extension_cookie_t cookie = xcb_query_extension(c, strlen("Composite"), "Composite");
        QScopedPointer<xcb_query_extension_reply_t, QScopedPointerPodDeleter> reply(xcb_query_extension_reply(c, cookie, NULL));
        compositeExtensionOpCode = reply->major_opcode;
    }

    // check the xcb_record extension exists
    {
        auto cookie = xcb_record_query_version(c, 0, 0);
        QScopedPointer<xcb_record_query_version_reply_t, QScopedPointerPodDeleter> reply(xcb_record_query_version_reply(c, cookie, &error));
        if (!reply) {
            qWarning() << ("Failed to create recording context");
        } else {
        }
    }

    xcb_record_range_t range = {0};
    range.ext_requests.major.first = compositeExtensionOpCode;
    range.ext_requests.major.last = compositeExtensionOpCode;
    range.ext_requests.minor.first = XCB_COMPOSITE_REDIRECT_WINDOW;
    range.ext_requests.minor.last = XCB_COMPOSITE_UNREDIRECT_SUBWINDOWS;
    range.client_died = true;
    xcb_record_client_spec_t spec = XCB_RECORD_CS_ALL_CLIENTS;

    m_recordingContext = xcb_generate_id(c);
    auto cookie = xcb_record_create_context_checked(c, m_recordingContext, 0, 1, 1, &spec, &range);
    auto err = xcb_request_check(c, cookie);
    if (err) {
        qWarning() << ("Failed to create recording context");
    }

    auto enableCookie = xcb_record_enable_context(c, m_recordingContext).sequence;
    xcb_flush(c);

    auto notifier = new QSocketNotifier(xcb_get_file_descriptor(c), QSocketNotifier::Read, this);
    connect(notifier, &QSocketNotifier::activated, this, [this, enableCookie] {
        xcb_generic_event_t *event;
        auto c = m_connection;
        while ((event = xcb_poll_for_event(m_connection))) {
            std::free(event);
        }
        xcb_record_enable_context_reply_t *reply = nullptr;
        xcb_generic_error_t *error = nullptr;
        while (enableCookie && xcb_poll_for_reply(c, enableCookie, (void **)&reply, &error)) {
            // xcb_poll_for_reply may set both reply and error to null if connection has error.
            // break if xcb_connection has error, no point to continue anyway.
            if (xcb_connection_has_error(c)) {
                break;
            }

            if (error) {
                std::free(error);
                break;
            }

            if (!reply) {
                continue;
            }

            handleNewRecord(reply);
            std::free(reply);
        }
    });
}

X11RecordingNotifier::~X11RecordingNotifier()
{
    if (m_recordingContext) {
        xcb_record_free_context(m_connection, m_recordingContext);
    }
    if (m_connection) {
        xcb_disconnect(m_connection);
    }
}

bool X11RecordingNotifier::isRedirected() const
{
    return m_redirectionCount.isEmpty();
}

void X11RecordingNotifier::handleNewRecord(xcb_record_enable_context_reply_t *reply)
{
    const bool wasRedirected = isRedirected();
    auto cleanup = qScopeGuard([wasRedirected, this] {
        if (isRedirected() != wasRedirected) {
            Q_EMIT isRedirectedChanged();
        }
    });

    if (reply->category == 3) {
        m_redirectionCount.take(reply->xid_base);
        return;
    }

    if (reply->category != 1) {
        return;
    }

    xcb_composite_redirect_window_request_t *request = reinterpret_cast<xcb_composite_redirect_window_request_t *>(xcb_record_enable_context_data(reply));

    if (!request) {
        return;
    }

    if (request->window != m_windowId) {
        return;
    }

    uint32_t caller = reply->xid_base;
    switch(request->minor_opcode) {
    case XCB_COMPOSITE_REDIRECT_WINDOW:
    case XCB_COMPOSITE_REDIRECT_SUBWINDOWS:
        m_redirectionCount[caller]++;
        break;
    case XCB_COMPOSITE_UNREDIRECT_WINDOW:
    case XCB_COMPOSITE_UNREDIRECT_SUBWINDOWS:
        if (m_redirectionCount[caller]-- == 0) {
            m_redirectionCount.remove(caller);
        }
        break;
    default:
        break;
    }
}
