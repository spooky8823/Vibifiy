cmake_minimum_required(VERSION 3.21)
project(Vibrium LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT DEFINED CEF_ROOT OR NOT EXISTS "${CEF_ROOT}/CMakeLists.txt")
    message(FATAL_ERROR
        "CEF_ROOT must point to an extracted CEF binary distribution.")
endif()

add_subdirectory("${CEF_ROOT}" "${CMAKE_BINARY_DIR}/cef")

add_executable(vibrium WIN32
    src/main.cc
)

target_include_directories(vibrium PRIVATE
    "${CEF_ROOT}"
)

target_link_libraries(vibrium PRIVATE
    libcef_lib
    libcef_dll_wrapper
)

if(TARGET libcef_dll_wrapper)
    set_target_properties(libcef_dll_wrapper PROPERTIES
        CXX_STANDARD 20
        CXX_STANDARD_REQUIRED ON
    )
endif()
