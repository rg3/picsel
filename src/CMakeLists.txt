# Core + image library: no QML, unit-tested.
add_library(picsel_core STATIC
  core/SelectionDb.cpp core/SelectionDb.h
  core/DirScanner.cpp core/DirScanner.h
  core/DirWatcher.cpp core/DirWatcher.h
  core/Sorting.cpp core/Sorting.h
  core/Settings.cpp core/Settings.h
  core/Metadata.cpp core/Metadata.h
  core/Mover.cpp core/Mover.h
  core/Similarity.cpp core/Similarity.h
  core/Mp4Info.cpp core/Mp4Info.h
  image/JpegDecoder.cpp image/JpegDecoder.h
  image/ImageCache.cpp image/ImageCache.h
)
target_include_directories(picsel_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(picsel_core PUBLIC
  Qt6::Core Qt6::Gui Qt6::Concurrent
  PkgConfig::JPEG PkgConfig::EXIV2 PkgConfig::SQLITE3)
target_compile_options(picsel_core PRIVATE -Wall -Wextra)

# Video thumbnails through QtMultimedia (kept out of picsel_core so its tests
# don't need a multimedia backend).
add_library(picsel_media STATIC
  image/VideoThumbnailer.cpp image/VideoThumbnailer.h
)
target_include_directories(picsel_media PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(picsel_media PUBLIC picsel_core Qt6::Multimedia)
target_compile_options(picsel_media PRIVATE -Wall -Wextra)

add_subdirectory(app)
