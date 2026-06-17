cmake_minimum_required(VERSION 3.16)
project(SuperMarioWorldSNESRecomp C)

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

find_package(SDL2 REQUIRED)
find_package(OpenGL REQUIRED)

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

add_executable(SuperMarioWorldSNESRecomp
    ${SNESRECOMP_RUNNER_SOURCES}
    # game-specific runtime
    src/config.c
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

target_include_directories(SuperMarioWorldSNESRecomp PRIVATE
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
target_compile_definitions(SuperMarioWorldSNESRecomp PRIVATE
    SMW_DISABLE_LM=1
    SYSTEM_VOLUME_MIXER_AVAILABLE=0   # Win32 WASAPI volume control excluded
    SNESRECOMP_TRACE=${_SMW_TRACE}    # TCP debug server: 0 in prod, 1 in debug
    SNESRECOMP_REVERSE_DEBUG=0        # raw WRAM stores; matches non-debug generated C
)

# The recompiled C is machine-generated; quiet its warnings and tolerate the
# K&R-style implicit declarations clang rejects by default.
if(NOT MSVC)
    target_compile_options(SuperMarioWorldSNESRecomp PRIVATE
        -w -Wno-implicit-function-declaration)
endif()

target_link_libraries(SuperMarioWorldSNESRecomp PRIVATE
    ${SDL2_LIBRARIES}
    OpenGL::GL
    m                                 # libm: glsl_shader.c uses pow() (implicit on macOS)
)
