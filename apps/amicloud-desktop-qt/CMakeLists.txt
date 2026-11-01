# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Ronny Krueger
# AmiCloud Desktop — Qt 6 Widgets background sync client (status menu).
# Plan: docs/design/desktop-sync-plan.md, section 6 (phase 5: macOS basics).
#
# Build:
#   make desktop   (top level), or directly:
#   cmake -S apps/amicloud-desktop-qt -B build/amicloud-desktop-qt
#   cmake --build build/amicloud-desktop-qt
# Output: <repo-root>/bin/pc/AmiCloud Desktop.app (macOS) or
#         <repo-root>/bin/pc/amicloud-desktop (elsewhere)
cmake_minimum_required(VERSION 3.16)
project(amicloud-desktop LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
# no AUTOUIC: it would take ui_hooks.h for a generated .ui header

if(APPLE)
    # decided 31.10.2026: Apple Silicon only, macOS 15 and later
    set(CMAKE_OSX_ARCHITECTURES arm64)
    set(CMAKE_OSX_DEPLOYMENT_TARGET 15.0)
endif()

find_package(Qt6 REQUIRED COMPONENTS Widgets)

# libcurl: on macOS the one from the SDK (system certificates, plan 6.6),
# elsewhere pkg-config
if(APPLE)
    find_library(CURL_LIBRARIES curl PATHS ${CMAKE_OSX_SYSROOT}/usr/lib NO_DEFAULT_PATH)
    if(NOT CURL_LIBRARIES)
        set(CURL_LIBRARIES curl)
    endif()
    set(CURL_INCLUDE_DIRS "")
else()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(CURL REQUIRED libcurl)
endif()

set(ROOT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../..)
set(LIBS_DIR ${ROOT_DIR}/core)

# The C core, shared verbatim with the CLI, shell and handlers
set(CORE_SRCS
    ${LIBS_DIR}/libcloudhttp/src/cmd.c
    ${LIBS_DIR}/libcloudhttp/src/http_pc.c
    ${LIBS_DIR}/libcloudcore/src/base64.c
    ${LIBS_DIR}/libcloudcore/src/config.c
    ${LIBS_DIR}/libcloudcore/src/parson.c
    ${LIBS_DIR}/libcloudcore/src/md5.c
    ${LIBS_DIR}/libcloudcore/src/ui_hooks.c
    ${LIBS_DIR}/libcloudcore/src/progress.c
    ${LIBS_DIR}/libcloudcore/src/charset.c
    ${LIBS_DIR}/libcloudcore/src/cloudtime.c
    ${LIBS_DIR}/libcloudfs/src/localfs_pc.c
    ${LIBS_DIR}/libcloudfs/src/localfs_util.c
    ${LIBS_DIR}/libcloudsync/src/sync.c
    ${LIBS_DIR}/libcloudsync/src/syncstate.c
    ${LIBS_DIR}/libcloudsync/src/amicloud_transport.c
)
set_source_files_properties(${CORE_SRCS} PROPERTIES LANGUAGE C)

set(APP_SRCS
    src/main.cpp
    src/appsettings.cpp
    src/corebridge.cpp
    src/coreworker.cpp
    src/synccontroller.cpp
    src/trayapp.cpp
    src/trayicon.cpp
    src/foldertree.cpp
    src/setupwizard.cpp
    src/settingsdialog.cpp
    src/activitywindow.cpp
)
if(APPLE)
    enable_language(OBJCXX)
    # localwatcher.h listed: its sources have other base names, AUTOMOC
    # would not find the Q_OBJECT header otherwise
    list(APPEND APP_SRCS src/platform_mac.mm src/localwatcher_mac.mm src/localwatcher.h)
else()
    list(APPEND APP_SRCS src/platform_other.cpp src/localwatcher_other.cpp src/localwatcher.h)
endif()

if(APPLE)
    add_executable(amicloud-desktop MACOSX_BUNDLE ${CORE_SRCS} ${APP_SRCS})
    set_target_properties(amicloud-desktop PROPERTIES
        OUTPUT_NAME "AmiCloud Desktop"
        MACOSX_BUNDLE_INFO_PLIST ${CMAKE_CURRENT_SOURCE_DIR}/macos/Info.plist.in
        MACOSX_BUNDLE_GUI_IDENTIFIER de.planet-rk.amicloud-desktop
    )
else()
    add_executable(amicloud-desktop ${CORE_SRCS} ${APP_SRCS})
endif()

# version from the core (AC_VERSION) for Info.plist
file(STRINGS ${LIBS_DIR}/libcloudcore/include/cloudcore/version.h AC_VERSION_LINE
     REGEX "#define AC_VERSION ")
string(REGEX REPLACE ".*\"([^\"]+)\".*" "\\1" AC_VERSION "${AC_VERSION_LINE}")
set_target_properties(amicloud-desktop PROPERTIES
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${AC_VERSION}"
    MACOSX_BUNDLE_BUNDLE_VERSION "${AC_VERSION}"
)

target_include_directories(amicloud-desktop PRIVATE
    ${LIBS_DIR}/libcloudcore/include/cloudcore
    ${LIBS_DIR}/libcloudhttp/include/cloudhttp
    ${LIBS_DIR}/libcloudfs/include/cloudfs
    ${LIBS_DIR}/libcloudsync/include/cloudsync
    ${CURL_INCLUDE_DIRS}
)
string(TIMESTAMP AC_BUILD_DATE "%d.%m.%Y")
target_compile_definitions(amicloud-desktop PRIVATE AC_BUILD_DATE=${AC_BUILD_DATE})
target_compile_options(amicloud-desktop PRIVATE $<$<COMPILE_LANGUAGE:CXX,OBJCXX>:-Wall -Wextra>)
target_link_libraries(amicloud-desktop PRIVATE Qt6::Widgets ${CURL_LIBRARIES})
if(APPLE)
    target_link_libraries(amicloud-desktop PRIVATE "-framework AppKit" "-framework CoreServices")
endif()

set_target_properties(amicloud-desktop PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${ROOT_DIR}/bin/pc
)

# Headless integration test against a real server (not part of the app):
#   cmake --build build/amicloud-desktop-qt --target amicloud-desktop-selftest
#   AC_TEST_SERVER=... AC_TEST_USER=... AC_TEST_PASS=... \
#     build/amicloud-desktop-qt/amicloud-desktop-selftest <empty-dir>
add_executable(amicloud-desktop-selftest EXCLUDE_FROM_ALL
    ${CORE_SRCS}
    tests/selftest.cpp
    src/appsettings.cpp
    src/corebridge.cpp
    src/coreworker.cpp
    src/synccontroller.cpp
    src/foldertree.cpp
    $<IF:$<BOOL:${APPLE}>,src/localwatcher_mac.mm,src/localwatcher_other.cpp>
    src/localwatcher.h
)
if(APPLE)
    target_link_libraries(amicloud-desktop-selftest PRIVATE "-framework CoreServices")
endif()
target_include_directories(amicloud-desktop-selftest PRIVATE src
    ${LIBS_DIR}/libcloudcore/include/cloudcore
    ${LIBS_DIR}/libcloudhttp/include/cloudhttp
    ${LIBS_DIR}/libcloudfs/include/cloudfs
    ${LIBS_DIR}/libcloudsync/include/cloudsync
    ${CURL_INCLUDE_DIRS}
)
target_compile_definitions(amicloud-desktop-selftest PRIVATE AC_BUILD_DATE=${AC_BUILD_DATE})
target_link_libraries(amicloud-desktop-selftest PRIVATE Qt6::Widgets ${CURL_LIBRARIES})
