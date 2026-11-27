# SPDX-License-Identifier: GPL-3.0-or-later
# picsel_add_test(<name> [extra sources...] [LIBS libs...]) builds
# tests/<name>.cpp against picsel_core (plus LIBS).
function(picsel_add_test name)
  cmake_parse_arguments(PARSE_ARGV 1 arg "" "" "LIBS")
  qt_add_executable(${name} ${name}.cpp ${arg_UNPARSED_ARGUMENTS})
  target_link_libraries(${name} PRIVATE picsel_core ${arg_LIBS} Qt6::Test)
  target_include_directories(${name} PRIVATE ${PROJECT_SOURCE_DIR}/src)
  add_test(NAME ${name} COMMAND ${name})
  set_tests_properties(${name} PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endfunction()

picsel_add_test(tst_jpegdecoder)
picsel_add_test(tst_sorting)
picsel_add_test(tst_settings)
picsel_add_test(tst_mover)
picsel_add_test(tst_selectiondb)
# Runs tools/init_db.py and checks its output.
target_compile_definitions(tst_selectiondb PRIVATE PICSEL_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
picsel_add_test(tst_scanner_reconcile)
picsel_add_test(tst_watcher)
picsel_add_test(tst_similarity)
picsel_add_test(tst_imagecache)
picsel_add_test(tst_mp4info)
picsel_add_test(tst_videothumbnailer LIBS picsel_media)
set_tests_properties(tst_videothumbnailer PROPERTIES TIMEOUT 300)

# Desktop integration files, checked when the validators are installed.
find_program(DESKTOP_FILE_VALIDATE desktop-file-validate)
if(DESKTOP_FILE_VALIDATE)
  add_test(NAME desktop_file COMMAND ${DESKTOP_FILE_VALIDATE}
           ${PROJECT_SOURCE_DIR}/data/${PICSEL_APP_ID}.desktop)
endif()
find_program(APPSTREAMCLI appstreamcli)
if(APPSTREAMCLI)
  add_test(NAME metainfo COMMAND ${APPSTREAMCLI} validate --no-net
           ${PROJECT_SOURCE_DIR}/data/${PICSEL_APP_ID}.metainfo.xml)
endif()
# Values that C++, QML and the README each spell out.
add_test(NAME qml_keys COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/qml_keys.sh ${PROJECT_SOURCE_DIR})
add_test(NAME qml_modules COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/qml_modules.sh ${PROJECT_SOURCE_DIR})
add_test(NAME readme_qt COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/readme_qt.sh ${PROJECT_SOURCE_DIR})
find_program(UPDATE_MIME_DATABASE update-mime-database)
if(UPDATE_MIME_DATABASE)
  add_test(NAME mime_glob COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/mime_glob.sh
           ${UPDATE_MIME_DATABASE} ${PROJECT_SOURCE_DIR}/data/${PICSEL_APP_ID}.mime.xml)
endif()
# Our QML type names must not collide with those of the Qt modules we import.
add_test(NAME qml_type_clash COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/qml_type_clash.sh
         ${PROJECT_BINARY_DIR}/src/app/Picsel "${QT6_INSTALL_PREFIX}/${QT6_INSTALL_QML}"
         ${PROJECT_SOURCE_DIR}/src/app/qml)
set_tests_properties(qml_type_clash PROPERTIES SKIP_RETURN_CODE 77)
