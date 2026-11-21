qt_add_executable(picsel
  main.cpp
  Application.cpp Application.h
  Session.cpp Session.h
  FileListModel.cpp FileListModel.h
  Automation.cpp Automation.h
  ../image/ImageProviders.cpp ../image/ImageProviders.h
)
target_include_directories(picsel PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_compile_definitions(picsel PRIVATE PICSEL_VERSION="${PROJECT_VERSION}")
target_compile_options(picsel PRIVATE -Wall -Wextra)

set_source_files_properties(qml/Theme.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

qt_add_qml_module(picsel
  URI Picsel
  VERSION 1.0
  QML_FILES
    qml/Theme.qml
    qml/Main.qml
    qml/Mosaic.qml
    qml/Tile.qml
    qml/SidePanel.qml
    qml/SetupWindow.qml
    qml/FolderPicker.qml
    qml/ErrorWindow.qml
)

target_link_libraries(picsel PRIVATE
  picsel_core
  Qt6::Quick Qt6::QuickControls2 Qt6::Multimedia Qt6::Network Qt6::Test)

install(TARGETS picsel RUNTIME DESTINATION bin)
