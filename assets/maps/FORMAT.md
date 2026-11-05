# Tailon Lock map format, version 1 (first written for Ace4Combat)

One map is one JSON document (`<id>.json`). The same data drives the browser game and the C port.
`maps_game.js` holds all four for the browser (`window.ACE_MAPS[id]`). For C, the heightmap and surface
grid are also written as raw files (`<id>.h16`, `<id>.mat`), so a loader only needs a small JSON parser for the rest.

| id | Name | Asked for |
|---|---|---|
| `twin-isles` | Twin Isles | Sea and land |
| `dune-sea` | Dune Sea | Desert |
| `iron-coast` | Iron Coast | Sea with a coastline |
| `high-valley` | High Valley | Green landscapes and snowy mountains |

## Axes and units

Metres. +X right, +Y up, +Z from the blue base (team 0) toward the red base (team 1). These are the browser
game's axes. glTF and three.js are right-handed, so they use (x, y, -z). Headings are degrees clockwise from +Z
(0 = toward red, 180 = toward blue). `arena` is the playable half-size (9.5 x 11.5 km, the same as the game today).

## Terrain

- `heightmap`: 129 x 129 samples, 200 m apart, origin (-12800, -12800), so it covers 25.6 km. Data is int16
  little-endian, row-major, row j = z, column i = x. Metres = value x `scale` (0.1). The heights are the
  *surface*: water is already flat at its level (sea level 0 on the sea maps, lakes and ponds at their own level).
  What you see is what you collide with.
- `surface`: 128 x 128 cells, uint8, one material per cell (row j = z, column i = x), as an index into `palette`.
- Triangulation (both engines must match): cell (i, j) has corners a=(i,j), b=(i+1,j), c=(i+1,j+1), d=(i,j+1).
  Two triangles, (a,b,c) and (a,c,d), split along the a–c diagonal. Height at a point (fu, fv inside the cell):
  `fu >= fv ? a + (b-a)fu + (c-b)fv : a + (c-d)fu + (d-a)fv`. This replaces the game's `terrainH()`.
- Low-poly look: flat-shaded triangles, one colour per triangle. Far terrain can be drawn at stride 2 or 4 (400 m
  or 800 m cells) as a level of detail; the 200 m grid is only needed near the camera.
- Beyond the 25.6 km square draw a flat plane in `sky_far_ground` at the water level (or 40 m on land maps).

## Palette and lighting

`palette[k] = {name, color, kind: land|water, jitter}`. Triangle colour:

```
shade  = ambient * ambient_intensity * ambient_factor
       + sun_color * sun_intensity * sun_factor * max(0, N·sun) * cloud_shadow
colour = palette.color * shade * (1 + jitter * (2*hash(i, j, t) - 1))      t = 0 or 1 for the two triangles
water  = colour * (0.55 + 0.45 * water_tint) * (1 - water_sky_mix) + avg(sky_horizon, sky_top) * water_sky_mix
hash(i,j,t): n = (i*73856093) ^ (j*19349663) ^ (t*83492791)  (uint32)
             n = (n ^ (n >> 13)) * 1274126177; n ^= n >> 16; return (n & 255) / 255
```

`ambient*`, `sun*` come from the time of day, `*_factor` from the weather. `cloud_shadow` is 1 with no cloud;
the preview darkens cells under puffs and sheet masks projected along the sun (down to 0.3).

## Time of day

`time_of_day.{dawn, day, dusk, night}`: `sky_top`, `sky_horizon`, `fog`, `fog_density`, `sun` (unit vector toward
the sun; at night it is the moon), `sun_color`, `sun_intensity`, `ambient`, `ambient_intensity`, `water_tint`,
`stars` (0–1), `lights` (0–1, how bright the night lights are). Fog distance = visibility x 1.2 / fog_density.

## Weather

`weather.presets.{clear, scattered, overcast, storm}`, tuned to the map's climate (`weather.climate`):
`visibility_km`, `wind {from_deg, speed_mps}`, `turbulence` (0–1), `precipitation` (none, drizzle, rain, showers,
virga), `lightning_per_min`, `dust`, `sun_factor`, `ambient_factor`, a `layers` summary (genus, base, top, oktas) and:

- `clouds`: puff clouds (Cu cumulus, Cb cumulonimbus, Fs stratus fractus). Each has `x, z, base, top` and `puffs`
  `[dx, dy, dz, radius, tier, squash?]` relative to (x, base, z). Draw each puff as a camera-facing billboard of
  1.4 x radius. `tier` 0 is always drawn, 1 on medium quality and up, 2 on high only. Puff colour:
  `n = normalize(dx, dy - 0.35*depth, dz)`, `h = dy/depth`,
  `colour = albedo * (ambient * (1 + 0.4h) + sun * (0.45 + 0.6 max(0, n·sun)) * (0.72 + 0.28h))`,
  albedo 1.0 (Cu), 0.92 (Cb), 0.62 (Fs), x0.72 in storms. Cb clouds carry `rain {radius}` and `lightning: true`.
- `sheets`: layer clouds (Sc, St, As, Ns, Ci, Cs, and Fg valley fog with `tod_only: ["dawn"]`). Each has `base`, `top`,
  `coverage`, `color` and a tileable 64 x 64 uint8 `mask` spanning 32 km (repeat it beyond). Draw a lit top plane at
  `top` and a darker underside at `base`, alpha = mask.

Real-world basis: cumulus bases sit near the lifting condensation level for the climate (about 650 m over a
tropical sea, 1,000 m on a temperate coast, 2,300 m in the Alps in summer, 3,200 m over a hot desert);
cumulonimbus tops reach the tropopause (10.5–13 km); desert storms are high-based with virga and blowing dust;
alpine overcast is a stratus deck in the valleys with the peaks above it.

## Sites

- `bases`: `{team, kind: runway, x, z, elev, heading, length: 2000, width: 90}`. The ground under the runway is flat.
- `carriers`: `{team, x, z, heading, speed, length: 320, beam: 86, deck_height: 20.5, model: cvn90, model_files}`.
  The model is the CVN-90 Stormhaven from the jets work (`../jets/carrier_game.js` in game axes, `../jets/cvn90.glb`,
  deck data with catapults, wires and landing lens in `../jets/carrier_data.json`). Carrier-local (lx, lz) with
  +X starboard, +Z bow maps to the world as `x + lx cos h + lz sin h`, `z - lx sin h + lz cos h`.
  Each carrier has two escort frigates.
- `targets`: six per side, `{team, type, x, z, heading}`, types `sam`, `fuel`, `radar`, `hangar` (as in the game)
  and the new `ship` (frigate, floats at sea level). y = terrain height at (x, z). Ground targets sit on flat pads.
- `spawns`: `{team, kind: air|runway|carrier, x, y, z, heading, ref}`. Carrier spawns sit on catapult 1 (`catapult: cat1`).
- `tanker`: `{x, z, radius, alt}` orbit, checked to clear the terrain by at least 700 m.

## Scenery and night lights

- `props`: `{type: tree|pine|palm|cactus|rock|house|lighthouse, x, y, z, s, rot}`. Optional; draw within a radius.
- `lights`: `{kind, x, y, z, size, blink}` with colours in `light_colors`. Kinds: runway edge, end, approach,
  beacon, carrier deck, deck edge, landing lens (`ols`), mast, town windows, lighthouse. `blink` is the period in seconds (0 = steady).
  Brightness = `time_of_day.lights`.

## Rebuilding

`python3 src/build_maps.py <outdir>` (numpy) then `python3 src/build_page.py <outdir> [path/to/carrier_game.js]` for the preview page.
The generator is deterministic and checks every map: runways flat and dry, targets on land (ships at sea),
carriers in open water, tanker orbit and air spawns clear of terrain, peaks under 3,200 m.
