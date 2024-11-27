cmake_minimum_required(VERSION 3.19)

project(recorder LANGUAGES C CXX)

# Set package information
set(PACKAGE_NAME "recorder")
set(PACKAGE_VERSION "1.2.2")
set(PACKAGE_DESCRIPTION "Lock-free, real-time flight recorder for C or C++ programs")
set(PACKAGE_URL "http://github.com/c3d/recorder")
set(PACKAGE_BUGS "christophe@dinechin.org")

# Include directories
include_directories(${PROJECT_SOURCE_DIR})

# Define sources and headers
set(SOURCES
    recorder_ring.c
    recorder.c
)

set(HEADERS
    recorder_ring.h
    recorder.h
)

include(CheckSymbolExists)
include(CheckIncludeFile)

check_symbol_exists(sigaction signal.h HAVE_SIGACTION)
check_include_file(regex.h HAVE_REGEX_H)
check_include_file(sys/mman.h HAVE_SYS_MMAN_H)
check_include_file(signal.h HAVE_SIGNAL_H)
check_symbol_exists(drand48 stdlib.h HAVE_DRAND48)
check_symbol_exists(setlinebuf stdio.h HAVE_SETLINEBUF)

# add flags depending on check results
add_compile_definitions(
    $<$<BOOL:${HAVE_SIGACTION}>:HAVE_SIGACTION>
    $<$<BOOL:${HAVE_REGEX_H}>:HAVE_REGEX_H>
    $<$<BOOL:${HAVE_SYS_MMAN_H}>:HAVE_SYS_MMAN_H>
    $<$<BOOL:${HAVE_SIGNAL_H}>:HAVE_SIGNAL_H>
    $<$<BOOL:${HAVE_DRAND48}>:HAVE_DRAND48>
    $<$<BOOL:${HAVE_SETLINEBUF}>:HAVE_SETLINEBUF>
)

# Define the library
add_library(${PACKAGE_NAME} SHARED ${SOURCES} ${HEADERS})

# Set the version of the library
set_target_properties(${PACKAGE_NAME} PROPERTIES VERSION ${PACKAGE_VERSION})

# Link Qt6 libraries
find_package(Qt6 REQUIRED COMPONENTS Core Gui Charts)
target_link_libraries(${PACKAGE_NAME} PRIVATE Qt6::Core Qt6::Gui Qt6::Charts)


# Platform-specific libraries and flags
if(WIN32)
    target_link_libraries(${PACKAGE_NAME} PRIVATE setupapi)
elseif(ANDROID)
    # No additional libraries for Android
elseif(FREEBSD)
    target_link_libraries(${PACKAGE_NAME} PRIVATE thr iconv)
elseif(APPLE)
    target_link_libraries(${PACKAGE_NAME} PRIVATE "-framework CoreFoundation" "-framework IOKit")
    target_compile_options(${PACKAGE_NAME} PRIVATE -fsanitize=address)
    target_link_libraries(${PACKAGE_NAME} PRIVATE -fsanitize=address)
endif()

# Compiler-specific flags
if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    target_compile_options(${PACKAGE_NAME} PRIVATE -Wno-unknown-pragmas)
endif()

# Installation
install(TARGETS ${PACKAGE_NAME} DESTINATION bin)
install(FILES ${HEADERS} DESTINATION include/${PACKAGE_NAME})

# Install man pages
install(DIRECTORY man/man1 DESTINATION share/man)
install(DIRECTORY man/man3 DESTINATION share/man)

# Add the scope subdirectory
add_subdirectory(scope)

# Tests
enable_testing()
add_executable(hanoi_test hanoi_test.c)
target_link_libraries(hanoi_test PRIVATE recorder)
add_executable(recorder_test recorder_test.c)
target_link_libraries(recorder_test PRIVATE m dl recorder)
add_executable(ring_test ring_test.c)
target_link_libraries(ring_test PRIVATE recorder)
add_executable(crash_test crash_test.c)
target_link_libraries(crash_test PRIVATE recorder)

add_test(NAME hanoi_test COMMAND hanoi_test 20)
add_test(NAME recorder_test COMMAND recorder_test)
add_test(NAME ring_test COMMAND ring_test)
add_test(NAME crash_test COMMAND crash_test)

# Set test arguments
set_tests_properties(hanoi_test PROPERTIES PASS_REGULAR_EXPRESSION "TIMING:")
set_tests_properties(crash_test PROPERTIES PASS_REGULAR_EXPRESSION "Signal handler for 11 called")