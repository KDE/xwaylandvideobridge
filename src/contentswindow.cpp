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
#include <QtGui/qguiapplication_platform.h>

#include <cstring>
#include <xcb/xcb.h>
#include <xcb/xfixes.h>

// Big enough to be recognisable in an X11 share dialog, small enough not to matter if it is ever left behind.
static const QSize s_idleSize(640, 360);

static xcb_connection_t *xcbConnection()
{
    auto *x11 = qGuiApp->nativeInterface<QNativeInterface::QX11Application>();
    return x11 ? x11->connection() : nullptr;
}

static xcb_atom_t internAtom(xcb_connection_t *connection, const char *name)
{
    QScopedPointer<xcb_intern_atom_reply_t, QScopedPointerPodDeleter> reply(
        xcb_intern_atom_reply(connection, xcb_intern_atom(connection, false, strlen(name), name), nullptr));
    return reply ? reply->atom : XCB_ATOM_NONE;
}

static bool haveXFixes(xcb_connection_t *connection)
{
    // Not xcb_xfixes_query_version(): the negotiated version is per client, and the platform plugin has
    // already negotiated one on this connection for its own use.
    const xcb_query_extension_reply_t *extension = xcb_get_extension_data(connection, &xcb_xfixes_id);
    return extension && extension->present;
}

ContentsWindow::ContentsWindow()
{
    if (!KWindowSystem::isPlatformX11()) {
        return;
    }

    setColor(Qt::black);
    setOpacity(0);

    // Flags have to be in place before the native window is created.
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

    // Qt destroys and recreates the native window when the screen it sits on is removed, so whatever is
    // watching the old id has to be told about the new one.
    m_windowId = windowId;
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

    // A window whose frame matches an output makes Mutter report that output as fullscreen, which hides the
    // GNOME panel, and makes KWin maximise the window.
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

    // Never NET::FullScreen. Mutter treats a fullscreen window as owning its output and hides the GNOME panel
    // for it, and KWin lifts fullscreen windows out of the below layer.
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

    // Qt turns Qt::WindowTransparentForInput into an input region while it sets the native window up and
    // nothing re-applies it afterwards, so set the region here instead of assuming that happened.
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
