# SPDX-License-Identifier: BSD-3-Clause
# SPDX-FileCopyrightText: 2023 David Edmundson <kde@davidedmundson.co.uk>
# SPDX-FileCopyrightText: 2023 Aleix Pol <aleixpol@kde.org>
# SPDX-FileCopyrightText: 2026 Hadi Chokr <hadichokr@icloud.com>

add_executable(xwaylandvideobridge)

qt_add_dbus_interface(
    XDP_SRCS
    org.freedesktop.portal.ScreenCast.xml
    xdp_dbus_screencast_interface
)

ecm_qt_install_logging_categories(EXPORT XWAYLANDVIDEOBRIDGE FILE xwaylandvideobridge.categories DESTINATION ${KDE_INSTALL_LOGGINGCATEGORIESDIR})
ecm_qt_declare_logging_category(XDP_SRCS HEADER xwaylandvideobridge_debug.h IDENTIFIER XWAYLANDBRIDGE CATEGORY_NAME org.kde.xwaylandvideobridge DESCRIPTION "Xwayland Video Bridge" EXPORT XWAYLANDVIDEOBRIDGE)

target_sources(xwaylandvideobridge PRIVATE
    main.cpp
    xwaylandvideobridge.cpp xwaylandvideobridge.h
    contentswindow.cpp contentswindow.h
    x11recordingnotifier.cpp x11recordingnotifier.h
    ${XDP_SRCS}
)

configure_file(version.h.in version.h)

target_link_libraries(xwaylandvideobridge
    KF6::I18n
    KF6::CoreAddons
    KF6::WindowSystem
    KF6::Crash
    KF6::StatusNotifierItem
    Qt6::Quick
    Qt6::DBus
    Qt6::Widgets
    K::KPipeWireRecord
    XCB::XCB
    XCB::COMPOSITE
    XCB::RECORD
    XCB::XFIXES
)

install(TARGETS xwaylandvideobridge ${KDE_INSTALL_TARGETS_DEFAULT_ARGS})
install(PROGRAMS org.kde.xwaylandvideobridge.desktop DESTINATION ${KDE_INSTALL_APPDIR})
install(FILES org.kde.xwaylandvideobridge.desktop DESTINATION ${KDE_INSTALL_AUTOSTARTDIR})
install(FILES org.kde.xwaylandvideobridge.appdata.xml DESTINATION ${KDE_INSTALL_METAINFODIR})
