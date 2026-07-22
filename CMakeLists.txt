cmake_minimum_required(VERSION 3.16)
project(SuperMarioWorldCoopSNESRecomp C)

# Cross-platform (macOS/Linux) build for SuperMarioWorldRecomp.
#
# The canonical Windows build is the MSVC solution (smw.sln). This CMake build
# mirrors the same source list (see snesrecomp/runner/runner.cmake) so the game
# builds natively on macOS/Linux with clang/gcc + SDL2 + OpenGL. Modeled on the
# MegamanXRecomp CMakeLists; SMW uses no Win32 Fiber API, so no fiber shim.
#
# Prerequisites: a snesrecomp checkout at ./snesrecomp (symlink/clone of the
# sibling repo), the generated C in src/gen/ (run `bash tools/regen.sh` with a
# verified smw.sfc at the repo root), SDL2 (e.g. `apt install libsdl2-dev`).

set(CMAKE_C_STANDARD 11)

set(SNESRECOMP_ROOT ${CMAKE_SOURCE_DIR}/snesrecomp)
include(${SNESRECOMP_ROOT}/runner/runner.cmake)

# Launcher UI backend: the SHARED recomp-ui launcher (F:\Projects\recomp-ui,
# consumed as the ./recomp-ui submodule/junction) so every recomp game drives
# ONE launcher repo. Mirrors MegamanXRecomp/CMakeLists.txt.
include(${CMAKE_SOURCE_DIR}/recomp-ui/recomp_ui.cmake)

find_package(SDL2 REQUIRED)
find_package(OpenGL REQUIRED)

# MinGW's SDL2 package includes SDL2main by default, but SMW provides a plain
# main() and uses the CRT entry point (matching the MSVC project).
if(WIN32)
    list(FILTER SDL2_LIBRARIES EXCLUDE REGEX "SDL2main")
endif()

# Generated recompiler output (produced by tools/regen.sh; not committed).
file(GLOB SMW_GEN_SOURCES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/src/gen/*.c)
if(NOT SMW_GEN_SOURCES)
    message(FATAL_ERROR
        "src/gen/ is empty — run `bash tools/regen.sh` with a verified smw.sfc "
        "at the repo root before building.")
endif()

# Active gameplay overrides (widescreen edge-cull, etc.). Globbed so new
# override .c files are picked up automatically: the Windows .vcxproj lists
# them explicitly, and that drift previously broke the Linux link —
# bank02_v2.c references Override_FireballGfx from overrides/widescreen/.
file(GLOB SMW_OVERRIDE_SOURCES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/overrides/*/*.c)

add_executable(SuperMarioWorldCoopSNESRecomp
    ${SNESRECOMP_RUNNER_SOURCES}
    # game-specific runtime
    src/config.c
    src/coop_patch.c
    src/glsl_shader.c
    src/main.c
    src/post_mortem.c
    src/opengl.c
    src/smw_00.c
    src/gen_stubs.c
    src/smw_cpu_infra.c
    src/smw_rtl.c
    src/smw_spc_player.c

    # OpenGL function loader
    third_party/gl_core/gl_core_3_1.c
    # recompiled banks (generated)
    ${SMW_GEN_SOURCES}
    # gameplay overrides (globbed above)
    ${SMW_OVERRIDE_SOURCES}
)

target_include_directories(SuperMarioWorldCoopSNESRecomp PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/recomp
    ${SNESRECOMP_RUNNER_INCLUDE_DIRS}
    ${SDL2_INCLUDE_DIRS}
)

# prod-vs-debug is the single switch SNESRECOMP_ENABLE_TRACE (defined in
# runner.cmake, default OFF). Prod: no debug_server.c, no TCP server, no rings.
# Debug: -DSNESRECOMP_ENABLE_TRACE=ON pulls debug_server.c in and flips the
# SNESRECOMP_TRACE code paths on. Keep the define derived from the one option so
# the two never disagree. tools/build-linux.sh --config {prod,debug} sets it.
if(SNESRECOMP_ENABLE_TRACE)
    set(_SMW_TRACE 1)
else()
    set(_SMW_TRACE 0)
endif()
target_compile_definitions(SuperMarioWorldCoopSNESRecomp PRIVATE
    SMW_COOP_BUILD=1
    SMW_DISABLE_LM=1
    SNESRECOMP_LLE_BOUNCE_DEFAULT=0  # expansion dispatchers stay byte-exact
    SYSTEM_VOLUME_MIXER_AVAILABLE=0   # Win32 WASAPI volume control excluded
    SNESRECOMP_TRACE=${_SMW_TRACE}    # TCP debug server: 0 in prod, 1 in debug
    SNESRECOMP_REVERSE_DEBUG=0        # raw WRAM stores; matches non-debug generated C
)

# The recompiled C is machine-generated; quiet its warnings and tolerate the
# K&R-style implicit declarations clang rejects by default.
if(NOT MSVC)
    target_compile_options(SuperMarioWorldCoopSNESRecomp PRIVATE
        -w -Wno-implicit-function-declaration)
endif()

target_link_libraries(SuperMarioWorldCoopSNESRecomp PRIVATE
    ${SDL2_LIBRARIES}
    OpenGL::GL
    m                                 # libm: glsl_shader.c uses pow() (implicit on macOS)
)

if(WIN32)
    # Trace/debug-server sockets and post-mortem symbolization.
    target_link_libraries(SuperMarioWorldCoopSNESRecomp PRIVATE ws2_32 dbghelp)
endif()

add_custom_command(TARGET SuperMarioWorldCoopSNESRecomp POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_SOURCE_DIR}/recomp/coop/smw_coop.ips
            $<TARGET_FILE_DIR:SuperMarioWorldCoopSNESRecomp>/smw_coop.ips)

# Dear ImGui pre-boot launcher (GUI) from the shared recomp-ui repo. One call
# wires sources/includes/RECOMP_LAUNCHER define/CXX/asset-staging (same SMW
# boxart; recomp-ui's default pad.tga is already the SNES pad).
recomp_target_launcher_ui(SuperMarioWorldCoopSNESRecomp
    BOXART ${CMAKE_SOURCE_DIR}/recomp/launcher/boxart.tga)
