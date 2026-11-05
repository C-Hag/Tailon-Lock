# Tailon Lock poster

Cover art for the game in the style of an 80s home-computer box: a big chrome
title, two of our own jets meeting nose to nose, missiles in the air and the sea
far below. The jets are the game's own models (`models/*.glb`, copied from the
hangar build) in their game paint, rendered photo-real with Blender Cycles.

    tools/poster/render.sh --preview          # 800x1200, 48 samples, quick look
    tools/poster/render.sh --variant sunset   # also: dusk, storm; --fx adds bloom and vignette
    tools/poster/render.sh                    # 2000x3000, 384 samples
    tools/poster/render.sh --no-title --out out/keyart.png

Default pairing is JA 46 Gladan (foreground, afterburners lit) against
Ks-55 Grifon (coming head on). Swap with `--hero ID --foe ID` using the ids
af40 lf27 ecf90 ja46 sy26 vk33 ks55 nf24.

Camera, jet positions, banks and sun are in the `SHOT` table at the top of
`poster.py` (camera-space metres). Light/exposure: `--sun`, `--sky`,
`--exposure`. `--save-blend file.blend` keeps the scene for hand tweaking.
A bold font named `poster*.ttf` placed in `tools/poster/fonts/` overrides the
system font for the title.

Output goes to `tools/poster/out/` (ignored by git except the published JPEGs:
the three title-free renders, `tailonlock-keyart.jpg`, `tailonlock-sunrise-clean.jpg`
and `tailonlock-burst-clean.jpg`; the titled ones are rendered again with the new title).

For GPU rendering, make your GPU runtime's libraries visible on `LD_LIBRARY_PATH`
before running `render.sh`; otherwise Cycles renders on the CPU.
