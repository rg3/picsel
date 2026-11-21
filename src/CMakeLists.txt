# Core + image library: no QML, unit-tested.
add_library(picsel_core STATIC
  core/SelectionDb.cpp core/SelectionDb.h
  core/DirScanner.cpp core/DirScanner.h
  core/Sorting.cpp core/Sorting.h
  core/Settings.cpp core/Settings.h
  core/Mover.cpp core/Mover.h
  image/JpegDecoder.cpp image/JpegDecoder.h
  image/ImageCache.cpp image/ImageCache.h
)
target_include_directories(picsel_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(picsel_core PUBLIC
  Qt6::Core Qt6::Gui Qt6::Concurrent
  PkgConfig::JPEG PkgConfig::EXIV2 PkgConfig::SQLITE3)
target_compile_options(picsel_core PRIVATE -Wall -Wextra)

add_subdirectory(app)
