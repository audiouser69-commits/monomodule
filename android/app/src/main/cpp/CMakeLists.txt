# Monomodule Bench (Android): the engine of Monomodule (mnmcore + the dsp56300 emulator, built as the desktop does)
# and MAKU (ext/maku, its own shared library, dlopen()ed) behind a small JNI test harness.
cmake_minimum_required(VERSION 3.22)
project(MonomoduleBench LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

get_filename_component(MNM_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../../../.." ABSOLUTE)
set(MNM_BUILD_TESTS OFF)
set(MNM_OS_SYX "")
# dsp56kBase links "rt", which bionic folds into libc: an empty target of that name stands in for it
add_library(rt STATIC ${CMAKE_CURRENT_SOURCE_DIR}/empty.c)
include(${MNM_ROOT}/cmake/dsp56300.cmake)
add_subdirectory(${MNM_ROOT}/src/core ${CMAKE_BINARY_DIR}/mnmcore)

# MAKU: one translation unit, global state, its own libc helpers: a library of its own, loaded with RTLD_LOCAL
add_library(maku SHARED ${MNM_ROOT}/ext/maku/plugin/maku_plugin.c)
target_include_directories(maku PRIVATE ${MNM_ROOT}/ext/maku/gen ${MNM_ROOT}/ext/maku/firmware/src)
target_compile_options(maku PRIVATE -std=gnu99 -w -ffp-contract=off -O2)
target_link_libraries(maku PRIVATE m)

add_library(mnmbench SHARED bench.cpp)
target_link_libraries(mnmbench PRIVATE mnmcore aaudio log dl)
add_dependencies(mnmbench maku)
