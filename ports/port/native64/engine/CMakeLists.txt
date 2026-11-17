cmake_minimum_required(VERSION 3.21)

# This build deliberately sits beside ports/CMakeLists.txt: that build's
# 32-bit address aliases and GNU relocatable-program pipeline remain intact.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(mvc2_native_engine LANGUAGES C ASM)
endif()

get_filename_component(MVC2_NATIVE_PORT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
get_filename_component(MVC2_NATIVE_GAME "${MVC2_NATIVE_PORT}/../.." ABSOLUTE)
find_package(Threads REQUIRED)
find_package(ZLIB REQUIRED)
find_package(Python3 REQUIRED COMPONENTS Interpreter)

file(GLOB MVC2_NATIVE_ARCADE_SOURCES CONFIGURE_DEPENDS "${MVC2_NATIVE_GAME}/src/*.c")
foreach(module shl23_00 at02_scene pl_char)
    list(REMOVE_ITEM MVC2_NATIVE_ARCADE_SOURCES "${MVC2_NATIVE_GAME}/src/${module}.c")
    list(APPEND MVC2_NATIVE_ARCADE_SOURCES "${MVC2_NATIVE_PORT}/game/${module}_host.c")
endforeach()
if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(arm|aarch64|ARM64)" OR CMAKE_OSX_ARCHITECTURES MATCHES "arm64")
    foreach(module s_pl07 s_pl1a s_pl39)
        list(REMOVE_ITEM MVC2_NATIVE_ARCADE_SOURCES "${MVC2_NATIVE_GAME}/src/${module}.c")
        list(APPEND MVC2_NATIVE_ARCADE_SOURCES "${MVC2_NATIVE_PORT}/game/${module}_host.c")
    endforeach()
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "x86|AMD64|amd64|i.86")
    list(REMOVE_ITEM MVC2_NATIVE_ARCADE_SOURCES "${MVC2_NATIVE_GAME}/src/set05_05.c")
    list(APPEND MVC2_NATIVE_ARCADE_SOURCES "${MVC2_NATIVE_PORT}/game/set05_05_host.c")
endif()

add_library(mvc2_native_game_objects OBJECT ${MVC2_NATIVE_ARCADE_SOURCES})
set_target_properties(mvc2_native_game_objects PROPERTIES C_STANDARD 90 C_STANDARD_REQUIRED YES C_EXTENSIONS YES)
target_include_directories(mvc2_native_game_objects PRIVATE
    "${MVC2_NATIVE_GAME}/include" "${MVC2_NATIVE_GAME}/src"
    "${MVC2_NATIVE_PORT}/include" "${MVC2_NATIVE_PORT}/native64")
target_compile_definitions(mvc2_native_game_objects PRIVATE HOST_NATIVE64=1 BUILD_DATE=20000225)
target_compile_options(mvc2_native_game_objects PRIVATE -std=gnu89 -fwrapv -fno-common
    -fno-strict-aliasing -fsigned-char -ffp-contract=off -fno-delete-null-pointer-checks
    -include "${CMAKE_CURRENT_LIST_DIR}/engine_prefix.h" -O1 -w)
set_source_files_properties("${MVC2_NATIVE_GAME}/src/main.c" PROPERTIES COMPILE_DEFINITIONS main=naomi_main)

set(MVC2_NATIVE_BACKEND_SOURCES
    "${MVC2_NATIVE_PORT}/naomi/naomi_board.c"
    "${MVC2_NATIVE_PORT}/naomi/naomi_rom.c"
    "${MVC2_NATIVE_PORT}/naomi/naomi_zip.c"
    "${MVC2_NATIVE_PORT}/naomi/naomi_m1.c"
    "${MVC2_NATIVE_PORT}/naomi/nl_am.c"
    "${MVC2_NATIVE_PORT}/dc/dc_shinobi.c"
    "${MVC2_NATIVE_PORT}/dc/dc_vmu.c"
    "${MVC2_NATIVE_PORT}/nl/nl_xmtr.c"
    "${MVC2_NATIVE_PORT}/nl/nl_tex.c"
    "${MVC2_NATIVE_PORT}/nl/nl_lz.c"
    "${MVC2_NATIVE_PORT}/nl/nl_sprite.c"
    "${MVC2_NATIVE_PORT}/nl/nl_strip.c"
    "${MVC2_NATIVE_PORT}/nl/nl_fog.c"
    "${MVC2_NATIVE_PORT}/nl/nl_sys.c"
    "${MVC2_NATIVE_PORT}/nl/nl_obj.c"
    "${MVC2_NATIVE_PORT}/nl/nl_work.c"
    "${MVC2_NATIVE_GAME}/src/lib/libnaomi/nlmath.c"
    "${MVC2_NATIVE_GAME}/src/lib/libnaomi/nlproj.c"
    "${MVC2_NATIVE_PORT}/render/rd.c"
    "${MVC2_NATIVE_PORT}/render/rd_soft.c"
    "${MVC2_NATIVE_PORT}/render/rd_posix.c"
    "${MVC2_NATIVE_PORT}/snd/arm7.c"
    "${MVC2_NATIVE_PORT}/snd/aica.c"
    "${MVC2_NATIVE_PORT}/snd/snd_drv.c"
    "${MVC2_NATIVE_PORT}/snd/snd_nl.c"
    "${MVC2_NATIVE_PORT}/snd/s3l.c"
    "${MVC2_NATIVE_PORT}/native64/memory.c"
    "${MVC2_NATIVE_PORT}/native64/game_memory.c"
    "${MVC2_NATIVE_PORT}/native64/model.c"
    "${MVC2_NATIVE_PORT}/native64/model_store.c"
    "${MVC2_NATIVE_PORT}/native64/texture_file.c"
    "${MVC2_NATIVE_PORT}/native64/system_textures.c"
    "${MVC2_NATIVE_PORT}/native64/game_tables.c"
    "${MVC2_NATIVE_PORT}/native64/sdk_native.c"
    "${CMAKE_CURRENT_LIST_DIR}/sdk_board.c"
    "${CMAKE_CURRENT_LIST_DIR}/sdk_lines.c"
    "${CMAKE_CURRENT_LIST_DIR}/sdk_graphics.c")
set(MVC2_NATIVE_SUPPORT_SOURCES
    "${CMAKE_CURRENT_LIST_DIR}/engine_state.c"
    "${CMAKE_CURRENT_LIST_DIR}/runtime.c"
    "${CMAKE_CURRENT_LIST_DIR}/host_alloc.c")
if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64|ARM64)$" OR CMAKE_OSX_ARCHITECTURES MATCHES "arm64")
    list(APPEND MVC2_NATIVE_SUPPORT_SOURCES
        "${MVC2_NATIVE_PORT}/sys/coro_arm64.c" "${MVC2_NATIVE_PORT}/sys/coro_arm64.S")
else()
    message(STATUS "Native coroutine execution requires ARM64; this host can build objects and inspect link dependencies")
endif()
add_library(mvc2_native_backend_objects OBJECT ${MVC2_NATIVE_BACKEND_SOURCES})
set_target_properties(mvc2_native_backend_objects PROPERTIES C_STANDARD 17 C_STANDARD_REQUIRED YES C_EXTENSIONS YES)
target_include_directories(mvc2_native_backend_objects PRIVATE
    "${MVC2_NATIVE_GAME}/include" "${MVC2_NATIVE_GAME}/src/lib/libnaomi"
    "${MVC2_NATIVE_PORT}/include" "${MVC2_NATIVE_PORT}/native64"
    "${MVC2_NATIVE_PORT}/sys" "${MVC2_NATIVE_PORT}/nl" "${MVC2_NATIVE_PORT}/render"
    "${MVC2_NATIVE_PORT}/snd")
target_compile_definitions(mvc2_native_backend_objects PRIVATE
    HOST_NATIVE64=1 RD_NATIVE_SYSTEM=1 _DEFAULT_SOURCE=1 _DARWIN_C_SOURCE=1)
target_compile_options(mvc2_native_backend_objects PRIVATE
    "$<$<COMPILE_LANGUAGE:C>:-std=gnu17;-O2;-fwrapv;-fno-common;-fno-strict-aliasing;-fsigned-char;-ffp-contract=off;-include;${CMAKE_CURRENT_LIST_DIR}/engine_prefix.h>")

# The checkpoint controller, host ownership/input/audio buffers, allocations,
# and coroutine implementation must survive restoration of engine globals.
add_library(mvc2_native_support_objects OBJECT ${MVC2_NATIVE_SUPPORT_SOURCES})
set_target_properties(mvc2_native_support_objects PROPERTIES C_STANDARD 17 C_STANDARD_REQUIRED YES C_EXTENSIONS YES)
target_include_directories(mvc2_native_support_objects PRIVATE
    "${MVC2_NATIVE_PORT}/include" "${MVC2_NATIVE_PORT}/native64"
    "${MVC2_NATIVE_PORT}/render" "${MVC2_NATIVE_PORT}/snd" "${MVC2_NATIVE_PORT}/sys")
target_compile_definitions(mvc2_native_support_objects PRIVATE HOST_NATIVE64=1)
target_compile_options(mvc2_native_support_objects PRIVATE
    "$<$<COMPILE_LANGUAGE:C>:-std=gnu17;-include;${MVC2_NATIVE_PORT}/include/port_prefix.h>")
if(CMAKE_C_COMPILER_ID MATCHES "Clang" AND (APPLE OR CMAKE_SYSTEM_NAME STREQUAL "Linux"))
    target_compile_definitions(mvc2_native_support_objects PRIVATE MVC2_ENGINE_SECTION_SNAPSHOT=1)
endif()

# Xcode requires a real source in a static archive assembled from object
# libraries. The immutable program descriptor needs no writable checkpoint.
add_library(mvc2_native_engine STATIC "${CMAKE_CURRENT_LIST_DIR}/program.c"
    $<TARGET_OBJECTS:mvc2_native_game_objects>
    $<TARGET_OBJECTS:mvc2_native_backend_objects> $<TARGET_OBJECTS:mvc2_native_support_objects>)
target_compile_definitions(mvc2_native_engine PRIVATE HOST_NATIVE64=1)
target_include_directories(mvc2_native_engine PUBLIC
    "${MVC2_NATIVE_PORT}/include" "${MVC2_NATIVE_PORT}/native64" "${MVC2_NATIVE_PORT}/render"
    "${CMAKE_CURRENT_LIST_DIR}")
target_link_libraries(mvc2_native_engine PUBLIC Threads::Threads ZLIB::ZLIB m)

# The inventory consumes every object, including code not reached by main.
# It reports missing definitions; it never generates stubs or address aliases.
file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/engine-objects-$<CONFIG>.txt" CONTENT
    "$<JOIN:$<TARGET_OBJECTS:mvc2_native_game_objects>,\n>\n$<JOIN:$<TARGET_OBJECTS:mvc2_native_backend_objects>,\n>\n$<JOIN:$<TARGET_OBJECTS:mvc2_native_support_objects>,\n>\n")
add_custom_target(mvc2_native_inventory
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/inventory.py"
        --nm "${CMAKE_NM}" --objects "${CMAKE_CURRENT_BINARY_DIR}/engine-objects-$<CONFIG>.txt"
        --definitions-from-archive "$<TARGET_FILE:mvc2_native_engine>"
        --output "${CMAKE_CURRENT_BINARY_DIR}/engine-inventory-$<CONFIG>.json"
    DEPENDS mvc2_native_engine
    VERBATIM)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    # An all-object link catches duplicate definitions without omitting
    # unreachable routines. The remaining SDK/system references are recorded
    # by mvc2_native_inventory; no x86 substitute coroutine is fabricated.
    add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/engine-link-check.o"
        COMMAND "${CMAKE_C_COMPILER}" -r -nostdlib
            "@${CMAKE_CURRENT_BINARY_DIR}/engine-objects-$<CONFIG>.txt"
            -o "${CMAKE_CURRENT_BINARY_DIR}/engine-link-check.o"
        DEPENDS mvc2_native_game_objects mvc2_native_backend_objects mvc2_native_support_objects
            $<TARGET_OBJECTS:mvc2_native_game_objects>
            $<TARGET_OBJECTS:mvc2_native_backend_objects>
            $<TARGET_OBJECTS:mvc2_native_support_objects>
        VERBATIM)
    add_custom_target(mvc2_native_link_check
        DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/engine-link-check.o" mvc2_native_inventory)
endif()

option(MVC2_NATIVE_ENGINE_TESTS "Build isolated native SDK regression tests" OFF)
if(MVC2_NATIVE_ENGINE_TESTS)
    enable_testing()
    add_executable(test_native_sdk_lines test_sdk_lines.c sdk_lines.c)
    target_include_directories(test_native_sdk_lines PRIVATE
        "${MVC2_NATIVE_GAME}/include" "${MVC2_NATIVE_PORT}/include"
        "${MVC2_NATIVE_PORT}/nl" "${MVC2_NATIVE_PORT}/render")
    target_compile_definitions(test_native_sdk_lines PRIVATE HOST_NATIVE64=1)
    target_compile_options(test_native_sdk_lines PRIVATE -UNDEBUG -Wall -Wextra -Werror
        -include "${MVC2_NATIVE_PORT}/include/port_prefix.h")
    target_link_libraries(test_native_sdk_lines PRIVATE m)
    add_test(NAME native_sdk_lines COMMAND test_native_sdk_lines)
    add_executable(test_native_engine_state tests/test_engine_state.c engine_state.c)
    target_include_directories(test_native_engine_state PRIVATE "${CMAKE_CURRENT_LIST_DIR}")
    target_compile_definitions(test_native_engine_state PRIVATE MVC2_ENGINE_SECTION_SNAPSHOT=1)
    target_compile_options(test_native_engine_state PRIVATE -UNDEBUG -Wall -Wextra -Werror -fno-common)
    add_test(NAME native_engine_state COMMAND test_native_engine_state)
    add_test(NAME native_engine_state_late_capture COMMAND test_native_engine_state late)
endif()
