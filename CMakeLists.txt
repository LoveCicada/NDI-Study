cmake_minimum_required(VERSION 3.16)
project(NDI-Study VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")

include(CompilerSettings)

option(NDI_STUDY_BUILD_SENDER "Build NDISender" ON)
option(NDI_STUDY_BUILD_RECEIVER "Build NDIReceiver" ON)
option(NDI_STUDY_ENABLE_SDL2 "Enable SDL2 audio playback" ON)

find_package(NDI REQUIRED)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt5 REQUIRED COMPONENTS Core Widgets)

if(NDI_STUDY_ENABLE_SDL2)
    find_path(SDL2_INCLUDE_DIR SDL.h
        HINTS "${SDL2_DIR}/include" "${CMAKE_SOURCE_DIR}/third_party/SDL2/include"
        PATH_SUFFIXES SDL2 SDL)
    find_library(SDL2_LIBRARY
        NAMES SDL2
        HINTS "${SDL2_DIR}/lib" "${SDL2_DIR}/lib/x64"
              "${CMAKE_SOURCE_DIR}/third_party/SDL2/lib/x64")
    if(SDL2_INCLUDE_DIR AND SDL2_LIBRARY)
        set(NDI_STUDY_HAS_SDL2 TRUE)
        find_file(SDL2_DLL NAMES SDL2.dll
            HINTS "${SDL2_DIR}/bin" "${SDL2_DIR}/bin/x64"
                  "${CMAKE_SOURCE_DIR}/third_party/SDL2/bin/x64")
        message(STATUS "SDL2 found: ${SDL2_LIBRARY}")
    else()
        set(NDI_STUDY_HAS_SDL2 FALSE)
        message(WARNING "SDL2 not found; NDIReceiver audio playback disabled")
    endif()
endif()

add_subdirectory(common)

if(NDI_STUDY_BUILD_SENDER)
    add_subdirectory(apps/NDISender)
endif()

if(NDI_STUDY_BUILD_RECEIVER)
    add_subdirectory(apps/NDIReceiver)
endif()
