cmake_minimum_required(VERSION 3.16)
project(SuperMarioWorldSNESRecomp C)

# The existing 1P build remains the default and retains its original target
# name. The simultaneous co-op executable is additive and consumes a separate
# generated source tree produced by `tools/regen.sh --coop`.
option(SMW_BUILD_COOP "Build the simultaneous two-player co-op executable" OFF)

set(CMAKE_C_STANDARD 11)

set(SNESRECOMP_ROOT ${CMAKE_SOURCE_DIR}/snesrecomp)
include(${SNESRECOMP_ROOT}/runner/runner.cmake)
include(${CMAKE_SOURCE_DIR}/recomp-ui/recomp_ui.cmake)

find_package(SDL2 REQUIRED)
find_package(OpenGL REQUIRED)

if(WIN32)
    list(FILTER SDL2_LIBRARIES EXCLUDE REGEX "SDL2main")
endif()

file(GLOB SMW_STOCK_GEN_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_SOURCE_DIR}/src/gen/*.c)
if(NOT SMW_STOCK_GEN_SOURCES)
    message(FATAL_ERROR
        "src/gen/ is empty - run `bash tools/regen.sh --stock` with a "
        "verified smw.sfc at the repository root before building.")
endif()

if(SMW_BUILD_COOP)
    file(GLOB SMW_COOP_GEN_SOURCES CONFIGURE_DEPENDS
        ${CMAKE_SOURCE_DIR}/src/gen-coop/*.c)
    if(NOT SMW_COOP_GEN_SOURCES)
        message(FATAL_ERROR
            "src/gen-coop/ is empty - run `bash tools/regen.sh --coop` with "
            "a verified stock smw.sfc before enabling SMW_BUILD_COOP.")
    endif()
endif()

file(GLOB SMW_OVERRIDE_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_SOURCE_DIR}/overrides/*/*.c)

set(SMW_RUNTIME_SOURCES
    ${SNESRECOMP_RUNNER_SOURCES}
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
    third_party/gl_core/gl_core_3_1.c
    ${SMW_OVERRIDE_SOURCES}
)

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

    target_include_directories(${target_name} PRIVATE
        ${CMAKE_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/recomp
        ${SNESRECOMP_RUNNER_INCLUDE_DIRS}
        ${SDL2_INCLUDE_DIRS}
    )

    target_compile_definitions(${target_name} PRIVATE
        SMW_DISABLE_LM=1
        SYSTEM_VOLUME_MIXER_AVAILABLE=0
        SNESRECOMP_TRACE=${_SMW_TRACE}
        SNESRECOMP_REVERSE_DEBUG=0
    )

    if(NOT MSVC)
        target_compile_options(${target_name} PRIVATE
            $<$<COMPILE_LANGUAGE:C>:-w;-Wno-implicit-function-declaration>
            $<$<COMPILE_LANGUAGE:CXX>:-w>)
    endif()

    target_link_libraries(${target_name} PRIVATE
        ${SDL2_LIBRARIES}
        OpenGL::GL
        m
    )

    if(WIN32)
        target_link_libraries(${target_name} PRIVATE ws2_32 dbghelp)
    endif()

    recomp_target_launcher_ui(${target_name}
        BOXART ${CMAKE_SOURCE_DIR}/recomp/launcher/boxart.tga)
endfunction()

smw_add_recomp_target(SuperMarioWorldSNESRecomp
    "${SMW_STOCK_GEN_SOURCES}")

if(SMW_BUILD_COOP)
    smw_add_recomp_target(SuperMarioWorldCoopSNESRecomp
        "${SMW_COOP_GEN_SOURCES}")
    target_sources(SuperMarioWorldCoopSNESRecomp PRIVATE src/coop_patch.c)
    target_include_directories(SuperMarioWorldCoopSNESRecomp BEFORE PRIVATE
        ${CMAKE_SOURCE_DIR}/recomp/coop)
    target_compile_definitions(SuperMarioWorldCoopSNESRecomp PRIVATE
        SMW_COOP_BUILD=1
        SNESRECOMP_LLE_BOUNCE_DEFAULT=0
    )
    add_custom_command(TARGET SuperMarioWorldCoopSNESRecomp POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${CMAKE_SOURCE_DIR}/recomp/coop/smw_coop.ips
                $<TARGET_FILE_DIR:SuperMarioWorldCoopSNESRecomp>/smw_coop.ips)
endif()
