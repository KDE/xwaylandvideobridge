/*
 * SPDX-License-Identifier: LicenseRef-KDE-Accepted-GPL
 * SPDX-FileCopyrightText: 2023 David Edmundson <kde@davidedmundson.co.uk>
 * SPDX-FileCopyrightText: 2023 Aleix Pol <aleixpol@kde.org>
 * SPDX-FileCopyrightText: 2026 Hadi Chokr <hadichokr@icloud.com>
 */

#include "contentswindow.h"

#include <KLocalizedString>
#include <KWindowSystem>
#include <KX11Extras>

#include <QCloseEvent>
#include <QExposeEvent>
#include <QGuiApplication>
#include <QResizeEvent>
#include <QScopedPointer>
#include <QScreen>
#include <QVarLengthArray>
#include <QtGui/qguiapplication_platform.h>

#include <cstring>
#include <xcb/xcb.h>
#include <xcb/xfixes.h>

// Kept small so it's harmless if it's left behind or stops being click-through.
static const QSize s_idleSize(64, 36);

static xcb_connection_t *xcbConnection()
{
    auto *x11 = qGuiApp->nativeInterface<QNativeInterface::QX11Application>();
    return x11 ? x11->connection() : nullptr;
}

static xcb_atom_t internAtom(xcb_connection_t *connection, const char *name)
{
    QScopedPointer<xcb_intern_atom_reply_t, QScopedPointerPodDeleter> reply(
        xcb_intern_atom_reply(connection, xcb_intern_atom(connection, false, strlen(name), name), nullptr));
    return reply ? reply->atom : static_cast<xcb_atom_t>(XCB_ATOM_NONE);
}

static bool haveXFixes(xcb_connection_t *connection)
{
    // Not xcb_xfixes_query_version(): the negotiated version is per client, and the platform plugin has
    // already negotiated one on this connection for its own use.
    const xcb_query_extension_reply_t *extension = xcb_get_extension_data(connection, &xcb_xfixes_id);
    return extension && extension->present;
}

// Qt::WindowDoesNotAcceptFocus only clears WM_HINTS.input, the xcb backend still advertises
// WM_TAKE_FOCUS. KWin's wantsInput() is true for anything advertising it whatever the input hint says,
// so directional window switching can focus this invisible window. Keep the other atoms: we need
// WM_DELETE_WINDOW for closeEvent() and _NET_WM_PING for hang detection.
static void removeTakeFocusProtocol(xcb_connection_t *connection, xcb_window_t window)
{
    static const xcb_atom_t protocolsAtom = internAtom(connection, "WM_PROTOCOLS");
    static const xcb_atom_t takeFocusAtom = internAtom(connection, "WM_TAKE_FOCUS");
    if (protocolsAtom == XCB_ATOM_NONE || takeFocusAtom == XCB_ATOM_NONE) {
        return;
    }

    QScopedPointer<xcb_get_property_reply_t, QScopedPointerPodDeleter> reply(
        xcb_get_property_reply(connection, xcb_get_property(connection, false, window, protocolsAtom, XCB_ATOM_ATOM, 0, 1024), nullptr));
    if (!reply || reply->type != XCB_ATOM_ATOM || reply->format != 32) {
        return;
    }

    const auto *atoms = static_cast<const xcb_atom_t *>(xcb_get_property_value(reply.data()));
    const int count = xcb_get_property_value_length(reply.data()) / int(sizeof(xcb_atom_t));

    QVarLengthArray<xcb_atom_t, 8> kept;
    for (int i = 0; i < count; ++i) {
        if (atoms[i] != takeFocusAtom) {
            kept.append(atoms[i]);
        }
    }
    if (kept.size() == count) {
        return;
    }

    xcb_change_property(connection, XCB_PROP_MODE_REPLACE, window, protocolsAtom, XCB_ATOM_ATOM, 32, kept.size(), kept.constData());
    xcb_flush(connection);
}

ContentsWindow::ContentsWindow()
{
    if (!KWindowSystem::isPlatformX11()) {
        return;
    }

    setColor(Qt::black);
    setOpacity(0);

    // Set before create() so the native window starts out with them.
    setFlag(Qt::WindowDoesNotAcceptFocus);
    setFlag(Qt::WindowTransparentForInput);
    setFlag(Qt::FramelessWindowHint);

    create();
    goIdle();
    trackScreen(screen());

    connect(this, &QWindow::screenChanged, this, [this](QScreen *newScreen) {
        trackScreen(newScreen);
        applyGeometry();
    });
    connect(this, &QWindow::visibleChanged, this, [this](bool visible) {
        if (visible) {
            applyWindowState();
        }
    });
}

void ContentsWindow::showForStream(const QSize &streamSize)
{
    m_streamSize = streamSize;
    applyGeometry();
    show();
}

void ContentsWindow::goIdle()
{
    setTitle(i18n("Wayland to X Recording bridge"));
    m_streamSize = QSize();
    applyGeometry();
}

void ContentsWindow::trackScreen(QScreen *newScreen)
{
    disconnect(m_screenConnection);
    if (newScreen) {
        m_screenConnection = connect(newScreen, &QScreen::availableGeometryChanged, this, &ContentsWindow::applyGeometry);
    }
}

void ContentsWindow::syncWindowId()
{
    const WId windowId = winId();
    if (windowId == m_windowId) {
        return;
    }

    // Qt can recreate the native window, so whoever watches the old id needs the new one.
    m_windowId = windowId;

    // Qt writes WM_PROTOCOLS only in create(), so fix it up here, while the window is still unmapped.
    if (auto *connection = xcbConnection()) {
        removeTakeFocusProtocol(connection, m_windowId);
    }

    Q_EMIT windowIdChanged(m_windowId);
}

void ContentsWindow::applyGeometry()
{
    const QSize wanted = m_streamSize.isEmpty() ? s_idleSize : m_streamSize / devicePixelRatio();
    resize(constrainToScreen(wanted));
    applyWindowState();
}

QSize ContentsWindow::constrainToScreen(const QSize &size) const
{
    const QScreen *currentScreen = screen();
    if (!currentScreen) {
        return size;
    }

    // Stay inside the work area. Mutter treats an output-sized window as fullscreen and hides the panel,
    // and KWin maximises new windows that fill the work area.
    const QSize limit = currentScreen->availableSize() - QSize(1, 1);
    if (limit.isEmpty()) {
        return size;
    }
    if (size.width() <= limit.width() && size.height() <= limit.height()) {
        return size;
    }
    return size.scaled(limit, Qt::KeepAspectRatio);
}

void ContentsWindow::applyWindowState()
{
    auto *connection = xcbConnection();
    if (!connection) {
        return;
    }

    syncWindowId();

    static const xcb_atom_t windowType = internAtom(connection, "_NET_WM_WINDOW_TYPE");
    static const xcb_atom_t normalType = internAtom(connection, "_NET_WM_WINDOW_TYPE_NORMAL");
    xcb_change_property(connection, XCB_PROP_MODE_REPLACE, m_windowId, windowType, XCB_ATOM_ATOM, 32, 1, &normalType);

    // No fullscreen or maximised state, the WM would resize us to the output or work area.
    // Mutter also hides the panel for fullscreen windows.
    KX11Extras::clearState(m_windowId, NET::FullScreen | NET::Max);
    KX11Extras::setState(m_windowId, NET::SkipTaskbar | NET::SkipPager | NET::SkipSwitcher | NET::KeepBelow);

    clearInputRegion();
}

void ContentsWindow::clearInputRegion()
{
    auto *connection = xcbConnection();
    if (!connection || !haveXFixes(connection)) {
        return;
    }

    // Qt already sets this on create. We redo it anyway since click-through depends on it and it's cheap.
    const xcb_xfixes_region_t region = xcb_generate_id(connection);
    xcb_xfixes_create_region(connection, region, 0, nullptr);
    xcb_xfixes_set_window_shape_region(connection, winId(), XCB_SHAPE_SK_INPUT, 0, 0, region);
    xcb_xfixes_destroy_region(connection, region);
    xcb_flush(connection);
}

void ContentsWindow::exposeEvent(QExposeEvent *event)
{
    QQuickWindow::exposeEvent(event);

    const bool exposed = isExposed();
    if (exposed && !m_exposed) {
        syncWindowId();
        clearInputRegion();
    }
    m_exposed = exposed;
}

void ContentsWindow::resizeEvent(QResizeEvent *event)
{
    QQuickWindow::resizeEvent(event);
    clearInputRegion();
}

void ContentsWindow::closeEvent(QCloseEvent *event)
{
    event->ignore();
    Q_EMIT mirrorWindowClosed();
}
