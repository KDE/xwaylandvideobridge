# SPDX-License-Identifier: BSD-3-Clause
# SPDX-FileCopyrightText: 2023 David Edmundson <kde@davidedmundson.co.uk>
# SPDX-FileCopyrightText: 2023 Aleix Pol <aleixpol@kde.org>

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
    KF${QT_MAJOR_VERSION}::I18n
    KF${QT_MAJOR_VERSION}::CoreAddons
    KF${QT_MAJOR_VERSION}::WindowSystem
    KF${QT_MAJOR_VERSION}::Crash
    Qt${QT_MAJOR_VERSION}::Quick
    Qt${QT_MAJOR_VERSION}::DBus
    Qt${QT_MAJOR_VERSION}::Widgets
    K::KPipeWireRecord
    XCB::XCB
    XCB::COMPOSITE
    XCB::RECORD
)

if(QT_MAJOR_VERSION STREQUAL "5")
    target_link_libraries(xwaylandvideobridge Qt5::X11Extras)
endif()

install(TARGETS xwaylandvideobridge ${KDE_INSTALL_TARGETS_DEFAULT_ARGS})
install(PROGRAMS org.kde.xwaylandvideobridge.desktop DESTINATION ${KDE_INSTALL_APPDIR})
install(FILES org.kde.xwaylandvideobridge.desktop DESTINATION ${KDE_INSTALL_AUTOSTARTDIR})
install(FILES org.kde.xwaylandvideobridge.appdata.xml DESTINATION ${KDE_INSTALL_METAINFODIR})
