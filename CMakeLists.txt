cmake_minimum_required(VERSION 3.25)
project(tailonlock VERSION 0.1.0 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

option(A4C_SANITIZE    "Build with AddressSanitizer and UBSan (GCC/Clang)" OFF)
option(A4C_BUILD_RELAY "Build the online relay server" ON)
option(A4C_BUILD_TESTS "Build the tests" ON)
option(A4C_WERROR      "Treat warnings in our code as errors (tools/check)" OFF)

# Static MSVC runtime (/MT), so the .exe has no VC++ redistributable dependency.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# ---- third-party ----------------------------------------------------------
include(FetchContent)

# SDL3 is not packaged by every Linux distribution, so it is always built from source and
# linked statically. Pinned to a release tarball with a checksum.
set(SDL_SHARED       OFF CACHE BOOL "" FORCE)
set(SDL_STATIC       ON  CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_TESTS        OFF CACHE BOOL "" FORCE)
set(SDL_EXAMPLES     OFF CACHE BOOL "" FORCE)
set(SDL_INSTALL      OFF CACHE BOOL "" FORCE)
FetchContent_Declare(SDL3
  URL      https://github.com/libsdl-org/SDL/releases/download/release-3.4.18/SDL3-3.4.18.tar.gz
  URL_HASH SHA256=9c75cf16330322c217dedd2e0609f1124f1b54b8633e763467b4684d0f4334a3
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
  SYSTEM)
FetchContent_MakeAvailable(SDL3)

# Monocypher is not kept in the repository either: a pinned release tarball
# with a checksum, until our own crypto (brick 22) replaces it
# (docs/BUILD_PLAN.md). Only its C file is used; its own build files are not.
FetchContent_Declare(monocypher
  URL      https://monocypher.org/download/monocypher-4.0.2.tar.gz
  URL_HASH SHA256=38d07179738c0c90677dba3ceb7a7b8496bcfea758ba1a53e803fed30ae0879c
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
  SOURCE_SUBDIR do-not-build)
FetchContent_MakeAvailable(monocypher)
set(MONOCYPHER_DIR ${monocypher_SOURCE_DIR}/src)

# ---- warnings and flags ---------------------------------------------------
add_library(a4c_flags INTERFACE)
if(MSVC)
  target_compile_options(a4c_flags INTERFACE /W4 /permissive- /utf-8
    /wd4100   # unreferenced parameter (stubs)
    $<$<CONFIG:Release>:/O2 /GL>)
  target_link_options(a4c_flags INTERFACE $<$<CONFIG:Release>:/LTCG>)
  target_compile_definitions(a4c_flags INTERFACE _CRT_SECURE_NO_WARNINGS)
else()
  # No -march=native: releases must run on plain x86-64 (some distributions
  # default to x86-64-v3, older PCs do not have AVX2).
  target_compile_options(a4c_flags INTERFACE
    -Wall -Wextra -Wshadow -Wstrict-prototypes -Wno-unused-parameter)
  if(A4C_WERROR)
    target_compile_options(a4c_flags INTERFACE -Werror)
  endif()
  if(A4C_SANITIZE)
    target_compile_options(a4c_flags INTERFACE -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(a4c_flags INTERFACE -fsanitize=address,undefined)
  endif()
  if(MINGW)
    target_link_options(a4c_flags INTERFACE -static -static-libgcc)
  endif()
endif()

# ---- game core (no SDL: sim, AI, world) -----------------------------------
add_library(a4c_core STATIC
  src/core/world.c
  src/core/mesh.c
  src/core/game.c
  src/core/sim.c
  src/core/ai.c
  src/core/jets.c
  src/core/map.c
  src/core/defend.c
  src/core/json.c
  src/core/glb.c)
target_include_directories(a4c_core PUBLIC src)
target_link_libraries(a4c_core PUBLIC a4c_flags)
if(NOT MSVC)
  target_link_libraries(a4c_core PUBLIC m)
endif()

# ---- the game -------------------------------------------------------------
add_executable(tailonlock
  src/main.c
  src/assets.c
  src/intro.c
  src/command.c
  src/synth.c
  src/compose.c
  src/editor.c
  src/gpu.c
  ${MONOCYPHER_DIR}/monocypher.c
  src/raster.c
  src/render3d.c
  src/hud.c
  src/cockpit.c
  src/hangar.c
  src/flighttest.c
  src/ui.c
  src/input.c
  src/config.c
  src/sfx.c
  src/music.c
  src/net.c)
if(WIN32)
  target_sources(tailonlock PRIVATE src/platform/plat_win32.c)
  target_link_libraries(tailonlock PRIVATE ws2_32 bcrypt)
  set_target_properties(tailonlock PROPERTIES WIN32_EXECUTABLE ON OUTPUT_NAME TailonLock)   # TailonLock.exe
else()
  target_sources(tailonlock PRIVATE src/platform/plat_posix.c)
endif()
target_link_libraries(tailonlock PRIVATE a4c_core SDL3::SDL3-static)
target_compile_definitions(tailonlock PRIVATE A4C_VERSION="${PROJECT_VERSION}"
  A4C_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
target_include_directories(tailonlock SYSTEM PRIVATE ${MONOCYPHER_DIR})
if(NOT MSVC)
  set_source_files_properties(${MONOCYPHER_DIR}/monocypher.c PROPERTIES COMPILE_OPTIONS -w)
endif()
# The game looks for assets/ next to the program, so a build folder (or a zip
# of it) runs on its own.
add_custom_command(TARGET tailonlock POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/assets $<TARGET_FILE_DIR:tailonlock>/assets
  VERBATIM)

# ---- relay server ---------------------------------------------------------
if(A4C_BUILD_RELAY AND NOT WIN32)
  add_executable(a4c_relay relay/relay.c)
  target_link_libraries(a4c_relay PRIVATE a4c_flags)
endif()

# ---- tests ----------------------------------------------------------------
if(A4C_BUILD_TESTS)
  enable_testing()
  add_executable(test_core tests/test_core.c)
  target_link_libraries(test_core PRIVATE a4c_core)
  add_test(NAME core COMMAND test_core)
  add_executable(test_json tests/test_json.c)
  target_link_libraries(test_json PRIVATE a4c_core)
  add_test(NAME json COMMAND test_json)
  # 10 seconds of fuzzing from the map and intro files (the brick audit runs it for 10 minutes)
  add_executable(fuzz_json tests/fuzz_json.c)
  target_link_libraries(fuzz_json PRIVATE a4c_core)
  add_test(NAME json_fuzz COMMAND fuzz_json 10 ${CMAKE_SOURCE_DIR}/assets/maps/maps_index.json
    ${CMAKE_SOURCE_DIR}/assets/jets/jets_data.json ${CMAKE_SOURCE_DIR}/assets/intro/intro_script.json
    ${CMAKE_SOURCE_DIR}/assets/music/ghost.json)
  add_executable(test_glb tests/test_glb.c)
  target_link_libraries(test_glb PRIVATE a4c_core)
  add_test(NAME glb COMMAND test_glb)
  add_executable(fuzz_glb tests/fuzz_glb.c)
  target_link_libraries(fuzz_glb PRIVATE a4c_core)
  add_test(NAME glb_fuzz COMMAND fuzz_glb 10)
  # the checker's golden-data dumper (tools/check, stage 3)
  add_executable(json_dump tests/json_dump.c)
  target_link_libraries(json_dump PRIVATE a4c_core)
  # the checker's picture and sound comparisons (tools/check, stages 7-8)
  add_executable(check_cmp tests/check_cmp.c)
  target_link_libraries(check_cmp PRIVATE a4c_flags $<$<NOT:$<BOOL:${WIN32}>>:m>)

  # Opens the game with SDL's dummy drivers and runs the menu demo, then a
  # co-op match with two seats and two CPU jets. This is the
  # headless smoke test CI uses; it needs no display or sound card.
  if(NOT CMAKE_CROSSCOMPILING)
    add_test(NAME smoke COMMAND tailonlock --frames 120)
    add_test(NAME smoke_play COMMAND tailonlock --frames 300 --play coop --seed 1)
    set_tests_properties(smoke smoke_play PROPERTIES
      ENVIRONMENT "SDL_VIDEO_DRIVER=dummy;SDL_AUDIO_DRIVER=dummy;SDL_RENDER_DRIVER=software"
      TIMEOUT 90)
  endif()
endif()
