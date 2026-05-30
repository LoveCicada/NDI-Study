set(_NDI_SDK_HINTS
    ENV NDI_SDK_DIR
    "${CMAKE_SOURCE_DIR}/third_party/NDI 6 Advanced SDK"
    "${CMAKE_SOURCE_DIR}/third_party/NDI_Advanced_SDK"
    "${CMAKE_SOURCE_DIR}/third_party/NDI_SDK")

find_path(NDI_INCLUDE_DIR
    NAMES Processing.NDI.Lib.h
    HINTS ${_NDI_SDK_HINTS}
    PATH_SUFFIXES Include include)

find_library(NDI_LIBRARY
    NAMES
        Processing.NDI.Lib.Advanced.x64
        Processing.NDI.Lib.x64
        Processing.NDI.Lib
    HINTS ${_NDI_SDK_HINTS}
    PATH_SUFFIXES Lib/x64 lib/x64 Lib lib)

find_file(NDI_DLL_PATH
    NAMES
        Processing.NDI.Lib.Advanced.x64.dll
        Processing.NDI.Lib.x64.dll
    HINTS
        ENV NDI_RUNTIME_DIR_V6
        ${_NDI_SDK_HINTS}
    PATH_SUFFIXES Bin/x64 bin/x64 Bin bin)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(NDI
    REQUIRED_VARS NDI_INCLUDE_DIR NDI_LIBRARY)

if(NDI_FOUND AND NOT TARGET NDI::Lib)
    add_library(NDI::Lib UNKNOWN IMPORTED)
    set_target_properties(NDI::Lib PROPERTIES
        IMPORTED_LOCATION "${NDI_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${NDI_INCLUDE_DIR}")
endif()

mark_as_advanced(NDI_INCLUDE_DIR NDI_LIBRARY NDI_DLL_PATH)
