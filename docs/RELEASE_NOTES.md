# Tailon Lock 0.1.0: release notes

## What it is

Tailon Lock is an original jet combat game written in C11. One or two
players fly against each other or the CPU, on one screen split in two or
over the network. It runs on macOS, Linux and Windows. A floppy edition
fits the whole game on one 1.44 MB disk.

## What's in it

| | |
|---|---|
| Game modes | 1 PLAYER, 2 PLAYERS (versus or co-op), DEFEND THE CARRIER, BEAT THE DRONE! |
| Match size | 1 vs 1, or 2 vs 2 with a CPU wingman on each side |
| CPU pilots | EASY, NORMAL or ACE |
| Maps | 4 maps (Twin Isles, Dune Sea, Iron Coast, High Valley) and the classic arena |
| Time and weather | dawn, day, dusk, night; clear, scattered, overcast, storm |
| Jets | 8 to choose from, plus the DR-7 Vesper drone in BEAT THE DRONE! |
| Two players | split screen on one computer, or network play between two computers |
| Network play | over the LAN, by internet address, or through a relay server with a 6-digit game code |
| Rules | MODERN or CLASSIC controls and rules; ARCADE or REALISTIC handling |
| Renderers | MODERN (on the GPU, four graphics levels) and RETRO (software, low-res, chunky pixels) |
| Music | 3 themes in the menu (Full Burn, Ghost Squadron, Steel Horizon), played live by the game's own synthesizer |
| Intro | an opening film timed to Steel Horizon; it can be skipped or turned off |

Also in this release:

- A hangar to pick each seat's jet and arm it, with a full sheet for every jet.
- A HUD, cockpit panel and radar; chase view and cockpit view.
- Gamepads for both players, and the mouse in the menus.
- Sound effects, a volume from 1 to 10 and a CHILD hearing mode that caps it.
- Network matches are encrypted with a key made from the game code.

## Systems and CPUs

| System | CPU | What you get |
|---|---|---|
| macOS 11 or later | arm64 and x86_64 (one app for both) | `TailonLock.app` |
| Linux | x86-64 (plain x86-64, no newer instruction sets needed) | `tailonlock` |
| Windows 10 or 11, 64-bit | x86-64 | `TailonLock.exe`, one file with no extra libraries |

The program is 5.0 MB on Linux and 5.7 MB on Windows; the assets add
3.4 MB.

## The floppy edition

The whole game on one 1.44 MB floppy (1,474,560 bytes). Sizes in bytes,
measured on the release head:

| | macOS arm64 | Linux x86_64 | Windows |
|---|---|---|---|
| program, packed | 426,424 | 472,825 | 499,174 |
| data, packed (Full Burn included) | 494,533 | 494,533 | 494,533 |
| unpacker | 3,708 | 3,993 | 3,993 |
| **total** | **924,665** | **971,351** | **997,700** |
| **free** | **549,895** | **503,209** | **476,860** |

The packing is lossless: the packed assets give the same pictures, to the
pixel, as the full build.

## Slow computers

- In network play, a computer that draws few frames a second catches up by
  running several game steps a frame. If it still falls more than a second
  behind, the host waits for it: the host shows WAITING FOR THE OTHER PLAYER
  and the other computer shows THE OTHER PLAYER IS WAITING - CATCHING UP.
  Both computers see every match end the same way.
- On Windows, where the screen can show only a few pictures a second (no
  GPU), the game paces its own frames instead of waiting for the screen.

## Known limits

- No flight stick (HOTAS) support yet, and the keys cannot be changed.
- Network play is for two computers, one player on each. Addresses are IPv4.
- A network match does not stop when you pause. If nothing arrives for
  6 seconds, it ends with CONNECTION LOST.
- Both computers need the same network version of the game; an older or
  newer copy is refused with a message.
- Joining by internet address needs UDP port 27960 forwarded on the host's
  router. The relay needs none, but the relay sees the game code, so use a
  relay you trust or run your own.
- Each computer reports its own jet, so a changed copy of the game could
  cheat.
- The relay server is not built on Windows.
- A renderer change takes effect after a restart.
- The first time it hosts, Windows asks to let the game through the
  firewall. A downloaded copy of the macOS app is not signed: open it once
  with right-click > Open.
- On a computer without a GPU, the MODERN renderer runs on the system's
  software fallback and is slow; RETRO is the one to pick there.
- Floppy edition: the Linux and Windows programs are over their own 420 KB
  share, but the disk as a whole has room.
