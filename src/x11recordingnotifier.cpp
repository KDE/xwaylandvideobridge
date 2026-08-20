/*
 * SPDX-License-Identifier: LicenseRef-KDE-Accepted-GPL
 * SPDX-FileCopyrightText: 2023 David Edmundson <kde@davidedmundson.co.uk>
 * SPDX-FileCopyrightText: 2023 Aleix Pol <aleixpol@kde.org>
 * SPDX-FileCopyrightText: 2026 Hadi Chokr <hadichokr@icloud.com>
 */

#include "x11recordingnotifier.h"

#include "xwaylandvideobridge_debug.h"

#include <QScopeGuard>
#include <QScopedPointer>
#include <QSocketNotifier>

#include <cstdlib>
#include <cstring>

#include <xcb/composite.h>
#include <xcb/record.h>
#include <xcb/xcb.h>
#include <xcb/xcbext.h>
#include <xcb/xproto.h>

// xcb does not declare the RECORD reply categories.
static constexpr uint8_t s_fromClient = 1;
static constexpr uint8_t s_clientDied = 3;

struct XCBResponse {
    XCBResponse() = default;
    XCBResponse(const XCBResponse &) = delete;
    XCBResponse &operator=(const XCBResponse &) = delete;
    ~XCBResponse();

    void reset();

    xcb_record_enable_context_reply_t *reply = nullptr;
    xcb_generic_error_t *error = nullptr;
};

void XCBResponse::reset()
{
    std::free(reply);
    std::free(error);

    reply = nullptr;
    error = nullptr;
}

XCBResponse::~XCBResponse()
{
    reset();
}

X11RecordingNotifier::X11RecordingNotifier(WId window, QObject *parent)
    : QObject(parent)
    , m_windowId(window)
{
    // A separate connection, as RECORD answers a single request with a stream of replies rather than events.
    m_connection = xcb_connect(nullptr, nullptr);
    if (xcb_connection_has_error(m_connection)) {
        qCWarning(XWAYLANDBRIDGE) << "Could not open a second X11 connection. Auto activation will fail";
        return;
    }
    auto *c = m_connection;

    int compositeOpCode = -1;
    {
        auto cookie = xcb_query_extension(c, strlen("Composite"), "Composite");
        QScopedPointer<xcb_query_extension_reply_t, QScopedPointerPodDeleter> reply(xcb_query_extension_reply(c, cookie, nullptr));
        if (!reply || !reply->present) {
            qCWarning(XWAYLANDBRIDGE) << "Composite extension unavailable. Auto activation will fail";
            return;
        }
        compositeOpCode = reply->major_opcode;
    }

    {
        auto cookie = xcb_record_query_version(c, XCB_RECORD_MAJOR_VERSION, XCB_RECORD_MINOR_VERSION);
        QScopedPointer<xcb_record_query_version_reply_t, QScopedPointerPodDeleter> reply(xcb_record_query_version_reply(c, cookie, nullptr));
        if (!reply) {
            qCWarning(XWAYLANDBRIDGE) << "Record extension unavailable. Auto activation will fail";
            return;
        }
    }

    xcb_record_range_t range = {};
    range.ext_requests.major.first = compositeOpCode;
    range.ext_requests.major.last = compositeOpCode;
    range.ext_requests.minor.first = XCB_COMPOSITE_REDIRECT_WINDOW;
    range.ext_requests.minor.last = XCB_COMPOSITE_UNREDIRECT_SUBWINDOWS;
    range.client_died = true;
    xcb_record_client_spec_t spec = XCB_RECORD_CS_ALL_CLIENTS;

    m_recordingContext = xcb_generate_id(c);
    auto cookie = xcb_record_create_context_checked(c, m_recordingContext, 0, 1, 1, &spec, &range);
    if (auto *error = xcb_request_check(c, cookie)) {
        qCWarning(XWAYLANDBRIDGE) << "Failed to create the recording context";
        std::free(error);
        m_recordingContext = 0;
        return;
    }

    const unsigned int enableCookie = xcb_record_enable_context(c, m_recordingContext).sequence;
    xcb_flush(c);

    auto *notifier = new QSocketNotifier(xcb_get_file_descriptor(c), QSocketNotifier::Read, this);
    connect(notifier, &QSocketNotifier::activated, this, [this, notifier, enableCookie] {
        while (xcb_generic_event_t *event = xcb_poll_for_event(m_connection)) {
            std::free(event);
        }

        XCBResponse record;
        while (xcb_poll_for_reply(m_connection, enableCookie, reinterpret_cast<void **>(&record.reply), &record.error)) {
            if (record.error || !record.reply) {
                break;
            }

            handleNewRecord(*record.reply);
            record.reset();
        }

        // A broken connection leaves the descriptor readable forever.
        if (xcb_connection_has_error(m_connection)) {
            qCWarning(XWAYLANDBRIDGE) << "Lost the recording connection. Auto activation is disabled";
            notifier->setEnabled(false);
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

void X11RecordingNotifier::setWindowId(WId window)
{
    if (m_windowId == window) {
        return;
    }

    const bool wasRedirected = isRedirected();
    m_windowId = window;

    // Counts are per window: whatever was redirecting the old one is not redirecting this one.
    m_redirectionCount.clear();
    if (wasRedirected) {
        Q_EMIT isRedirectedChanged();
    }
}

bool X11RecordingNotifier::isRedirected() const
{
    return !m_redirectionCount.isEmpty();
}

void X11RecordingNotifier::handleNewRecord(xcb_record_enable_context_reply_t &reply)
{
    const bool wasRedirected = isRedirected();
    auto cleanup = qScopeGuard([wasRedirected, this] {
        if (isRedirected() != wasRedirected) {
            Q_EMIT isRedirectedChanged();
        }
    });

    if (reply.category == s_clientDied) {
        m_redirectionCount.remove(reply.xid_base);
        return;
    }

    if (reply.category != s_fromClient) {
        return;
    }

    const uint8_t *data = xcb_record_enable_context_data(&reply);
    int available = xcb_record_enable_context_data_length(&reply);
    bool sawRedirect = false;

    // One reply can carry several requests. Stopping after the first one loses unredirects and leaves the
    // window pinned at capture size for the rest of the session.
    while (data && available >= static_cast<int>(sizeof(xcb_composite_redirect_window_request_t))) {
        auto *request = reinterpret_cast<const xcb_composite_redirect_window_request_t *>(data);
        const int requestSize = request->length * 4;
        if (requestSize < static_cast<int>(sizeof(*request)) || requestSize > available) {
            break;
        }

        data += requestSize;
        available -= requestSize;

        if (request->window != m_windowId) {
            continue;
        }

        switch (request->minor_opcode) {
        case XCB_COMPOSITE_REDIRECT_WINDOW:
        case XCB_COMPOSITE_REDIRECT_SUBWINDOWS:
            m_redirectionCount[reply.xid_base]++;
            sawRedirect = true;
            break;
        case XCB_COMPOSITE_UNREDIRECT_WINDOW:
        case XCB_COMPOSITE_UNREDIRECT_SUBWINDOWS: {
            auto it = m_redirectionCount.find(reply.xid_base);
            if (it != m_redirectionCount.end() && --(*it) <= 0) {
                m_redirectionCount.erase(it);
            }
            break;
        }
        default:
            break;
        }
    }

    // A client that drops and retakes the window in one batch of requests leaves isRedirected() unchanged,
    // so the count alone cannot say that a fresh capture started.
    if (sawRedirect && isRedirected()) {
        Q_EMIT redirectRequested();
    }
}
