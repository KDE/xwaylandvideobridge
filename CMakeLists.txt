# SPDX-License-Identifier: BSD-3-Clause
# SPDX-FileCopyrightText: 2023 David Edmundson <kde@davidedmundson.co.uk>
# SPDX-FileCopyrightText: 2023 Aleix Pol <aleixpol@kde.org>

cmake_minimum_required(VERSION 3.16)

project(xwaylandvideobridge)

set(PROJECT_VERSION "0.4.0")
set(QT_MIN_VERSION "5.15.0")
set(KF_MIN_VERSION "5.83.0")

find_package(ECM ${KF_MIN_VERSION} REQUIRED NO_MODULE)
set(CMAKE_MODULE_PATH ${ECM_MODULE_PATH})

include(KDEInstallDirs)
include(KDECMakeSettings)
include(KDECompilerSettings NO_POLICY_SCOPE)
include(ECMInstallIcons)
include(ECMQtDeclareLoggingCategory)
include(FeatureSummary)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(QT_MAJOR_VERSION STREQUAL "6")
    set(REQUIRED_QT_VERSION "6.4")
    set(KF_MIN_VERSION "5.240.0")
    set(KF_MAJOR_VERSION "6")
else()
    set(KF_MAJOR_VERSION "5")
endif()


find_package(Qt${QT_MAJOR_VERSION} ${QT_MIN_VERSION} CONFIG REQUIRED COMPONENTS Quick DBus Widgets)
if(QT_MAJOR_VERSION STREQUAL "5")
    find_package(Qt5X11Extras ${QT_MIN_VERSION} REQUIRED)
endif()

find_package(KF${QT_MAJOR_VERSION} ${KF_MIN_VERSION} REQUIRED COMPONENTS
    CoreAddons
    I18n
    WindowSystem
    Crash
)

find_package(XCB COMPONENTS REQUIRED XCB COMPOSITE EVENT RECORD XFIXES)

find_package(KPipeWire REQUIRED)

add_subdirectory(src)
add_subdirectory(icons)

# Make it possible to use the po files fetched by the fetch-translations step
ki18n_install(po)

feature_summary(WHAT ALL FATAL_ON_MISSING_REQUIRED_PACKAGES)
