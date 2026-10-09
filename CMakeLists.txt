cmake_minimum_required(VERSION 4.2)
cmake_policy(SET CMP0091 NEW)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
set(CMAKE_USER_MAKE_RULES_OVERRIDE_CXX "${CMAKE_CURRENT_SOURCE_DIR}/cmake/msvc_standard.cmake")
project(Loadbar VERSION 1.1.5 LANGUAGES CXX RC)
configure_file(resources/loadbar_version.h.in generated/loadbar_version.h @ONLY)

if(NOT WIN32 OR NOT MSVC OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "Loadbar requires Windows and the pinned x64 MSVC developer environment")
endif()
if(NOT CMAKE_VERSION VERSION_EQUAL "4.4.4")
    message(FATAL_ERROR "The tested CMake pin is 4.4.4")
endif()
if(NOT "$ENV{WindowsSDKVersion}" MATCHES "^10\\.0\\.28000\\.0")
    message(FATAL_ERROR "Select Windows SDK 10.0.28000.0 using scripts/enter-dev-shell.ps1")
endif()
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED YES)
set(CMAKE_CXX_EXTENSIONS NO)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
option(LOADBAR_ASAN "Instrument all project objects with MSVC ASan" OFF)
option(LOADBAR_ANALYZE "Run MSVC native analysis during compilation" OFF)

include(CheckCXXSourceCompiles)
check_cxx_source_compiles("#include <expected>
#include <thread>
#include <condition_variable>
#include <format>
#include <chrono>
int main() {
 std::expected<int, int> value(42);
 auto text = std::format(\"{}\", *value);
 std::mutex mutex; std::condition_variable_any changed;
 std::jthread worker([&](std::stop_token token) {
  std::unique_lock lock(mutex);
  changed.wait_for(lock, token, std::chrono::milliseconds(1), [] { return false; });
 });
 return text.empty();
}" LOADBAR_REQUIRED_FEATURES)
if(NOT LOADBAR_REQUIRED_FEATURES)
    message(FATAL_ERROR "Required compiler/STL features failed")
endif()

add_library(loadbar_options INTERFACE)
target_compile_definitions(loadbar_options INTERFACE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX
    WINVER=0x0A00 _WIN32_WINNT=0x0A00)
target_compile_options(loadbar_options INTERFACE
    $<$<COMPILE_LANGUAGE:CXX>:/W4;/WX;/permissive-;/Zc:__cplusplus;/Zc:preprocessor;/utf-8;/EHsc;/GS;/guard:cf;/external:env:INCLUDE;/external:W0>)
target_link_options(loadbar_options INTERFACE /DYNAMICBASE /NXCOMPAT /HIGHENTROPYVA /guard:cf
    /INCREMENTAL:NO)
target_compile_options(loadbar_options INTERFACE $<$<AND:$<COMPILE_LANGUAGE:CXX>,$<CONFIG:Release>>:/Zi>)
target_link_options(loadbar_options INTERFACE $<$<CONFIG:Release>:/DEBUG:FULL;/OPT:REF;/OPT:ICF>)
if(LOADBAR_ASAN)
    target_compile_options(loadbar_options INTERFACE $<$<COMPILE_LANGUAGE:CXX>:/fsanitize=address;/Zi>)
endif()
if(LOADBAR_ANALYZE)
    target_compile_options(loadbar_options INTERFACE $<$<COMPILE_LANGUAGE:CXX>:/analyze;/analyze:external->)
endif()

add_library(loadbar_model STATIC src/model/model.cpp src/model/settings.cpp src/model/settings_form.cpp src/model/geometry.cpp src/model/placement.cpp src/model/snapshot.cpp src/model/layout.cpp src/model/presentation.cpp src/model/session_peaks.cpp src/model/continuity.cpp)
target_include_directories(loadbar_model PUBLIC src)
target_link_libraries(loadbar_model PUBLIC loadbar_options)
add_library(loadbar_platform STATIC src/platform/desktop.cpp)
target_link_libraries(loadbar_platform PUBLIC loadbar_model user32 shell32 shcore advapi32)
add_library(loadbar_telemetry STATIC src/telemetry/pdh.cpp src/telemetry/discovery.cpp src/telemetry/collector.cpp src/telemetry/network.cpp)
target_link_libraries(loadbar_telemetry PUBLIC loadbar_platform pdh setupapi iphlpapi ws2_32 ole32 dxgi dxcore)
add_library(loadbar_ui STATIC src/ui/renderer.cpp src/ui/text_preferences.cpp)
target_link_libraries(loadbar_ui PUBLIC loadbar_platform d2d1 dwrite runtimeobject)
add_executable(loadbar WIN32 src/app/main.cpp src/app/application.cpp src/app/settings_ui.cpp resources/loadbar.rc)
target_include_directories(loadbar PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
# RC's file-backed resources are not discovered by Ninja's C++ dependency scanner.
set_property(SOURCE resources/loadbar.rc APPEND PROPERTY OBJECT_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/resources/loadbar.ico"
    "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE"
    "${CMAKE_CURRENT_BINARY_DIR}/generated/loadbar_version.h")
set_target_properties(loadbar PROPERTIES OUTPUT_NAME Loadbar)
target_link_libraries(loadbar PRIVATE loadbar_telemetry loadbar_ui wtsapi32 comctl32)
target_link_options(loadbar PRIVATE /MANIFEST:EMBED
    "/MANIFESTINPUT:${CMAKE_CURRENT_SOURCE_DIR}/resources/loadbar.manifest")

enable_testing()
add_executable(loadbar_tests tests/tests.cpp tests/regression.cpp tests/placement.cpp tests/resources.cpp tests/design.cpp tests/metrics_upgrade.cpp tests/continuity.cpp tests/optimization_model.cpp tests/optimization_app.cpp tests/telemetry_optimization.cpp tests/discovery.cpp)
target_link_libraries(loadbar_tests PRIVATE loadbar_telemetry version)
target_include_directories(loadbar_tests PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
add_dependencies(loadbar_tests loadbar)
add_test(NAME loadbar-unit COMMAND loadbar_tests)
add_test(NAME loadbar-resources COMMAND loadbar_tests --artifact $<TARGET_FILE:loadbar> "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
set_tests_properties(loadbar-unit loadbar-resources PROPERTIES TIMEOUT 60)
add_executable(loadbar_render_tests tests/rendering.cpp)
target_link_libraries(loadbar_render_tests PRIVATE loadbar_ui windowscodecs ole32)
add_test(NAME loadbar-rendering COMMAND loadbar_render_tests "${CMAKE_CURRENT_BINARY_DIR}/render-fixtures")
set_tests_properties(loadbar-rendering PROPERTIES TIMEOUT 60)

# Explicit documentation fixture only; never linked into the application or normal tests.
add_executable(loadbar_readme_frames EXCLUDE_FROM_ALL tests/readme_frames.cpp)
# No modules are used. Keep its compile command usable by tidy before this opt-in target
# is built; otherwise CMake adds a response file generated only by the optional build.
set_target_properties(loadbar_readme_frames PROPERTIES CXX_SCAN_FOR_MODULES OFF)
target_link_libraries(loadbar_readme_frames PRIVATE loadbar_ui windowscodecs ole32)

add_executable(loadbar_settings_tests tests/settings_window.cpp src/app/application.cpp src/app/settings_ui.cpp)
target_link_libraries(loadbar_settings_tests PRIVATE loadbar_telemetry loadbar_ui wtsapi32 comctl32)
target_link_options(loadbar_settings_tests PRIVATE /MANIFEST:EMBED
    "/MANIFESTINPUT:${CMAKE_CURRENT_SOURCE_DIR}/resources/loadbar.manifest")
# /MANIFESTINPUT alone is only a linker option, not a Ninja file dependency.
foreach(target IN ITEMS loadbar loadbar_settings_tests)
    set_property(TARGET ${target} APPEND PROPERTY LINK_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/resources/loadbar.manifest")
endforeach()
add_test(NAME loadbar-settings COMMAND loadbar_settings_tests)
set_tests_properties(loadbar-settings PROPERTIES TIMEOUT 60)

find_program(LOADBAR_POWERSHELL NAMES pwsh REQUIRED)
add_custom_target(format-check COMMAND "${LOADBAR_POWERSHELL}" -NoProfile -File
    "${CMAKE_CURRENT_SOURCE_DIR}/scripts/check-tools.ps1" -Action Format
    -BuildDirectory "${CMAKE_CURRENT_BINARY_DIR}" VERBATIM)
add_custom_target(tidy COMMAND "${LOADBAR_POWERSHELL}" -NoProfile -File
    "${CMAKE_CURRENT_SOURCE_DIR}/scripts/check-tools.ps1" -Action Tidy
    -BuildDirectory "${CMAKE_CURRENT_BINARY_DIR}" VERBATIM)
if(NOT LOADBAR_ANALYZE)
    add_custom_target(analyze
        COMMAND "${CMAKE_COMMAND}" -S "${CMAKE_CURRENT_SOURCE_DIR}"
            -B "${CMAKE_CURRENT_BINARY_DIR}/analyze" -G Ninja
            -DCMAKE_BUILD_TYPE=Debug -DLOADBAR_ANALYZE=ON
        COMMAND "${CMAKE_COMMAND}" --build "${CMAKE_CURRENT_BINARY_DIR}/analyze"
        VERBATIM)
endif()
