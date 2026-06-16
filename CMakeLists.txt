cmake_minimum_required(VERSION 3.24)
project(wgccli LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# MSVC check deferred to after compiler detection
# if (NOT CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
#     message(FATAL_ERROR "wgccli requires MSVC on Windows")
# endif()

# Locate Windows SDK and C++/WinRT headers
set(WINSDK_INCLUDE_BASE "C:/Program Files (x86)/Windows Kits/10/Include/10.0.26100.0")
set(CPPWINRT_INCLUDE_DIR "${WINSDK_INCLUDE_BASE}/cppwinrt")

if (NOT EXISTS "${CPPWINRT_INCLUDE_DIR}/winrt/base.h")
    message(FATAL_ERROR
        "C++/WinRT headers not found at ${CPPWINRT_INCLUDE_DIR}\n"
        "Ensure the Windows 10 SDK 10.0.26100.0 is installed.")
endif()

add_executable(wgccli
    src/main.cpp
    src/cli_args.cpp
    src/window_enum.cpp
    src/capture_wgc.cpp
    src/d3d_helpers.cpp
    src/image_wic.cpp
    src/output.cpp
    src/server.cpp
    src/client.cpp
)

target_include_directories(wgccli PRIVATE
    ${CPPWINRT_INCLUDE_DIR}
    ${WINSDK_INCLUDE_BASE}/um
    ${WINSDK_INCLUDE_BASE}/shared
    ${WINSDK_INCLUDE_BASE}/winrt
)

target_compile_definitions(wgccli PRIVATE
    UNICODE
    _UNICODE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
    _SILENCE_ALL_CXX17_DEPRECATION_WARNINGS
)

target_compile_options(wgccli PRIVATE /bigobj /EHsc)

target_link_libraries(wgccli PRIVATE
    d3d11
    dxgi
    windowsapp
    windowscodecs
    runtimeobject
)
