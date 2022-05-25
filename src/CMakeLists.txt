add_executable(screenrecord)

qt5_add_dbus_interface(
   XDP_SRCS
   org.freedesktop.portal.ScreenCast.xml
   xdp_dbus_screencast_interface
)

target_sources(screenrecord PRIVATE
    main.cpp
    RecordMe.cpp
    ${XDP_SRCS}
)

target_link_libraries(screenrecord
    KF5::Notifications
    KF5::I18n
    KF5::CoreAddons
    Qt5::Gui
    Qt5::DBus
    K::KPipeWireRecord
)

install(TARGETS screenrecord ${KDE_INSTALL_TARGETS_DEFAULT_ARGS})
install(PROGRAMS org.example.screenrecord.desktop DESTINATION ${KDE_INSTALL_APPDIR})
install(FILES org.example.screenrecord.appdata.xml DESTINATION ${KDE_INSTALL_METAINFODIR})
