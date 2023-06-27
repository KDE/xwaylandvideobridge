/*
 * Copyright 2023 David Edmundson <davidedmundson@kde.org>
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

#include "contentswindow.h"

#include <KLocalizedString>
#include <KWindowSystem>

#include <QX11Info>

struct MotifHints
{
    u_int32_t flags = 0;
    u_int32_t functions = 0;
    u_int32_t decorations = 0;
    int32_t inputMode = 0;
    u_int32_t status = 0;
};

struct GtkFrameExtents
{
    uint32_t left = 0;
    uint32_t right = 0;
    uint32_t top = 0;
    uint32_t bottom = 0;
};

static xcb_atom_t intern_atom(xcb_connection_t *c, const char *name)
{
    xcb_atom_t atom = XCB_NONE;
    QScopedPointer<xcb_intern_atom_reply_t, QScopedPointerPodDeleter> reply(xcb_intern_atom_reply(c, xcb_intern_atom(c, false, strlen(name), name), nullptr));
    if (reply) {
        atom = reply->atom;
    }
    return atom;
}

ContentsWindow::ContentsWindow()
{
    resize(QSize(100, 100));
    setTitle(i18n("Wayland to X Recording bridge"));

    setOpacity(0);
    setFlag(Qt::WindowDoesNotAcceptFocus);
    setFlag(Qt::WindowTransparentForInput);
    KWindowSystem::setState(winId(), NET::SkipTaskbar | NET::SkipPager);

    // remove decoration. We can't use the Qt helper as we need our window type to remain something
    // that keeps us valid for streams
    MotifHints hints;
    hints.flags = 2;
    xcb_atom_t motif_hints_atom = intern_atom(QX11Info::connection(), "_MOTIF_WM_HINTS");
    xcb_change_property(QX11Info::connection(), XCB_PROP_MODE_REPLACE, winId(), motif_hints_atom, motif_hints_atom, 32, 5, (const void *)&hints);

    handleResize();
}

void ContentsWindow::resize(const QSize &size)
{
    if (size.isEmpty()) {
        return;
    }
    QQuickWindow::resize(size);
    handleResize();
}

void ContentsWindow::handleResize()
{
    // In theory this isn't needed - we're transparent for input, but a bug in either Xorg or kwin means
    // that it doesn't. If we forward to X it falls to the X window underneath, but kwin still "knows" theres
    // an xwayland surface there and sends to X and not the right client
    // Solvable by either kwin checking event mask or Xserver setting a null input_region based on the event mask

    // but I want things now! so instead do a hack of moving things offscreen
    // hopefully drop for a Plasma 6

    // if we blindly just set oursevles to offscreen, kwin will try to position us back
    // if we set override-redirect screencast clients skip us.

    // frames can be offscreen though, so lets make the whole window part of the frame and then
    // put that bit of "frame" offscreen

    // Don't let window manager devs see this
    GtkFrameExtents frame;
    frame.top = height() - 1;
    frame.left = width() -1 ;
    setX(-width() + 1); // unforutnately we still need 1px on screen to get callbacks

    xcb_atom_t gtk_frame_extent_atom = intern_atom(QX11Info::connection(), "_GTK_FRAME_EXTENTS");
    xcb_change_property(QX11Info::connection(), XCB_PROP_MODE_REPLACE, winId(), gtk_frame_extent_atom, XCB_ATOM_CARDINAL, 32, 4, (const void *)&frame);
}
