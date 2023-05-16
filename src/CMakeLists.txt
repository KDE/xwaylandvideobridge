add_executable(xwaylandvideobridge)

qt5_add_dbus_interface(
   XDP_SRCS
   org.freedesktop.portal.ScreenCast.xml
   xdp_dbus_screencast_interface
)

ecm_qt_declare_logging_category(XDP_SRCS HEADER xwaylandvideobridge_debug.h IDENTIFIER XWAYLANDBRIDGE CATEGORY_NAME org.kde.xwaylandvideobridge DESCRIPTION "Xwayland Video Bridge" EXPORT XWAYLANDVIDEOBRIDGE)
ecm_qt_install_logging_categories(EXPORT XWAYLANDVIDEOBRIDGE FILE xwaylandvideobridge.categories DESTINATION ${KDE_INSTALL_LOGGINGCATEGORIESDIR})

target_sources(xwaylandvideobridge PRIVATE
    main.cpp
    xwaylandvideobridge.cpp
    contentswindow.cpp
    x11recordingnotifier.cpp
    ${XDP_SRCS}
)

target_link_libraries(xwaylandvideobridge
    KF5::I18n
    KF5::CoreAddons
    KF5::WindowSystem
    KF5::Notifications
    Qt5::Quick
    Qt5::DBus
    Qt5::X11Extras
    K::KPipeWireRecord
    XCB::XCB
    XCB::COMPOSITE
    XCB::RECORD
)

install(TARGETS xwaylandvideobridge ${KDE_INSTALL_TARGETS_DEFAULT_ARGS})
install(PROGRAMS org.kde.xwaylandvideobridge.desktop DESTINATION ${KDE_INSTALL_APPDIR})
install(FILES org.kde.xwaylandvideobridge.appdata.xml DESTINATION ${KDE_INSTALL_METAINFODIR})
