<!--
SPDX-License-Identifier: BSD-3-Clause
SPDX-FileCopyrightText: 2023 David Edmundson <kde@davidedmundson.co.uk>
SPDX-FileCopyrightText: 2023 Aleix Pol <aleixpol@kde.org>
-->

# XWayland Video Bridge

# About

By design, X11 applications can't access window or screen contents for wayland clients. This is fine in principle, but it breaks screen sharing in tools like Discord, MS Teams, Skype, etc and more.

This tool allows us to share specific windows to X11 clients, but within the control of the user at all times.

# How to use

xwaylandvideobridge should autostart on login. It will run in the background. Next time you try to share a window a prompt will appear.
The previously selected window should now be available for sharing. The title will always be "Wayland to X11 bridge" no matter what window is selected.

The system tray icon provides finer control.

# Compilation

Right now this requires a specific hash of kpipewire until we bump this to Qt6

# Use outside Plasma

This should work on any desktop that supports the Xdg desktop portals and pipewire streaming and have a working system tray.

# Future

Ideally this should be more automatic, but this tool aims purely to serve as a stop-gap whilst we wait for these clients to get native wayland support and for the surrounding wayland protocols to be better. How much more it gets developed depends on feedback and how the surrounding ecosystem evolves.

# Release Process

- Check it works
- appstream-metainfo-release-update --version 0.3.0 -d today ./src/org.kde.xwaylandvideobridge.appdata.xml
- Update set(PROJECT_VERSION "0.3.0") in CMakeLists.txt
- In Releaseme  mkdir xwaylandvideobridge; cd xwaylandvideobridge; ../tarme.rb --origin trunk --version 0.3.0 xwaylandvideobridge
- scp xwaylandvideobridge ftpadmin@tinami.kde.org:  and move to right place
- Write a blog post and put on kde-announce
