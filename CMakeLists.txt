cmake_minimum_required(VERSION 3.16)
project(SuperMarioWorldRecomp C)

# Cross-platform (macOS/Linux) build for SuperMarioWorldRecomp.
#
# The canonical Windows build is the MSVC solution (smw.sln). This CMake build
# mirrors the same source list (see snesrecomp/runner/runner.cmake) so the game
# builds natively on macOS/Linux with clang/gcc + SDL2 + OpenGL.
#
# Prerequisites: a snesrecomp checkout at ./snesrecomp (symlink/clone of the
# sibling repo), the generated C in src/gen/ (run `bash tools/regen.sh` with a
# verified smw.sfc at the repo root), SDL2 (e.g. `brew install sdl2`).

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

add_executable(SuperMarioWorldRecomp
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
)

target_include_directories(SuperMarioWorldRecomp PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/recomp
    ${SNESRECOMP_RUNNER_INCLUDE_DIRS}
    ${SDL2_INCLUDE_DIRS}
)

target_compile_definitions(SuperMarioWorldRecomp PRIVATE
    SMW_DISABLE_LM=1
    SYSTEM_VOLUME_MIXER_AVAILABLE=0   # Win32 WASAPI volume control excluded
    SNESRECOMP_TRACE=0                # no TCP debug server (debug_server.c excluded)
    SNESRECOMP_REVERSE_DEBUG=0        # raw WRAM stores; matches non-debug generated C
)

# The recompiled C is machine-generated; keep its warnings from drowning the
# build, and tolerate the K&R-style implicit declarations clang rejects by
# default (as on the NES side).
if(NOT MSVC)
    target_compile_options(SuperMarioWorldRecomp PRIVATE
        -w -Wno-implicit-function-declaration)
endif()

target_link_libraries(SuperMarioWorldRecomp PRIVATE
    ${SDL2_LIBRARIES}
    OpenGL::GL
)
