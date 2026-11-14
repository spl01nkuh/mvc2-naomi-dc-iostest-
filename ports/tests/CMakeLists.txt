cmake_minimum_required(VERSION 3.21)
project(mvc2_native_components LANGUAGES C ASM)
set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
include(CTest)
find_package(Python3 REQUIRED COMPONENTS Interpreter)
find_package(ZLIB REQUIRED)
find_package(Threads REQUIRED)
set(PORT "${CMAKE_CURRENT_SOURCE_DIR}/../port")
get_filename_component(GAME "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)

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
add_library(mvc2_native_memory STATIC "${PORT}/native64/memory.c")
target_include_directories(mvc2_native_memory PUBLIC "${PORT}/native64")
target_compile_options(mvc2_native_memory PRIVATE -Wall -Wextra -Wpedantic -Werror)
add_executable(test_native64_memory test_native64_memory.c)
target_link_libraries(test_native64_memory PRIVATE mvc2_native_memory)
target_compile_options(test_native64_memory PRIVATE -UNDEBUG -Wall -Wextra -Werror)
add_test(NAME native64_memory COMMAND test_native64_memory)
add_library(mvc2_native_game_memory STATIC "${PORT}/native64/game_memory.c")
target_include_directories(mvc2_native_game_memory PUBLIC "${PORT}/native64" "${PORT}/include")
target_compile_options(mvc2_native_game_memory PRIVATE -Wall -Wextra -Wpedantic -Werror)
target_link_libraries(mvc2_native_game_memory PUBLIC mvc2_native_memory)
add_executable(test_native64_game_memory test_native64_game_memory.c)
target_link_libraries(test_native64_game_memory PRIVATE mvc2_native_game_memory)
target_compile_options(test_native64_game_memory PRIVATE -UNDEBUG -Wall -Wextra -Werror)
add_test(NAME native64_game_memory COMMAND test_native64_game_memory)
add_library(mvc2_native_model STATIC "${PORT}/native64/model.c")
target_include_directories(mvc2_native_model PUBLIC "${PORT}/native64" "${PORT}/nl" "${PORT}/render" "${PORT}/include")
target_compile_options(mvc2_native_model PRIVATE -Wall -Wextra -Wpedantic -Werror)
add_library(mvc2_native_model_store STATIC "${PORT}/native64/model_store.c")
target_include_directories(mvc2_native_model_store PUBLIC "${PORT}/native64")
target_compile_options(mvc2_native_model_store PRIVATE -Wall -Wextra -Wpedantic -Werror)
target_link_libraries(mvc2_native_model_store PUBLIC mvc2_native_model mvc2_native_game_memory)
add_executable(test_native64_model test_native64_model.c
    "${PORT}/nl/nl_tex.c" "${PORT}/naomi/naomi_board.c")
target_include_directories(test_native64_model PRIVATE "${PORT}/include" "${PORT}/sys" "${PORT}/snd" "${GAME}/include")
target_compile_definitions(test_native64_model PRIVATE HOST_NATIVE64=1 _DEFAULT_SOURCE=1 _DARWIN_C_SOURCE=1)
target_compile_options(test_native64_model PRIVATE -UNDEBUG -include "${PORT}/include/port_prefix.h" -ffunction-sections -fdata-sections)
target_link_libraries(test_native64_model PRIVATE mvc2_native_model m)
if(APPLE)
    target_link_options(test_native64_model PRIVATE "LINKER:-dead_strip")
else()
    target_link_options(test_native64_model PRIVATE "LINKER:--gc-sections")
endif()
add_test(NAME native64_model COMMAND test_native64_model)
add_library(mvc2_native_texture_files STATIC "${PORT}/native64/texture_file.c"
    "${PORT}/native64/system_textures.c")
target_include_directories(mvc2_native_texture_files PUBLIC "${PORT}/native64" "${PORT}/include" "${PORT}/nl" "${PORT}/render")
target_compile_options(mvc2_native_texture_files PRIVATE -Wall -Wextra -Wpedantic -Werror)
add_executable(test_native64_texture_file test_native64_texture_file.c
    "${PORT}/nl/nl_tex.c" "${PORT}/nl/nl_sprite.c" "${PORT}/nl/nl_strip.c" "${PORT}/naomi/naomi_board.c")
target_include_directories(test_native64_texture_file PRIVATE "${PORT}/include" "${PORT}/sys" "${PORT}/snd")
target_compile_definitions(test_native64_texture_file PRIVATE _DEFAULT_SOURCE=1 _DARWIN_C_SOURCE=1)
target_compile_options(test_native64_texture_file PRIVATE -UNDEBUG -ffunction-sections -fdata-sections)
target_link_libraries(test_native64_texture_file PRIVATE mvc2_native_texture_files m)
if(APPLE)
    target_link_options(test_native64_texture_file PRIVATE "LINKER:-dead_strip")
else()
    target_link_options(test_native64_texture_file PRIVATE "LINKER:--gc-sections")
endif()
add_test(NAME native64_texture_file COMMAND test_native64_texture_file)

# The same CPU renderer used by the existing game, with a Darwin/POSIX worker
# implementation. No SDL or GPU-window dependency is required for iOS frames.
add_library(mvc2_native_renderer STATIC "${PORT}/render/rd.c"
    "${PORT}/render/rd_soft.c" "${PORT}/render/rd_posix.c")
target_include_directories(mvc2_native_renderer PUBLIC "${PORT}/render")
target_compile_definitions(mvc2_native_renderer PRIVATE RD_NATIVE_SYSTEM=1)
target_compile_options(mvc2_native_renderer PRIVATE -Wall -Wextra -Werror -ffp-contract=off)
target_link_libraries(mvc2_native_renderer PUBLIC Threads::Threads m)
add_executable(test_native64_render test_native64_render.c
    "${PORT}/nl/nl_tex.c" "${PORT}/nl/nl_obj.c" "${PORT}/nl/nl_xmtr.c"
    "${PORT}/nl/nl_sys.c" "${PORT}/nl/nl_work.c" "${PORT}/naomi/naomi_board.c"
    "${GAME}/src/lib/libnaomi/nlproj.c")
target_include_directories(test_native64_render PRIVATE "${PORT}/include" "${PORT}/sys"
    "${PORT}/snd" "${GAME}/include" "${GAME}/src/lib/libnaomi")
target_compile_definitions(test_native64_render PRIVATE HOST_NATIVE64=1 _DEFAULT_SOURCE=1 _DARWIN_C_SOURCE=1)
target_compile_options(test_native64_render PRIVATE -UNDEBUG -include "${PORT}/include/port_prefix.h"
    -ffp-contract=off -ffunction-sections -fdata-sections)
target_link_libraries(test_native64_render PRIVATE mvc2_native_model_store mvc2_native_renderer)
if(APPLE)
    target_link_options(test_native64_render PRIVATE "LINKER:-dead_strip")
else()
    target_link_options(test_native64_render PRIVATE "LINKER:--gc-sections")
endif()
add_test(NAME native64_render COMMAND test_native64_render)

# Isolated tests execute original engine routines under the experimental
# native record ABI. Dead stripping removes unrelated SDK routines; no dummy
# implementations are supplied. The full port still requires its 32-bit ABI.
function(mvc2_core_test name)
    cmake_parse_arguments(CORE "DREAMCAST;LEGACY_WARNINGS" "" "" ${ARGN})
    add_executable(test_${name} ${CORE_UNPARSED_ARGUMENTS})
    target_compile_definitions(test_${name} PRIVATE HOST_NATIVE64=1)
    if(CORE_DREAMCAST)
        target_compile_definitions(test_${name} PRIVATE TARGET_DC=1 BUILD_DATE=20000529)
    else()
        target_compile_definitions(test_${name} PRIVATE BUILD_DATE=20000225)
    endif()
    target_include_directories(test_${name} PRIVATE "${GAME}/include" "${PORT}/include" "${PORT}/native64")
    target_compile_options(test_${name} PRIVATE -UNDEBUG
        -include "${PORT}/include/port_prefix.h" -ffunction-sections -fdata-sections)
    if(CORE_LEGACY_WARNINGS)
        # Large original character/system units retain SHC-era diagnostics.
        # Pointer truncation/conversion remains a hard error in this path.
        target_compile_options(test_${name} PRIVATE -Werror=pointer-to-int-cast
            -Werror=int-to-pointer-cast -Werror=int-conversion
            -fno-strict-aliasing -ffp-contract=off)
    else()
        target_compile_options(test_${name} PRIVATE -Wall -Wextra -Werror)
    endif()
    target_link_libraries(test_${name} PRIVATE m)
    if(APPLE)
        target_link_options(test_${name} PRIVATE "LINKER:-dead_strip")
    else()
        target_link_options(test_${name} PRIVATE "LINKER:--gc-sections")
    endif()
    add_test(NAME ${name} COMMAND test_${name})
endfunction()
mvc2_core_test(native64_load_queue "${PORT}/native64/tests/test_load_queue.c")
mvc2_core_test(native64_effect_words LEGACY_WARNINGS "${PORT}/native64/tests/test_effect_words.c" "${GAME}/src/work.c")
mvc2_core_test(native64_effect_thanos LEGACY_WARNINGS "${PORT}/native64/tests/test_effect_thanos.c"
    "${GAME}/src/pl_char.c" "${GAME}/src/hit_work.c" "${GAME}/src/work.c")
mvc2_core_test(native64_state_clears LEGACY_WARNINGS "${PORT}/native64/tests/test_state_clears.c" "${GAME}/src/work.c")
mvc2_core_test(native64_state_clears_dc DREAMCAST LEGACY_WARNINGS "${PORT}/native64/tests/test_state_clears.c" "${GAME}/src/work.c")
target_compile_options(test_native64_load_queue PRIVATE -Wno-unused-variable)
mvc2_core_test(native64_sys_trans "${PORT}/native64/tests/test_sys_trans.c")
mvc2_core_test(native64_system_pad LEGACY_WARNINGS "${PORT}/native64/tests/test_system_pad.c"
    "${GAME}/src/work.c" "${GAME}/src/lib/libnaomi/nlmath.c" "${PORT}/dc/dc_shinobi.c")
target_include_directories(test_native64_system_pad PRIVATE "${PORT}/snd")
mvc2_core_test(native64_throw_callback LEGACY_WARNINGS "${PORT}/native64/tests/test_throw_callback.c"
    "${GAME}/src/pl_act.c" "${GAME}/src/s_pl00.c" "${GAME}/src/hit.c" "${GAME}/src/work.c")
mvc2_core_test(native64_player_loading LEGACY_WARNINGS test_native64_player_loading.c
    "${GAME}/src/am_load.c" "${GAME}/src/pl_data_tbl.c" "${GAME}/src/pl_char.c"
    "${GAME}/src/sprite.c" "${GAME}/src/work.c")
target_link_libraries(test_native64_player_loading PRIVATE mvc2_native_game_memory)
add_library(mvc2_native_game_tables STATIC "${PORT}/native64/game_tables.c")
target_include_directories(mvc2_native_game_tables PRIVATE "${GAME}/include")
target_compile_definitions(mvc2_native_game_tables PRIVATE HOST_NATIVE64=1)
target_compile_options(mvc2_native_game_tables PRIVATE -Wall -Wextra -Werror
    -include "${PORT}/include/port_prefix.h")
add_test(NAME native64_game_tables COMMAND "${Python3_EXECUTABLE}"
    "${PORT}/native64/tests/test_game_tables.py")
mvc2_core_test(native64_game_records "${PORT}/native64/tests/test_game_records.c" "${GAME}/src/obj_uv.c")
mvc2_core_test(native64_work_pool "${PORT}/native64/tests/test_work_pool.c"
    "${GAME}/src/hit_work.c" "${GAME}/src/work.c")
mvc2_core_test(native64_cpu_commands "${PORT}/native64/tests/test_cpu_commands.c"
    "${GAME}/src/em_cmd.c" "${GAME}/src/o_sub1.c" "${GAME}/src/work.c" "${GAME}/src/em_tbl.c")
mvc2_core_test(native64_game_records_dc DREAMCAST "${PORT}/native64/tests/test_game_records.c" "${GAME}/src/obj_uv.c")
mvc2_core_test(native64_work_pool_dc DREAMCAST "${PORT}/native64/tests/test_work_pool.c"
    "${GAME}/src/hit_work.c" "${GAME}/src/work.c")
mvc2_core_test(native64_cpu_commands_dc DREAMCAST "${PORT}/native64/tests/test_cpu_commands.c"
    "${GAME}/src/em_cmd.c" "${GAME}/src/o_sub1.c" "${GAME}/src/work.c" "${GAME}/src/em_tbl.c")
set_source_files_properties("${GAME}/src/obj_uv.c" PROPERTIES COMPILE_OPTIONS "-Wno-unused-variable")
set_source_files_properties("${GAME}/src/hit_work.c" PROPERTIES COMPILE_OPTIONS "-Wno-unused-parameter;-Wno-unused-value")
set_source_files_properties("${GAME}/src/em_cmd.c" PROPERTIES COMPILE_OPTIONS "-Wno-unused-parameter")
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
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_test(NAME native64_guard_preservation COMMAND "${CMAKE_COMMAND}" -E env
        "CC=${CMAKE_C_COMPILER}" "${Python3_EXECUTABLE}" "${PORT}/tools/test_native64_guards.py")
endif()

if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64|ARM64)$")
    find_package(Threads REQUIRED)
    add_executable(test_coro test_coro.c coro_registers_arm64.S
        "${PORT}/sys/coro_arm64.c" "${PORT}/sys/coro_arm64.S")
    target_include_directories(test_coro PRIVATE "${PORT}/sys")
    target_link_libraries(test_coro PRIVATE Threads::Threads)
    target_compile_options(test_coro PRIVATE -UNDEBUG -Wall -Wextra -Werror)
    add_test(NAME arm64_coroutine COMMAND test_coro)
    add_executable(test_native64_board test_native64_board.c
        "${PORT}/naomi/naomi_board.c" "${PORT}/naomi/naomi_rom.c" "${PORT}/naomi/nl_am.c"
        "${PORT}/nl/nl_tex.c" "${PORT}/snd/snd_nl.c" "${PORT}/snd/aica.c" "${PORT}/snd/arm7.c"
        "${PORT}/sys/coro_arm64.c" "${PORT}/sys/coro_arm64.S")
    target_include_directories(test_native64_board PRIVATE "${PORT}/include" "${PORT}/sys"
        "${PORT}/nl" "${PORT}/snd" "${GAME}/include")
    target_compile_definitions(test_native64_board PRIVATE HOST_NATIVE64=1 _DEFAULT_SOURCE=1 _DARWIN_C_SOURCE=1)
    target_compile_options(test_native64_board PRIVATE -UNDEBUG
        "$<$<COMPILE_LANGUAGE:C>:-include;${PORT}/include/port_prefix.h>" -ffunction-sections -fdata-sections)
    target_link_libraries(test_native64_board PRIVATE mvc2_native_model_store mvc2_native_renderer mvc2_native_texture_files Threads::Threads)
    if(APPLE)
        target_link_options(test_native64_board PRIVATE "LINKER:-dead_strip")
    else()
        target_link_options(test_native64_board PRIVATE "LINKER:--gc-sections")
    endif()
    add_test(NAME native64_board COMMAND test_native64_board)
else()
    message(STATUS "ARM64 coroutine execution requires an ARM64 host; not registered on this architecture")
endif()
