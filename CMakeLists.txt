cmake_minimum_required(VERSION 3.16)
project(SuperMarioWorldSNESRecomp C)

# The existing 1P build remains the default and retains its original target
# name. The simultaneous co-op executable is additive and consumes a separate
# generated source tree produced by `tools/regen.sh --coop`.
option(SMW_BUILD_COOP "Build the simultaneous two-player co-op executable" OFF)
option(SMW_NETPLAY_ICE
    "Enable ICE/NAT traversal for the co-op online lobby build" ON)

# Online lobbies must use ICE for peers outside the local network. Keep this
# game-facing option on by default so a normal co-op release cannot silently
# degrade to direct UDP and hang behind NAT. Developers can still configure
# -DSMW_NETPLAY_ICE=OFF for a deliberately LAN-only build.
if(SMW_BUILD_COOP)
    set(SNESRECOMP_NET_ICE ${SMW_NETPLAY_ICE} CACHE BOOL
        "Enable ICE/libjuice transport in recomp-net" FORCE)
endif()

set(CMAKE_C_STANDARD 11)

# Reported by host_report_init() in crash breadcrumbs/minidumps and used as the
# netplay game version. The MSVC .sln plumbs this through SnesRecompBuildVersion
# (src/smw.vcxproj); the CMake path needs its own knob or a release build ships
# stamped "dev" and user crash reports cannot be tied to an actual release.
set(SNESRECOMP_BUILD_VERSION "dev" CACHE STRING
    "Version reported in crash diagnostics and network metadata")

# Widescreen is a mod package, not a Display setting (same migration as Mega
# Man X / X2 / Super Mario Kart). Must be set BEFORE runner.cmake is included:
# the engine forces RECOMP_UI_ENABLE_MODS on from this and compiles the package
# loader + trusted-plugin runtime.
set(SNESRECOMP_ENABLE_MODS ON CACHE BOOL
    "Enable Super Mario World's built-in mod catalog and SNES mod packages" FORCE)

set(SNESRECOMP_ROOT "${CMAKE_SOURCE_DIR}/snesrecomp" CACHE PATH
    "Path to the snesrecomp checkout used by this game")
include(${SNESRECOMP_ROOT}/runner/runner.cmake)
if(SNESRECOMP_ENABLE_LUA AND NOT TARGET snesrecomp_lua)
    message(FATAL_ERROR "SNESRECOMP_ENABLE_LUA requires snesrecomp with Lua bridge support")
endif()
if(SNESRECOMP_ENABLE_LUA AND SMW_BUILD_COOP)
    message(FATAL_ERROR "The Lua TCP spike currently supports the stock single-player target only")
endif()
set(RECOMP_UI_ROOT "${CMAKE_SOURCE_DIR}/recomp-ui" CACHE PATH
    "Path to the recomp-ui checkout used by this game")
include(${RECOMP_UI_ROOT}/recomp_ui.cmake)

find_package(OpenGL REQUIRED)
find_package(Python3 REQUIRED COMPONENTS Interpreter)

file(GLOB SMW_STOCK_GEN_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_SOURCE_DIR}/src/gen/*.c)
if(NOT SMW_STOCK_GEN_SOURCES)
    message(FATAL_ERROR
        "src/gen/ is empty - run `bash tools/regen.sh --stock` with a "
        "verified smw.sfc at the repository root before building.")
endif()

# The extended viewport and SMW's object lifecycle must move together. The
# generated bank hooks widen draw culling, despawn bounds, sprite allocation,
# special-object OAM, and fireballs; building pristine generated C would widen
# only the framebuffer and leave gameplay at the stock 256-pixel boundaries.
#
# Make every generated object depend on an idempotent injection stamp. A regen
# makes the bank C newer than the stamp, so the hooks are restored before any
# affected source recompiles. Keeping this in CMake also covers direct builds
# that do not go through a release wrapper.
set(SMW_WIDESCREEN_OVERRIDE_STAMP
    ${CMAKE_CURRENT_BINARY_DIR}/smw_widescreen_overrides.stamp)
set(SMW_FALCON_OVERRIDE_MANIFEST
    ${CMAKE_SOURCE_DIR}/overrides/falcon/overrides.manifest)
add_custom_command(
    OUTPUT ${SMW_WIDESCREEN_OVERRIDE_STAMP}
    COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_SOURCE_DIR}/tools/apply_overrides.py
            --check
            --gen-dir ${CMAKE_SOURCE_DIR}/src/gen
            --manifest ${CMAKE_SOURCE_DIR}/overrides/widescreen/overrides.manifest
    COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_SOURCE_DIR}/tools/apply_overrides.py
            --check
            --gen-dir ${CMAKE_SOURCE_DIR}/src/gen
            --manifest ${SMW_FALCON_OVERRIDE_MANIFEST}
    COMMAND ${CMAKE_COMMAND} -E touch ${SMW_WIDESCREEN_OVERRIDE_STAMP}
    DEPENDS
        ${CMAKE_SOURCE_DIR}/tools/apply_overrides.py
        ${CMAKE_SOURCE_DIR}/overrides/widescreen/overrides.manifest
        ${SMW_FALCON_OVERRIDE_MANIFEST}
        ${CMAKE_SOURCE_DIR}/overrides/falcon/falcon_smw_adapter.c
        ${SMW_STOCK_GEN_SOURCES}
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Applying widescreen object-lifecycle hooks"
    VERBATIM)
set_source_files_properties(${SMW_STOCK_GEN_SOURCES} PROPERTIES
    OBJECT_DEPENDS ${SMW_WIDESCREEN_OVERRIDE_STAMP})

if(SMW_BUILD_COOP)
    file(GLOB SMW_COOP_GEN_SOURCES CONFIGURE_DEPENDS
        ${CMAKE_SOURCE_DIR}/src/gen-coop/*.c)
    if(NOT SMW_COOP_GEN_SOURCES)
        message(FATAL_ERROR
            "src/gen-coop/ is empty - run `bash tools/regen.sh --coop` with "
            "a verified stock smw.sfc before enabling SMW_BUILD_COOP.")
    endif()

    # Keep the experimental co-op widescreen spike buildable for future work.
    # The IPS emits separate object-lifecycle variants and moves its Layer 1
    # streamer into bank $1F, so the generated co-op tree needs its own check.
    # Runtime and launcher support remain gated off until streaming is reliable.
    set(SMW_COOP_WIDESCREEN_OVERRIDE_STAMP
        ${CMAKE_CURRENT_BINARY_DIR}/smw_coop_widescreen_overrides.stamp)
    add_custom_command(
        OUTPUT ${SMW_COOP_WIDESCREEN_OVERRIDE_STAMP}
        COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_SOURCE_DIR}/tools/apply_overrides.py
                --check
                --gen-dir ${CMAKE_SOURCE_DIR}/src/gen-coop
                --manifest ${CMAKE_SOURCE_DIR}/overrides/widescreen/overrides.manifest
        COMMAND ${CMAKE_COMMAND} -E touch ${SMW_COOP_WIDESCREEN_OVERRIDE_STAMP}
        DEPENDS
            ${CMAKE_SOURCE_DIR}/tools/apply_overrides.py
            ${CMAKE_SOURCE_DIR}/overrides/widescreen/overrides.manifest
            ${SMW_COOP_GEN_SOURCES}
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        COMMENT "Applying co-op widescreen object-lifecycle hooks"
        VERBATIM)
    set_source_files_properties(${SMW_COOP_GEN_SOURCES} PROPERTIES
        OBJECT_DEPENDS ${SMW_COOP_WIDESCREEN_OVERRIDE_STAMP})
endif()

file(GLOB SMW_OVERRIDE_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_SOURCE_DIR}/overrides/*/*.c)

set(SMW_RUNTIME_SOURCES
    ${SNESRECOMP_RUNNER_SOURCES}
    src/config.c
    src/foreign_controller.c
    src/mod_external_rom_compat.c
    src/main.c
    src/opengl.c
    src/smw_00.c
    src/gen_stubs.c
    src/smw_cpu_infra.c
    src/smw_rtl.c
    src/smw_spc_player.c
    src/mods/falcon/falcon_locomotion.c
    src/mods/falcon/captain_falcon_foreign.c
    src/mods/falcon/smw_falcon_combat_policy.c
    src/mods/falcon/smw_falcon_combat_apply.c
    src/mods/falcon/falcon_presentation.c
    src/mods/falcon/smw_falcon_audio.c
    src/mods/falcon/smw_falcon_presentation_runtime.c
    src/mods/smw_falcon_plugin.c
    # Trusted static plugin backing the widescreen mod package's activation.
    src/mods/smw_widescreen_plugin.c
    # Trusted static plugin backing the MSU-1 audio mod package's activation.
    src/mods/smw_msu1_plugin.c
    third_party/gl_core/gl_core_3_1.c
    ${SMW_OVERRIDE_SOURCES}
)

# Newer framework checkouts provide this shared helper. Retain compatibility
# with the game's older submodule pin without linking both implementations.
if(NOT EXISTS "${SNESRECOMP_ROOT}/runner/src/mod_audio.c")
    list(APPEND SMW_RUNTIME_SOURCES src/mod_audio.c)
endif()
if(SNESRECOMP_ENABLE_LUA)
    list(APPEND SMW_RUNTIME_SOURCES src/lua_fire_stream.c)
endif()

if(SNESRECOMP_ENABLE_TRACE)
    set(_SMW_TRACE 1)
else()
    set(_SMW_TRACE 0)
endif()

function(smw_add_recomp_target target_name generated_sources)
    add_executable(${target_name}
        ${SMW_RUNTIME_SOURCES}
        ${generated_sources}
    )
    set_property(SOURCE src/main.c APPEND PROPERTY COMPILE_DEFINITIONS
        SNESRECOMP_BUILD_VERSION="${SNESRECOMP_BUILD_VERSION}")

    target_include_directories(${target_name} PRIVATE
        ${CMAKE_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/recomp
        ${SNESRECOMP_RUNNER_INCLUDE_DIRS}
    )
    snesrecomp_target_post_mortem(${target_name})
    snesrecomp_target_glsl_shader(${target_name})
    snesrecomp_target_sdl(${target_name})

    target_compile_definitions(${target_name} PRIVATE
        SMW_DISABLE_LM=1
        SYSTEM_VOLUME_MIXER_AVAILABLE=0
        SNESRECOMP_TRACE=${_SMW_TRACE}
        SNESRECOMP_REVERSE_DEBUG=0
        # SMW's stock title attract demo is a recorded choreography under
        # game mode $07. Letting the LLE scheduler bounce into generated AOT
        # bodies by default skews that path enough for Mario to enter death
        # state around frame 1026. Keep the stock runtime on the interpreted
        # floor by default; Falcon/mod work can still opt into bounce with
        # SNESRECOMP_LLE_BOUNCE=1 or a narrower target-specific exclusion.
        SNESRECOMP_LLE_BOUNCE_DEFAULT=0
    )

    if(NOT MSVC)
        target_compile_options(${target_name} PRIVATE
            $<$<COMPILE_LANGUAGE:C>:-w;-Wno-implicit-function-declaration>
            $<$<COMPILE_LANGUAGE:CXX>:-w>)
    endif()

    target_link_libraries(${target_name} PRIVATE
        OpenGL::GL
        ${SNESRECOMP_RUNNER_LIBRARIES}
    )

    if(WIN32)
        target_link_libraries(${target_name} PRIVATE ws2_32)
    endif()

    # CONSOLE is required by recomp-ui: it selects exactly one validated
    # assets/consoles/<id>/ manifest, so an SNES game cannot silently stage
    # N64 cartridge or NES pad art next to the exe.
    recomp_target_launcher_ui(${target_name}
        CONSOLE snes
        BOXART ${CMAKE_SOURCE_DIR}/recomp/launcher/boxart.tga)
    if(SNESRECOMP_ENABLE_LUA)
        set(_lua_dir "$<TARGET_FILE_DIR:${target_name}>/lua")
        add_custom_command(TARGET ${target_name} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/lua ${_lua_dir}
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${CMAKE_SOURCE_DIR}/tools/lua/smw.lua ${_lua_dir}/smw.lua
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${SNESRECOMP_ROOT}/tools/lua_tcp.py ${_lua_dir}/lua_tcp.py)
        set_property(TARGET ${target_name} APPEND PROPERTY LINK_DEPENDS
            ${CMAKE_SOURCE_DIR}/lua/100_fireballs.lua
            ${CMAKE_SOURCE_DIR}/lua/README.md
            ${CMAKE_SOURCE_DIR}/lua/LICENSE-Lua.txt
            ${CMAKE_SOURCE_DIR}/tools/lua/smw.lua
            ${SNESRECOMP_ROOT}/tools/lua_tcp.py)
    endif()
endfunction()

smw_add_recomp_target(SuperMarioWorldSNESRecomp
    "${SMW_STOCK_GEN_SOURCES}")

# Release-owned mod catalog staged next to the exe. mod_runtime resolves "mods"
# against the anchored cwd (the exe dir on Windows, the .AppImage's folder on
# Linux), so the catalog has to sit beside the binary, not inside the source
# tree. tools/make_release.ps1 and tools/build-linux.sh both pick it up from
# here, and the Linux packaging step fails outright if it is missing.
set(SMW_PRELOADED_MODS "${CMAKE_CURRENT_SOURCE_DIR}/mods/preloaded")
if(EXISTS "${SMW_PRELOADED_MODS}/packages")
    add_custom_command(TARGET SuperMarioWorldSNESRecomp POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${SMW_PRELOADED_MODS}"
                "$<TARGET_FILE_DIR:SuperMarioWorldSNESRecomp>/mods")
endif()

if(SMW_BUILD_COOP)
    smw_add_recomp_target(SuperMarioWorldCoopSNESRecomp
        "${SMW_COOP_GEN_SOURCES}")
    target_sources(SuperMarioWorldCoopSNESRecomp PRIVATE
        src/coop_patch.c
        src/smw_netplay_lobby.c)
    target_include_directories(SuperMarioWorldCoopSNESRecomp BEFORE PRIVATE
        ${CMAKE_SOURCE_DIR}/recomp/coop)
    target_compile_definitions(SuperMarioWorldCoopSNESRecomp PRIVATE
        SMW_COOP_BUILD=1
        SNESRECOMP_LLE_BOUNCE_DEFAULT=0
        SNES_GAME_VERSION="smw-coop-netplay-1"
    )
    # recomp-net supplies deterministic two-peer delay-sync, LAN/ICE transport,
    # and the lobby client consumed by recomp-ui's game-owned callbacks.
    snesrecomp_enable_recomp_net(SuperMarioWorldCoopSNESRecomp)
    add_custom_command(TARGET SuperMarioWorldCoopSNESRecomp POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${CMAKE_SOURCE_DIR}/recomp/coop/smw_coop.ips
                $<TARGET_FILE_DIR:SuperMarioWorldCoopSNESRecomp>/smw_coop.ips)
endif()
