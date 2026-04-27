# CMakeLists.txt — build ExtIO_PlutoSDR.dll for Win32 (x86) using MSVC.
#
# Configure with:
#   cmake -S . -B build -A Win32 \
#         -DLIBIIO_ROOT=<path-with-include-and-lib-subdirs>
# Build with:
#   cmake --build build --config Release

cmake_minimum_required(VERSION 3.20)
project(ExtIO_PlutoSDR LANGUAGES CXX C)

if(NOT WIN32)
    message(FATAL_ERROR "ExtIO_PlutoSDR is a Windows DLL — configure on Windows / MSVC.")
endif()

# HDSDR is 32-bit, so the DLL must be 32-bit too.
if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(WARNING "Building 64-bit. HDSDR is 32-bit — pass `-A Win32`.")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Tell MSVC we want a static CRT-free build by default.
if(MSVC)
    add_compile_options(/W3 /EHsc /utf-8)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS UNICODE _UNICODE NOMINMAX)
endif()

# --- Locate libiio ---------------------------------------------------------
# Pass -DLIBIIO_ROOT=... pointing at a folder that contains:
#   include/iio.h
#   lib/libiio.lib       (32-bit MSVC import lib)
#
# The GitHub Actions workflow extracts the official libiio Windows installer
# and points LIBIIO_ROOT at the unpacked tree.

if(NOT DEFINED LIBIIO_ROOT)
    message(FATAL_ERROR "Set LIBIIO_ROOT to the libiio install root (with include/ and lib/).")
endif()

find_path(LIBIIO_INCLUDE_DIR iio.h
          HINTS "${LIBIIO_ROOT}/include" "${LIBIIO_ROOT}"
          NO_DEFAULT_PATH)
find_library(LIBIIO_LIBRARY libiio
             HINTS "${LIBIIO_ROOT}/lib" "${LIBIIO_ROOT}"
             NO_DEFAULT_PATH)

if(NOT LIBIIO_INCLUDE_DIR OR NOT LIBIIO_LIBRARY)
    message(FATAL_ERROR
        "libiio not found under LIBIIO_ROOT='${LIBIIO_ROOT}'. "
        "Need include/iio.h and lib/libiio.lib.")
endif()

message(STATUS "libiio include : ${LIBIIO_INCLUDE_DIR}")
message(STATUS "libiio library : ${LIBIIO_LIBRARY}")

# --- Target ----------------------------------------------------------------

add_library(ExtIO_PlutoSDR SHARED
    src/ExtIO_PlutoSDR.cpp
    src/ExtIO_PlutoSDR.rc
    src/ExtIO_PlutoSDR.def
)

target_include_directories(ExtIO_PlutoSDR PRIVATE
    "${LIBIIO_INCLUDE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

target_link_libraries(ExtIO_PlutoSDR PRIVATE
    "${LIBIIO_LIBRARY}"
    comctl32
    user32
)

set_target_properties(ExtIO_PlutoSDR PROPERTIES
    PREFIX ""
    OUTPUT_NAME "ExtIO_PlutoSDR"
    SUFFIX ".dll"
)

# Useful for the install/zip step in CI: dump the build path.
install(TARGETS ExtIO_PlutoSDR RUNTIME DESTINATION .)
