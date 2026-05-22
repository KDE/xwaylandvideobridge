# SPDX-License-Identifier: BSD-3-Clause
# SPDX-FileCopyrightText: 2023 David Edmundson <kde@davidedmundson.co.uk>
# SPDX-FileCopyrightText: 2023 Aleix Pol <aleixpol@kde.org>
# SPDX-FileCopyrightText: 2026 Hadi Chokr <hadichokr@icloud.com>

cmake_minimum_required(VERSION 3.16)

project(xwaylandvideobridge)

set(PROJECT_VERSION "0.5.0")
set(QT_MIN_VERSION "6.4")
set(KF_MIN_VERSION "5.240.0")
set(KF_MAJOR_VERSION "6")

find_package(ECM ${KF_MIN_VERSION} REQUIRED NO_MODULE)
set(CMAKE_MODULE_PATH ${ECM_MODULE_PATH})

include(KDEInstallDirs)
include(KDECMakeSettings)
include(KDECompilerSettings NO_POLICY_SCOPE)
include(ECMInstallIcons)
include(ECMQtDeclareLoggingCategory)
include(FeatureSummary)

include(KDEGitCommitHooks)
include(KDEClangFormat)

file(GLOB_RECURSE ALL_CLANG_FORMAT_SOURCE_FILES src/*.cpp src/*.h)
kde_clang_format(${ALL_CLANG_FORMAT_SOURCE_FILES})

kde_configure_git_pre_commit_hook(CHECKS CLANG_FORMAT)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 ${QT_MIN_VERSION} CONFIG REQUIRED COMPONENTS Quick DBus Widgets)

find_package(KF6 ${KF_MIN_VERSION} REQUIRED COMPONENTS
    CoreAddons
    I18n
    WindowSystem
    Crash
    StatusNotifierItem
)

find_package(KPipeWire REQUIRED)

find_package(XCB COMPONENTS REQUIRED XCB COMPOSITE EVENT RECORD XFIXES)

feature_summary(WHAT ALL FATAL_ON_MISSING_REQUIRED_PACKAGES)

add_subdirectory(src)
add_subdirectory(icons)

# Make it possible to use the po files fetched by the fetch-translations step
ki18n_install(po)
