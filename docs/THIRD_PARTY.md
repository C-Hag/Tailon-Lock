# Third-party code

The repository holds no third-party source. Until our own code replaces it
(docs/BUILD_PLAN.md), CMake downloads one library at configure time, a
pinned release checked against its SHA-256:

| Library | Version | Used for | Replaced in | Licence |
|---------|---------|----------|-------------|---------|
| SDL3 | 3.4.18 | window, input, sound, GPU | brick 20 | zlib |

A binary built from this repository contains it, so a release must carry
its licence notice. The notices are in the downloaded sources (the
`LICENSE.txt` file under `build/*/_deps/`).
