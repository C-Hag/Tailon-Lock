# Third-party code

The repository holds no third-party source. Until our own code replaces them
(docs/BUILD_PLAN.md), CMake downloads three libraries at configure time,
each a pinned release checked against its SHA-256:

| Library | Version | Used for | Replaced in | Licence |
|---------|---------|----------|-------------|---------|
| SDL3 | 3.4.18 | window, input, sound, GPU | brick 20 | zlib |
| cJSON | 1.7.19 | reading the JSON assets | brick 4 | MIT |
| Monocypher | 4.0.2 | network encryption | brick 22 | BSD-2-Clause or CC0 |

A binary built from this repository contains them, so a release must carry
their licence notices. The notices are in the downloaded sources (the
`LICENSE.txt`, `LICENSE` and `LICENCE.md` files under `build/*/_deps/`).
