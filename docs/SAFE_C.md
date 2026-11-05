# Safe C

The rules every C file of the game, the platform layers, the C tools and the tests keeps. `tools/static` checks
them (and so does `certify plat`'s P0); the compiler flags below are on in every build.

## The language

- **C11 only.** Game code uses the C standard library and nothing else; POSIX and OS calls live in `src/platform`.
- **No variable-length arrays** (`-Wvla`) and **no `alloca`**: a fixed array, or `malloc` with its result checked.
- **Every allocation is checked.** A failed `malloc`, `calloc` or `realloc` takes a clean way out (the old block
  kept: `q = realloc(p, n); if (!q) …; p = q;`), never a write through NULL.
- **Sizes that can overflow are checked** with `safe_add` and `safe_mul` (`core/safe.h`) before they are used to
  allocate or index.
- **Every switch case that falls through says so** with `A4C_FALLTHROUGH;` (`-Wimplicit-fallthrough`).
- **No cast that loses alignment** (`-Wcast-align`): read unaligned data with `memcpy`; where the alignment is
  certain (offsetof a float field, an allocator's own header), the cast goes through `void *` with a comment that
  says why.

## Text and numbers

| Banned | Use instead | Why |
|---|---|---|
| `strcpy`, `strcat` | `snprintf(dst, sizeof dst, "%s", src)` | the size is always given |
| `sprintf`, `vsprintf` | `snprintf`, `vsnprintf` | the same |
| `gets` | `fgets` | the same |
| `strtok` | `safe_token(&cursor, delims)` | no hidden state shared between calls or threads |
| `atoi`, `atof`, `atol` | `safe_int(s, def, lo, hi)`, `safe_num(s, def)` | no number gives `def`, an overflow is held to the range instead of being undefined |
| `scanf` `%s` or `%[` without a width | `%63s`, `%15[^:]` | the field cannot run past its buffer |

- **Format strings are literals.** A function that passes its format on to `printf` or `vsnprintf` is declared
  with `A4C_PRINTF(f, a)`, so the compiler checks every call (`-Wformat=2`). Text from a file, the network or the
  command line is never a format: it goes in as `"%s"`.
- **Every buffer write is bounded**: `snprintf` with `sizeof` of the target, `memcpy` with a length checked
  against both ends.

## Data from outside

Network packets, settings.ini, songs, maps and terrain, JSON, glTF and WAV files are read as if they were hostile:
every length checked against what is left, every index against its array, every number against its range. Each
reader has a fuzz target (tests/fuzz_*.c) that runs under AddressSanitizer and UBSan.

## Saving

A file the player would miss (settings, songs) is written whole or not at all: `plat_save_begin` opens
`<path>.tmp`, `plat_save_end` checks every write and the close and only then puts it in place.

## The flags

On every GCC and Clang build: `-Wall -Wextra -Wshadow -Wstrict-prototypes -Wformat=2 -Wvla -Wcast-align
-Wnull-dereference -Wimplicit-fallthrough`, `-Werror` in tools/check, certify and CI; `-fstack-protector-strong`;
`-D_FORTIFY_SOURCE=3` (2 where the compiler has no 3) outside Debug and sanitizer builds. `-ffp-contract=off` keeps
the simulation's floats the same on every computer.

`-Wconversion -Wsign-conversion` run as a report: each one is fixed or explained in the audit
(docs/audits/safe-c-2026-11-02.md).

## The checks

- `tools/static`: the banned calls above (no line in tools/static.supp can excuse one), cppcheck and clang-tidy.
- `certify plat` P0 runs it; P1 runs the fuzz targets under ASan and UBSan.
