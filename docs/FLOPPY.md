# The floppy edition

The whole game on one 1.44 MB floppy (1,474,560 bytes), playing and looking as the full build does. Everything that
packs it is our own code; xz is only the yardstick.

## The tools (tools/ff)

| Tool | What it does |
|---|---|
| `inventory` | every byte the game ships, by kind, against the budget (asks the game what it reads: `A4C_FILES=1`) |
| `qcheck` | the quality gate: 196 stills drawn with the original and the packed assets, PSNR, SSIM, the worst pixel, a contact sheet; `--digest` the audit digest, `--sound` the songs and effects |
| `tlpk` (src/ff/tlpk_model.h, src/ff/tlpk.c) | our packer: LZ77 with an optimal parse, the last four distances, context-mixed literals, filters for x86 and arm64 code and for tables |
| `tlj` (src/ff/tlj.c) | binary JSON: the same values bit for bit, numbers in columns |
| `tlh` (src/ff/tlh_model.h, src/ff/tlh.c) | terrain heights, lossless, after LOCO-I |
| `forge` | the floppy's assets folder: each file packed every lossless way, the smallest kept, each checked back through the game's own loader |

The game reads the packed files directly (src/assets.c, `asset_load`); a file that is not packed is read as it is.

## The budget (2026-11-01)

| | macOS arm64 | Linux x86_64 | Windows (MinGW) | Budget |
|---|---|---|---|---|
| program, TLPK (one CPU, -Os, stripped, no SDL3) | 396,158 | 440,511 | 471,837 | 430,080 |
| data, forge | 467,310 | 467,310 | 467,310 | 921,600 |
| Full Burn as scores (song and film cut, still to merge) | 27,223 | 27,223 | 27,223 | |
| unpacker (code, -Os) | 3,708 | 3,993 | 3,993 | |
| **total** | **894,399** | **939,037** | **970,363** | 1,474,560 |
| **free** | **580,161** | **535,523** | **504,197** | at least 102,400 |

All three fit with half a megabyte to spare.

## Measured again (2026-11-03, on the release head with every review fix)

(The Linux column here was measured on the arm64 Linux VM, not on x86-64; the release head's own figures, with
Linux on x86-64, follow below.)

| | macOS arm64 | Linux arm64 (VM) | Windows (MinGW) |
|---|---|---|---|
| program, TLPK | 405,516 | 424,241 | 474,539 |
| data, forge | 467,310 | 467,310 | 467,310 |
| Full Burn as scores, unpacker | 30,931 | 31,216 | 31,216 |
| **total** | **903,757** | **922,767** | **973,065** |
| **free** | **570,803** | **551,793** | **501,495** |

The data is the same to the byte. The programs moved with the fixes since: macOS +9 KB, Windows +3 KB, Linux -16 KB.
The unpackers are fuzzed (tests/lf_ff.c, in tools/certify plat P1), and tools/ff/qcheck on the packed assets gives
the same 195 stills to the pixel and the same audit digest. All 19 sounds are the same once the Full Burn recording
is beside them. Without it, 16 effects differ, because `--render-sfx` plays each one over Full Burn's first chorus
and the floppy has no Full Burn yet: its version as scores is still to merge. The Linux and Windows programs are over their own share of 420 KB, but
the floppy as a whole has room.

The data, raw 2,387,745 bytes, packs to 19.6 % (xz -9e on the same files: 554,692). So far every step is lossless:
the packed assets give the same stills to the pixel, the same audit digest and the same sound.

## On the release head (ac2113c, 2026-11-04)

Every program packed with TLPK (one CPU, -Os, stripped, dead code dropped); the data is the forge's 32 files, now
with Full Burn and its film cut as live scores (2,665,098 bytes raw).

| | macOS arm64 | Linux x86_64 | Windows (MinGW) |
|---|---|---|---|
| program, TLPK | 426,424 | 472,825 | 499,174 |
| data, forge (Full Burn included) | 494,533 | 494,533 | 494,533 |
| unpacker | 3,708 | 3,993 | 3,993 |
| **total** | **924,665** | **971,351** | **997,700** |
| **free** | **549,895** | **503,209** | **476,860** |

The programs grew by 20-25 KB with the tanker, the missiles in bays, the live pilot's checks and the landing rework.
Linux x86_64 was measured on an x86-64 Linux computer (gcc 14.3, -Os, gc-sections; 475,935 without gc-sections); the Linux arm64 VM gives 446,752.
All three fit with close to half a megabyte to spare; tools/ff/qcheck on the packed assets gives the same stills,
the same audit digest and the same sound.

## On the 0.2.0 head

Measured with tools/ff/mkdisk (gcc 14.3 on Linux, MinGW gcc 15.1 for Windows, -Os, -ffunction-sections, --gc-sections,
stripped). The macOS column was not measured on this head.

| | Linux x86_64 | Windows x64 (MinGW gcc) |
|---|---|---|
| start program | 19,440 | 45,568 |
| game, packed | 481,764 | 539,150 |
| assets, packed | 504,817 | 504,817 |
| **on the disk** | **1,006,021** | **1,089,535** |
| **free on the disk** | **437,760** | **353,792** |

## The disk (tools/ff/mkdisk, tools/ff/loader.c)

`tools/ff/mkdisk FOLDER OUT.img` makes a 1,474,560-byte FAT12 image of a folder with our own code, and the same files give
the same bytes on every system; `--check OUT.img FOLDER` reads an image back and compares every file. `tools/ff/loader.c`
is the disk's start program: it unpacks `game.tlpk` from its own folder to a temporary one, plays it with the disk's
`assets/` and removes the copy. So a disk holds the loader (`tailonlock`, or `TailonLock.exe` on Windows), `game.tlpk`
(the one-CPU, -Os, stripped program through `tlpk c -f auto`) and `assets/` (the forge's output). tools/ff/forge needs
tools/ff/inventory's list first: `tools/ff/inventory --no-program --build DIR`.

The Windows program for the disk: llvm-mingw's clang with `-Os -ffunction-sections -fdata-sections -Wl,--gc-sections`, or
MinGW gcc with `-ffunction-sections` only. MinGW gcc with `-fdata-sections` puts the game's big zero-filled buffers (32 MB of
them, in the test and figure code among others) into the file: the program came out at 34 MB and tlpk ran for over 10 minutes.

The game does not run in place: it needs a writable temporary folder to unpack into. The sizes above count only the
unpacker's code; on a disk the loader is a whole program, about 16 KB on Windows and 35 KB on macOS.
