# SPDX-License-Identifier: GPL-3.0-or-later
qt_add_executable(bench_decode bench_decode.cpp)
target_link_libraries(bench_decode PRIVATE picsel_core)
target_include_directories(bench_decode PRIVATE ${PROJECT_SOURCE_DIR}/src)
