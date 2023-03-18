add_executable(pwbypass)

qt5_add_dbus_interface(
   XDP_SRCS
   org.freedesktop.portal.ScreenCast.xml
   xdp_dbus_screencast_interface
)

target_sources(pwbypass PRIVATE
    main.cpp
    pwbypass.cpp
    contentswindow.cpp
    x11recordingnotifier.cpp
    ${XDP_SRCS}
)

target_link_libraries(pwbypass
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

install(TARGETS pwbypass ${KDE_INSTALL_TARGETS_DEFAULT_ARGS})
install(PROGRAMS org.kde.xwaylandvideobridge.desktop DESTINATION ${KDE_INSTALL_APPDIR})
install(FILES org.kde.xwaylandvideobridge.appdata.xml DESTINATION ${KDE_INSTALL_METAINFODIR})
