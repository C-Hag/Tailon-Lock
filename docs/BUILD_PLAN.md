# Tailon Lock: the no-dependencies build plan

Goal: a game that is 100 % our own code. The only things it uses from outside
are the operating system's interfaces:

- **Linux:** libc and libm, libwayland-client (with hand-written xdg-shell glue),
  libX11 as a fallback, ALSA libasound, evdev input and the GPU driver.
- **Windows:** the system DLLs (Win32, WASAPI/waveOut, XInput, Raw Input/HID).
- **Later, mobile:** the platform APIs of Android and iOS.

**Clean-room rule.** We take inspiration only. Nobody copies or paraphrases code
from SDL, cJSON, Monocypher, cgltf or any other project, and nobody reads their
source. We work from public specs and OS documentation: the RFCs, the Khronos
glTF 2.0 spec, the Wayland and xdg-shell protocol specs, the kernel's evdev and
uinput docs, the ALSA docs, and Microsoft's, Apple's and Google's docs. We use
our own naming. Every new file starts with a header saying it is original code
written from the spec, and naming that spec.

## Status

A brick is done when its gate passed on its own commit (the checker report is
in `build/check/<date>-<commit>.txt`).

| Brick | What | State | Commit |
|---|---|---|---|
| 0 | `tools/check`, the local code checker (10 stages, about 75 s) | done | in the root commit |
| 1 | JSON tokenizer (RFC 8259, RFC 3629): 200 malformed inputs, 8 x 10 min of fuzzing | done | in the root commit |
| 2 | JSON tree in one arena, typed getters, stress and leak tests | done | in the root commit |
| 3 | our JSON reader and cJSON side by side on every asset: 43 of 43 identical | done | 5f67a14 |
| 4 | the game reads JSON with our reader, our JSON writer; cJSON deleted | done | 3ab1a5b |
| 5-8 | our own GLB loader (glTF 2.0); the game loads its meshes from .glb | done | 479daf4 ... 2f44c99 |
| 21 | our own crypto: X25519, ChaCha20-Poly1305, BLAKE2b, Elligator 2 | done | (this commit) |
| 22 | online play on our own crypto; Monocypher deleted | next | |
| 9 | the `plat.h` interface (touch, callback-driven loop) | planned | |
| 25-29 | touch controls, scaling, Android | planned | |
| 10-20 | our own desktop platform layer; SDL3 deleted | planned | |
| 23-24 | the last downloaded library gone; final audit | planned | |
| 30-32 | iOS | later | |
| W1-W2 | mobile mode in the browser game (web v28) | done in the web build; repo copy to refresh | |
| W3 | the browser game published as a web page | planned, after the push and Pages | |

Outside code still in the build: SDL3 (until brick 20) and Monocypher (until
brick 22), both downloaded at build time with checksums (docs/THIRD_PARTY.md).

## The process: brick by brick

Each brick builds on the last one. A brick is audited and confirmed before the
next one starts.

**One brick, one local commit.** Nothing is pushed.

**The gate for every brick:**

1. `tools/check` passes, and its report is saved to
   `build/check/<date>-<commit>.txt`.
2. The brick's own audit, listed below, passes.
3. A self-review of the diff finds a clean-room header on every new file,
   nothing copied, and no debug leftovers.

If a brick fails its gate twice, we revert to the last confirmed commit and
redesign the brick. We never stack a brick on top of a failing one. After
each brick, a short report goes to the coordinator: the brick number, the
commit, the checker result and notes.

## Brick 0: `tools/check`, the local code checker

Run it as `tools/check`, or as `tools/check --quick` for stages 1-4 only. It
prints PASS or FAIL for each stage, and exits non-zero if any stage fails.

1. **Build.** Release and ASan+UBSan builds, each with GCC and with Clang, all
   with `-Wall -Wextra -Werror` on our code.
2. **Unit tests.** Every test.
3. **Golden data.** Every asset JSON (and every `.glb`, once the game loads any)
   is dumped to a canonical text form and compared byte for byte with the
   reference dumps. The references were taken with the old libraries.
4. **Dependency guard.** `ldd` and `objdump` of the binaries, plus a scan of
   CMake and the `#include`s. Only the libraries listed under the goal are
   allowed. Every library not yet replaced has an allow-list entry, and the
   list shrinks as each brick replaces one.
5. **Clean-room guard.** `git grep` for third-party names, licence headers and
   personal or machine terms. The README homage line is the only permitted
   hit, apart from the names on the allow-list.
6. **Headless.** The 900-frame intro, plus 3,000 frames of each game mode with
   CPU pilots, under ASan. No crash and no leak.
7. **Picture.** Screenshots of the menu, the cockpit and the title card, each
   under 0.5 % pixel difference from its reference.
8. **Sound.** The music render matches the DMX10 master within 2 LSB, with 0
   new clicks.
9. **Performance.** Frame time in a fixed flight scene is no more than 5 %
   slower than the baseline.
10. **Windows.** A MinGW cross-build of `TailonLock.exe`, plus an import-table
    check.

Brick 0 is done when the checker passes on today's game, with the old
libraries still in place. That run is the baseline.

How the checker uses the machine:

- **CPU.** Every build uses all cores (`cmake --build -j <cores>`), and so
  does `ctest`. The four Linux builds run side by side after the first one,
  which fetches SDL3 once for all of them. Stages 2-8 and 10 then run side by
  side. Stage 9 runs alone at the end, so nothing else disturbs the timings.
- **GPU.** Stages 7 and 9 also run the game's own GPU renderer, headless,
  through the driver's offscreen context. The GPU and software screenshots are
  taken in one parallel batch, each compared with its own reference. Stage 9
  logs the GPU frame time and fps for every map at every time of day. Without
  a GPU, both stages fall back to the software renderer alone.

Notes from brick 0:

- The references live in `tests/golden/`: JSON dump hashes, the screenshots,
  the sound hash and the frame-time baselines. The full dumps and the
  200-second reference render are too large for git, so they are kept in
  `build/check/ref/`.
- Stage 6 suppresses one leak (`tests/golden/lsan-system.supp`): memory that
  the system EGL driver keeps after SDL's offscreen driver probes it. It goes
  away with SDL in brick 20.
- The DMX10 master matches the render within 2.28 LSB: the player's gain plus
  the int16 truncation of `--render-music`, with no clicks through the loop.
- GPU frame times count the whole frame ("ms per frame in all"); "ms per frame
  drawn" alone would only measure command recording on the CPU.
- Test runs with `--frames` no longer pause when the headless window loses
  focus.

## Bricks 1-4: our own JSON reader (replaces cJSON)

1. **Tokenizer.** Tested against the RFC 8259 cases, 200 malformed inputs and
   10 minutes of ASan fuzzing.
2. **Arena tree with typed getters.** Plus stress and leak tests.
3. **Both readers side by side** in debug builds. Every asset must give
   identical results.
4. **Switch over** and delete cJSON. The writer used by the music editor's
   score export moves to our own module too.

## Bricks 5-8: our own GLB loader

**Finding.** Today the game loads no `.glb` at all. cgltf is not compiled in:
it is only named in a README milestone and in a comment in `mesh.c`. Every mesh
is built in code or read from JSON (`assets/jets`). So no outside code is
involved, and there is nothing to replace. These bricks add our own loader,
written from the Khronos glTF 2.0 spec, for the Blender pipeline in milestone 6.

5. **GLB container**, with bounds checks and fuzzing.
6. **Buffer views and accessors.**
7. **Meshes, materials and nodes** into our mesh format, byte-identical to
   golden dumps. The goldens are made from a test model exported from today's
   JSON meshes, so the round trip is exact.
8. **Switch over.** Hangar and cockpit screenshots must match.

## Bricks 21-22: our own crypto (replaces Monocypher)

These come before the platform layer.

21. **The primitives, written from the RFCs:** X25519 (RFC 7748),
    ChaCha20-Poly1305 (RFC 8439) and BLAKE2b (RFC 7693), plus anything else
    `net.c` uses from Monocypher. Gate: every official RFC test vector, 1
    million random round trips against fixed outputs, and a constant-time
    review of all secret-dependent code.
    **Brick 21 audit (done):**
    - Every official vector passes: RFC 7748 (both scalar vectors, 1 and
      1,000 iterations, Diffie-Hellman), RFC 7693 (BLAKE2b-512 of "abc" and
      the appendix E self-test digest), and RFC 8439 (the block function,
      encryption, Poly1305 and the AEAD).
    - 140,000 random cases agree with independent implementations: Python's
      hashlib and the `cryptography` package as test oracles, plus a
      big-integer model of RFC 9380 for the Elligator map.
    - A million AEAD round trips (plus 1,000 X25519, 100,000 Elligator and
      100,000 BLAKE2b) hash to the digest that the independent
      implementations give for the same inputs.
    - The constant-time review covers every secret-dependent path:
      - the field arithmetic has no data-dependent branch or index;
        division only by the constant 65536;
      - the ladder swaps with masks;
      - exponentiation branches only on public exponents (p-2, (p-1)/2);
      - Elligator picks its root with a masked swap;
      - Poly1305's final reduction is masked;
      - tag and point comparisons OR all bytes together before a single
        test;
      - BLAKE2b and ChaCha20 branch only on lengths.
      X25519 takes the same time for one-bit, all-bits and random scalars,
      within 1 %.
22. **Switch online play over.** Two instances on one machine connect with a
    6-digit code and play, and a wrong code is rejected. Then delete
    Monocypher.

## Bricks 9-20: our own platform layer (`src/platform/plat.h`, replaces SDL3)

SDL3 stays as a selectable backend (`--platform=sdl`) until brick 20.

9. **The interface**, with SDL wrapped behind it. Checker results are
   identical.

   The interface must already allow two things, so the mobile phase does not
   force a redesign later:
   - touch input: several pointers, each with an id, down, move and up;
   - a main loop owned by the platform: iOS and Android drive the game through
     callbacks (init, frame, event, pause, resume, quit) instead of the game
     owning `while (running)`.

   The brick 9 audit checks both.

10. **Linux timer and loop.** Jitter under 0.5 ms over 10 minutes.
11. **Wayland window.** Hand-written protocol glue: registry, xdg-shell,
    resize and close. Decorations come from xdg-decoration or our own title
    bar. The `WAYLAND_DEBUG` log must be clean.
12. **Present and fullscreen.** A `wl_shm` double buffer and fullscreen.
    Screenshots match the SDL backend.
13. **Keyboard and mouse.** A scripted key test passes, and every menu can be
    clicked.
14. **X11 fallback** through libX11. The same tests pass under Xwayland.
15. **ALSA output at 48 kHz.** A capture matches the music render, with no
    underruns in 30 minutes.
16. **Gamepads and HOTAS** through evdev, with hotplug polling, profiles and
    the bind screen. A virtual uinput device test passes, then the owner
    tests his real HOTAS.
17. **Win32 window, present and input.** The window, `StretchDIBits` present,
    keyboard and mouse. Tested in the VM when it is free.
18. **WASAPI sound.** 10 minutes in the VM without crackle.
19. **Gamepads on Windows.** XInput plus Raw Input HID: a gamepad in the VM,
    then the owner tests the HOTAS on Windows.
20. **Delete the SDL backend.** If the GPU renderer uses the SDL GPU API, we
    either port it to our own Vulkan loader (system driver only), or keep it
    off and make the software renderer the default. The decision is written
    down in that brick.

## Bricks 23-24: finish

23. **Drop the last downloaded libraries.** The credits and the README say the game is
    100 % our own code, plus the one homage line.
24. **Final run.** The full checker on both platforms, a 30-minute play session
    and a review of the whole diff. The owner gives the final OK.

**Order (Android first, from the owner):**

1. Brick 0.
2. Bricks 1-4: JSON.
3. Bricks 5-8: GLB.
4. Bricks 21-22: crypto.
5. Brick 9: the `plat.h` interface, already supporting touch and a
   callback-driven main loop.
6. Bricks 25-26: touch controls and scaling, developed on Linux with simulated
   touch.
7. Bricks 27-29: Android (our own NativeActivity entry, AAudio, input, and an
   APK built on Linux).
8. Bricks 10-20: our own desktop platform layer.
9. Bricks 23-24: the final desktop audit.
10. Bricks 30-32: iOS, later.

The desktop keeps its SDL backend until brick 20, so nothing breaks while
Android goes first.

**Android without Google's tools (the owner's decision).** We use no SDK, no
NDK, no emulator, no adb and no Java, and download nothing from Google.
Instead:

- **Compiler:** the system Clang cross-compiles to `aarch64-linux-android`.
- **Headers and libraries:** we write our own minimal headers and stub `.so`
  files with the right sonames (`libc`, `libm`, `libdl`, `liblog`,
  `libandroid`, `libEGL`/`libvulkan`, `libaaudio`), from the public API
  documentation. They exist only so the linker records the dependencies; the
  device supplies the real libraries.
- **Packaging:** our own APK writer covers the zip container, the binary
  manifest (a `NativeActivity` with `hasCode=false`), and APK Signature
  Scheme v2 signing with our own crypto.
- **Testing:** by sideloading the APK onto the owner's tablet.

## Web version: mobile mode in the browser game (bricks W1-W3)

The browser version of the game (`reference/ace4combat.html`), published as a
web page from the repository, so anyone can open the link on a phone or
tablet and play. It is a separate track from the C bricks and can run beside
them.

- **W1-W2: done in the web version.** The browser build (Tailon Lock web
  v28) has a "Play on: Computer / Mobile" choice on the home menu, picked by
  the pointer type and remembered, plus:
  - a floating stick, a throttle slider, and GUN, MSL, FLR, VIEW and pause
    buttons;
  - a slim see-through panel at the lower left;
  - the recorded sound and DMX10 score, the version 7 intro, and the CIC
    control-room scene.
  It is built by re-appliable patch scripts on top of the browser source.
  `reference/ace4combat.html` in the repository is the older copy from
  before the mobile mode. It is to be refreshed with the built
  `tailonlock.html` and its `sfx/` and `music/` folders. There must be no
  second touch implementation in the old copy.
- **W3. The published page:** the page served from the repository as a site,
  with a short link in the README. Gate: it opens and plays on the owner's
  tablet and phone from the link.

The C game's touch controls (brick 25) follow the same design, so both
versions feel the same.

## Mobile phase (bricks 25-32)

Android comes after brick 9, iOS comes last (see the order above). It uses the same core and the same `plat.h` interface, with
clean-room platform files written from Apple's and Google's public docs.

25. **Touch controls.** Developed on Linux, with the mouse simulating touch.
    The design is simple and intuitive:
    - **Left half of the screen:** a floating stick appears wherever the thumb
      touches. Drag to pitch and roll; release and the jet levels itself.
    - **Right half:** hold anywhere for guns. One big missile button, which
      locks on by itself when a bandit is ahead.
    - **Right edge:** swipe up for afterburner, swipe down for airbrake.
      Otherwise the throttle is automatic.
    - **Two-finger tap:** flares.
    - **Pause:** a button in the top corner.
    - **Defaults:** arcade aim assist and auto-throttle are on. Tilt steering
      with the gyro is optional.
    - **Haptics** on lock, on hits and on warnings.
    - **First flight:** a three-step tutorial (steer, shoot, missile), shown
      once.
    - **Layout:** a left-handed mirror mode, and layouts that adapt to phones
      and tablets. A connected gamepad hides the touch controls.

    - **Home screen:** a "Controls: Touch / Keyboard & mouse / Gamepad /
      HOTAS" choice on the main menu, also in the settings, so the touch mode
      can be picked on touchscreen PCs too. It is picked automatically on
      phones and tablets, and the home screen offers the mobile game mode.

    Gate: a new player can win a dogfight against an easy CPU using touch
    alone, and every button is at least 9 mm across on a phone.
26. **Scaling and power.** High-DPI UI scaling, a low-power quality tier and a
    30 or 60 fps cap. Gate: 60 fps in the fixed scene on the lowest tier.
27. **Android entry.** Our own NativeActivity entry point, with no
    android_native_app_glue or any other Google helper code. It covers
    surface present, multi-touch and the back button.
    - The toolchain: Clang targeting `aarch64-linux-android`, our own
      headers, and stub `.so` files with the right sonames.
    - A checker stage verifies that the `.so` needs only those sonames
      (readelf) and exports `ANativeActivity_onCreate`.
    - Gate: runs on the owner's tablet, using the APK from brick 29. Until
      then, the native-library checks above must pass.
28. **Android sound and gamepads.** AAudio output, and Bluetooth gamepads
    through Android input events.
29. **Android package: our own APK writer.**
    - **The zip container** (from the PKWARE APPNOTE), with the native
      library stored uncompressed and page-aligned.
    - **The binary manifest** (from the public format description), with
      `hasCode=false` and NativeActivity naming our library.
    - **APK Signature Scheme v2**, which needs crypto beyond brick 21, all
      written from the standards: SHA-256 (FIPS 180-4), ECDSA P-256
      (FIPS 186-5) or RSA PKCS#1 v1.5 (RFC 8017), and a self-signed X.509
      certificate in DER (RFC 5280). These come with their official test
      vectors and the same constant-time review as brick 21.
    - **Gate:** the owner sideloads the APK, and it installs and runs on the
      tablet. Android's own installer is the verifier. Our own checks of the
      zip layout and signature block run in tools/check first.
30. **iOS view.** A UIKit view, `CAMetalLayer` present and multi-touch. Gate:
    runs in the simulator.
31. **iOS sound and gamepads.** Core Audio and the GameController framework.
32. **iOS build.** Xcode build and signing. Gate: runs on the owner's iPad
    and iPhone. This needs a Mac with Xcode.
