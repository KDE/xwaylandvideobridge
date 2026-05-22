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
#include <QGuiApplication>
#include <QScreen>

#include <xcb/xcb.h>

static xcb_connection_t *xcbConnection()
{
    auto *x11 = qApp->nativeInterface<QNativeInterface::QX11Application>();
    return x11 ? x11->connection() : nullptr;
}

static xcb_atom_t intern_atom(xcb_connection_t *c, const char *name)
{
    xcb_atom_t atom = XCB_NONE;
    QScopedPointer<xcb_intern_atom_reply_t, QScopedPointerPodDeleter> reply(
        xcb_intern_atom_reply(c, xcb_intern_atom(c, false, strlen(name), name),
                              nullptr));
    if (reply)
        atom = reply->atom;
    return atom;
}

ContentsWindow::ContentsWindow()
{
    if (!KWindowSystem::isPlatformX11())
        return;

    setTitle(i18n("Wayland to X Recording bridge"));
    setColor(Qt::black);
    setOpacity(0);
    setFlag(Qt::WindowDoesNotAcceptFocus);
    setFlag(Qt::WindowTransparentForInput);
    KX11Extras::setState(winId(),
                         NET::SkipTaskbar | NET::SkipPager | NET::SkipSwitcher);

    auto *c = xcbConnection();

    xcb_atom_t net_wm_window_type = intern_atom(c, "_NET_WM_WINDOW_TYPE");
    xcb_atom_t wm_type_normal = intern_atom(c, "_NET_WM_WINDOW_TYPE_NORMAL");
    xcb_change_property(c, XCB_PROP_MODE_REPLACE, winId(), net_wm_window_type,
                        XCB_ATOM_ATOM, 32, 1, &wm_type_normal);

    QRect combined;
    for (QScreen *s : QGuiApplication::screens())
        combined = combined.united(s->geometry());
    setPosition(combined.topLeft());
    QQuickWindow::resize(combined.size());

    showNormal();
    KX11Extras::setState(winId(), NET::KeepBelow | NET::FullScreen |
    NET::SkipTaskbar | NET::SkipPager |
    NET::SkipSwitcher);
}

void ContentsWindow::closeEvent(QCloseEvent *event)
{
    event->ignore();
    Q_EMIT mirrorWindowClosed();
}
