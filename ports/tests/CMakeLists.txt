cmake_minimum_required(VERSION 3.21)
project(mvc2_native_components LANGUAGES C ASM)
set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
include(CTest)
find_package(Python3 REQUIRED COMPONENTS Interpreter)
find_package(ZLIB REQUIRED)
set(PORT "${CMAKE_CURRENT_SOURCE_DIR}/../port")

# These independently testable components do not change the existing game's
# 32-bit ABI requirement or claim that the complete game is ARM64-ready.
add_library(mvc2_ios_assets STATIC
    "${PORT}/ios/assets/mvc2_rom.c" "${PORT}/ios/assets/zip_reader.c")
target_include_directories(mvc2_ios_assets PUBLIC "${PORT}/ios/assets")
target_link_libraries(mvc2_ios_assets PUBLIC ZLIB::ZLIB)
target_compile_options(mvc2_ios_assets PRIVATE -Wall -Wextra -Wpedantic -Werror)
add_library(mvc2_ios_controls STATIC "${PORT}/ios/input/input.c" "${PORT}/ios/display/viewport.c")
target_include_directories(mvc2_ios_controls PUBLIC "${PORT}/ios" "${PORT}/include")
target_compile_options(mvc2_ios_controls PRIVATE -Wall -Wextra -Wpedantic -Werror)
target_link_libraries(mvc2_ios_controls PUBLIC m)
foreach(component IN ITEMS input viewport)
    add_executable(test_ios_${component} test_ios_${component}.c)
    target_link_libraries(test_ios_${component} PRIVATE mvc2_ios_controls)
    target_compile_options(test_ios_${component} PRIVATE -UNDEBUG -Wall -Wextra -Werror)
    add_test(NAME ios_${component} COMMAND test_ios_${component})
endforeach()
add_test(NAME ios_asset_import COMMAND "${CMAKE_COMMAND}" -E env
    "CC=${CMAKE_C_COMPILER}" "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/test_ios_rom_import.py")
add_test(NAME arm64_audit_tool COMMAND "${CMAKE_COMMAND}" -E env
    "CC=${CMAKE_C_COMPILER}" "${Python3_EXECUTABLE}" "${PORT}/tools/test_audit_arm64.py")

if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64|ARM64)$")
    find_package(Threads REQUIRED)
    add_executable(test_coro test_coro.c coro_registers_arm64.S
        "${PORT}/sys/coro_arm64.c" "${PORT}/sys/coro_arm64.S")
    target_include_directories(test_coro PRIVATE "${PORT}/sys")
    target_link_libraries(test_coro PRIVATE Threads::Threads)
    target_compile_options(test_coro PRIVATE -UNDEBUG -Wall -Wextra -Werror)
    add_test(NAME arm64_coroutine COMMAND test_coro)
else()
    message(STATUS "ARM64 coroutine execution requires an ARM64 host; not registered on this architecture")
endif()
