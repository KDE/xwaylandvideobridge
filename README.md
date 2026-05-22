<!--
SPDX-License-Identifier: BSD-3-Clause
SPDX-FileCopyrightText: 2023 David Edmundson <kde@davidedmundson.co.uk>
SPDX-FileCopyrightText: 2023 Aleix Pol <aleixpol@kde.org>
SPDX-FileCopyrightText: 2026 Hadi Chokr <hadichokr@icloud.com>
-->

# XWayland Video Bridge

## About

By design, X11 applications cannot access window or screen contents for Wayland clients. This is fine in principle, but it breaks screen sharing in tools like Discord, MS Teams, Skype, Webex, and others.

This tool allows you to share specific windows with X11 clients, while keeping the user in full control at all times.

## How to use

xwaylandvideobridge should autostart on login. It will run silently in the background. The next time you try to share a window, a prompt will appear asking you to select what to share. The previously selected window will then be available for sharing in the X11 application.

The system tray icon provides finer control over the bridge.

## Use outside Plasma

This should work on any desktop that supports XDG Desktop Portals and PipeWire streaming and has a working system tray.

## Future

Ideally this should be more automatic, but this tool aims purely to serve as a stop-gap whilst we wait for these clients to get native Wayland support and for the surrounding Wayland protocols to mature. How much further it gets developed depends on feedback and how the surrounding ecosystem evolves.

# Release Process

- Check it works
- appstream-metainfo-release-update --version 0.5.0 -d today ./src/org.kde.xwaylandvideobridge.appdata.xml
- Update set(PROJECT_VERSION "0.5.0") in CMakeLists.txt
- In Releaseme  mkdir xwaylandvideobridge; cd xwaylandvideobridge; ../tarme.rb --origin trunk --version 0.5.0 xwaylandvideobridge
- scp xwaylandvideobridge ftpadmin@tinami.kde.org:  and move to right place
- Write a blog post and put on kde-announce
