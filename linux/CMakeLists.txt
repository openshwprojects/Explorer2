# Explorer2 Linux Build & Win32/Direct2D/DirectWrite/WIC HAL (32-bit & 64-bit C++20)
#
# Targets:
#   explorer2-readers  - Standalone headless format reader CLI
#   explorer2-stage15  - Stage 15 format + VCS test runner
#   explorer2          - Complete Explorer2 Linux application + CLI modes (--version, --self-test, --inspect-files, --dump-hw, --monitor-daemon)
#
# Options:
#   -DE2_BUILD_32BIT=ON      Compile 32-bit x86 (i686 / -m32) ELF binaries (with 64-bit _FILE_OFFSET_BITS=64)
#   -DEXPLORER2_SANITIZE=ON  Enable AddressSanitizer + UndefinedBehaviorSanitizer
cmake_minimum_required(VERSION 3.13)
project(Explorer2Linux LANGUAGES CXX)

option(EXPLORER2_SANITIZE "Build with AddressSanitizer and UndefinedBehaviorSanitizer (for fuzzing)" OFF)
option(E2_BUILD_32BIT "Build 32-bit x86 (-m32) Linux binaries" OFF)
option(E2_STATIC_LINUX "Build 100% statically linked Linux binaries (zero .so dependencies)" OFF)

find_program(CCACHE_PROGRAM ccache)
if(CCACHE_PROGRAM)
    set(CMAKE_CXX_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
endif()

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()
# -O2 as build.bat uses /O2: -O3 takes much longer on the large reader files and gains nothing measurable
string(REPLACE "-O3" "-O2" CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE}")

if(E2_BUILD_32BIT)
    # Use IEEE-754 SSE2 math on 32-bit x86 (matching x86_64 & MSVC /arch:SSE2) instead of x87 80-bit excess precision
    add_compile_options(-m32 -msse2 -mfpmath=sse)
    add_link_options(-m32)
endif()

add_compile_definitions(_GNU_SOURCE=1 _FILE_OFFSET_BITS=64 _LARGEFILE64_SOURCE=1)

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../build/version_gen.h")
    add_compile_definitions(E2_HAS_GENERATED_VERSION=1)
    include_directories("${CMAKE_CURRENT_SOURCE_DIR}/../build")
endif()

set(E2_SRC ${CMAKE_CURRENT_SOURCE_DIR}/../src)
set(E2_HAL_INC ${CMAKE_CURRENT_SOURCE_DIR}/win32_hal/include)
set(E2_HAL_SRC ${CMAKE_CURRENT_SOURCE_DIR}/win32_hal/src)

find_package(Threads REQUIRED)

# 1. Shared object library for the heavy format readers + core Win32/WIC/Net/Sys HAL
add_library(e2_readers_obj OBJECT
    ${E2_SRC}/ArchiveReaders.cpp
    ${E2_SRC}/DocReaders.cpp
    ${E2_SRC}/EdaReaders.cpp
    ${E2_SRC}/ImageFormatLoader.cpp
    ${E2_SRC}/Mesh3DReaders.cpp
    ${E2_SRC}/ZipDeflate.cpp
    ${E2_HAL_SRC}/Win32HalCore.cpp
    ${E2_HAL_SRC}/WicHal.cpp
    ${E2_HAL_SRC}/D2DSoftwareHal.cpp
    ${E2_HAL_SRC}/NetAndSysHal.cpp)

target_include_directories(e2_readers_obj PRIVATE ${E2_HAL_INC} ${E2_SRC})
target_compile_definitions(e2_readers_obj PRIVATE E2_WIN32_HAL=1 UNICODE _UNICODE)
set_target_properties(e2_readers_obj PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
target_compile_options(e2_readers_obj PRIVATE -Wall -Wno-unknown-pragmas)

# 2. Shared object library for Viewers + VCS + Format/VCS Test Suites
add_library(e2_viewers_tests_obj OBJECT
    ${E2_SRC}/Model3DViewer.cpp
    ${E2_SRC}/EdaViewer.cpp
    ${E2_SRC}/SpreadsheetDocViewer.cpp
    ${E2_SRC}/ArchiveExplorer.cpp
    ${E2_SRC}/NotepadViewer.cpp
    ${E2_SRC}/HexViewer.cpp
    ${E2_SRC}/PdfViewer.cpp
    ${E2_HAL_SRC}/PdfHal.cpp
    ${E2_SRC}/MediaPlayer.cpp
    ${E2_SRC}/ToolProcess.cpp
    ${E2_SRC}/GitIntegration.cpp
    ${E2_SRC}/SvnWorkingCopy.cpp
    ${E2_SRC}/RepoBrowser.cpp
    ${E2_SRC}/FormatTests3D.cpp
    ${E2_SRC}/FormatTestsEda.cpp
    ${E2_SRC}/FormatTestsImages.cpp
    ${E2_SRC}/FormatTestsDocs.cpp
    ${E2_SRC}/FormatTestsDocsB.cpp
    ${E2_SRC}/FormatTestsArchives.cpp
    ${E2_SRC}/FormatTestsBatch70.cpp
    ${E2_SRC}/SelfTestVcs.cpp)

target_include_directories(e2_viewers_tests_obj PRIVATE ${E2_HAL_INC} ${E2_SRC})
target_compile_definitions(e2_viewers_tests_obj PRIVATE E2_WIN32_HAL=1 UNICODE _UNICODE)
set_target_properties(e2_viewers_tests_obj PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
target_compile_options(e2_viewers_tests_obj PRIVATE -Wall -Wno-unknown-pragmas)

# Target A: Headless readers CLI
add_executable(explorer2-readers
    $<TARGET_OBJECTS:e2_readers_obj>
    AppLogLinux.cpp
    ReaderCli.cpp)

target_include_directories(explorer2-readers PRIVATE ${E2_SRC})
set_target_properties(explorer2-readers PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
target_compile_options(explorer2-readers PRIVATE -Wall -Wno-unknown-pragmas)
target_link_libraries(explorer2-readers PRIVATE Threads::Threads ${CMAKE_DL_LIBS})

# Target B: Stage 15 + VCS Test Runner
add_executable(explorer2-stage15
    $<TARGET_OBJECTS:e2_readers_obj>
    $<TARGET_OBJECTS:e2_viewers_tests_obj>
    AppLogLinux.cpp
    Stage15Runner.cpp)

target_include_directories(explorer2-stage15 PRIVATE ${E2_HAL_INC} ${E2_SRC})
target_compile_definitions(explorer2-stage15 PRIVATE E2_WIN32_HAL=1 UNICODE _UNICODE)
set_target_properties(explorer2-stage15 PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
target_compile_options(explorer2-stage15 PRIVATE -Wall -Wno-unknown-pragmas)
target_link_libraries(explorer2-stage15 PRIVATE Threads::Threads ${CMAKE_DL_LIBS})

# Target C: Complete Explorer2 Application (all 43 cross-platform TUs + WinMainEntry)
add_executable(explorer2
    $<TARGET_OBJECTS:e2_readers_obj>
    $<TARGET_OBJECTS:e2_viewers_tests_obj>
    ${E2_SRC}/ExplorerApp.cpp
    ${E2_SRC}/ExplorerAppRender.cpp
    ${E2_SRC}/SelfTestSuite.cpp
    ${E2_SRC}/main.cpp
    ${E2_SRC}/FileEngine.cpp
    ${E2_SRC}/ImageCache.cpp
    ${E2_SRC}/ShellMenuEmulator.cpp
    ${E2_SRC}/HardwareInfo.cpp
    ${E2_SRC}/ProgramsInfo.cpp
    ${E2_SRC}/Monitoring.cpp
    ${E2_SRC}/LanScanner.cpp
    ${E2_SRC}/NetShares.cpp
    ${E2_SRC}/AppLog.cpp
    ${E2_SRC}/BackupManager.cpp
    ${E2_SRC}/TransferManager.cpp
    ${E2_SRC}/FtpClient.cpp
    ${E2_SRC}/FileContentPane.cpp
    ${E2_HAL_SRC}/X11GuiHal.cpp
    ${E2_HAL_SRC}/WinMainEntry.cpp)

target_include_directories(explorer2 PRIVATE ${E2_HAL_INC} ${E2_SRC})
target_compile_definitions(explorer2 PRIVATE E2_WIN32_HAL=1 UNICODE _UNICODE)
set_target_properties(explorer2 PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
target_compile_options(explorer2 PRIVATE -Wall -Wno-unknown-pragmas)
target_link_libraries(explorer2 PRIVATE Threads::Threads ${CMAKE_DL_LIBS})

if(EXPLORER2_SANITIZE)
    foreach(tgt e2_readers_obj e2_viewers_tests_obj explorer2-readers explorer2-stage15 explorer2)
        target_compile_options(${tgt} PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
    endforeach()
    foreach(exe explorer2-readers explorer2-stage15 explorer2)
        target_link_options(${exe} PRIVATE -fsanitize=address,undefined)
    endforeach()
elseif(E2_STATIC_LINUX)
    # 100% static ELF binaries (musl + static libX11/libxcb/libXau/libXdmcp): zero shared library dependencies,
    # runs on any Linux distribution / glibc or musl version / 64-bit host without 32-bit multilib installed.
    target_compile_definitions(explorer2 PRIVATE E2_STATIC_X11=1)
    target_link_libraries(explorer2 PRIVATE X11 xcb Xau Xdmcp)
    # musl gives a new thread 128 KiB of stack, where the readers' and workers' deep recursion overflows; it takes
    # the default for its threads from the PT_GNU_STACK size this option sets (glibc uses the 8 MiB stack limit)
    foreach(exe explorer2-readers explorer2-stage15 explorer2)
        target_link_options(${exe} PRIVATE -static -Wl,-z,stack-size=2097152)
    endforeach()
else()
    # Statically link libstdc++ and libgcc so portable binaries do not need libstdc++.so.6 / libgcc_s.so.1 at runtime
    foreach(exe explorer2-readers explorer2-stage15 explorer2)
        target_link_options(${exe} PRIVATE -static-libstdc++ -static-libgcc)
    endforeach()
endif()
