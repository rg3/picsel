# picsel_add_test(<name> [extra sources...]) builds tests/<name>.cpp against picsel_core.
function(picsel_add_test name)
  qt_add_executable(${name} ${name}.cpp ${ARGN})
  target_link_libraries(${name} PRIVATE picsel_core Qt6::Test)
  target_include_directories(${name} PRIVATE ${PROJECT_SOURCE_DIR}/src)
  add_test(NAME ${name} COMMAND ${name})
  set_tests_properties(${name} PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endfunction()

picsel_add_test(tst_jpegdecoder)
picsel_add_test(tst_sorting)
picsel_add_test(tst_selectiondb)
picsel_add_test(tst_scanner_reconcile)
picsel_add_test(tst_watcher)
