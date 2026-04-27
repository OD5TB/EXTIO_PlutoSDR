cmake_minimum_required(VERSION 3.20)
project(ExtIO_PlutoSDR LANGUAGES CXX C)

if(NOT WIN32)
    message(FATAL_ERROR "ExtIO_PlutoSDR is a Windows DLL — configure on Windows / MSVC.")
endif()

if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(WARNING "Building 64-bit. HDSDR is 32-bit — pass `-A Win32`.")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(MSVC)
    add_compile_options(/W3 /EHsc /utf-8)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS UNICODE _UNICODE NOMINMAX)
endif()

if(NOT DEFINED LIBIIO_ROOT)
    message(FATAL_ERROR "Set LIBIIO_ROOT to the libiio install root (with include/ and lib/).")
endif()

find_path(LIBIIO_INCLUDE_DIR iio.h
          HINTS "${LIBIIO_ROOT}/include" "${LIBIIO_ROOT}"
          NO_DEFAULT_PATH)
find_library(LIBIIO_LIBRARY
             NAMES libiio iio
             HINTS "${LIBIIO_ROOT}/lib" "${LIBIIO_ROOT}"
             NO_DEFAULT_PATH)

if(NOT LIBIIO_INCLUDE_DIR OR NOT LIBIIO_LIBRARY)
    message(FATAL_ERROR
        "libiio not found under LIBIIO_ROOT='${LIBIIO_ROOT}'. "
        "Need include/iio.h and lib/{libiio,iio}.lib.")
endif()

message(STATUS "libiio include : ${LIBIIO_INCLUDE_DIR}")
message(STATUS "libiio library : ${LIBIIO_LIBRARY}")

# Sources may live at the repo root OR in src/ — pick whichever exists.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/ExtIO_PlutoSDR.cpp")
    set(SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src")
else()
    set(SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
endif()
message(STATUS "Sources from: ${SRC_DIR}")

add_library(ExtIO_PlutoSDR SHARED
    "${SRC_DIR}/ExtIO_PlutoSDR.cpp"
    "${SRC_DIR}/ExtIO_PlutoSDR.rc"
    "${SRC_DIR}/ExtIO_PlutoSDR.def"
)

target_include_directories(ExtIO_PlutoSDR PRIVATE
    "${LIBIIO_INCLUDE_DIR}"
    "${SRC_DIR}"
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

install(TARGETS ExtIO_PlutoSDR RUNTIME DESTINATION .)
