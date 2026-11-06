cmake_minimum_required(VERSION 3.25)
# macOS: run on macOS 11 and later, Intel and Apple silicon (set before project() so the compiler sees it)
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Oldest macOS the game runs on")
project(tailonlock VERSION 0.2.0 LANGUAGES C)
if(APPLE) # the macOS backend (plat_mac.m, gx_metal.m) is Objective-C on Apple's frameworks, with ARC
  enable_language(OBJC)
  set(CMAKE_OBJC_FLAGS "${CMAKE_OBJC_FLAGS} -fobjc-arc")
endif()

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
option(A4C_LIBFUZZER   "Build the libFuzzer targets tests/lf_*.c (clang with -fsanitize=fuzzer)" OFF)

# Static MSVC runtime (/MT), so the .exe has no VC++ redistributable dependency.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# ---- no third-party code ---------------------------------------------------
# The game uses only what the operating system gives: our own platform layer
# (src/platform/) talks to Wayland, X11, ALSA, evdev and Vulkan on Linux, to
# Win32, WASAPI, XInput and Direct3D 12 on Windows, and to Cocoa, CoreAudio,
# GameController and Metal on macOS. The Linux libraries are opened at run time.

# ---- warnings and flags ---------------------------------------------------
add_library(a4c_flags INTERFACE)
if(MSVC)
  target_compile_options(a4c_flags INTERFACE /W4 /permissive- /utf-8 /fp:precise # /fp:precise: no contractions (MSVC 2022 and later)
    /wd4100   # unreferenced parameter (stubs)
    $<$<CONFIG:Release>:/O2 /GL>)
  target_link_options(a4c_flags INTERFACE $<$<CONFIG:Release>:/LTCG>)
  target_compile_definitions(a4c_flags INTERFACE _CRT_SECURE_NO_WARNINGS)
else()
  # No -march=native: releases must run on plain x86-64 (some distributions
  # default to x86-64-v3, older PCs do not have AVX2).
  # So the baseline is asked for outright on x86-64 Linux and Windows: a GCC that defaults to x86-64-v3
  # builds programs that would stop with an illegal instruction on a CPU without AVX2 (macOS builds its x86_64 slice for its own
  # minimum, and an arm64 build has no such choice)
  if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64|amd64)$" AND NOT APPLE)
    target_compile_options(a4c_flags INTERFACE -march=x86-64 -mtune=generic)
  endif()
  target_compile_options(a4c_flags INTERFACE
    -Wall -Wextra -Wshadow -Wstrict-prototypes -Wno-unused-parameter
    # docs/SAFE_C.md: no variable-length arrays, format strings checked, no cast that loses alignment, every switch
    # fall-through marked, a NULL the compiler can prove is dereferenced is an error
    -Wformat=2 -Wvla -Wcast-align -Wnull-dereference -Wimplicit-fallthrough)
  # the stack's canaries on every function with an array or an address taken; the C library's checked string and
  # memory calls (level 3 where the compiler has it, 2 otherwise; only with optimisation, as glibc wants)
  target_compile_options(a4c_flags INTERFACE -fstack-protector-strong)
  if(NOT A4C_SANITIZE)
    include(CheckCCompilerFlag)
    check_c_compiler_flag("-D_FORTIFY_SOURCE=3 -O2 -Werror" A4C_HAVE_FORTIFY3)
    target_compile_options(a4c_flags INTERFACE $<$<NOT:$<CONFIG:Debug>>:-U_FORTIFY_SOURCE
      $<IF:$<BOOL:${A4C_HAVE_FORTIFY3}>,-D_FORTIFY_SOURCE=3,-D_FORTIFY_SOURCE=2>>)
  endif()
  # The simulation must compute the same bits on every computer (network play,
  # replays, the audit's hash): no fusing a * b + c into one rounding, which
  # Clang does by default where the CPU has FMA (arm64) and GCC with -mfma.
  target_compile_options(a4c_flags INTERFACE -ffp-contract=off)
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

# ---- game core (no platform code: sim, AI, world) --------------------------
set(A4C_CORE_SOURCES # also built into the libFuzzer targets, instrumented
  src/ff/tlpk.c # the floppy's unpackers (docs/FLOPPY.md): assets.c reads packed files through them
  src/ff/tlj.c
  src/ff/tlh.c
  src/core/world.c
  src/core/mesh.c
  src/core/game.c
  src/core/sim.c
  src/core/ai.c
  src/core/jets.c
  src/core/map.c
  src/core/defend.c
  src/core/json.c
  src/core/glb.c
  src/core/crypto.c
  src/core/fmath.c
  src/figure.c
  src/room.c)
add_library(a4c_core STATIC ${A4C_CORE_SOURCES})
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
  src/platform/plat_common.c
  src/platform/plat_core.c
  src/cic.c
  src/paint.c
  src/command.c
  src/synth.c
  src/compose.c
  src/editor.c
  src/gpu.c
  src/raster.c
  src/render3d.c
  src/viewfx.c
  src/stores.c
  src/surfaces.c
  src/hud.c
  src/cockpit.c
  src/hangar.c
  src/flighttest.c
  src/doctrinetest.c
  src/audit.c
  src/watch.c
  src/eyes.c
  src/testpilot.c
  src/cheat.c
  src/ui.c
  src/input.c
  src/btpad.c
  src/config.c
  src/sfx.c
  src/music.c
  src/net.c)
# each system's backend (A4C_PLAT_SOURCES), also used by the platform tests
if(WIN32)
  set(A4C_PLAT_SOURCES src/platform/plat_win32.c src/platform/plat_win.c src/platform/plat_keys.c src/platform/gx_d3d12.c)
  set(A4C_PLAT_LIBS ws2_32 bcrypt ole32)
elseif(APPLE)
  set(A4C_PLAT_SOURCES src/platform/plat_posix.c src/platform/plat_mac.m src/platform/gx_metal.m)
  set(A4C_PLAT_LIBS "-framework Foundation" "-framework AppKit" "-framework QuartzCore" "-framework Metal"
    "-framework CoreAudio" "-framework AudioToolbox" "-framework GameController" objc)
else()
  set(A4C_PLAT_SOURCES src/platform/plat_posix.c src/platform/plat_linux.c src/platform/plat_wayland.c
    src/platform/plat_x11.c src/platform/plat_keys.c src/platform/gx_vulkan.c)
  set(A4C_PLAT_LIBS ${CMAKE_DL_LIBS} pthread)
endif()
target_sources(tailonlock PRIVATE ${A4C_PLAT_SOURCES})
# Windows: UTF-8 as the program's code page (the manifest), for paths outside the system's code page
if(MINGW)
  set(A4C_WIN_MANIFEST src/platform/win_utf8.rc)
elseif(MSVC)
  set(A4C_WIN_MANIFEST src/platform/win_utf8.manifest)
endif()
if(WIN32)
  target_sources(tailonlock PRIVATE ${A4C_WIN_MANIFEST})
endif()
target_link_libraries(tailonlock PRIVATE ${A4C_PLAT_LIBS})
if(WIN32)
  set_target_properties(tailonlock PROPERTIES WIN32_EXECUTABLE ON OUTPUT_NAME TailonLock)   # TailonLock.exe
elseif(APPLE)
  # TailonLock.app: a double-clickable bundle, the program in Contents/MacOS, assets/ in Contents/Resources
  set_target_properties(tailonlock PROPERTIES MACOSX_BUNDLE ON OUTPUT_NAME TailonLock
    MACOSX_BUNDLE_INFO_PLIST ${CMAKE_SOURCE_DIR}/cmake/Info.plist.in)
endif()
target_link_libraries(tailonlock PRIVATE a4c_core)
target_compile_definitions(tailonlock PRIVATE A4C_VERSION="${PROJECT_VERSION}"
  A4C_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
# The game looks for assets/ next to the program, so a build folder (or a zip
# of it) runs on its own.
# The copy runs on every build (not only when the game relinks), so a changed
# asset never goes stale in a build folder.
if(APPLE)
  set(A4C_ASSET_DIR $<TARGET_BUNDLE_CONTENT_DIR:tailonlock>/Resources/assets)
else()
  set(A4C_ASSET_DIR $<TARGET_FILE_DIR:tailonlock>/assets)
endif()
add_custom_target(game_assets ALL
  COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/assets ${A4C_ASSET_DIR}
  VERBATIM)
add_dependencies(game_assets tailonlock)

# ---- relay server ---------------------------------------------------------
if(A4C_BUILD_RELAY AND NOT WIN32)
  add_executable(a4c_relay relay/relay.c)
  target_link_libraries(a4c_relay PRIVATE a4c_flags)
endif()

# ---- tests ----------------------------------------------------------------
if(A4C_BUILD_TESTS)
  enable_testing()
  if(A4C_BUILD_RELAY AND NOT WIN32)
    # the relay's limits on guessing game codes, on its own functions
    add_executable(test_relay_limits tests/test_relay_limits.c)
    target_link_libraries(test_relay_limits PRIVATE a4c_flags)
    add_test(NAME relay_limits COMMAND test_relay_limits)
    # the real relay, started on a free port, against a sweep for live codes from one address
    add_executable(test_relay_sweep tests/test_relay_sweep.c)
    target_link_libraries(test_relay_sweep PRIVATE a4c_flags)
    add_test(NAME relay_sweep COMMAND test_relay_sweep $<TARGET_FILE:a4c_relay>)
  endif()
  # what net.c takes from the other computer, on its own functions
  add_executable(test_net tests/test_net.c src/assets.c src/platform/plat_common.c)
  target_link_libraries(test_net PRIVATE a4c_core $<$<BOOL:${WIN32}>:ws2_32>)
  target_include_directories(test_net PRIVATE src)
  add_test(NAME net COMMAND test_net)
  # the sound mix asked for more frames at once than its buffer holds
  add_executable(test_sfx tests/test_sfx.c)
  target_link_libraries(test_sfx PRIVATE a4c_core)
  target_include_directories(test_sfx PRIVATE src)
  add_test(NAME sfx COMMAND test_sfx)
  # settings.ini read as if hostile: every value held to its range
  add_executable(test_config tests/test_config.c src/platform/plat_common.c)
  target_link_libraries(test_config PRIVATE a4c_core)
  target_include_directories(test_config PRIVATE src)
  add_test(NAME config COMMAND test_config)
  # assets.c's readers on short or odd data
  add_executable(test_assets tests/test_assets.c src/platform/plat_common.c)
  target_link_libraries(test_assets PRIVATE a4c_core)
  target_include_directories(test_assets PRIVATE src)
  add_test(NAME assets COMMAND test_assets)
  if(WIN32) # the UTF-8 manifest works: the code page, and a non-ASCII file name through fopen
    add_executable(test_win_utf8 tests/test_win_utf8.c ${A4C_WIN_MANIFEST})
    target_link_libraries(test_win_utf8 PRIVATE a4c_flags)
    add_test(NAME win_utf8 COMMAND test_win_utf8)
  endif()
  # the song editor's drag and double click, on its own functions
  add_executable(test_editor tests/test_editor.c src/synth.c src/compose.c src/raster.c src/platform/plat_common.c)
  target_link_libraries(test_editor PRIVATE a4c_core)
  target_include_directories(test_editor PRIVATE src)
  add_test(NAME editor COMMAND test_editor)
  # the floppy's start program (docs/FLOPPY.md): built here so that the checkers see it; tools/ff/mkdisk tests the disk
  add_executable(floppy_loader tools/ff/loader.c src/ff/tlpk.c)
  target_include_directories(floppy_loader PRIVATE src)
  if(WIN32)
    set_target_properties(floppy_loader PROPERTIES WIN32_EXECUTABLE ON) # no console window beside the game
  endif()
  # the carrier commander's contact pick
  add_executable(test_command tests/test_command.c src/raster.c src/platform/plat_common.c)
  target_link_libraries(test_command PRIVATE a4c_core)
  target_include_directories(test_command PRIVATE src)
  add_test(NAME command COMMAND test_command)
  # the rasteriser's circles and rings with a broken radius
  add_executable(test_raster tests/test_raster.c src/raster.c)
  target_link_libraries(test_raster PRIVATE a4c_core)
  target_include_directories(test_raster PRIVATE src)
  add_test(NAME raster COMMAND test_raster)
  set_tests_properties(raster PROPERTIES TIMEOUT 20)
  # the menus' keys
  add_executable(test_ui tests/test_ui.c src/raster.c src/platform/plat_common.c)
  target_link_libraries(test_ui PRIVATE a4c_core)
  target_include_directories(test_ui PRIVATE src)
  add_test(NAME ui COMMAND test_ui)
  # the CPU hands that run inside sim_update use fmath, not libm (the same bits on every system)
  add_test(NAME sim_libm COMMAND ${CMAKE_COMMAND} -DSRC=${CMAKE_SOURCE_DIR} -P ${CMAKE_SOURCE_DIR}/tests/sim_libm.cmake)
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
  # our cryptography: the RFC vectors, edge cases and a million round trips (about 3 s; --quick under ASan)
  add_executable(test_crypto tests/test_crypto.c)
  target_link_libraries(test_crypto PRIVATE a4c_core)
  if(A4C_SANITIZE)
    add_test(NAME crypto COMMAND test_crypto --quick)
  else()
    add_test(NAME crypto COMMAND test_crypto)
  endif()
  # our cryptography on request, for the cross-checks (tools/crypto_crosscheck.py)
  add_executable(crypto_tool tests/crypto_tool.c)
  target_link_libraries(crypto_tool PRIVATE a4c_core)
  # the checker's mesh round trip (tools/check, stage 3)
  add_executable(glb_dump tests/glb_dump.c)
  target_link_libraries(glb_dump PRIVATE a4c_core)
  # the checker's golden-data dumper (tools/check, stage 3)
  add_executable(json_dump tests/json_dump.c)
  target_link_libraries(json_dump PRIVATE a4c_core)
  # the figure engine against Leonardo's canon, measured on the mesh (and its triangles, for the parity check)
  add_executable(test_figure tests/test_figure.c)
  target_link_libraries(test_figure PRIVATE a4c_core)
  add_test(NAME figure COMMAND test_figure)
  # the vector painter on its own: a square fills, a path of NaN fills nothing
  add_executable(test_paint tests/test_paint.c src/paint.c)
  target_link_libraries(test_paint PRIVATE a4c_flags $<$<NOT:$<BOOL:${MSVC}>>:m>)
  target_include_directories(test_paint PRIVATE src)
  add_test(NAME paint COMMAND test_paint)
  # the brick 9 audit of plat.h: the callback loop, and several fingers of touch with their ids
  add_executable(test_plat tests/test_plat.c src/platform/plat_core.c src/platform/plat_common.c ${A4C_PLAT_SOURCES})
  target_link_libraries(test_plat PRIVATE a4c_core ${A4C_PLAT_LIBS})
  target_include_directories(test_plat PRIVATE src)
  if(WIN32)
    set_target_properties(test_plat PROPERTIES WIN32_EXECUTABLE ON)
  endif()
  add_test(NAME plat COMMAND test_plat)
  set_tests_properties(plat PROPERTIES ENVIRONMENT "A4C_VIDEO=offscreen;A4C_AUDIO=dummy" TIMEOUT 30)
  # plat_wait_until from three threads at once, on the real backend: every wait on time, none hangs
  add_executable(test_plat_wait tests/test_plat_wait.c src/platform/plat_core.c src/platform/plat_common.c ${A4C_PLAT_SOURCES})
  target_link_libraries(test_plat_wait PRIVATE a4c_core ${A4C_PLAT_LIBS})
  target_include_directories(test_plat_wait PRIVATE src)
  # its threads come from the platform's own libraries (A4C_PLAT_LIBS: pthread on Linux, the system on macOS and
  # Windows), as the game's do: nothing found or fetched (tools/check stage 4)
  add_test(NAME plat_wait COMMAND test_plat_wait 10)
  set_tests_properties(plat_wait PROPERTIES ENVIRONMENT "A4C_VIDEO=offscreen;A4C_AUDIO=dummy" TIMEOUT 40)
  # platform brick P1: the shared core and the key tables on their own (a stand-in backend), with fuzzing
  # (with the system's file calls, plat_posix.c or plat_win32.c, for the save's sync and replace)
  if(WIN32)
    set(A4C_PLAT_SYS src/platform/plat_win32.c)
    set(A4C_PLAT_SYS_LIBS ws2_32 bcrypt)
  else()
    set(A4C_PLAT_SYS src/platform/plat_posix.c)
    set(A4C_PLAT_SYS_LIBS $<$<BOOL:${APPLE}>:objc>)
  endif()
  add_executable(test_plat_core tests/test_plat_core.c src/platform/plat_core.c src/platform/plat_common.c src/platform/plat_keys.c
    ${A4C_PLAT_SYS})
  target_link_libraries(test_plat_core PRIVATE a4c_flags ${A4C_PLAT_SYS_LIBS} $<$<NOT:$<BOOL:${WIN32}>>:m>)
  target_include_directories(test_plat_core PRIVATE src)
  add_test(NAME plat_core COMMAND test_plat_core 5)
  # platform brick P2: the real backend started and stopped again and again, headless (for LeakSanitizer)
  add_executable(test_plat_cycles tests/test_plat_cycles.c src/platform/plat_core.c src/platform/plat_common.c ${A4C_PLAT_SOURCES})
  target_link_libraries(test_plat_cycles PRIVATE a4c_flags ${A4C_PLAT_LIBS} $<$<NOT:$<BOOL:${WIN32}>>:m>)
  target_include_directories(test_plat_cycles PRIVATE src)
  if(WIN32)
    set_target_properties(test_plat_cycles PROPERTIES WIN32_EXECUTABLE ON)
  endif()
  add_test(NAME plat_cycles COMMAND test_plat_cycles 100 10)
  set_tests_properties(plat_cycles PROPERTIES ENVIRONMENT
    "A4C_VIDEO=offscreen;A4C_AUDIO=dummy;LSAN_OPTIONS=suppressions=${CMAKE_SOURCE_DIR}/tests/golden/lsan-system.supp" TIMEOUT 120)
  # platform brick P5 on Linux: pads made with uinput, plugged in and out (tools/certify plat runs it)
  if(UNIX AND NOT APPLE)
    add_executable(test_plat_pads tests/test_plat_pads.c src/platform/plat_core.c src/platform/plat_common.c ${A4C_PLAT_SOURCES})
    target_link_libraries(test_plat_pads PRIVATE a4c_flags ${A4C_PLAT_LIBS} m)
    target_include_directories(test_plat_pads PRIVATE src)
    # brick P3's hands on X11 (XTEST), a test tool
    add_executable(x11_poke tests/x11_poke.c)
    target_link_libraries(x11_poke PRIVATE a4c_flags ${CMAKE_DL_LIBS})
    add_executable(wl_poke tests/wl_poke.c)
    target_link_libraries(wl_poke PRIVATE a4c_flags ${CMAKE_DL_LIBS})
  endif()
  # the checker's picture and sound comparisons (tools/check, stages 7-8)
  add_executable(check_cmp tests/check_cmp.c)
  target_link_libraries(check_cmp PRIVATE a4c_flags $<$<NOT:$<BOOL:${WIN32}>>:m>)
  target_include_directories(check_cmp PRIVATE src)
  # the ear check: a hearing model that scans the songs for what could hurt or tire the ears
  add_executable(ear_check tests/ear_check.c)
  target_link_libraries(ear_check PRIVATE a4c_flags $<$<NOT:$<BOOL:${WIN32}>>:m>)
  target_include_directories(ear_check PRIVATE src)
  add_test(NAME ear COMMAND ear_check --selftest)
  # the live test player (tools/livepilot): sees the game's pictures and plays with keys and clicks (POSIX)
  if(NOT WIN32)
    add_executable(livepilot tools/livepilot/livepilot.c tools/livepilot/link.c tools/livepilot/see.c src/raster.c)
    target_include_directories(livepilot PRIVATE src tools/livepilot)
    target_link_libraries(livepilot PRIVATE a4c_flags m)
  endif()

  # libFuzzer targets (docs/TEST_GUIDE.md): each one's code built in again with the fuzzer's coverage, ASan and
  # UBSan, and A4C_FUZZ for the entry points net.c and assets.c keep for them; `cmake --build . --target libfuzzers`
  if(A4C_LIBFUZZER)
    if(NOT CMAKE_C_COMPILER_ID MATCHES "Clang")
      message(FATAL_ERROR "A4C_LIBFUZZER needs clang (one with libFuzzer: -fsanitize=fuzzer)")
    endif()
    set(A4C_LF_FLAGS -fsanitize=fuzzer,address,undefined -fno-sanitize-recover=undefined -fno-omit-frame-pointer -g)
    # the core once for every target; the cryptography with the sanitizers but without the fuzzer's coverage, at
    # -O2: traced, the handshake's curve sums took ten times as long, and their branches say nothing about the
    # packets (tests/test_crypto checks them)
    set(A4C_LF_CORE ${A4C_CORE_SOURCES})
    list(REMOVE_ITEM A4C_LF_CORE src/core/crypto.c)
    add_library(a4c_lf_core OBJECT ${A4C_LF_CORE})
    add_library(a4c_lf_crypto OBJECT src/core/crypto.c)
    target_compile_options(a4c_lf_core PRIVATE ${A4C_LF_FLAGS} -O1)
    target_compile_options(a4c_lf_crypto PRIVATE -fsanitize=address,undefined -fno-sanitize-recover=undefined -g -O2)
    foreach(t a4c_lf_core a4c_lf_crypto)
      target_include_directories(${t} PRIVATE src)
      target_link_libraries(${t} PRIVATE a4c_flags)
      target_compile_definitions(${t} PRIVATE A4C_FUZZ)
    endforeach()
    function(a4c_libfuzzer name)
      add_executable(lf_${name} tests/lf_${name}.c ${ARGN} $<TARGET_OBJECTS:a4c_lf_core> $<TARGET_OBJECTS:a4c_lf_crypto>)
      target_include_directories(lf_${name} PRIVATE src tests)
      target_link_libraries(lf_${name} PRIVATE a4c_flags m)
      if(APPLE)
        target_link_libraries(lf_${name} PRIVATE objc) # plat_posix.c asks the Objective-C runtime for the bundle
      endif()
      target_compile_definitions(lf_${name} PRIVATE A4C_FUZZ)
      target_compile_options(lf_${name} PRIVATE ${A4C_LF_FLAGS} -O1)
      target_link_options(lf_${name} PRIVATE -fsanitize=fuzzer,address,undefined)
    endfunction()
    set(A4C_LF_PLAT src/platform/plat_common.c src/platform/plat_posix.c)
    a4c_libfuzzer(net src/net.c src/assets.c src/platform/plat_common.c)
    a4c_libfuzzer(config src/config.c src/assets.c src/music.c src/synth.c ${A4C_LF_PLAT})
    a4c_libfuzzer(song src/synth.c src/assets.c ${A4C_LF_PLAT})
    a4c_libfuzzer(map src/assets.c ${A4C_LF_PLAT})
    a4c_libfuzzer(ff) # the floppy's unpackers, src/ff (in a4c_lf_core)
    a4c_libfuzzer(json)
    a4c_libfuzzer(glb)
    a4c_libfuzzer(wav src/platform/plat_core.c src/platform/plat_keys.c ${A4C_LF_PLAT})
    set(A4C_LF_ALL lf_net lf_config lf_song lf_map lf_json lf_glb lf_wav lf_ff)
    add_custom_target(libfuzzers DEPENDS ${A4C_LF_ALL})
  endif()

  # Opens the game without a screen or a sound card and runs the menu demo, then a
  # co-op match with two seats and two CPU jets. This is the
  # headless smoke test CI uses; it needs no display or sound card.
  if(NOT CMAKE_CROSSCOMPILING)
    add_test(NAME smoke COMMAND tailonlock --frames 120)
    add_test(NAME smoke_play COMMAND tailonlock --frames 300 --play coop --seed 1)
    set_tests_properties(smoke smoke_play PROPERTIES
      ENVIRONMENT "A4C_VIDEO=offscreen;A4C_AUDIO=dummy;LSAN_OPTIONS=suppressions=${CMAKE_SOURCE_DIR}/tests/golden/lsan-system.supp"
      TIMEOUT 90)
    # with nothing showing the frames (the network tests' dummy video) the real-time loop waits for each tick:
    # about 60 frames a second, not thousands (a game copy used to take a whole core)
    add_test(NAME paced COMMAND tailonlock --seconds 2 --set intro=0)
    set_tests_properties(paced PROPERTIES ENVIRONMENT "A4C_VIDEO=dummy;A4C_AUDIO=dummy"
      PASS_REGULAR_EXPRESSION "ran [0-9]+ frames in [0-9.]+ s: [0-9]?[0-9] fps" TIMEOUT 30)
    # the audit's stills with a --shots folder whose name is too long for a still's path: none is saved, and
    # nothing is written past the path (the old code did)
    set(A4C_LONG_DIR ${CMAKE_BINARY_DIR}/long-shots/dddddddddddddddddddddddddddddddddddddddddddddddddddddddddddd/dddddddddddddddddddddddddddddddddddddddddddddddddddddddddddd/dddddddddddddddddddddddddddddddddddddddddddddddddddddddddddd/dddddddddddddddddddddddddddddddddddddddddddddddddddddddddddd/dddddddddddddddddddddddddddddddddddddddddddddddddddddddddddd/dddddddddddddddddddddddddddddddddddddddddddddddddddddddddddd/dddddddddddddddddddddddddddddddddddddddddddddddddddddddddddd/dddddddddddddddddddddddddddddddddddddddddddddddddddddddddddd/dddddddddddddddddddddddddddddddddddddddddddddddddddddddddddd/dddddddddddddddddddddddddddddddddddddddddddddddddddddddddddd)
    add_test(NAME audit_long_shots_dir COMMAND ${CMAKE_COMMAND} -E make_directory ${A4C_LONG_DIR})
    set_tests_properties(audit_long_shots_dir PROPERTIES FIXTURES_SETUP long_shots)
    add_test(NAME audit_long_shots COMMAND tailonlock --audit --part views --shots ${A4C_LONG_DIR})
    set_tests_properties(audit_long_shots PROPERTIES FIXTURES_REQUIRED long_shots PASS_REGULAR_EXPRESSION "no still: the folder"
      ENVIRONMENT "A4C_VIDEO=dummy;A4C_AUDIO=dummy;A4C_VIEWS_ALL=1" TIMEOUT 120)
  endif()
endif()
