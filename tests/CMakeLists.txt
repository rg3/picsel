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
find_program(UPDATE_MIME_DATABASE update-mime-database)
if(UPDATE_MIME_DATABASE)
  add_test(NAME mime_glob COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/mime_glob.sh
           ${UPDATE_MIME_DATABASE} ${PROJECT_SOURCE_DIR}/data/${PICSEL_APP_ID}.mime.xml)
endif()
