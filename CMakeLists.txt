cmake_minimum_required(VERSION 3.13)
project(VCAI VERSION 1.0 LANGUAGES C CXX)

message(STATUS "CMAKE_SOURCE_DIR in root: ${CMAKE_SOURCE_DIR}")

# Set C++ standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Set C standard
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Enable position independent code
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Export compile commands for clangd and other tools
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Set build type if not specified
if (NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif ()

# Add CPU-specific optimizations for Release builds
if (CMAKE_BUILD_TYPE STREQUAL "Release")
    # Check if compiler supports -march=native (check both C and C++)
    include(CheckCXXCompilerFlag)
    include(CheckCCompilerFlag)
    check_cxx_compiler_flag("-march=native" COMPILER_SUPPORTS_MARCH_NATIVE)
    check_c_compiler_flag("-march=native" C_COMPILER_SUPPORTS_MARCH_NATIVE)
    
    if (COMPILER_SUPPORTS_MARCH_NATIVE AND C_COMPILER_SUPPORTS_MARCH_NATIVE)
        add_compile_options(-march=native -mtune=native)
        message(STATUS "Enabled CPU-specific optimizations: -march=native -mtune=native")
    else ()
        message(STATUS "CPU-specific optimizations (-march=native) not supported by compiler")
    endif ()
endif ()

# Option for Link-Time Optimization (LTO) - increases build time but can improve performance
option(ENABLE_LTO "Enable Link-Time Optimization for Release builds (increases build time)" OFF)
if (ENABLE_LTO AND CMAKE_BUILD_TYPE STREQUAL "Release")
    include(CheckCXXCompilerFlag)
    check_cxx_compiler_flag("-flto" COMPILER_SUPPORTS_LTO)
    
    if (COMPILER_SUPPORTS_LTO)
        set(CMAKE_INTERPROCEDURAL_OPTIMIZATION ON)
        message(STATUS "Enabled Link-Time Optimization (LTO)")
    else ()
        message(WARNING "LTO requested but not supported by compiler")
    endif ()
endif ()


# Find required packages
find_package(Threads REQUIRED)

# Include cpp warnings after project setup
include(cmake/cpp_warnings.cmake)

# libvcai is now built locally by each subproject

option(BUILD_ENCODER_CLI "Build the encoder_cli" OFF)
option(BUILD_RTP_SERVER "Build the rtp_server" OFF)
option(BUILD_VCAI_DECODER "Build the vcai_decoder plugin" OFF)
option(BUILD_TESTS "Build the tests" OFF)

if (BUILD_ENCODER_CLI)
    add_subdirectory(encoder_cli)
endif ()

if (BUILD_RTP_SERVER)
    add_subdirectory(rtp_server)
endif ()

if (BUILD_VCAI_DECODER)
    add_subdirectory(vcai_decoder)
endif ()

if (BUILD_TESTS)
    add_subdirectory(test)
endif ()
