# The floppy edition

The whole game on one 1.44 MB floppy (1,474,560 bytes), playing and looking as the full build does. Everything that
packs it is our own code; xz is only the yardstick.

## The tools (tools/ff)

| Tool | What it does |
|---|---|
| `inventory` | every byte the game ships, by kind, against the budget (asks the game what it reads: `TL_FILES=1`) |
| `qcheck` | the quality gate: 196 stills drawn with the original and the packed assets, PSNR, SSIM, the worst pixel, a contact sheet; `--digest` the audit digest, `--sound` the songs and effects |
| `tlpk` (src/ff/tlpk_model.h, src/ff/tlpk.c) | our packer: LZ77 with an optimal parse, the last four distances, context-mixed literals, filters for x86 and arm64 code and for tables |
| `tlj` (src/ff/tlj.c) | binary JSON: the same values bit for bit, numbers in columns |
| `tlh` (src/ff/tlh_model.h, src/ff/tlh.c) | terrain heights, lossless, after LOCO-I |
| `forge` | the floppy's assets folder: each file packed every lossless way, the smallest kept, each checked back through the game's own loader |

The game reads the packed files directly (src/assets.c, `asset_load`); a file that is not packed is read as it is.

## The budget (0.2.1, measured with tools/ff/inventory on macOS)

| | arm64 | x86_64 | Budget |
|---|---|---|---|
| program (one CPU, -Os, stripped), raw | 1,112,720 | 1,247,928 | |
| program, xz -9e (the yardstick) | 454,464 | 525,748 | 430,080 packed with tlpk |
| data the game reads, packed | 591,352 | 591,352 | 921,600 |
| **total, arm64 with the xz yardstick** | **1,045,816** | | 1,474,560 |
| **left for the unpacker, headers and margin** | **428,744** | | at least 102,400 |

tlpk packs code tighter than xz, so the yardstick is the upper bound. The disk carries the Ghost Squadron and Steel
Horizon scores; the recorded music does not fit, so on the floppy the opening film plays without its song. The disk sizes for Linux and Windows are made with
tools/ff/mkdisk on those systems.

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
