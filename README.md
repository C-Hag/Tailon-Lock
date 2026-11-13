# Tailon Lock: The Great Nose2Nose Engagement

A simple dogfight arcade game, made to see what Claude can build and come up
with: two jets, a carrier at sea and a sky to fight in, written from scratch
by Claude in C11, on its own platform layer: it links nothing but the operating system's
own libraries.

**To install and play it, follow [docs/INSTALL.md](docs/INSTALL.md):** step by
step for macOS, Windows and Linux. The keys are in [docs/PLAYING.md](docs/PLAYING.md).

One code base builds a Linux binary, a Windows
.exe and a macOS app, with two renderers:

- **Modern** (default): our own GPU layer on Direct3D 12 (Windows), Metal (macOS) or
  Vulkan (Linux). HDR with ACES tonemapping, MSAA, sun
  shadow maps, bloom, a procedural sea with Fresnel reflection and sun
  glitter, height fog, lit cloud puffs and a star sky. Graphics LOW, MEDIUM,
  HIGH or ULTRA in the menu set resolution scale, MSAA, shadow size and bloom.
  The HUD and menus are still drawn by the software rasterizer and laid on top.
- **Retro**: the software rasterizer into a low-res framebuffer,
  scaled up nearest-neighbour, for the chunky look and old PCs. It is also
  the fallback when no GPU device can be made.

The renderer menu row shows which backend is running; a change takes effect
after a restart.

**Status: playable.** The opening intro film, eight
selectable jets, four maps plus the classic arena with time of day and
weather, 1 player against the CPU (easy, normal, ace), 2 player split screen
versus or co-op, guns and missiles with lock-on, the HUD, cockpit
panel and radar, sound effects, the three themes of the soundtrack
played live by the game's own synthesizer, and network play (on the LAN, by
internet address, or through a relay with a 6-digit game code).
HOTAS, rebindable keys, overcast cloud sheets and scenery props are still to
come.

The game loads its data from `assets/` next to the program (the build copies
it there), so a build folder runs on its own. The whole game is about 60 MB,
most of it the recorded music and the detailed jet models.

## Controls

| | Player 1 | Player 2 (2P) | Gamepad |
|---|---|---|---|
| Nose down / up | W S | Up Down (or I K) | Left stick |
| Roll | A D | Left Right (or J L) | Left stick |
| Throttle | R F | P ; | Right / left trigger, right stick |
| Cannon | Space | Enter | A, right shoulder |
| Missile | Left Shift | Right Shift | B |
| Flares | Q | ' | X, left shoulder |

**Starts and matches**: MATCH SIZE plays 1 vs 1 or 2 vs 2 (each side gets a
CPU wingman). START set to CARRIER VS BASE puts player 1's side on its
carrier's catapults (on maps without one, its runway): go to full power
and the catapult fires. The other side starts at the end of its runway:
power up, and pull back above the rotation speed (about 160 kt). IN THE
AIR starts everyone airborne.

**Handling** (menu): ARCADE turns the easy arcade way. REALISTIC
limits every pull to the g the jet can take, so a hard break turns at about
25 to 30 degrees a second at 8 to 9 g, as real fighters do, and the G meter
shows the g actually pulled.

**Gear and flaps** (realistic handling only): G lowers and raises the
landing gear, V steps the flaps a notch at a time through UP, T/O and LDG
and back again (player 2: comma and period; gamepad: d-pad down and up).
The HUD shows both under the G meter. The gear takes five seconds to travel
and drags hard: leave it down and the jet is slow, and flying it much past
290 kt tears at it. The flaps lower the stall speed (the landing notch from
175 to about 155 kt) and lift the jet off the runway sooner, at the cost of
drag; past 250 kt (take-off notch) or 200 kt (landing notch) they blow back
on their own and come out again as the jet slows. Touch down with the gear
not locked down and it is a belly landing: a crash. The gear will not come
up while the jet sits on its wheels. Jets start on the runway or the
catapult with the gear down and take-off flaps, in the air clean. In
realistic handling the padlock look (eyes on the enemy, held) moves from G
to B. Arcade works all of this on its own.

**Control surfaces**: seen from outside, every jet's ailerons, elevators
(tailerons with a little roll in them), canards and rudders move with the
stick (a delta with no tail has elevons, pitch and roll together), and on
realistic handling the flaps go down with their notches (the ECF-90, a
delta with no flaps, droops its elevons instead).

**Autopilot**: T (player 1) or Y (player 2), or the right stick button,
holds the wings level, the altitude and the heading; moving the stick
takes the jet back.

**Hangar** (in the menu): the chosen jet turns on a dark backdrop with
its name, role, five bars (speed, turn, climb, range, firepower) and top
speed. Left and Right, or the arrows beside the name, change the jet; Enter
takes it and goes to START MISSION. MORE FLIGHT INFO opens the jet's whole
sheet: its story, all seven scores, performance, avionics and weapons. You
arm the jet the classic way: the cannon (3000 rounds) is fixed (the Tianlong, sy26, has
none: missiles only, as its sheet says); up to 8 heat-seekers,
fewer with other stores; up to 6 radar-guided missiles, but none with
air-to-ground; up to 2 air-to-ground missiles. Classic rules use the whole
load; modern rules use the heat-seekers. SETTINGS holds the graphics,
sound and other options.

**Mouse**: the menus, pause and game over also work with the mouse.
Pointing selects, a click presses a button or steps a value on, the right
button or the wheel down steps it back, and the arrows around a value step
it either way.

**Classic** (CONTROLS in the menu, or `--classic`): the classic keys
and rules.

| | Player 1 | Player 2 |
|---|---|---|
| Dive / climb | Q A | P L |
| Bank left / right | Z X | N M |
| Power up / down | D S | K J |
| Fire the selected weapon | F | H |
| Select weapon (C H R S) | W | O |
| Flares or chaff | R | U |
| Map view | C | B |

F1 pauses and F2 quits to the menu. The weapons cycle through C cannon
(under 1 mile; skipped in the Tianlong, which has none), H heat-seeker (under 8 miles), R radar-guided (under 25
miles, guided by your radar lock all the way, so keep the target in front)
and S air-to-ground (below 2000 ft and under 500 kt, on a locked ground
target or ship). Flares decoy heat-seekers and chaff decoys radar missiles;
the decoy key drops chaff when a radar missile is on you and flares
otherwise. Fly off your own end of the map below 1000 ft to land back on
the carrier, refuelled and rearmed. The stall speed is 140 kt and the
ceiling 60,000 ft. Both players get the classic cockpit panels. The classic-rules
options appear with it: SCENARIO (full-scale conflict, or a close range
dogfight with cannon and heat-seekers that starts in the air), PLANES EACH
(1-20), CRASH DETECTION, and MISSILE HITS TO KILL (1-3). A missile hit
leaves the controls dead for a few seconds, and the score counts the classic
way: 20 a cannon hit, 50 a missile hit, 200 a kill, 2000 a ground target.

**Beat the Drone!** (MODE in the menu): one jet against the DR-7 Vesper, an
unmanned combat drone with a vectoring nozzle and no pilot to black out (it
takes 14 g). THE DRONE sets who flies it: the CPU (DRONE AI sets how often
it uses its tricks) or player 2 on split screen. Over the network the host
picks MATCH: DRONE, with the guest or the host flying it.

The drone's nose and its flight path are separate. In normal flight it
flies like the jets (arcade or realistic). Hold the vector key (T or E for
player 1, Y for player 2, a stick click or Y on a pad) and the nozzle swings
the nose at the same rate at any speed, far past the stall:

- **Cobra**: hold vector and pull, let go of the stick with the nose up.
  The wing flown broadside brakes hard and a chasing fighter overshoots;
  let go of vector and the nose falls back onto the path.
- **Flip**: hold vector and keep pulling; the nose goes all the way round.
- **J-turn**: pull the nose over until it faces back down the path and hold
  it there; the speed goes and the thrust pulls the path round behind it.
- **Nose pointing**: with vector held the nose (and the gun) can point off
  the path at a target the path cannot reach yet.
- **Fast rolls**: the drone rolls half again as fast as a jet, more than
  twice as fast when vectoring.

The HUD shows VECTOR or POST-STALL and the angle of attack; every pilot
here is told when the drone pulls a Cobra, a flip or a J-turn. Post-stall
flight costs energy, and the drone has to get its speed back before the
wing works again. `--flight-test` measures the moves.

**Defend the carrier** (MODE in the menu): player 1 flies off the carrier's
catapult; player 2 runs the carrier from its control room, or the turrets
run on their own with CREW set to PILOT ONLY. Five waves of strike jets come
in from the north with anti-ship missiles. Player 2, on the arrow keys (or
the second pad). Player 2 is the captain, on foot in first person: he starts
on the bridge, walks to the tactical console or the air ops console, takes
the stairs down to the flight deck, runs out to any of the five turrets to
man it, or climbs into the jet on catapult 2 and launches. Everything is one
shared world, so the pilot sees the captain on deck and the turrets' hits.

| Key | Pad 2 | On foot | At the tactical console | In a turret |
|---|---|---|---|---|
| Up / Down | left stick | walk | pick a turret | aim up and down |
| Left / Right | left / right stick | turn | pick a contact | aim left and right |
| P / ; | right stick | look up and down | P launches a jet | |
| ' | right shoulder | run | turret on AUTO or HOLD | |
| Enter | A | use what is in front of you | order the turret onto the contact | fire |
| Right Shift | B | | step back | step back |

The three CIWS guns are the last line against missiles; the two SAM
launchers reach 7.5 km. Land back on the deck to climb out of the jet.

**Music editor** (a development tool, hidden from the menu: `--music-editor`): a piano
roll over the soundtrack, playing live on the SID synth. Pick an instrument
on the left (M mutes, S solos), double-click to add a note, drag to move it
(its end to lengthen it), drag on empty space to box-select, right-click to
delete; wheel scrolls (shift: time, ctrl: zoom), the ruler moves the
playhead, space plays. The instrument's SID patch sits at the bottom: click
or scroll a knob and hear it change. The rewrite tools:

- **Compose** (C): writes a new melody into the boxed range, from a model
  learned from all eight themes, kept in the detected key and leaning on the
  chord underneath on strong beats. Press again for another take.
- **Vary** (V), **Harmonize** (H), **Key -/+** (arrows), **Maj/Min** (M),
  **Humanize** (U), **Arpeggio** (R): on the selected notes.

Ctrl+Z undoes, Ctrl+S saves the theme to `assets/music` (in the build and,
for a development build, in the source tree), and the game plays the saved
version. [ and ] switch themes, G changes the grid, Esc goes back.

**Flight model** (from basic fighter manoeuvres and Boyd's
energy-maneuverability theory): the g a wing can pull grows with the square
of the speed up to the airframe limit, so each jet turns fastest at its
corner speed (about 350-380 kt) and slower either side; the HUD says
CORNER SPEED, SLOW or FAST in a hard turn. Energy counts: pulling g bleeds
speed, climbing trades speed for height and diving gives it back, so the
yo-yos, boom and zoom and an unloaded dive work as the books describe.
ARCADE turns twice as quickly as REALISTIC on the same curve and runs the
engine faster. CPU pilots fly the manoeuvres: pure, lead and lag pursuit,
high and low yo-yos, breaks into an attacker, guns jinks, extensions and
reversals; aces climb above you before they attack and, like Chuck Yeager,
hold their fire until close and shoot in short bursts.

**G effects** (menu): above about 7 G the view greys out and narrows, and
holding it blacks the pilot out for a few seconds; below about -2.5 G it
reds out. Set it to ARCADE to fly without them.

Tab steps through cockpit, chase and flyby view, Esc or Start pauses, M toggles sound,
Alt+Enter or F11 toggles fullscreen. In 1 player both key sets fly your jet.
Pads join seats in the order they were plugged in. Settings are saved in
`~/.config/tailonlock/settings.ini` (`%APPDATA%\TailonLock` on Windows,
`~/Library/Application Support/TailonLock` on macOS). The first time it
runs, the game copies the settings over from the old `tailonlock` (`Tailon Lock`) folder if there is one.

## Network play

NETWORK in the main menu plays a 2-player match between two computers, in
versus or co-op, 1 vs 1 or 2 vs 2 (the host's computer flies the CPU jets).
The host's main menu sets the map, time, weather and rules; each player's
jet and arming are player 1's in their own hangar. Each player flies with
the player 1 keys and the full screen.

The host's screen shows a 6-digit GAME CODE; the guest types it in every
way of joining:

- **LAN**: one picks HOST A GAME over LAN and presses HOST GAME; the other
  picks JOIN A GAME over LAN, chooses the game from the list, types the
  code and presses JOIN.
- **Internet address**: the host forwards UDP port 27960 on the router to
  their computer and hosts over INTERNET ADDRESS; the guest types the
  host's public address (`203.0.113.7`, or `host:port`) and the code.
- **Internet relay**: no port forwarding. Someone runs `tl_relay` (built
  with the game on Linux) on a server with UDP port 27970 open. Both type
  its address under RELAY SERVER, and the guest types the code.

**Security.** The code is a password the two computers turn into a shared
secret key with a password handshake (CPace on X25519), so a computer that
does not know the code never gets the key, and someone listening learns
nothing to test codes against later. Every match packet is then encrypted
and signed with ChaCha20-Poly1305 (our own, from RFC 8439: `src/core/crypto.c`)
and numbered, so it cannot be read, changed or replayed; packets that fail
are dropped and the match carries on. Each wrong code costs a full round
trip, and the host stops after 10 (host again for a new code). The relay
only forwards sealed packets and allows each address 10 wrong codes a
minute. The cost is 24 bytes and under a microsecond per packet. Two limits
remain: the relay sees the code, so a relay you do not trust could sit in
the middle (run your own), and each computer still reports its own jet, so
a modified copy of the game could cheat.

Each computer flies its own jet and sends its state
30 times a second; the other computer smooths its copy toward it. A hit
counts on the computer that owns the jet or target that was hit, and
missiles, flares and chaff travel as events, so a laggy link shows a jet a
little behind, never two different outcomes. Pause does not stop a network
match. If nothing arrives for 6 seconds the match ends with CONNECTION LOST.
On Windows, allow the game through the firewall the first time it hosts.

## System requirements

Network play sends 25-30 packets a second each way, at most 373 bytes each:
4-7 KB/s per direction in a 2 vs 2 match. The program is 3.4 MB on macOS (one
app for Intel and Apple silicon); with its assets (most of them the recorded
music and the detailed jet models) the game takes about 60 MB.

- **Minimum:** 64-bit Windows 10/11, Linux or macOS 11+ (Intel or Apple silicon), any dual-core CPU, 1 GB RAM,
  60 MB of disk. A GPU with Vulkan, Direct3D 12 or Metal for the MODERN renderer
  (LOW); without one, RETRO runs on the CPU (40 fps on four slow cores).
  For online play, 64 kbit/s each way.
- **Recommended:** a quad-core CPU, 4 GB RAM, and a Vulkan or Direct3D 12
  GPU with 2 GB of memory from about 2016 on (GTX 1050 or RX 560 class) for
  HIGH or ULTRA at 1080p and 60 fps.

## Building on Linux

Any current Linux distribution with GCC 14+ (or Clang) works. The game needs no
development libraries: it opens the system's own Wayland or X11, ALSA and Vulkan
libraries when it starts. The commands below are for dnf-based distributions;
on others install the same tools under their own names.

1. Build tools:

   ```sh
   sudo dnf install gcc clang cmake ninja-build git
   ```

2. Configure, build, test and run:

   ```sh
   cmake --preset linux-release
   cmake --build --preset linux-release
   ctest --preset linux-release
   ./build/linux-release/tailonlock
   ```

The full local check is `tools/check` (about 75 s with every core busy;
`tools/check --quick` runs only the build, test, golden-data and dependency
stages). It builds with GCC and Clang, with and without ASan/UBSan, runs every
test, compares the JSON assets, screenshots (GPU and software) and the music
with the references in `tests/golden/`, times a flight scene, and
cross-builds the Windows .exe. GCC's sanitizer
runtimes come from the `libasan` and `libubsan` packages.

The compiled shaders are checked in as `src/gpu_shaders.h`, so a build needs
no shader compiler. After editing `shaders/*.hlsl`, run
`tools/build_shaders.sh`; it downloads a pinned Microsoft dxc into
`build/_tools/` and regenerates the header for both Vulkan and Direct3D 12.
macOS uses `shaders/metal.msl`, the same shaders written in Metal's own
language: keep it in step with the HLSL and run `tools/embed_msl.py`, which
writes `src/gpu_msl.h` (Metal compiles it when the game starts).

### Presets

| Preset | What it builds |
|--------|----------------|
| `linux-debug` | GCC, debug |
| `linux-release` | GCC, optimised |
| `linux-clang` | Clang, debug |
| `linux-asan` | Clang with AddressSanitizer and UBSan |
| `windows-mingw` | Windows .exe cross-compiled from Linux |
| `windows-msvc` | Windows, MSVC (only on Windows, from a Developer Command Prompt) |
| `macos-release` | macOS app, one binary for Intel and Apple silicon (only on a Mac) |
| `macos-debug` | macOS app, debug, for the Mac it is built on |

Each preset builds into `build/<preset>/`.

### Windows .exe from Linux

```sh
sudo dnf install mingw64-gcc mingw64-winpthreads-static   # the MinGW-w64 cross compiler
cmake --preset windows-mingw
cmake --build --preset windows-mingw
wine build/windows-mingw/TailonLock.exe   # if wine is installed
```

The .exe is fully static (no DLLs to ship). If your
distribution has no MinGW-w64 packages, build inside a container that has them:

```sh
podman run --rm -v "$PWD":/src:Z -w /src fedora:42 sh -c \
  'dnf install -y cmake ninja-build mingw64-gcc mingw64-winpthreads-static &&
   cmake --preset windows-mingw && cmake --build --preset windows-mingw'
```

Check gamepads and fullscreen on a real Windows PC before a release.

## Building on macOS

macOS 11 or later, on Intel or Apple silicon. Apple's Command Line Tools give
the compiler (full Xcode is not needed); CMake comes from cmake.org or pip:

```sh
xcode-select --install             # once: clang, make, git
pip3 install --user cmake          # or the cmake.org installer
cmake --preset macos-release
cmake --build --preset macos-release -j
ctest --preset macos-release
open build/macos-release/TailonLock.app
```

The build makes `TailonLock.app` with the assets inside it
(`Contents/Resources/assets`), so the app can be copied anywhere. To see the
log, run `build/macos-release/TailonLock.app/Contents/MacOS/TailonLock` from
Terminal. A copy that was downloaded rather than built on the Mac is
unsigned: open it once with right-click > Open, or clear the quarantine flag
with `xattr -dr com.apple.quarantine TailonLock.app`.

### Command-line flags

- `--scale N`: internal resolution divisor, 1, 2 or 3 (default 1 with the modern renderer, 2 with retro).
- `--frames N`: quit after N frames, running exactly one 60 Hz tick per
  frame so runs repeat. With `TL_VIDEO=dummy` this is the headless
  smoke test that `ctest` and CI run.
- `--play 1p|2p|coop`: skip the menu.
- `--seed N`: fixed random seed.
- `--screenshot FILE.bmp`: save the last frame.
- `--test-pattern`: the milestone 1 test pattern.
- `--play intro`: the intro, whatever the setting.
- `--jet ID` (af40, lf27, ecf90, ja46, sy26, vk33, ks55, nf24), `--map ID`, `--time dawn|day|dusk|night`, `--weather clear|scattered|overcast|storm`: override the menu.
- `--chase`: start in chase view (for screenshots).
- `--play defend` or `defend1`: the carrier defence with or without a commander.
- `--play drone` or `drone2`: Beat the Drone!, the CPU or player 2 flying the drone. `--set net_drone=1|2` with `--host`: a network drone match (the guest or the host flies it).
- `--pull`, `--vector`, `--cmd ACTIONS`: test hooks (full back stick; the drone's vector key held; commander key presses).
- `--click F:X:Y` (or `R:F:X:Y` right button, `W:F:X:Y` wheel up): at frame F, point at window pixel (X, Y) and click; repeatable, for menu tests.
- `--render-music KEY FILE.wav`: render one pass of a theme (horizon, pop,
  h2h, anthem, strike, cine, chip, overtone, neon, midnight, rocknroll, ghost) from the synth, to compare with
  the mp3s in `music/tracks/` (ghost and horizon; the retired themes' mp3s are not kept).

With `--frames`, the log line at the end gives the average time spent
drawing a frame.
- `--renderer modern|retro`, `--gfx low|medium|high|ultra`: override the settings.
- `--classic`: classic keys and rules.
- `--flight-test`: fly every jet through the test manoeuvres (top speed,
  climb, ceiling, turn, roll, stall and recovery, endurance, take-off,
  catapult, landing) under each rule set and print what they measured.
- `--set key=value`: any settings.ini line. `--cmd b` lets CPU pilots fly the
  players' jets; with `TL_CHECK=1` a `--frames` run logs broken physics.
- `--host [RELAY]`: host a network match straight away (LAN and UDP 27960,
  or through the relay); `--join ADDR` joins by address and
  `--join RELAY/` through a relay, with `--code NNNNNN` (with `TL_PLANES=1` the
  host logs its code). `--seconds N` quits after N seconds;
  with `TL_PLANES=1` the log shows how each match ended on both computers.
- `--version`

## Layout

```
src/
  main.c            the game's side of the loop: fixed 60 Hz update, render, flow
  raster.c          software framebuffer, fills (later polygons, depth buffer)
  render3d.c        camera, near clip, sky/ground, per-view draw
  gpu.c             modern renderer: shadows, sea, fog, bloom, on the platform's GPU layer
  gpu_shaders.h     compiled shaders (SPIR-V + DXIL), made by tools/build_shaders.sh
  hud.c  ui.c       cockpit, radar, bitmap text; in-game menus
  input.c           keyboard seats and gamepads
  config.c          settings.ini
  sfx.c  music.c    sound effects and mixer; theme choice and levels
  synth.c           SID-style synthesizer playing the scores in assets/music
  net.c             UDP online play
  core/             game rules, no platform: mathx.h, world, mesh, game, sim, ai
  platform/         plat.h, everything from the computer: loop, window, input, sound;
                    plat_mac.m + gx_metal.m (macOS), plat_win32.c + gx_d3d12.c (Windows),
                    plat_linux.c, plat_wayland.c, plat_x11.c + gx_vulkan.c (Linux),
                    plat_posix.c (config folder, sockets, paths), plat_common.c (files)
shaders/            HLSL sources for the modern renderer
relay/              matchmaking and forwarding server for online play
tests/              unit tests (core, JSON, GLB, crypto, plat.h)
intro.c             the opening cinematic (assets/intro/INTRO.md)
assets.c            loads jets, carrier, maps and intro from assets/
assets/             game data: jets, carrier, maps, intro (see assets/README.md)
docs/               install guide, playing, CI, third-party code (none: docs/THIRD_PARTY.md)
music/              soundtrack: rendered tracks and the JS engines (see music/README.md)
reference/          a music listening page, for side-by-side checks
cmake/              MinGW toolchain file
```

`src/core` never includes the platform layer, so the rules can be unit-tested on their own
and run the same on both computers in an online match. Each file says
which part of the game it holds.

## CI

Our own CI, `tools/ci`, runs `tools/check` on every new branch head on the
build computer and posts the result to GitHub as a commit status; there are
no GitHub Actions workflows. See docs/CI.md.

## Credits

Tailon Lock was designed, written and tested by Claude. The music and the
drums are the project's own: the scores play on the game's own SID-style
synthesizer, and the intro film's song is the project's own recording. The
third-party code the build still downloads, until our own replaces it, is
listed in [docs/THIRD_PARTY.md](docs/THIRD_PARTY.md).
