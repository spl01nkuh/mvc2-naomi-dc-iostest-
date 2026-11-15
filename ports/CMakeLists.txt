# Marvel vs. Capcom 2 (NAOMI and Dreamcast) on PC and 3DS, from the decompilation (MVC2_DECOMP: by default
# the folder above).
#   cmake -G Ninja -B build -DCMAKE_C_COMPILER=gcc && ninja -C build    (MSYS2 mingw32; -DMVC2_SDL3=OFF: SDL2)
cmake_minimum_required(VERSION 3.21)
project(mvc2_ports C)

set(PORT ${CMAKE_SOURCE_DIR}/port)
get_filename_component(DEFAULT_DECOMP ${CMAKE_SOURCE_DIR}/.. ABSOLUTE)
set(MVC2_DECOMP ${DEFAULT_DECOMP} CACHE PATH "the decompilation")
set(GAME ${MVC2_DECOMP})
if(EXISTS ${GAME}/pointers.tsv)
    set(POINTERS ${GAME})
else()
    set(POINTERS ${GAME}/build)
endif()
option(MVC2_SDL3 "the PC front end on SDL3, with the GPU renderer (OFF: SDL2)" ON)

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
    message(FATAL_ERROR "the game stores addresses in 32 bits: build for a 32-bit target (MSYS2 mingw32, or 32-bit ARM Linux)")
endif()
find_package(Python3 COMPONENTS Interpreter REQUIRED)
# no fused multiply-add: the same results on each CPU
add_compile_options(-ffp-contract=off)
# a fixed load address, so 0x0C000000 stays free for the game's RAM
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_compile_options(-fno-pie)
    add_link_options(-no-pie)
endif()

# the game's C as SHC took it
set(GAME_OPTIONS
    -std=gnu89 -fwrapv -fno-strict-aliasing -fsigned-char -w -O1 -fno-delete-null-pointer-checks
    -include ${PORT}/include/port_prefix.h
    -fno-toplevel-reorder -fno-zero-initialized-in-bss)
# floats in single precision, as the SH-4's: x87's wider ones round differently
set(HOST_FLOAT)
if(WIN32 OR CMAKE_SYSTEM_PROCESSOR MATCHES "x86|i686|AMD64|i386")
    set(HOST_FLOAT -mfpmath=sse -msse2)
    list(APPEND GAME_OPTIONS -malign-data=abi ${HOST_FLOAT})
endif()
option(MVC2_GAME_O2 "the game's C at -O2 (a trial on the 3DS)" OFF)
if(MVC2_GAME_O2)
    list(APPEND GAME_OPTIONS -O2)
endif()
option(MVC2_GAME_OS "3DS: the game's C at -Os (about 400 KB less code, for the Old 3DS)" OFF)
if(NINTENDO_3DS AND MVC2_GAME_OS)
    list(APPEND GAME_OPTIONS -Os)
endif()
# SHC's enums are int-sized, devkitARM's as small as fit: the game and the layers that share its structs
# keep SHC's (the 3DS front end, with libctru, keeps devkitARM's)
set(HOST_ABI)
set(RELOC_FLAGS)
if(NINTENDO_3DS)
    set(HOST_ABI -fno-short-enums)
    set(RELOC_FLAGS -Wl,--no-enum-size-warning)
    list(APPEND GAME_OPTIONS ${HOST_ABI})
endif()

# Capcom's modules; shl23_00, at02_scene, pl_char (and on x86 set05_05) go through wrappers (port/game/*_host.c)
file(GLOB ARC_SOURCES ${GAME}/src/*.c)
list(REMOVE_ITEM ARC_SOURCES ${GAME}/src/shl23_00.c ${GAME}/src/at02_scene.c ${GAME}/src/pl_char.c)
if(WIN32 OR CMAKE_SYSTEM_PROCESSOR MATCHES "x86|i686|AMD64|i386")
    # in its place in the list: the objects' order lays out their data
    list(TRANSFORM ARC_SOURCES REPLACE "^${GAME}/src/set05_05\.c$" "${PORT}/game/set05_05_host.c")
endif()
list(APPEND ARC_SOURCES ${PORT}/game/shl23_00_host.c ${PORT}/game/at02_scene_host.c ${PORT}/game/pl_char_host.c)
set_source_files_properties(${PORT}/game/shl23_00_host.c ${PORT}/game/at02_scene_host.c ${PORT}/game/pl_char_host.c
                            ${PORT}/game/set05_05_host.c PROPERTIES INCLUDE_DIRECTORIES ${GAME}/src)
# on ARM, three routines that test a float as a Uint32 too: a negative wraps on the SH4 and stops at 0 on ARM
if(NINTENDO_3DS OR CMAKE_SYSTEM_PROCESSOR MATCHES "^arm")
    list(REMOVE_ITEM ARC_SOURCES ${GAME}/src/s_pl07.c ${GAME}/src/s_pl1a.c ${GAME}/src/s_pl39.c)
    list(APPEND ARC_SOURCES ${PORT}/game/s_pl07_host.c ${PORT}/game/s_pl1a_host.c ${PORT}/game/s_pl39_host.c)
    set_source_files_properties(${PORT}/game/s_pl07_host.c ${PORT}/game/s_pl1a_host.c ${PORT}/game/s_pl39_host.c
                                PROPERTIES INCLUDE_DIRECTORIES ${GAME}/src)
endif()
# the host runs the game's main() as a coroutine
set_source_files_properties(${GAME}/src/main.c PROPERTIES COMPILE_DEFINITIONS main=naomi_main)

# the decomp's plain-C library modules, one copy for both programs
set(LIB_SOURCES ${GAME}/src/lib/libnaomi/nlmath.c ${GAME}/src/lib/libnaomi/nlproj.c ${PORT}/nl/nl_work.c)
add_library(mvc2_lib OBJECT ${LIB_SOURCES})
target_include_directories(mvc2_lib PRIVATE ${GAME}/src/lib/libnaomi ${GAME}/include ${PORT}/include)
target_compile_options(mvc2_lib PRIVATE ${GAME_OPTIONS})

# the board and Sega's libraries
set(NAOMI_SOURCES
    ${PORT}/naomi/naomi_board.c
    ${PORT}/naomi/naomi_rom.c
    ${PORT}/naomi/naomi_zip.c
    ${PORT}/naomi/naomi_m1.c
    ${PORT}/naomi/nl_am.c
    ${PORT}/nl/nl_xmtr.c
    ${PORT}/nl/nl_tex.c
    ${PORT}/nl/nl_lz.c
    ${PORT}/nl/nl_sprite.c
    ${PORT}/nl/nl_strip.c
    ${PORT}/nl/nl_fog.c
    ${PORT}/nl/nl_sys.c
    ${PORT}/nl/nl_obj.c
    ${PORT}/render/rd.c
    ${PORT}/snd/arm7.c
    ${PORT}/snd/aica.c
    ${PORT}/snd/snd_drv.c
    ${PORT}/snd/snd_nl.c
    ${PORT}/snd/s3l.c)
# the test build runs on a New 3DS at an Old 3DS's speed, with its memory
option(OLD_3DS_TEST "3DS: make the Old 3DS test build" OFF)
option(CTR_SOFT "3DS: build in the software renderer, for checks" OFF)
# into prof.log
option(MVC2_PROF "3DS: time the frame's parts" OFF)
option(MVC2_PROF_FUNCS "3DS: with MVC2_PROF, sample the game's functions" OFF)
option(MVC2_PROF_GAMEZ "3DS: with MVC2_PROF, time the game's frame by its routines" OFF)
set(PROGRAM_WRAPS)
set(PROGRAM_WRAP_SOURCES)
if(NINTENDO_3DS AND MVC2_PROF)
    add_compile_definitions(MVC2_PROF)
    if(MVC2_PROF_GAMEZ)
        add_compile_definitions(MVC2_PROF_GAMEZ)
        # the calls between the game's modules to these go through port/ctr/ctr_gamez_wrap.c, inside each program
        foreach(fn System_Main SysTrans SysTrans2 load_main sound_vsync key_repeat vms_check col_send game_main
                   all_work_move pl_work_move scr_move stage_frame pl_hit_work_set bg_move fog_set hit_ck0 vs_sys_move
                   flash_move disp_prio_clr pl_work_disp game_disp sprite_disp sprite_tex_set lz_unpack
                   work_list_move work_list_disp)
            list(APPEND PROGRAM_WRAPS -Wl,--wrap=${fn})
        endforeach()
        set(PROGRAM_WRAP_SOURCES ${PORT}/ctr/ctr_gamez_wrap.c)
    endif()
    if(MVC2_PROF_FUNCS)
        add_compile_definitions(MVC2_PROF_FUNCS)
        add_compile_options(-finstrument-functions)
        # the front end, the sound and port/sys left out: their time is in the zones
        file(GLOB UNTIMED ${PORT}/ctr/*.c ${PORT}/snd/*.c ${PORT}/sys/*.c)
        set_source_files_properties(${UNTIMED} PROPERTIES COMPILE_OPTIONS -fno-instrument-functions)
    endif()
endif()
if(NINTENDO_3DS)
    set(HOST_SOURCES)
    if(CTR_SOFT)
        set(HOST_SOURCES ${PORT}/render/rd_soft.c)
        add_compile_definitions(CTR_SOFT)
    endif()
    list(APPEND HOST_SOURCES ${PORT}/snd/arm7_jit.c ${PORT}/snd/arm7_jit2.c)    # the sound CPU translated to the 3DS's own code
    # nlmath's angle routines' last answers kept (nl_trig.c), for the game's calls and nlproj's
    list(APPEND HOST_SOURCES ${PORT}/nl/nl_trig.c)
    set(TRIG_WRAPS -Wl,--wrap=nlSin,--wrap=nlCos,--wrap=nlTan,--wrap=nlArcTan)
    set_source_files_properties(${PORT}/snd/arm7.c PROPERTIES COMPILE_DEFINITIONS ARM7_JIT)
    # the MPCore's VFP is the ARM1136's VFP11, whose latencies gcc schedules for only under that name
    set_source_files_properties(${PORT}/nl/nl_obj.c PROPERTIES COMPILE_OPTIONS -mtune=arm1136jf-s)
    set(SYS_SOURCES ${PORT}/sys/coro_arm.c ${PORT}/sys/divzero_arm.c)
    set(FRONT_SOURCES ${PORT}/ctr/main_3ds.c ${PORT}/ctr/ctr_render.c ${PORT}/ctr/ctr_rom.c ${PORT}/ctr/ctr_m1.c ${PORT}/ctr/ctr_art.c ${PORT}/ctr/ctr_options.c
        ${PORT}/ctr/ctr_log.c ${PORT}/ctr/ctr_alloc.c ${PORT}/ctr/ctr_memcpy.c ${PORT}/ctr/ctr_stream.c ${PORT}/sdl/start.c)
else()
    set(HOST_SOURCES ${PORT}/render/rd_soft.c)
    if(MVC2_SDL3)
        list(APPEND HOST_SOURCES ${PORT}/render/rd_gpu.c)
    endif()
    if(WIN32)
        set(SYS_SOURCES ${PORT}/sys/coro_win32.c ${PORT}/sys/divzero_win32.c)
    elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "^arm")
        set(SYS_SOURCES ${PORT}/sys/coro_arm.c ${PORT}/sys/divzero_arm.c)
    else()
        message(FATAL_ERROR "port/sys has coroutines and division by zero for Windows (x86) and 32-bit ARM only")
    endif()
    set(FRONT_SOURCES ${PORT}/sdl/main_sdl.c ${PORT}/sdl/start.c ${PORT}/sdl/input_sdl.c ${PORT}/sdl/menu_sdl.c
        ${PORT}/sdl/sdl_audio.c)
endif()
if(NINTENDO_3DS)
    list(APPEND FRONT_SOURCES ${PORT}/ctr/ctr_audio.c ${PORT}/ctr/ctr_code.c)    # the sound out and its translator's code memory
    if(MVC2_PROF)
        # the game's calls into the library timed by layer, through the linker's --wrap
        list(APPEND FRONT_SOURCES ${PORT}/ctr/ctr_prof.c ${PORT}/ctr/ctr_game_zones.c ${PORT}/ctr/ctr_regcheck.c
            ${PORT}/ctr/ctr_gpuprobe.c)
        if(MVC2_PROF_GAMEZ)
            list(APPEND FRONT_SOURCES ${PORT}/ctr/ctr_gamez.c)
        endif()
        set_source_files_properties(${PORT}/ctr/ctr_game_zones.c PROPERTIES INCLUDE_DIRECTORIES ${GAME}/include
                                    COMPILE_OPTIONS -fno-short-enums)
        set(GAME_ZONE_WRAPS
            nlArcTan2 nlCalcPoint nlCalcVector nlFloor nlFogDensity nlFogDensityF nlFogTableColor
            nlFogTableOff nlFogTableSet nlGetMatrix nlInitLibrary nlInitMatrix nlLoadMatrix nlLookAt
            nlMakeModelViewProjectMatrix nlMatrixMode nlMultiMatrix nlObjPutSetFadeColor nlPerspective nlPerspectiveX
            nlPopMatrix nlPow nlProjectScreen nlPushMatrix nlRand nlRandom nlRotateAX nlRotateAY nlRotateAZ nlScaleV
            nlScreenPoint nlSetBackColor nlSetBackColorZ nlSetScaleFactor nlShiftPerspective nlTranslate
            nlTranslateV nlUnitMatrix nlUnitVector nlaChangeCreditSequence nlaCheckCreditEnough nlaGetBackupData
            nlaGetBootFlag nlaGetCreditConf nlaGetCreditData nlaGetRTC nlaGetSystemFlag nlaInTestMode nlaInitSystem
            nlaInitUserBackup nlaInitUserEEPROM nlaJAMMACreditInit nlaJAMMAInit nlaJAMMASetPrimaryNode
            nlaJumpTestMode nlaNRSDecodeRead nlaPutLED nlaROMTypeSet nlaReadROMDMA nlaUpdateSystemBackup
            nlaUpdateUserBackup nlaUpdateUserEEPROM
            gxCmdQueueRun gspSubmitGxCommand)    # C3D_FrameEnd's hand-over to GSP (ctr_prof.c)
    endif()
endif()
add_library(mvc2_port OBJECT ${NAOMI_SOURCES} ${HOST_SOURCES})
target_include_directories(mvc2_port PRIVATE ${PORT}/include ${PORT}/sys ${PORT}/render ${PORT}/nl ${PORT}/snd)
# the library routines take the game's own types from its headers
set_source_files_properties(${PORT}/naomi/nl_am.c PROPERTIES INCLUDE_DIRECTORIES ${GAME}/include)
target_compile_options(mvc2_port PRIVATE -O2 -std=gnu17 -fwrapv -fno-strict-aliasing -fsigned-char -Wall ${HOST_ABI} ${HOST_FLOAT})
if(MVC2_SDL3 AND NOT NINTENDO_3DS)
    find_package(SDL3 REQUIRED CONFIG)
    set(SDL_LIBS SDL3::SDL3)
    target_compile_definitions(mvc2_port PRIVATE RD_SDL3)
    target_link_libraries(mvc2_port PRIVATE SDL3::SDL3)
elseif(NOT NINTENDO_3DS)
    find_package(SDL2 REQUIRED)
    set(SDL_LIBS SDL2::SDL2main SDL2::SDL2)
    target_link_libraries(mvc2_port PRIVATE SDL2::SDL2)
endif()
# the coroutines and the division by zero, which keep their state when the rest starts again
add_library(mvc2_sys OBJECT ${SYS_SOURCES})
target_include_directories(mvc2_sys PRIVATE ${PORT}/include ${PORT}/sys)
target_compile_options(mvc2_sys PRIVATE -O2 -std=gnu17 -Wall ${HOST_FLOAT})
# the names that bracket a relocatable's .data and .bss, linked first and last in it (port/sys/restart.c)
add_library(mvc2_span_begin OBJECT ${PORT}/game/span_begin.c)
add_library(mvc2_span_end OBJECT ${PORT}/game/span_end.c)

# A program: the game's objects, what gen_host finds they use and nothing defines (the program's data,
# filled from the user's image at start-up, aliases into it, fixed buffers, and stubs for the library
# routines the port lacks), all made one relocatable (ld -r -d) in which only NAME_program stays global
# (objcopy -G). What it uses and does not define binds to the host, and two programs can share one
# executable. The names it keeps in place become NAME.name, and the executable's link gives their addresses.
set(GEN ${CMAKE_BINARY_DIR}/gen)
if(WIN32 AND CMAKE_SIZEOF_VOID_P EQUAL 4)
    set(UNDERSCORE --underscore)
    set(SYM_PREFIX _)
endif()
function(mvc2_program NAME)
    cmake_parse_arguments(P "" "PROGRAM" "SOURCES;DEFINITIONS;HOST" ${ARGN})
    set(OUT ${GEN}/${NAME})
    add_library(${NAME}_game OBJECT ${P_SOURCES})
    target_include_directories(${NAME}_game PRIVATE ${GAME}/include ${PORT}/include)
    target_compile_options(${NAME}_game PRIVATE ${GAME_OPTIONS})
    target_compile_definitions(${NAME}_game PRIVATE ${P_DEFINITIONS})
    set(HOST_DIRS)
    set(HOST_OBJECTS)
    foreach(h ${P_HOST})
        list(APPEND HOST_DIRS ${CMAKE_BINARY_DIR}/CMakeFiles/${h}.dir)
        list(APPEND HOST_OBJECTS $<TARGET_OBJECTS:${h}>)
    endforeach()
    add_custom_command(OUTPUT ${OUT}/host_data.c ${OUT}/host_stubs.c ${OUT}/host_defsyms.rsp ${OUT}/host_rename.txt
        COMMAND Python3::Interpreter ${PORT}/tools/gen_host.py ${GAME} ${CMAKE_NM} ${OUT}
                ${CMAKE_BINARY_DIR}/CMakeFiles/${NAME}_game.dir ${HOST_DIRS} --program=${P_PROGRAM} --prefix=${NAME}
                ${UNDERSCORE}
        DEPENDS ${NAME}_game ${P_HOST} $<TARGET_OBJECTS:${NAME}_game> ${HOST_OBJECTS} ${PORT}/tools/gen_host.py
                ${GAME}/symbols.txt ${GAME}/symbols_dc.txt ${GAME}/mvc2.sub ${GAME}/mvc2dc.sub
                ${POINTERS}/pointers.tsv ${POINTERS}/pointers_dc.tsv
        COMMENT "Finding what the ${P_PROGRAM} program uses that nothing defines")
    add_library(${NAME}_gen OBJECT ${OUT}/host_data.c ${OUT}/host_stubs.c ${PORT}/game/program.c ${PROGRAM_WRAP_SOURCES})
    target_include_directories(${NAME}_gen PRIVATE ${PORT}/include)
    target_compile_options(${NAME}_gen PRIVATE -w)
    target_compile_definitions(${NAME}_gen PRIVATE PROGRAM=${NAME}_program)
    file(GENERATE OUTPUT ${OUT}/objects.rsp
         CONTENT "$<TARGET_OBJECTS:mvc2_span_begin>\n$<JOIN:$<TARGET_OBJECTS:${NAME}_game>,\n>\n$<JOIN:$<TARGET_OBJECTS:${NAME}_gen>,\n>\n$<TARGET_OBJECTS:mvc2_span_end>\n")
    add_custom_command(OUTPUT ${CMAKE_BINARY_DIR}/${NAME}.o
        COMMAND ${CMAKE_C_COMPILER} -r -nostdlib -Wl,-d ${RELOC_FLAGS} ${PROGRAM_WRAPS} @${OUT}/objects.rsp -o ${OUT}/all.o
        COMMAND ${CMAKE_OBJCOPY} --redefine-syms=${OUT}/host_rename.txt -G ${SYM_PREFIX}${NAME}_program
                ${OUT}/all.o ${CMAKE_BINARY_DIR}/${NAME}.o
        DEPENDS ${NAME}_game ${NAME}_gen $<TARGET_OBJECTS:${NAME}_game> $<TARGET_OBJECTS:${NAME}_gen>
                mvc2_span_begin mvc2_span_end $<TARGET_OBJECTS:mvc2_span_begin> $<TARGET_OBJECTS:mvc2_span_end>
                ${OUT}/host_rename.txt ${OUT}/objects.rsp
        COMMENT "Making the ${P_PROGRAM} program one relocatable")
    set_source_files_properties(${CMAKE_BINARY_DIR}/${NAME}.o PROPERTIES EXTERNAL_OBJECT TRUE GENERATED TRUE)
endfunction()

mvc2_program(arc PROGRAM arcade SOURCES ${ARC_SOURCES} DEFINITIONS BUILD_DATE=20000225 HOST mvc2_port mvc2_lib)

# the Dreamcast's (USA disc): the same modules with its switches, src/dc's in place of their namesakes and
# its own beside them, but not its start-up (strt2.c) or Sega's library modules (src/dc/lib)
file(GLOB DC_ONLY_SOURCES ${GAME}/src/dc/*.c)
list(REMOVE_ITEM DC_ONLY_SOURCES ${GAME}/src/dc/strt2.c)
set(DC_SOURCES ${ARC_SOURCES})
foreach(f ${DC_ONLY_SOURCES})
    get_filename_component(n ${f} NAME)
    list(REMOVE_ITEM DC_SOURCES ${GAME}/src/${n})
endforeach()
list(APPEND DC_SOURCES ${DC_ONLY_SOURCES})
# what the Dreamcast adds to the board and the libraries: the disc, Shinobi and the music's player
add_library(mvc2_dc OBJECT ${PORT}/dc/dc_board.c ${PORT}/dc/dc_gd.c ${PORT}/dc/dc_shinobi.c ${PORT}/dc/dc_adx.c
            ${PORT}/dc/dc_vmu.c)
target_include_directories(mvc2_dc PRIVATE ${PORT}/include ${GAME}/include ${PORT}/snd)
target_compile_options(mvc2_dc PRIVATE -O2 -std=gnu17 -fwrapv -fno-strict-aliasing -fsigned-char -Wall ${HOST_ABI} ${HOST_FLOAT})
mvc2_program(dc PROGRAM dc SOURCES ${DC_SOURCES} DEFINITIONS TARGET_DC BUILD_DATE=20000529
             HOST mvc2_port mvc2_lib mvc2_dc)

# an executable of the host and one or more programs (arc, dc), with the front end told which
function(mvc2_executable TARGET)
    set(HOST_LIBS mvc2_port mvc2_lib)
    set(OBJECTS ${CMAKE_BINARY_DIR}/${TARGET}_host.o $<TARGET_OBJECTS:mvc2_sys>)
    set(DEFS)
    foreach(p ${ARGN})
        list(APPEND OBJECTS ${CMAKE_BINARY_DIR}/${p}.o)
        if(p STREQUAL "dc")
            list(APPEND HOST_LIBS mvc2_dc)
            list(APPEND DEFS MVC2_DC)
        else()
            list(APPEND DEFS MVC2_ARCADE)
        endif()
    endforeach()
    # the host's layers as one relocatable too, between the span names (renamed host_span_*), so that
    # their data can be put back as it started
    set(HOST_RSP ${GEN}/${TARGET}_host.rsp)
    set(HOST_CONTENT "$<TARGET_OBJECTS:mvc2_span_begin>\n")
    set(HOST_DEPENDS mvc2_span_begin mvc2_span_end $<TARGET_OBJECTS:mvc2_span_begin> $<TARGET_OBJECTS:mvc2_span_end>)
    foreach(h ${HOST_LIBS})
        string(APPEND HOST_CONTENT "$<JOIN:$<TARGET_OBJECTS:${h}>,\n>\n")
        list(APPEND HOST_DEPENDS ${h} $<TARGET_OBJECTS:${h}>)
    endforeach()
    string(APPEND HOST_CONTENT "$<TARGET_OBJECTS:mvc2_span_end>\n")
    file(GENERATE OUTPUT ${HOST_RSP} CONTENT "${HOST_CONTENT}")
    set(RENAME)
    foreach(s data_begin data_end bss_begin bss_end)
        list(APPEND RENAME --redefine-sym ${SYM_PREFIX}span_${s}=${SYM_PREFIX}host_span_${s})
    endforeach()
    add_custom_command(OUTPUT ${CMAKE_BINARY_DIR}/${TARGET}_host.o
        COMMAND ${CMAKE_C_COMPILER} -r -nostdlib ${RELOC_FLAGS} ${TRIG_WRAPS} @${HOST_RSP} -o ${GEN}/${TARGET}_host_all.o
        COMMAND ${CMAKE_OBJCOPY} ${RENAME} ${GEN}/${TARGET}_host_all.o ${CMAKE_BINARY_DIR}/${TARGET}_host.o
        DEPENDS ${HOST_DEPENDS} ${HOST_RSP}
        COMMENT "Making the host's layers one relocatable for ${TARGET}")
    set_source_files_properties(${CMAKE_BINARY_DIR}/${TARGET}_host.o PROPERTIES EXTERNAL_OBJECT TRUE GENERATED TRUE)
    add_executable(${TARGET} ${OBJECTS} ${FRONT_SOURCES} ${PORT}/sys/restart.c)
    target_include_directories(${TARGET} PRIVATE ${PORT}/include ${PORT}/render ${PORT}/snd)
    target_compile_definitions(${TARGET} PRIVATE ${DEFS})
    target_compile_options(${TARGET} PRIVATE -O2 -std=gnu17 -Wall ${HOST_FLOAT})
    foreach(p ${ARGN})
        # the addresses of the names the program keeps in place (NAME.sys_font_thd)
        target_link_options(${TARGET} PRIVATE @${GEN}/${p}/host_defsyms.rsp)
        set_property(TARGET ${TARGET} APPEND PROPERTY LINK_DEPENDS ${GEN}/${p}/host_defsyms.rsp)
    endforeach()
    if(NINTENDO_3DS)
        target_include_directories(${TARGET} PRIVATE ${PORT}/ctr)
        target_link_libraries(${TARGET} PRIVATE citro3d ctru m)
        if(OLD_3DS_TEST)
            target_compile_definitions(${TARGET} PRIVATE OLD_3DS_TEST=1)
            set(CIA_MODE -DTITLE=MVSC2O3DS -DPRODUCT_CODE=CTR-H-MVCO -DUNIQUE_ID=0xF5C33 -DMODE_EXT=Legacy -DCPU_SPEED=268MHz
                -DL2_CACHE=false)
            set(TITLE "Marvel vs. Capcom 2 O3DS")
            set(DESCRIPTION "Old 3DS test build")
        else()
            set(CIA_MODE -DTITLE=MVSC2 -DPRODUCT_CODE=CTR-H-MVC2 -DUNIQUE_ID=0xF5C32 -DMODE_EXT=124MB -DCPU_SPEED=804MHz
                -DL2_CACHE=true)
            set(TITLE "Marvel vs. Capcom 2")
            set(DESCRIPTION "NAOMI and Dreamcast port")
        endif()
        # division by zero gives 0, as SHC's code did (divzero_arm.c)
        foreach(fn __aeabi_idiv __aeabi_uidiv __aeabi_idivmod __aeabi_uidivmod ${GAME_ZONE_WRAPS})
            target_link_options(${TARGET} PRIVATE "-Wl,--wrap=${fn}")
        endforeach()
        # an allocation that fails stops the program, saying so (ctr_alloc.c)
        target_link_options(${TARGET} PRIVATE -Wl,--wrap=malloc,--wrap=calloc,--wrap=realloc)
        # memcpy by ldm and stm with MVC2_MEMCPY_FAST=1 (ctr_memcpy.c)
        target_link_options(${TARGET} PRIVATE -Wl,--wrap=memcpy)
        # citro3d's GX queue, for MVC2_LIST_SPLIT to start it early (ctr_render.c)
        target_link_options(${TARGET} PRIVATE -Wl,--wrap=GX_BindQueue)
        target_link_options(${TARGET} PRIVATE -Wl,--no-enum-size-warning)
        target_link_options(${TARGET} PRIVATE ${TRIG_WRAPS})
        # the vertex shader, embedded as a header
        set(SHADERS ${CMAKE_BINARY_DIR}/shaders)
        add_custom_command(OUTPUT ${SHADERS}/ctr_render_shbin.h
            COMMAND ${CMAKE_COMMAND} -E make_directory ${SHADERS}
            COMMAND ${CTR_PICASSO} -o ${SHADERS}/ctr_render.shbin ${PORT}/ctr/ctr_render.v.pica
            COMMAND ${CMAKE_COMMAND} -DIN=${SHADERS}/ctr_render.shbin -DOUT=${SHADERS}/ctr_render_shbin.h
                    -DNAME=ctr_render_shbin -P ${PORT}/cmake/3ds-bin2c.cmake
            DEPENDS ${PORT}/ctr/ctr_render.v.pica ${PORT}/cmake/3ds-bin2c.cmake
            COMMENT "Assembling the 3DS renderer's shader")
        target_sources(${TARGET} PRIVATE ${SHADERS}/ctr_render_shbin.h)
        # the pictures (port/ctr/gfx), as LZ11-packed RGBA8 textures; the opaque background RGB565, as the bottom screen
        foreach(art version left right options_bg native stretch switch controls unlock)
            set(format rgba8)
            if(art STREQUAL options_bg)
                set(format rgb565)
            endif()
            add_custom_command(OUTPUT ${SHADERS}/${art}_lz.h
                COMMAND ${CMAKE_COMMAND} -E make_directory ${SHADERS}
                COMMAND ${CTR_TEX3DS} -f ${format} -z lz11 -r -o ${SHADERS}/${art}.lz ${PORT}/ctr/gfx/${art}.png
                COMMAND ${CMAKE_COMMAND} -DIN=${SHADERS}/${art}.lz -DOUT=${SHADERS}/${art}_lz.h -DNAME=${art}_lz
                        -P ${PORT}/cmake/3ds-bin2c.cmake
                DEPENDS ${PORT}/ctr/gfx/${art}.png ${PORT}/cmake/3ds-bin2c.cmake
                COMMENT "Packing ${art}.png")
            target_sources(${TARGET} PRIVATE ${SHADERS}/${art}_lz.h)
        endforeach()
        target_include_directories(${TARGET} PRIVATE ${SHADERS} ${PORT}/render)
        ctr_create_3dsx(${TARGET} "${TITLE}" "${DESCRIPTION}" ${PORT}/ctr/icon.png)
        set(CIA_DIR ${PORT}/ctr/cia)
        if(MVC2_PROF)
            # the profiler's builds may read the CPU's performance counters (MVC2_PERFCTR=1)
            set(CIA_DIR ${CMAKE_BINARY_DIR}/cia)
            file(READ ${PORT}/ctr/cia/mvc2.rsf RSF)
            string(REPLACE "    OutputDebugString: 61" "    OutputDebugString: 61\n    ControlPerformanceCounter: 62"
                   RSF "${RSF}")
            file(WRITE ${CIA_DIR}/${TARGET}.rsf "${RSF}")
            configure_file(${PORT}/ctr/cia/banner.png ${CIA_DIR}/banner.png COPYONLY)
            configure_file(${PORT}/ctr/cia/banner.wav ${CIA_DIR}/banner.wav COPYONLY)
            set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${PORT}/ctr/cia/mvc2.rsf)
        endif()
        ctr_create_cia(${TARGET} "${TITLE}" "${DESCRIPTION}" ${CIA_DIR} ${PORT}/ctr/icon.png ${CIA_MODE})
    else()
        target_link_libraries(${TARGET} PRIVATE ${SDL_LIBS} m)
        if(MVC2_SDL3)
            target_compile_definitions(${TARGET} PRIVATE MVC2_SDL3 RD_SDL3)
        endif()
        if(WIN32)
            # a fixed load address, so the RAM at 0x0C000000 stays free and runs are repeatable
            target_link_options(${TARGET} PRIVATE "-Wl,--disable-dynamicbase")
        elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "^arm")
            # libgcc's division helpers, through port/sys/divzero_arm.c
            target_link_options(${TARGET} PRIVATE
                -Wl,--wrap=__aeabi_idiv,--wrap=__aeabi_uidiv,--wrap=__aeabi_idivmod,--wrap=__aeabi_uidivmod)
        endif()
    endif()
endfunction()

# both programs, the arcade's unless the command line says --dc
mvc2_executable(mvc2 arc dc)
# the Dreamcast alone, while its host layer is young
if(NOT NINTENDO_3DS)
    mvc2_executable(mvc2dc dc)
endif()

# cmake --install BUILD --prefix FOLDER: the executables, SDL's DLL (on Linux the system's), PLAYING.md and
# mvc2.cfg (unless there is one)
if(WIN32)
    get_filename_component(TOOL_BIN ${CMAKE_C_COMPILER} DIRECTORY)
    set(DLL_DIRS ${TOOL_BIN})
    if(TARGET SDL3::SDL3-shared)
        get_target_property(SDL_DLL SDL3::SDL3-shared LOCATION)
        get_filename_component(SDL_BIN ${SDL_DLL} DIRECTORY)
        list(PREPEND DLL_DIRS ${SDL_BIN})
    endif()
    install(TARGETS mvc2 mvc2dc
            RUNTIME_DEPENDENCIES
                DIRECTORIES ${DLL_DIRS}
                PRE_EXCLUDE_REGEXES "^api-ms-" "^ext-ms-"
                POST_EXCLUDE_REGEXES "[Ss][Yy][Ss][Tt][Ee][Mm]32" "[Ss][Yy][Ss][Ww][Oo][Ww]64"
            RUNTIME DESTINATION .)
elseif(NOT NINTENDO_3DS)
    install(TARGETS mvc2 mvc2dc RUNTIME DESTINATION .)
endif()
if(NOT NINTENDO_3DS)
    install(FILES ${PORT}/dist/PLAYING.md DESTINATION .)
    install(CODE "if(NOT EXISTS \"\${CMAKE_INSTALL_PREFIX}/mvc2.cfg\")
        file(INSTALL \"${PORT}/dist/mvc2.cfg\" DESTINATION \"\${CMAKE_INSTALL_PREFIX}\")
    endif()")
endif()
