# Tailon Lock opening cinematic: shot script format

The intro that plays before the main menu is data. `intro_script.json` holds every camera move, every jet and missile
path, the flares, the events, the text cards and the screen effects, all baked to world space. The game only has to
interpolate keys and draw; nothing is choreographed in engine code. `src/build_script.js` writes the file.

The film runs 81.75 s at 60 ticks a second, to the project's own song (`heartbeat-intro`). Its shots:

- `fleet` (0.00 - 9.16 s): A wide flyby of CVN-90 Stormhaven at dawn: in from high off the stern, closing down her port side toward the bow, the whole ship, the sea and the horizon in the frame.
- `sunrise` (9.16 - 12.86 s): Dawn on the deck through a long lens: both alert jets against the low sun in heat haze, a slow push and arc.
- `radar` (12.86 - 16.72 s): The carrier's radar scope, filling the picture: the sweep finds two contacts closing from the north, and a slow push goes in as the operator hooks one.
- `cic` (16.72 - 20.52 s): Inside the carrier's control room: the radar operator calls the contacts, the supervisor sounds the alert, the commander sends the alert fighters.
- `takeoff` (20.52 - 24.32 s): One wide take of the launch from a helicopter off the carrier's port bow: the whole ship with both jets on the catapults at the alarm, off the bow on the band hit, the victory roll and the climb into the dawn.
- `deck` (24.32 - 25.27 s): One take on the flight deck: up the deck from the shooter, through a long lens in the steam as he gives his run-up and launch signals; as his hand comes down, the catapult take.
- `catapult` (25.27 - 28.60 s): One take on the catapults: at Viper 2's nozzle as the burners light, Viper 1 going, the shooter black against the sky, then after Viper 2 down the deck, off the bow in slow motion and level with it, the sun over it.
- `takeoff2` (28.60 - 30.70 s): One wide take of the launch from a helicopter off the carrier's port bow: the whole ship with both jets on the catapults at the alarm, off the bow on the band hit, the victory roll and the climb into the dawn.
- `drone` (30.70 - 33.32 s): A camera drone above and ahead of the pair as they leave the bow, wide, leading the climb: looking back at the noses with the carrier behind them, the victory roll, and both accelerating up toward the lens.
- `ride` (33.32 - 38.12 s): Riding low beside the pair, looking across them into the sun and the water: Viper 1 and the Gladan side by side, vapour cones and the ba-boom.
- `cockpit` (38.12 - 44.40 s): In Viper 1's cockpit: two contacts on the scope and in the head-up display's brackets, then the KS-55 and the drone in the windscreen, growing from dots until they fill it as they pass.
- `merge` (44.40 - 46.46 s): From outside, a very long lens high on the line of the merge: the Vipers coming on, the KS-55 and the drone going away, stacked by the lens; they cross with vapour cones and all four break.
- `fight1` (46.46 - 54.12 s): Into the sun through a long lens, level behind Viper 1 with Viper 2 on its wing: fox two, the flares fan out against the sun in slow motion and take the missile. The fight turns into the sun, the second missile, and the KS-55 goes up against it in slow motion, shards and smoke.
- `fight2` (54.12 - 61.78 s): A slow half circle round the fight, low: the drone fires at Viper 2, flares and a hard break; the two cross against the sun in a flat scissors, wingtip trails, until Viper 2 is on the guns.
- `pursuit` (61.78 - 67.53 s): Over the Gladan's shoulder: the drone runs for the sunrise, weaving; Viper 1 with them.
- `lock` (67.53 - 73.14 s): In Viper 2's cockpit, the sun through the canopy: Viper 1 ahead in the windscreen, the drone beyond it running for the sunrise, jinking; the lock closes and Viper 2 fires.
- `finale` (73.14 - 81.75 s): Behind the Gladan into the sun: Viper 2 (the Gladan) hits the drone on bar 13, dives a little and breaks away to the right with Viper 1 on its wing, and the fire and smoke drift together into TAILON LOCK, a cloud in the dawn sky, which the camera holds as it thins away.

The framing rule: every jet of the film, parked ones too, counts in every shot for the whole shot. `tools/frame_check.js --strict` takes the 8 corners of each jet's bounding box and fails if any corner is outside 92 % of the letterbox at any sample, or if a jet has no path there, with no grace. Jets seen through a long lens in the background may be listed per shot as `partial` (their centre stays in the frame). The only ways out are written in the script per shot and listed in the check's output: `absent` `[{id, reason, from?, to?}]` says a jet is not in this shot and why, and it must then not show; `closeup` names one jet that may run past the frame edge with its centre staying in the frame (`catapult`: Viper 2).
Who is where: the bandits come in at the cut into the merge and stay in its frame to its end. Viper 2 flies on Viper 1's wing through the first fight, the drone having no path there. Viper 1 flies top cover over the second fight, on Viper 2's right through the pursuit, and ahead of it in the windscreen through the lock (from that cockpit Viper 2 itself is not drawn). In the finale both Vipers are behind the drone at the hit, then go down after the wreck and circle where it hits the sea.
The `seahaze` effect (`amount`) is a haze just beyond the jets when the camera looks steeply down from height, over the sea's blotches; it is in the film's own code, not the game's sea.
The title holds sharp until 1.25 s before the wreck's splash, then vaporizes; while it reads, the camera keeps the wreck and the Vipers low in the picture, under the letters.
The format also has `warp`, a list of `[film clock, story time]` pairs: the music and the sync points run on the film clock, everything in this file on story time, and outside the list the two are the same. The builder eases the rate down and up and makes up the time by easing a little faster (at most 1.4 times), with no jumps, and it fails if any sync point is off. A shot's `shade` (0..1) dims the jets, and turns the crew black, when they are against the sun. The `glow` effect (`amount`) is the sun in the lens: soft layers over the sky's hard disc, a bloom, a veil and two faint ghosts, under the grade; the grade's numbers do not change, and the grain keeps to the film clock in slow motion. At an `explode` of size 2 or more (a kill) the game ducks the music by 4 dB for half a second (`sfx_duck_music`, eased with a 40 ms time constant, before the master volume and the limiter); it is restored at once if the film is skipped, and nothing else in the game uses it. The browser player ignores `warp`, `shade`, `glow` and the duck, and has no Gladan mesh: its copy of the script (`web/index.html`) gives Viper 2 the NF-24, and it plays the same shots at normal speed.

## Clock, music and the timeline

Time is seconds from the first sample of the film's song (`heartbeat-intro`, a recording); the cues below are the
beat grid the film is cut to. Drive the picture from the audio clock, not the frame counter, so a slow frame never puts picture and music
out of step. Without audio, run on the wall clock.

`music.cues`: jet build-up at 120 BPM (bar lines at 8, 10, 12 s), the jet pass and band hit at `band_hit` = 12.05 s,
then about 140 BPM with one bar = `bar` = 1.70667 s (16 sixteenths of 5/5/6 frames at 50 Hz). Bar n after the hit
starts at `band_hit + n * bar`. The shots cut on those bars and half bars.

| Time | Shot | Lens | In | What happens |
|---|---|---|---|---|
| 0 – 5.5 | sunrise | 300 mm | fade | The sun rises behind the carrier's island, a kilometre away through heat haze. "SOMEWHERE IN THE PACIFIC". |
| 5.5 – 8.0 | deck | 18 mm | dissolve 1.2 s | Knee height on the deck: steam, crew, both alert jets on the bow catapults. |
| 8.0 – 10.0 | shooter | 85 mm | cut | Klaxon, and the control room on the radio in the lower bar: "RADAR: BOGEYS INBOUND, BEARING 3-5-0", then "COMMANDER: SEND THE BIRDS!" (the radar inset at the lower right). The shooter's run-up signal, afterburner behind him, then the launch sign. |
| 10.0 – 12.05 | launch | 14 mm | cut | Low on the deck edge: both catapults fire, Viper 1 roars past, Viper 2 follows. |
| 12.05 – 13.76 | offbow | 35 mm | cut | Band hit: Viper 1 leaves the bow; chase as it settles toward the sea. |
| 13.76 – 16.32 | topdown | 21 mm | whip 0.35 s, right | Straight down on the pair over the waves. |
| 16.32 – 18.02 | skim | 24 mm | dissolve 0.45 s | A metre over the waves; the pair passes overhead with vapour cones and pulls up. |
| 18.02 – 19.88 | cockpit | 21 mm | cut | Viper 1's cockpit and head-up display; the bandits flash past on the fanfare crash (18.88). |
| 19.88 – 21.30 | railcam | 12 mm | cut | Under the wing: the missile drops off the rail and lights. "Fox two." |
| 21.30 – 22.29 | teleflare | 300 mm | whip 0.25 s, left | From far away: flares fan out, the missile takes them. |
| 22.29 – 24.00 | tailcam | 15 mm | cut | Looking back from Viper 2: the bandit fires, flares, a hard break, the missile misses. |
| 24.00 – 25.70 | guncam | 21 mm | whip 0.3 s, right | Viper 2 on the guns, tracers reaching out. |
| 25.70 – 27.41 | splash | 35 mm | cut | Viper 1's missile ignores the late flares and hits (27.04). Splash one. |
| 27.41 – 31.15 | pursuit | 50 mm | dissolve 0.8 s | Over Viper 2's shoulder: the last bandit runs for the sunrise, jinking; Viper 1 joins high right. |
| 31.15 – 33.10 | lock | 21 mm | cut | Viper 2's head-up display: lock, tone, fox two (missile away at 32.65). |
| 33.10 – … | finale | 40 mm | cut | Behind Viper 2 into the sun: the hit on bar 13 (`last_kill` 34.24), the Vipers split, the camera glides to a stop. The fire and smoke form **TAILON LOCK** from 34.59 to `title_formed` 37.54, with the subtitle "THE GREAT NOSE2NOSE ENGAGEMENT" under it. |
| 41.06 | | | | `menu_at` (bar 17): the menu opens over the last shot, which keeps drifting and billowing. |

Music cue times (build-up 8, 10, 12 s; hit 12.05; bar 1.70667 s), so any score written
to them still lines up. The intro is just longer, so the score needs bars 14 to 17 to carry the title.

## Shots

`{name, t0, t1, desc, cam: keys with fov, lens?, rig?, attach?, hud?, vibe?}`. The shot whose `[t0, t1)` holds t
is live; a new shot is a hard cut unless it has `in`. `fov` is the vertical field of view in degrees; `lens` is the focal length in mm on
a full-frame sensor (24 mm tall) it was chosen as, and `rig` says what kind of camera it is.

- `attach {actor, offset [right, up, fwd], dir [right, up, fwd], roll}`: the camera is bolted to that actor. Build
  it from the actor's frame every frame: position = actor + offset in its axes, view along dir in its axes, up =
  the actor's up. The keys hold the same camera baked, for engines that do not do this.
- `hud {actor, target?, guns?}`: the view is from that cockpit. Do not draw that jet. Draw a canopy frame and glare
  shield, and a head-up display: pitch ladder, flight path marker, heading, speed and altitude, plus a box on
  `target` with LOCK (missile) or a gun pipper (`guns`).
- `vibe {a, f}`: camera jitter with amplitude `a` times the fov (in degrees) at about `f` Hz: slow drift for a
  long lens held by hand or on a boat, fast buzz for a camera bolted to an airframe.
- `scene`: the shot is not of the 3D world but a scene the engine draws itself. `"cic"` is the carrier's control
  room (`src/cic.c`), drawn in code from the shot's `t0` and `t1`. The
  room is a 3D mesh made in code (`assets/figure/figure.js` `room`, `src/room.c`): a refit-era gun ship's combat
  centre on our fictional carrier, lit by its own lamps, each screen's content mapped onto its face. Its
  `cam` keys only hold a still pose for engines that need one. A dissolve to or from it fades the drawn room over the
  3D picture.
- `in {type, dur, dir?}`: how the shot starts.
  - `dissolve`: over `[t0, t0 + dur]` draw the previous shot, then this one over it, with this one's opacity
    eased (smoothstep) from 0 to 1. The previous shot keeps moving: every shot's `cam` keys run 1.2 s past its `t1`
    (where its actors still exist), so sample the previous shot at t, not at its last key.
  - `whip`: a whip pan centred on the cut, over `[t0 - dur/2, t0 + dur/2]`. With u = 0..1 across that window,
    the first half shows the previous shot with its yaw offset by `dir * 80 * (2u)²` degrees, and the second half shows
    this shot with yaw offset `-dir * 80 * (2(1-u))²`, so the camera swings out of one shot and lands in the next with
    no visible joint. `dir` 1 swings right, -1 left. Smear the picture sideways while it swings: the browser draws
    the frame over itself 5 times at 30% opacity, each shifted by `dir * 0.035 * width * sin(πu)` more. A motion blur
    along the screen x axis is the same thing. The last shot runs past `menu_at` so the menu has a moving background
(the browser plays it at quarter speed behind the menu).

## Cards

`{t0, t1, style, text, type?, blink?}`. `type` = characters per second for a typewriter reveal; `blink` = on/off period.
Styles: `location` (lower third, large, underline grows), `sub` (under it), `alert` (red, boxed, top third),
`alert-sub`, `callsign` (left, in the lower letterbox bar), `radio` (centred, in quotes, in the lower bar), `dialog`
(a line spoken on screen: `speaker` in amber, then the text in white, centred as one line on the whole text in the
lower bar), `title`
(the game title, huge, slams in from 1.9× over 0.22 s with a glint),
`subtitle` (small, centred just under the smoke title at 42% of the height plus 0.115 × min(width, 1.6 × height),
fading in over 1.2 s), `press` ("PRESS ANY KEY"). Cards fade out over
their last 0.5 s.

## Effects

`{t0, t1, type, ...}`: `grade` (`warm` tint, `vignette` darkness, `grain` strength: the film look),
`haze` (heat shimmer over the screen band `y0`–`y1` as fractions of the height, `amount` in pixels at 360 lines;
also draw it behind every lit afterburner), `fade` (`from`→`to` alpha of `color`, over everything), `flash` (same, inside the picture),
`letterbox` (`amount`, or `from`→`to`; bars to 2.35:1, at least 9% of the height), `alarm` (red vignette pulsing with
`period`), `radar` (scope inset, `contacts` blips at `bearing`), `shake` (camera jitter of a few tenths of a degree
fading over the window, scaled by `amount`).

`smoketitle` `{t0, t1, text, pos, form [f0, f1], seed}`: the last kill's fire and smoke turn into a cloud
that spells `text`. Draw it inside the picture, under the grade and letterbox, so the film look applies to it.

1. Layout, once per screen size: rasterize `text` in the game font, bold, centred at 42% of the screen height, at
   0.14 × min(width, 1.6 × height), shrunk if needed to 84% of the width. Sample the glyph coverage on a grid with
   step = max(3, size / 15) pixels. Each covered cell is one puff, at the cell plus up to ±0.4 step of jitter. Store
   `lit` = 1 at the top of the glyphs and 0 at the bottom. From a seeded random number generator (`seed`), give each
   puff a delay d (0–1), a spawn offset (sx, sy, each ±0.5), a curl (±0.3 to ±1.0), a phase (0–2π) and a size (0.85–1.25).
2. Each frame, project `pos` (the kill, in world space) to the screen: that is the fireball. For each puff,
   e0 = clamp((t - f0 - 0.4·d·span) / (0.6·span), 0, 1) with span = f1 - f0, and e = easeInOutCubic(e0).
   Start = fireball + (sx, sy) × 0.06 × min(W, H) × (1 + 2e). Position = lerp(start, target, e), plus a curl
   sideways to the path of sin(πe) × curl × 0.25 × the path vector rotated 90 degrees, and a lift of
   sin(πe) × 0.048 × min(W, H) upward.
3. Colour by e: fire (255,236,170 → 255,128,50) up to 0.18, drawn additively at 60% opacity; then to smoke
   (118,104,112) by 0.38; from 0.5 to 0.9 to the cloud colour, a mix of shadow lavender (196,180,210) and sunlit warm
   white (255,242,228) by lit^0.6. Radius = step × size × lerp(0.5, 1.45, e). Draw each as a soft round sprite
   (radial alpha 1 → 0.75 at 45% → 0). Once e > 0.6, also draw a darker lavender (150,138,176) shadow puff 1.1× as big, offset
   (0.2r, 0.35r), behind it, which gives the letters volume.
4. After forming it keeps billowing: offset each puff by (sin(0.7t + phase) × 0.35, cos(0.55t + 1.3·phase) × 0.25) × step,
   breathe the radius by ±8%, and drift the whole cloud slowly right and up (0.04 and 0.02 step per second). It stays
   up behind the menu.

## Environment

`env.sun` overrides the map's dawn sun direction: it is lowered to 3.3 degrees so the sun sits right
behind the carrier's island in the 400 mm opening shot. Light everything with it.

`env.map` / `time_of_day` / `weather` name the map preset the scene uses (Twin Isles, dawn, scattered). The data an
engine needs from them is in `intro_assets.json`; the browser draws sky and sea from the dawn colours, puff clouds per
the maps format, and Twin Isles land at 400 m. `env.extra_clouds` adds a cumulus on the climb path, in the maps
format (`x, z, base, top, puffs [dx, dy, dz, r]`). `env.clear_clouds` lists `[x, z, r]` circles of
open sky: leave out every map cloud whose centre is inside one, so the last chase does not fly through a towering cumulus. The camera flies through it: a puff contains the camera when the
distance to its centre is under its radius; fade the picture toward the cloud colour by how deep inside it is.

## Playing it

- Start on a key press or click (browsers need a gesture before audio). Any key, click, gamepad or HOTAS button
  after the first 0.5 s skips to the menu; the menu then opens over the last shot.
- "Intro at start" in the menu turns the intro off; "Watch intro" plays it again.
