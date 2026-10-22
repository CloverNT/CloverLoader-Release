cmake_minimum_required(VERSION 3.19)

if (TARGET CloverNT::Runtime)
    return()
endif ()

set(_clover_root "${CMAKE_CURRENT_LIST_DIR}")

if (WIN32)
    set(_clover_system CloverNT_SYSTEM_WINDOWS)
    set(_clover_osdir  windows)
else ()
    set(_clover_system CloverNT_SYSTEM_LINUX)
    set(_clover_osdir  linux)
endif ()

if (MSVC AND CMAKE_GENERATOR_PLATFORM)
    set(_clover_arch_raw "${CMAKE_GENERATOR_PLATFORM}")
elseif (CMAKE_SYSTEM_PROCESSOR)
    set(_clover_arch_raw "${CMAKE_SYSTEM_PROCESSOR}")
else ()
    set(_clover_arch_raw "${CMAKE_HOST_SYSTEM_PROCESSOR}")
endif ()
string(TOLOWER "${_clover_arch_raw}" _clover_arch_raw)
if (_clover_arch_raw MATCHES "arm64|aarch64")
    set(_clover_arch   CloverNT_ARCH_ARM64)
    set(_clover_archdir arm64)
else ()
    set(_clover_arch   CloverNT_ARCH_X64)
    set(_clover_archdir x64)
endif ()

if (WIN32)
    set(_clover_link "${_clover_root}/lib/${_clover_osdir}-${_clover_archdir}/CloverLoader.lib")
else ()
    set(_clover_link "${_clover_root}/lib/${_clover_osdir}-${_clover_archdir}/libCloverLoader.so")
endif ()
if (NOT EXISTS "${_clover_link}")
    message(FATAL_ERROR
            "CloverLoader: no link artifact for ${_clover_osdir}-${_clover_archdir} "
            "(expected ${_clover_link}); this SDK tag ships only the platforms it was built for.")
endif ()

add_library(CloverNT_Runtime UNKNOWN IMPORTED GLOBAL)
set_target_properties(CloverNT_Runtime PROPERTIES
        IMPORTED_LOCATION             "${_clover_link}"
        INTERFACE_INCLUDE_DIRECTORIES "${_clover_root}/include"
        INTERFACE_COMPILE_DEFINITIONS "${_clover_system};${_clover_arch}")
add_library(CloverNT::Runtime ALIAS CloverNT_Runtime)