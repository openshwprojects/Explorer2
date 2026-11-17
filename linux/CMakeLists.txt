# Explorer2's file readers without the window, for Linux (C++20; tested with GCC 9.4 and CMake 3.16).
#
#   cmake -S linux -B build-linux -DCMAKE_BUILD_TYPE=Release
#   cmake --build build-linux -j
#   build-linux/explorer2-readers testdata/*
#   python3 tools/fuzz_samples.py --exe build-linux/explorer2-readers
#
# Explorer2 itself is a Windows program (build.bat). This builds only its format readers, with the few Windows calls
# they make supplied by src/PlatformCompat.h. What has no counterpart here (WIC, Media Foundation, DirectWrite, GDI,
# CryptoAPI, the CAB API, the PDF renderer, external 7-Zip and UnRAR) is left out, and the files that need it are
# turned down; README.md lists them.
cmake_minimum_required(VERSION 3.13)
project(Explorer2Readers LANGUAGES CXX)

option(EXPLORER2_SANITIZE "Build with AddressSanitizer and UndefinedBehaviorSanitizer (for fuzzing)" OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()
# -O2 as build.bat uses /O2: -O3 takes much longer on the large reader files and gains nothing measurable
string(REPLACE "-O3" "-O2" CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE}")

set(E2_SRC ${CMAKE_CURRENT_SOURCE_DIR}/../src)

add_executable(explorer2-readers
    # the readers: the same files build.bat compiles into Explorer2.exe
    ${E2_SRC}/ArchiveReaders.cpp
    ${E2_SRC}/DocReaders.cpp
    ${E2_SRC}/EdaReaders.cpp
    ${E2_SRC}/ImageFormatLoader.cpp
    ${E2_SRC}/Mesh3DReaders.cpp
    ${E2_SRC}/ZipDeflate.cpp
    # what Linux needs instead of the Windows program around them
    AppLogLinux.cpp
    ReaderCli.cpp)

target_include_directories(explorer2-readers PRIVATE ${E2_SRC})
set_target_properties(explorer2-readers PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
# The sources carry MSVC pragmas (#pragma warning, #pragma comment), which mean nothing here
target_compile_options(explorer2-readers PRIVATE -Wall -Wno-unknown-pragmas)

if(EXPLORER2_SANITIZE)
    target_compile_options(explorer2-readers PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(explorer2-readers PRIVATE -fsanitize=address,undefined)
endif()

find_package(Threads REQUIRED)
target_link_libraries(explorer2-readers PRIVATE Threads::Threads ${CMAKE_DL_LIBS})
