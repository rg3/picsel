# SPDX-License-Identifier: GPL-3.0-or-later
cmake_minimum_required(VERSION 3.24)
project(picsel VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
endif()

option(PICSEL_BUILD_TOOLS "Build developer tools (bench_decode)" OFF)
include(CTest) # BUILD_TESTING, ON by default

set(picsel_qt_components Core Gui DBus Qml Quick QuickControls2 Concurrent Multimedia Network)
if(BUILD_TESTING)
  list(APPEND picsel_qt_components Test)
endif()
find_package(Qt6 6.8 REQUIRED COMPONENTS ${picsel_qt_components})
find_package(PkgConfig REQUIRED)
pkg_check_modules(JPEG REQUIRED IMPORTED_TARGET libjpeg)
pkg_check_modules(EXIV2 REQUIRED IMPORTED_TARGET exiv2)
pkg_check_modules(SQLITE3 REQUIRED IMPORTED_TARGET sqlite3)

qt_standard_project_setup(REQUIRES 6.8)
# picsel installs no shared libraries, so drop the $ORIGIN install RPATH Qt
# adds (distro package checks reject it); keep any the packager passes.
list(FILTER CMAKE_INSTALL_RPATH EXCLUDE REGEX "^\\$ORIGIN")

# Reverse-DNS id shared by the desktop file, icon, AppStream metainfo and
# the Wayland app_id.
set(PICSEL_APP_ID io.github.rg3.picsel)

add_subdirectory(src)
if(PICSEL_BUILD_TOOLS)
  add_subdirectory(tools)
endif()
if(BUILD_TESTING)
  add_subdirectory(tests)
endif()

include(GNUInstallDirs)
install(FILES data/${PICSEL_APP_ID}.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
install(FILES data/${PICSEL_APP_ID}.svg DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)
install(FILES data/${PICSEL_APP_ID}.metainfo.xml DESTINATION ${CMAKE_INSTALL_DATADIR}/metainfo)
# Installed under the app id, as shared-mime-info recommends for package files.
install(FILES data/${PICSEL_APP_ID}.mime.xml DESTINATION ${CMAKE_INSTALL_DATADIR}/mime/packages
        RENAME ${PICSEL_APP_ID}.xml)
install(FILES data/picsel.1 DESTINATION ${CMAKE_INSTALL_MANDIR}/man1)
