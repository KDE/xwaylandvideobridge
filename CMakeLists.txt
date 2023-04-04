cmake_minimum_required(VERSION 3.16)

project(xwaylandvideobridge)

set(QT_MIN_VERSION "5.15.0")
set(KF_MIN_VERSION "5.83.0")

find_package(ECM ${KF_MIN_VERSION} REQUIRED NO_MODULE)
set(CMAKE_MODULE_PATH ${ECM_MODULE_PATH})

include(KDEInstallDirs)
include(KDECMakeSettings)
include(KDECompilerSettings NO_POLICY_SCOPE)
include(ECMInstallIcons)
include(FeatureSummary)

find_package(Qt5 ${QT_MIN_VERSION} CONFIG REQUIRED COMPONENTS Quick DBus X11Extras)

find_package(KF5 ${KF_MIN_VERSION} REQUIRED COMPONENTS
    CoreAddons
    I18n
    WindowSystem
    Notifications
    WidgetsAddons
)

find_package(XCB COMPONENTS REQUIRED XCB COMPOSITE RECORD XFIXES)

find_package(KPipeWire REQUIRED)

add_subdirectory(src)
add_subdirectory(icons)

# Make it possible to use the po files fetched by the fetch-translations step
ki18n_install(po)

feature_summary(WHAT ALL INCLUDE_QUIET_PACKAGES FATAL_ON_MISSING_REQUIRED_PACKAGES)
