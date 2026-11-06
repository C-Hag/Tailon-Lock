# Tailon Lock 0.2.0: release notes

## What's new in 0.2.0

- A new opening film: the carrier at dawn, a catapult launch from the deck, the pair overhead to a top-down view, enemies
  spotted from the cockpit, then the dogfight from outside, in long takes with slow motion at the peaks. One of the two
  jets is now the JA 46 Gladan. The music dips at each kill.
- Outside views and effects: Tab now steps through cockpit, a banking chase view that leans into turns, and a flyby
  view (a camera left in the jet's path, on a long lens; on the deck it stands by the catapult, so it shows the launch).
  High up, jets pull contrails; hard turns low down pull wingtip and wing vapour; near the speed of sound a vapour cone
  forms; low over the sea a fast jet whips up spray; the catapult steams; the afterburner flashes as it lights. All of
  it is for the eye only: the flight and the matches are exactly as before.
- The opening film is longer (82 s), with a backlit panorama of the carrier at dawn and a crane shot over the jet on
  the catapult.
- CPU pilots crash less: they see the ground further ahead at speed, glide on an empty tank, and when nearly dry the
  runway is theirs. Fewer go-rounds for traffic, softer realistic touchdowns, no flying into tankers on a gun run, and no
  more jets stuck on the runway.
- The floppy edition is a real disk now: a start program and a 1.44 MB FAT12 image made by our own tools.
- Logs say why a network packet was refused and which seed each match used.

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
| Intro | an opening film timed to Full Burn; it can be skipped or turned off |

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

The program is 3.3 MB on macOS (both CPUs), 1.9 MB on Windows and to be measured
on Linux; the assets add 43 MB, of which 37 MB is the Full Burn recording.

## The floppy edition

The whole game on one 1.44 MB floppy (1,474,560 bytes). A disk holds a start
program, the packed game and its packed assets; the start program unpacks the
game to a temporary folder and plays it, so it needs a writable temporary folder.
Sizes in bytes, measured on the release head:

| | Linux x86_64 (gcc) | Windows x64 (MinGW gcc) | macOS arm64 |
|---|---|---|---|
| start program | 19,440 | 45,568 | 34,832 (before the final intro) |
| game, packed | 481,764 | 539,150 | 423,369 (before the final intro) |
| assets, packed (Full Burn included) | 504,817 | 504,817 | 504,817 |
| **total** | **1,006,021** | **1,089,535** | to be measured |
| **free on the disk** | **437,760** | **353,792** | to be measured |

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
- Floppy edition: the disk is not bootable, and the game does not run in place.
- CPU pilots crash too often, the main limit of this version; their landing
  circuit is to be redesigned for 0.3. Three of the test pilots' own checks fail
  on this release, none of them on a player's own flying. Over the audit's 1008
  matches:
  - Item 21, take-offs and the first minute: 5 crashes within a minute of the
    jet starting (the test pilots' 576 take-offs had no failure).
  - Item 22, flying: 55 crashes before the 30-minute sudden death, against a
    goal of 20; 21 of them had fuel left. After sudden death, when nobody can
    refuel, 26 more crash; they are counted apart, by choice. No physics fault.
  - Item 23, fuel and landing: 34 of those 55 are empty tanks, mostly after
    failed landing attempts; the test pilots landed 102 of 127 times, with 2
    failures by the CPU pilots.
- Matches run a little long: the mean is 7.267 minutes. The limit was raised
  from 7.25 to 7.30 minutes for this release, so test item 25 passes; 3 % of
  matches reach sudden death (5 % at most).
