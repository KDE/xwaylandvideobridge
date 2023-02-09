add_executable(pwbypass)

qt5_add_dbus_interface(
   XDP_SRCS
   org.freedesktop.portal.ScreenCast.xml
   xdp_dbus_screencast_interface
)

target_sources(pwbypass PRIVATE
    main.cpp
    pwbypass.cpp
    ${XDP_SRCS}
)

target_link_libraries(pwbypass
    KF5::I18n
    KF5::CoreAddons
    KF5::WindowSystem
    Qt5::Quick
    Qt5::DBus
    K::KPipeWireRecord
)

install(FILES pwbypass.notifyrc DESTINATION ${KDE_INSTALL_KNOTIFYRCDIR})
install(TARGETS pwbypass ${KDE_INSTALL_TARGETS_DEFAULT_ARGS})
install(PROGRAMS org.kde.pwbypass.desktop DESTINATION ${KDE_INSTALL_APPDIR})
install(FILES org.kde.pwbypass.appdata.xml DESTINATION ${KDE_INSTALL_METAINFODIR})
