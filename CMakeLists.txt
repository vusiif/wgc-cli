cmake_minimum_required(VERSION 3.24)
project(wgccli LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT WIN32 OR NOT MSVC)
    message(FATAL_ERROR "wgccli requires Windows and an MSVC-compatible toolchain")
endif()

# Locate Windows SDK and C++/WinRT headers
set(WINSDK_INCLUDE_BASE "" CACHE PATH "Optional Windows SDK Include/<version> override")
if(NOT WINSDK_INCLUDE_BASE)
    get_filename_component(WINSDK_ROOT
        "[HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Windows Kits\\Installed Roots;KitsRoot10]" ABSOLUTE)
    # Prefer the SDK selected by the developer shell or Visual Studio generator.
    set(SDK_CANDIDATES
        "$ENV{WindowsSdkDir}/Include/$ENV{WindowsSDKVersion}"
        "${WINSDK_ROOT}/Include/${CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION}")
    file(GLOB INSTALLED_SDKS LIST_DIRECTORIES true "${WINSDK_ROOT}/Include/10.*")
    list(SORT INSTALLED_SDKS COMPARE NATURAL ORDER DESCENDING)
    list(APPEND SDK_CANDIDATES ${INSTALLED_SDKS})
    foreach(candidate IN LISTS SDK_CANDIDATES)
        if(EXISTS "${candidate}/cppwinrt/winrt/base.h" AND EXISTS "${candidate}/um/windows.h")
            set(WINSDK_INCLUDE_BASE "${candidate}")
            break()
        endif()
    endforeach()
endif()
set(CPPWINRT_INCLUDE_DIR "${WINSDK_INCLUDE_BASE}/cppwinrt")

if (NOT EXISTS "${CPPWINRT_INCLUDE_DIR}/winrt/base.h")
    message(FATAL_ERROR
        "C++/WinRT headers not found at ${CPPWINRT_INCLUDE_DIR}\n"
        "Install a Windows SDK with C++/WinRT, or set WINSDK_INCLUDE_BASE to Include/<version>.")
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
    src/diagnose.cpp
    src/mcp_server.cpp
    src/capture_common.cpp
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

# Size optimization for release builds
if(MSVC)
  target_compile_options(wgccli PRIVATE
    $<$<CONFIG:Release>:/O1>      # size-optimized code
    $<$<CONFIG:Release>:/GL>      # whole program optimization
    $<$<CONFIG:Release>:/Gy>      # function-level linking
    $<$<CONFIG:Release>:/Gw>      # global data optimization
    $<$<CONFIG:Release>:/GF>      # merge duplicate strings
    $<$<CONFIG:Release>:/Zc:inline>
  )
  target_link_options(wgccli PRIVATE
    $<$<CONFIG:Release>:/LTCG>
    $<$<CONFIG:Release>:/OPT:REF>
    $<$<CONFIG:Release>:/OPT:ICF>
    $<$<CONFIG:Release>:/INCREMENTAL:NO>
  )
endif()

target_link_libraries(wgccli PRIVATE
    d3d11
    dxgi
    windowsapp
    windowscodecs
    runtimeobject
)

option(WGC_BUILD_TESTS "Build regression tests (no desktop capture)" OFF)
if(WGC_BUILD_TESTS)
  enable_testing()
  get_target_property(WGC_SOURCES wgccli SOURCES)
  list(REMOVE_ITEM WGC_SOURCES src/main.cpp src/mcp_server.cpp)
  add_executable(wgccli_regression tests/regression.cpp ${WGC_SOURCES})
  foreach(prop INCLUDE_DIRECTORIES COMPILE_DEFINITIONS COMPILE_OPTIONS LINK_LIBRARIES LINK_OPTIONS)
    get_target_property(value wgccli ${prop})
    if(value)
      set_property(TARGET wgccli_regression PROPERTY ${prop} "${value}")
    endif()
  endforeach()
  foreach(case json overflow output mcp_args)
    add_test(NAME ${case} COMMAND wgccli_regression ${case})
  endforeach()
  find_program(PWSH_EXECUTABLE pwsh)
  if(PWSH_EXECUTABLE)
    add_test(NAME protocol COMMAND "${PWSH_EXECUTABLE}" -NoProfile -File
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/protocol.ps1" -Executable "$<TARGET_FILE:wgccli>")
    set_tests_properties(protocol PROPERTIES TIMEOUT 30)
  endif()
endif()
