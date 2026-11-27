# SPDX-License-Identifier: GPL-3.0-or-later
qt_add_executable(picsel
  main.cpp
  Application.cpp Application.h
  Session.cpp Session.h
  FileListModel.cpp FileListModel.h
  Automation.cpp Automation.h
  SystemCheck.cpp SystemCheck.h
  ../image/ImageProviders.cpp ../image/ImageProviders.h
)
target_include_directories(picsel PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_compile_definitions(picsel PRIVATE PICSEL_VERSION="${PROJECT_VERSION}" PICSEL_APP_ID="${PICSEL_APP_ID}")
target_compile_options(picsel PRIVATE -Wall -Wextra)

set_source_files_properties(qml/Theme.qml qml/MouseSelection.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

qt_add_qml_module(picsel
  URI Picsel
  VERSION 1.0
  QML_FILES
    qml/Theme.qml
    qml/Main.qml
    qml/Mosaic.qml
    qml/Tile.qml
    qml/SidePanel.qml
    qml/FullView.qml
    qml/ConfirmMoveDialog.qml
    qml/DialogButton.qml
    qml/PathBox.qml
    qml/SelectableLabel.qml
    qml/MouseSelection.qml
    qml/ConflictDialog.qml
    qml/MoveProgressDialog.qml
    qml/CameraClocksDialog.qml
    qml/VideoPlayer.qml
    qml/SetupWindow.qml
    qml/FolderPicker.qml
    qml/ErrorWindow.qml
)

target_link_libraries(picsel PRIVATE
  picsel_core picsel_media
  Qt6::DBus Qt6::Qml Qt6::Quick Qt6::QuickControls2 Qt6::Multimedia Qt6::Network)

install(TARGETS picsel RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

# The window icon is sent to the compositor (xdg-toplevel-icon); embed it so
# it never depends on icon-theme lookup.
qt_add_resources(picsel icons
  PREFIX /icons
  BASE ${PROJECT_SOURCE_DIR}/data
  FILES ${PROJECT_SOURCE_DIR}/data/${PICSEL_APP_ID}.svg)
