
cmake_minimum_required(VERSION 3.21)

# Geode v5 requires C++23
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if ("${CMAKE_SYSTEM_NAME}" STREQUAL "iOS" OR IOS)
    set(CMAKE_OSX_ARCHITECTURES "arm64")
else()
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64")
endif()

project(MinixxsClippy VERSION 1.0.0)

# Plain C++ sources + Objective-C++ file for ReplayKit / VideoToolbox
file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS src/*.cpp)
add_library(${PROJECT_NAME} SHARED ${SOURCES} src/Native.mm)

# Objective-C++ configuration
set_source_files_properties(src/Native.mm PROPERTIES
    SKIP_PRECOMPILE_HEADERS ON
    COMPILE_FLAGS "-fobjc-arc"
)

# Locate Geode SDK
if (NOT DEFINED ENV{GEODE_SDK})
    message(FATAL_ERROR "Unable to find Geode SDK! Please define GEODE_SDK environment variable to point to Geode")
else()
    message(STATUS "Found Geode: $ENV{GEODE_SDK}")
endif()

# Add Geode SDK
add_subdirectory($ENV{GEODE_SDK} ${CMAKE_CURRENT_BINARY_DIR}/geode)

# Fix missing FMOD declarations in generated iOS bindings
if (IOS OR "${CMAKE_SYSTEM_NAME}" STREQUAL "iOS")
    if (TARGET GeodeBindings)
        target_compile_options(GeodeBindings PRIVATE
            -include Geode/fmod/fmod.hpp
        )
    endif()
endif()

# iOS frameworks
if ("${CMAKE_SYSTEM_NAME}" STREQUAL "iOS" OR IOS)
    target_link_libraries(${PROJECT_NAME}
        "-framework Foundation"
        "-framework UIKit"
        "-framework ReplayKit"
        "-framework AVFoundation"
        "-framework AudioToolbox"
        "-framework CoreMedia"
        "-framework CoreVideo"
        "-framework VideoToolbox"
        "-framework CoreGraphics"
    )
endif()

# Setup Geode mod
setup_geode_mod(${PROJECT_NAME})
