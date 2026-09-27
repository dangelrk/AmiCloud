# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Ronny Krueger
# AmiCloud Commander — Qt 6 Widgets dual-pane file manager.
#
# Copy of the old monorepo's client/gui_qt/CMakeLists.txt
# (docs/design/05_Refactoring_Client_Handler.md, M5), pointed at the
# AmiCloudCore sources in core/ instead of the flat client/ .c files, and with the Sync menu/TransferJob::SyncPanels path
# removed from the C++ sources (see mainwindow.h/.cpp, transferjob.h/.cpp
# in this dir) — sync no longer ships wired into any interactive
# frontend, the engine itself lives on as libcloudsync's cloudsync_run().
#
# Build:
#   make qt        (top level, see docs/BUILD.md), or directly:
#   cmake -S apps/amicloud-commander-qt -B build/amicloud-commander-qt
#   cmake --build build/amicloud-commander-qt
# Output: <repo-root>/bin/pc/amicloud-commander (shared top-level bin dir)
cmake_minimum_required(VERSION 3.16)
project(amicloud-commander LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
# No .ui files in this project — AUTOUIC would also misinterpret our
# ui_hooks.h include (matches its "ui_<name>.h generated from <name>.ui"
# naming convention) and fail looking for a nonexistent hooks.ui.

find_package(Qt6 REQUIRED COMPONENTS Widgets)

# libcurl — prefer pkg-config when available, otherwise fall back to the
# same Homebrew paths core/libcloudhttp/Makefile.pc uses.
find_package(PkgConfig)
if(PkgConfig_FOUND)
    pkg_check_modules(CURL libcurl)
endif()
if(NOT CURL_FOUND)
    set(CURL_INCLUDE_DIRS /opt/homebrew/include /usr/local/include)
    set(CURL_LIBRARIES curl)
    set(CURL_CFLAGS_OTHER "")
    link_directories(/opt/homebrew/lib /usr/local/lib)
endif()

set(APPS_DIR   ${CMAKE_CURRENT_SOURCE_DIR}/..)
set(ROOT_DIR    ${APPS_DIR}/..)
set(LIBS_DIR   ${ROOT_DIR}/core)
set(COMMON_DIR ${ROOT_DIR}/core/common)

# Core .c files, shared verbatim with the CLI/shell/handler worker via
# core/ — see the "no C++ contamination of the core" rule (module-
# encapsulation, docs/design/05_Refactoring_Client_Handler.md).
set(CORE_SRCS
    ${LIBS_DIR}/libcloudhttp/src/cmd.c
    ${LIBS_DIR}/libcloudhttp/src/http_pc.c
    ${LIBS_DIR}/libcloudcore/src/base64.c
    ${LIBS_DIR}/libcloudcore/src/config.c
    ${LIBS_DIR}/libcloudcore/src/parson.c
    ${LIBS_DIR}/libcloudcore/src/md5.c
    ${LIBS_DIR}/libcloudcore/src/ui_hooks.c
    ${LIBS_DIR}/libcloudcore/src/progress.c
    ${COMMON_DIR}/src/panel.c
    ${COMMON_DIR}/src/search.c
    ${LIBS_DIR}/libcloudfs/src/localfs_pc.c
    ${LIBS_DIR}/libcloudfs/src/localfs_util.c
    ${LIBS_DIR}/libcloudfs/src/pattern_pc.c
)
set_source_files_properties(${CORE_SRCS} PROPERTIES LANGUAGE C)

add_executable(amicloud-commander
    ${CORE_SRCS}
    src/main.cpp
    src/mainwindow.cpp
    src/panelwidget.cpp
    src/paneltablemodel.cpp
    src/transferjob.cpp
    src/connectdialog.cpp
    src/infodialog.cpp
    src/searchdialog.cpp
    src/searchresultswindow.cpp
)

target_include_directories(amicloud-commander PRIVATE
    ${LIBS_DIR}/libcloudcore/include/cloudcore
    ${LIBS_DIR}/libcloudhttp/include/cloudhttp
    ${LIBS_DIR}/libcloudfs/include/cloudfs
    ${COMMON_DIR}/include/common
    ${CURL_INCLUDE_DIRS}
)
target_link_libraries(amicloud-commander PRIVATE Qt6::Widgets ${CURL_LIBRARIES})
target_compile_options(amicloud-commander PRIVATE ${CURL_CFLAGS_OTHER})

set_target_properties(amicloud-commander PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/../../bin/pc
)
