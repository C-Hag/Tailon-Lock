# Test guide: Linux, Windows and macOS

What has to pass before a build of Tailon Lock counts as working on all three systems. It follows ordinary game QA
practice (a build gate, a regression suite, a platform matrix, soak runs, network tests on bad links, desync checks,
sanitizers, logs) with our own tools: `tools/check` (the gate) and `tools/playtest` (unattended matches). Nothing here
needs a third-party test tool. The hands-on list (section 5) borrows the kind of item the console makers'
certification lists check (no hang in any flow, controllers coming and going, losing the network mid-match,
settings surviving a hard quit), the public Steam Deck and accessibility guidelines, and each system's own
conventions (Retina, Cmd+Q, the settings folder).

Three layers: sections 1 to 3 run on every change, section 4 and a long soak before every release, section 5 by
hand before every release, recorded in section 7.

## The certificate: `tools/certify`

All of this is one stack of building blocks. Each block runs only when every block under it passed; the first
failure stops the run, and nothing above it counts. After a fix, the whole stack is built again from block 1 on
the new commit, because a fix can knock out a block that stood before. Each passed block is sealed with a hash of
its result and of the seal under it, so a certificate cannot be edited without the chain breaking.

```
                        release      tools/certify release ...   every certificate on one commit + hands-on, ticked
              cross linux-macos  cross linux-windows  cross macos-windows       tools/certify cross
          linux                  macos                  windows                 tools/certify run / windows
10 behaviour  9 soak  8 sanitizer  7 network  6 offline  5 smoke  4 gate  3 tests  2 build  1 clean
```

1. On Linux and on the Mac: `tools/certify run` (all ten blocks; `--quick` runs 1-5 as a draft).
2. On Linux for Windows: cross-build, play the VM's matches (section 4), then `tools/certify windows --vm-logs DIR`.
3. Each pair of machines: `tools/certify cross --name linux-macos HOST.log GUEST.log ...` (and the other two pairs).
4. A person ticks section 5 on each system, saved as `certs/<commit>-hands-on-<system>.md`.
   The CPU pilots' item 22 in `tools/handson` counts crashes before sudden death against its goal of 1 in 50 matches;
   crashes after it, when nobody can refuel, are reported on their own and do not count.
5. `tools/certify release certs/<commit>-*.cert --hands-on certs/<commit>-hands-on-*.md`: the release certificate.
   `tools/certify verify CERT` checks any certificate's chain.

The finished certificates are committed in `certs/` as the record (section 7). They name the commit and the system,
never the computer or its owner.

A release needs every box in sections 1 to 5 ticked on the same commit, on every system. All machines test the
same commit; network play refuses a game of another protocol version, so two machines on different commits never
play each other by accident.

## 1. The build gate (every commit)

| | Linux | Windows 11 | macOS |
|---|---|---|---|
| Builds without warnings | `tools/check` stage 1 (GCC, Clang, ASan) | stage 10 (MinGW cross-build on Linux) | `cmake --preset macos-release` and `macos-debug` |
| Unit tests | `ctest` (stage 2) | in the VM: the game starts and plays (section 3) | `ctest --preset macos-release` |
| Golden data, pictures, sound | stages 3, 7, 8 | | |
| Only our own code, only OS libraries | stages 4, 5 | stage 10 (import table) | `otool -L TailonLock.app/Contents/MacOS/TailonLock`: system frameworks only |
| One binary for the CPUs | x86-64 baseline, no AVX2 | x86-64 | `lipo -info`: `x86_64 arm64` |

## The fuzz targets (libFuzzer)

Every reader of data from outside has a coverage-guided fuzz target under AddressSanitizer and UBSan, one file
each in `tests/lf_<name>.c`, seeded from `tests/corpus/<name>/` (cut-down real files from `assets/`, and for the
network packets from the game's own builders; `regress-*` are inputs that once crashed, kept so the bug stays
fixed). `tools/certify plat` runs each one for 60 s in P1 when it finds a clang with libFuzzer, and notes it
skipped them when it does not.

| Target | What it reads |
|---|---|
| `lf_net` | every packet the game takes (`src/net.c`): LAN beacons, the relay's answers, the handshake, the welcome, a match's state and events, the watch stream; sealed ones encrypted under the right key first |
| `lf_config` | `settings.ini` (`config_load`), then saved and read again; every value must stay in its range |
| `lf_song` | a song (`synth_load`), saved as the music editor saves it and read again, then played a moment |
| `lf_map` | a map's JSON, `.h16` heights and `.mat` surface (`src/assets.c`), then the terrain looked up across it |
| `lf_json`, `lf_glb` | our JSON and glTF readers, with the rules of `fuzz_json` and `fuzz_glb` |
| `lf_wav` | the WAV reader (`plat_load_wav`) at the mixer's rate |
| `lf_ff` | the floppy's unpackers (`src/ff`): TLPK, TLJ (its text must parse) and TLH, as `asset_load` runs them on every packed file |

They build only with clang's libFuzzer (Apple's clang has none; an LLVM release does):

```
cmake -S . -B build/libfuzzer -DTL_LIBFUZZER=ON -DCMAKE_C_COMPILER=/path/to/llvm/bin/clang
cmake --build build/libfuzzer --target libfuzzers
mkdir -p /tmp/lf-net && build/libfuzzer/lf_net -max_total_time=1800 -rss_limit_mb=2048 /tmp/lf-net tests/corpus/net
```

The first folder takes the new inputs it finds (keep it out of the tree); a crash is saved as `crash-*` in the
current folder and replays with `build/libfuzzer/lf_net crash-...`. Each target writes its files only in a temp
folder of its own and points `TL_CONFIG_DIR` there, so a run never touches the player's settings. A fix for a
crash goes in with the input added as `tests/corpus/<name>/regress-<what>`. `TL_LF_SEEDS=DIR build/libfuzzer/lf_net`
writes the network seeds again (after a change to the packets).

## 2. Offline matches (every system)

`tools/playtest solo --games 56 --per-run 2` plays every mode (1p, 2p, coop, defend, defend1, drone, drone2) with
every one of the eight jets, CPU pilots in every seat, a different map, time of day, weather and rule set each run,
with the physics check on. Then the same with an ASan build for 24 matches (`--build` the ASan folder).

Pass: every match ends, no crash, no sanitizer report, no physics fault. The report is `report.txt` in the work
folder; keep it with the commit's test notes.

## 3. Network matches on one machine (every system)

`tools/playtest local --games 40 --per-pair 4` plays host and guest pairs on the same computer:

- `address`: joining by address (the LAN and internet-address path)
- `relay`: both through `tl_relay` (the internet path without port forwarding)
- `bad-address`, `bad-relay`: the same behind our bad-internet proxy (40-120 ms delay with jitter, 3 % loss, 1 %
  doubled packets, reordering)

Pass: every match starts and ends, no connection lost, and both computers agree on every match (winner, every jet's
kills and lives). Disagreement is a desync bug, never a flaky test.

Also before a release: a soak, one pair playing `--per-pair 50` through `bad-relay`, watching the game's memory stay
flat (no growth from match to match); and the packet readers fuzzed (`tests/fuzz_*`, 10 minutes each, as the brick
audit does), since every byte from the network comes from a stranger.

## 4. Network matches between machines

Each pair plays at least 20 matches with `tools/playtest side host|guest` on the two machines (same `--code`), and
`tools/playtest compare HOST.log GUEST.log` checks them. Every pair is tested both ways round (each machine hosts once).

| Pair | LAN by address | Through the relay |
|---|---|---|
| Linux - macOS | yes | yes (relay on Linux) |
| Linux - Windows (VM) | yes (the VM reaches the host through its NAT) | yes |
| macOS - Windows | through the relay on Linux | yes |

The relay run stands in for the internet. A real internet test (relay on a public server, one side on a phone
hotspot) is done before a release when such a server is available.

On Windows the game has no console: the VM side runs `TailonLock.exe` with `TL_PLANES=1`, `TL_TEST_CODE=NNNNNN`,
`TL_LOG=C:\path\guest.log` and the same arguments `playtest side` prints, then the log is copied back for `compare`.
Windows asks once to let the game through the firewall, macOS too (and again after a rebuild of an unsigned app).

The VM also runs the game's own audit, which `tools/certify windows` checks against the same reference as block 10
on Linux and macOS (the simulation computes the same bits on every system, so the matches must come out the same):

```
set TL_VIDEO=dummy
set TL_AUDIO=dummy
set TL_AUDIT_DEATHS=1
TailonLock.exe --audit --part sorties --sorties 112 --out C:\path\audit.log
```

`--out` writes the audit's log to the file by itself (the game has no console there). Copy `audit.log` into the
`--vm-logs` folder; `tools/certify digest audit.log` checks one by hand on any system.

## 5. Hands-on checks (a person, on each system)

The play tests cannot judge feel, so before a release someone plays each system for ten minutes and ticks:

- [ ] The window opens sharp at the right size (Retina / high-DPI too); fullscreen on and off (F11, Alt+Enter)
- [ ] Mouse clicks land where the pointer is, in every menu
- [ ] Keyboard: both key sets in 2P; pause; M mutes
- [ ] Music and every sound effect, no clicks or dropouts; sound comes back after the window loses focus
- [ ] The intro plays to the menu and can be skipped
- [ ] Modern renderer on the GPU (the log names Direct3D 12, Vulkan or Metal); Retro too; every graphics level
- [ ] A setting changed, the game quit and started again: the setting is still there
- [ ] Quit from the menu, the window's close button, and Cmd+Q on macOS
- [ ] A gamepad: plugged in before and during play; two pads in 2P
- [ ] HOTAS (when it is supported): stick, throttle, pedals, bindings saved
- [ ] A gamepad unplugged mid-match: the game pauses or carries on sanely, and takes it back when it returns
- [ ] The window minimised, or the computer put to sleep and woken, mid-match: no hang, sound comes back
- [ ] LAN: host on one machine, the game shows up in the other's list, join with the code, play a match, rematch
- [ ] A wrong code is refused with a message; the right code then works
- [ ] The network cable pulled (or Wi-Fi off) mid-match: CONNECTION LOST within about 6 seconds, back to the menu,
      and a new match can be hosted
- [ ] The game force-quit (kill it) and started again: it starts, the settings are intact
- [ ] Text readable at 1280x800; every menu works with the gamepad alone

## 6. When a test fails

1. Keep the logs (the work folder of the run); the report names the pair or run and its seed.
2. Rerun that one case with the same seed to see if it repeats.
3. Find the cause in our code; fix it so all three systems keep working (no `#ifdef` unless the system really
   differs) and the network protocol stays the same, or bump `VERSION` in `src/net.c` and `relay/relay.c` together.
4. Rerun the failing case, then section 1 on Linux, then the section that failed on every system.
5. Write down the bug and the fixing commit in the commit message.

## 7. The record

Every test round leaves a dated entry here: the commit, the system and its version, and the result of each
section, so an audit can be traced later. Logs stay on the machine that ran them; the `report.txt` summaries go
in the commit message or below.

| Date | Commit | System | 1 build | 2 offline | 3 network | 4 cross | 5 hands-on | Notes |
|---|---|---|---|---|---|---|---|---|
