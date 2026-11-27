cmake_minimum_required(VERSION 3.21)
project(Vibrium LANGUAGES CXX)

if(WIN32)
    enable_language(RC)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT DEFINED CEF_ROOT OR NOT EXISTS "${CEF_ROOT}/CMakeLists.txt")
    message(FATAL_ERROR
        "CEF_ROOT must point to an extracted CEF binary distribution.")
endif()

# Match the CEF DLL wrapper's static MSVC runtime (/MT).
set(CMAKE_MSVC_RUNTIME_LIBRARY
    "MultiThreaded$<$<CONFIG:Debug>:Debug>"
)

add_subdirectory("${CEF_ROOT}" "${CMAKE_BINARY_DIR}/cef")

# Generate a standard multi-size-compatible Windows icon resource from the
# original artwork. The embedded ICO supplies Explorer/file/shortcut icons;
# main.cc still creates the live accent-tinted window/taskbar icon at runtime.
set(VIBRIUM_ICON_ICO "${CMAKE_CURRENT_BINARY_DIR}/vibrium_app.ico")
execute_process(
    COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass
        -File "${CMAKE_CURRENT_SOURCE_DIR}/tools/png_to_ico.ps1"
        -InputPath "${CMAKE_CURRENT_SOURCE_DIR}/icons/appicon.png"
        -OutputPath "${VIBRIUM_ICON_ICO}"
    RESULT_VARIABLE VIBRIUM_ICON_RESULT
    OUTPUT_VARIABLE VIBRIUM_ICON_OUTPUT
    ERROR_VARIABLE VIBRIUM_ICON_ERROR
)
if(NOT VIBRIUM_ICON_RESULT EQUAL 0 OR NOT EXISTS "${VIBRIUM_ICON_ICO}")
    message(FATAL_ERROR
        "Failed to generate Vibrium executable icon.\n"
        "${VIBRIUM_ICON_OUTPUT}\n${VIBRIUM_ICON_ERROR}")
endif()
file(TO_CMAKE_PATH "${VIBRIUM_ICON_ICO}" VIBRIUM_ICON_FILE)
# The generated .rc is compiled from the build directory. Stage its header
# beside it so the Windows resource compiler can resolve #include "resource.h".
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/c++/resource.h"
    "${CMAKE_CURRENT_BINARY_DIR}/resource.h"
    COPYONLY
)
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/c++/vibrium.rc.in"
    "${CMAKE_CURRENT_BINARY_DIR}/vibrium.rc"
    @ONLY
)

add_executable(vibrium WIN32
    "${CMAKE_CURRENT_BINARY_DIR}/vibrium.rc"
    c++/main.cc
    c++/resource.h
    c++/vibrium.rc.in
    c++/tabbar.cc
    c++/tabbar.h
    c++/profile_picker.cc
    c++/profile_picker.h
    c++/tabbar_geometry.h
    c++/accent_icon.cc
    c++/accent_icon.h
    c++/ico_format.h
    c++/searchengine.cc
    c++/searchengine.h
    c++/settings_page.cc
    c++/settings_page.h
    c++/settings/settings_adblock.cc
    c++/settings/settings_adblock.h
    c++/settings/settings_appearance.cc
    c++/settings/settings_appearance.h
    c++/settings/settings_common.cc
    c++/settings/settings_common.h
    c++/settings/settings_console.cc
    c++/settings/settings_console.h
    c++/settings/settings_section.cc
    c++/settings/settings_section.h
    c++/settings/settings_about.cc
    c++/settings/settings_about.h
    c++/settings/settings_downloads.cc
    c++/settings/settings_downloads.h
    c++/settings/settings_privacy.cc
    c++/settings/settings_privacy.h
    c++/settings/settings_rows.cc
    c++/settings/settings_rows.h
    c++/settings/settings_updates.cc
    c++/settings/settings_updates.h
    c++/native_page.cc
    c++/native_page.h
    c++/icon_util.cc
    c++/icon_util.h
    c++/history.cc
    c++/history.h
    c++/downloads.cc
    c++/downloads.h
    c++/browsermenu.cc
    c++/browsermenu.h
    c++/update_launcher.cc
    c++/update_launcher.h
)

include("${CMAKE_CURRENT_LIST_DIR}/cmake/vibrium_adblock.cmake")

target_compile_definitions(vibrium PRIVATE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
    UNICODE
    _UNICODE
)

target_include_directories(vibrium PRIVATE
    "${CEF_ROOT}"
    "${CMAKE_CURRENT_SOURCE_DIR}/c++"
)

# Keep symbols for native crash diagnosis. These files are generated for the
# exact executable that CI packages, allowing WinDbg to resolve vibrium+RVA
# frames from ProcDump dumps without changing the shipped runtime.
if(MSVC)
    target_compile_options(vibrium PRIVATE
        "$<$<CONFIG:Release>:/Zi>"
    )
    target_link_options(vibrium PRIVATE
        "$<$<CONFIG:Release>:/DEBUG:FULL>"
        "$<$<CONFIG:Release>:/MAP>"
    )
endif()

target_link_libraries(vibrium PRIVATE
    "${CEF_ROOT}/Release/libcef.lib"
    libcef_dll_wrapper

    user32
    advapi32
    dwmapi
    gdi32
    gdiplus
    msimg32
    comctl32
    comdlg32
    crypt32
    delayimp
    rpcrt4
    shlwapi
    wintrust
    ws2_32
)

if(TARGET libcef_dll_wrapper)
    set_target_properties(libcef_dll_wrapper PROPERTIES
        CXX_STANDARD 20
        CXX_STANDARD_REQUIRED ON
    )
endif()

# Helper started by "Update Vibrium Browser": waits for the browser to exit,
# downloads the newest CI build, swaps the files in and relaunches. Static CRT,
# no CEF dependency, shipped next to vibrium.exe by CI.
add_executable(vibrium_updater WIN32 "${CMAKE_CURRENT_BINARY_DIR}/vibrium.rc" c++/updater/updater_main.cc)
target_compile_definitions(vibrium_updater PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE)
target_link_libraries(vibrium_updater PRIVATE winhttp bcrypt comctl32 user32 gdi32 shell32)