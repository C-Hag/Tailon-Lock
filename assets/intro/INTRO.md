# Tailon Lock opening cinematic: shot script format, version 7

The intro that plays before the main menu is data. `intro_script.json` holds every camera move, every jet and missile
path, the flares, the events, the text cards and the screen effects, all baked to world space. An engine only has to
interpolate keys and draw; nothing is choreographed in engine code. The browser game plays it today
(the browser game (`web/index.html`), the `IntroCore` and `Intro` modules between `---- intro core start ----` and `---- intro end ----`);
the C port can load the same file.

Version 2 (2026-10-24) reshoots it like an aviation film. It has 24 shots cut on the beat, with long lenses on the deck at sunrise
and the deck crew, including the shooter's launch signal. Cameras are bolted to the jets: cockpit with head-up display, wing, rail and tail.
A hovering helicopter camera gives a speed pass. Heat haze, wing vapour from the g load, vapour cones, a warm grade and film grain finish the look.
Everything new is additive: a version 1 player still plays it with keyed cameras and no crew.

Version 3 (2026-10-24) slows the cut down and tells a story. Version 2 was "TikTok fast". It now has 16 shots
over 41 s instead of 24 over 36 s, joined by dissolves and whip pans as well as hard cuts, in three acts:

1. **Dawn and launch** (0 – 18 s): the sun rises behind the carrier, the deck wakes up, the klaxon goes, both Vipers
   launch and run low over the sea.
2. **The dogfight** (18 – 27.4 s): the merge, missiles against flares, Viper 2 defending, guns, and splash one on the riff.
3. **The last kill** (27.4 s – end): Viper 2 chases the last bandit into the sunrise, locks, fires, and the hit lands
   on bar 13. Its fire and smoke drift together into a cloud that spells **TAILON LOCK** in the dawn sky, with THE GREAT NOSE2NOSE ENGAGEMENT under it.

Version 4 (2026-10-24, the C port) cuts it to about 55 s with longer shots and real fighter manoeuvres, on a new
music cue: Steel Horizon's film cut in C minor (`horizon-intro`, `tools/write_synthwave.py` horizon_film), the band hit
on bar 7 of the C64 frame clock (11.99 s). The low run lasts two bars longer and the merge is on bar 6 after the hit;
every dogfight phase is two bars, so the turns are flown at real rates (about 25 degrees a second): Viper 1 fires,
is decoyed, and flies a high yo-yo to stay behind the turning bandit; Viper 2 breaks hard into the bandit's shot;
the two fall into a flat scissors, the bandit a beat late on each reversal, and Viper 2 gets the guns; Viper 1
comes down in a low yo-yo for the splash; the last bandit is chased in lag pursuit and killed on bar 19. The deck
and shooter shots are one crane move, the gun camera gives way to an outside view of the scissors, and after the
smoke spells TAILON LOCK it holds, then dissolves back into drifting smoke (`dissolve` on the `smoketitle` effect)
before the menu. The browser game still plays version 3.

Version 5 (2026-10-27, the C port) holds each take longer and lets the camera follow the jets. the brief asked for
longer takes, camera angles that follow the movement, wider framing, slower pacing between takes with quicker cuts, a
more detailed cockpit, and an enemy of one jet plus one drone. The music, the flight paths and the cue times are
unchanged. The 14 shots become 11, joined by hard cuts and two short dissolves (0.6 s and 0.3 s):

- `deck` is one take from 5.5 s to the band hit: the dolly, the crane to the shooter, and a pan onto Viper 1 as the
  catapult fires and it roars past. `launch` is gone.
- `chase` (band hit to 17.96 s) replaces `offbow` and `topdown`: a camera jet starts behind Viper 1 and swings out and
  forward round the pair until it looks back at both NF-24s over the sea.
- `foxtwo` (23.94 to 27.35 s) replaces `railcam` and `teleflare`: on Viper 1's wing as the missile leaves, then falling
  back while the frame opens onto the bandit's flares.
- `lock` (37.59 to 43.42 s) replaces `pursuit` and the short lock shot: one take in Viper 2's cockpit.
- The chase cameras use `smooth()` in the builder, an operator's lag and lead on the jets they follow, and a level
  frame along the jet's track, so the horizon stays put while the jet rolls.
- The second bandit is the DR-7 Vesper drone (`model: "dr7"`, the game's `MESH_DRONE`, one centre-line nozzle, about
  12 m long). The radar inset shows 2 contacts, and the cards and radio calls name the KS-55 and the drone.

Version 7 (2026-10-27) is cut to the main theme, Full Burn, instead of Steel Horizon's film cut: first "Arp sound DMX3", re-scored on 2026-10-28 to "Arp sound DMX10" (the same bars and timing, so every cue holds).
`music.track` is `fullburn-intro`, a recording (`assets/music/fullburn-intro.wav`, credits in `fullburn-intro.txt`).
It starts with half a second of silence and runs bars 1 - 40 of the song. The game plays it once (`music.c` plays a
theme from `assets/music/<key>.wav` when one exists), and the menu theme takes over when it ends. A bar is 96 / 50.125
= 1.9152 s. The full beat comes in on bar 7, at 11.99 s, which stays the band hit. `bar(n)` is n bars after it.
The story follows the song's sections. In the intro, the sunrise and the deck. In the riff (bar 2 - 10), the chase, a
skim pass with a camera low over the water, and the cockpit through the climb and the merge (bar 9). In the verse
(10 - 18), the two fights, 7.7 s each. In the pre-chorus (18 - 26), the pursuit (bar 18 - 21), the lock and the last
kill (bar 24.25). The smoke forms TAILON LOCK as the chorus comes in on bar 26 (61.8 s), and the menu opens at 69.2 s.
After it forms, the title vaporizes over 3.5 s (`dissolve` ends at the wreck's `splash`). The drone's wreck dives
into the sea 7.6 s after the hit (`kill` with `fall`), and the finale camera tilts down after it, so the title is gone
as the wreck splashes in, at 65.9 s. The menu opens at 66.4 s. There are 10 shots, every take from 5.3 to 7.7 s. The low run now climbs to 480 m from bar 4.5, because the longer run
reached the islands. The builder checks every jet against the terrain grid, and it clears the clouds along the drone's
run.

Version 6 (2026-10-27) cuts like a film: the brief asked for takes of 5 to 8 seconds, "a real intro, not a TikTok
one". There are 8 shots, and every take before the finale runs 5.4 to 6.8 s: `sunrise` (5.5), `deck` (6.5), `chase` to
18.3 s (6.3), `cockpit` from the low run through the merge (5.6), `fight1` (6.8), `fight2` (6.8), `lock` (5.9), then
`finale`. The two dogfights are flown as continuous fights now, so a camera can stay on one for a whole take without a
jet jumping. In `fight1`, Viper 1 comes out of its pull-up after the KS-55 (`onward` with `pursue`). The first missile
is decoyed, Viper 1 stays in the bandit's turns, and the second missile is splash one at 29.55 s. In `fight2`, the drone
flies Viper 2's own track shifted in time, starting 1.9 s behind it (it fires) and ending 1.1 s ahead of it. It slides
outside the break as it overshoots, so Viper 2 ends up behind it for the guns. The drone's run for the sunrise and the
last kill on bar 19 are unchanged. The skim pass is cut.

Framing (2026-10-29): every shot lists its `subjects`, actor ids or `[id, from, to]`. The builder's `frameSubjects`
keeps them inside the letterboxed 2.35:1 frame (74 % of its half-width and half-height): it moves the aim just enough,
and widens the lens when they do not fit, both eased over about a second. A ship counts by its bow and stern, so the
whole ship stays in. `tools/frame_check.js --strict` checks every shot, and the checker runs it in stage 3. The flyby
is now wide: water, the whole carrier and the horizon stay in the frame from a kilometre out until we pass the bow.
On the control room's scope the two contacts close from the rim toward the ship over the shot, each sweep leaving an
echo of where they were.
The people in the control room have bodies from a small figure engine (`figure()` in the browser game, the same in
`src/cic.c`). It is a skeleton with adult proportions from published body measurements, in head heights: shoulders
1.75 across, upper arm 1.45, forearm 1.16, hand 0.83, the neck's base 1.25 below the crown. Each person is posed by
joint angles (trunk lean; shoulder flex and spread, elbow, wrist and finger curl for each arm). It is drawn as shaded,
tapered limbs under the uniform: the shoulder's cap, the upper arm and forearm muscles, creases at the elbow, the
cuff, and a hand with thumb, palm and curling fingers. The operator leans in with both hands at the keyboard. The
supervisor holds the clipboard against his chest. The commander rests his forearm on the armrest and raises his hand
as he gives the order.

The opening (2026-10-29, the same in the browser game and the C port) starts with a fast flyby: in from high off
the carrier's stern, down her port side, turning to hold the whole ship as we pass the bow (`sunrise`, 0 - 5.2 s).
Then a 0.5 s dissolve into the carrier's control room (`cic`, 5.2 - 9.0 s, `scene: "cic"`): a take that opens tight
on the two hostile blips on the radar scope and pulls back to the radar operator, the supervisor and the commander.
The three speak in turn in the lower bar (`dialog` cards): "BOGEYS! TWO CONTACTS, BEARING 3-5-0, CLOSING FAST.",
"ALERT, ALERT! WE HAVE INBOUND.", "SEND THE BIRDS.". The `deck` take comes in at 9.0 s with a 0.3 s dissolve, with
the alarm vignette (9 - 10 s) and a shake (9.6 - 10 s). The radar inset and the alert cards are gone, and the
location cards end at 5.1 s. Sonic booms (`boom`) sound at 19.5 s and 29.0 s (`vol` 0.7). All the film's sound
effects play at 45% of their level, under the music.

New in version 5: `hud.panel` (true) draws the game's own cockpit in that shot instead of the simple one. That is the
match HUD (`hud_draw`: heading tape, gunsight, G meter, brackets with ranges on both enemies, a wingman tag, ENEMY JETS,
the lock diamond and status lines) and the jet's instrument panel (`hud_panel`, the NF-24's dials, VDI and HSD). The
engine fills a stand-in match with the script's live jets, giving each its speed and its g from the path. The panel
takes the place of the lower letterbox bar, and the callsign and radio lines move to the upper bar. A canopy bow with
mirrors, the side rails, the HUD combiner glass and the glare shield frame the view. `hud.lock [t0, t1]` closes the
lock diamond over that window and shows LOCK from t1. Where one path of a jet ends as its next starts, at the same
time, `sample` now takes the later path, so a shot cut on that time sees the jet where its new path starts.

New in version 3: shot transitions (`in`), the `smoketitle` effect, the `subtitle` card, and cues `last_kill` and
`title_formed`. The colour grade is the same as version 2; keep it exactly. A version 2 player plays a version 3 script
with hard cuts and no title, so add the `smoketitle` before shipping.

| File | What it is |
|---|---|
| `intro_script.json` | The shot script (this document). |
| `intro_assets.json` | What the scene needs from the rest of the project, collected by `src/build_assets.py`: the NF-24 mesh and far LOD, the CVN-90 mesh and catapults, Twin Isles terrain at 400 m, its dawn light, the cumulus puffs of its "scattered" weather. |
| `src/build_script.js` | The authoring tool. Waypoints, missile guidance, camera moves and cue times live here; it writes `intro_script.json`. Edit this, not the JSON. |
| `src/intro_core.js` | Engine-neutral helpers shared by the builder and the game: key sampling, angles to axes, flare paths, ship motion. Port these first. |
| `src/intro.js` | The browser player (rendering, particles, sounds, skip handling). |
| `src/build_intro.py` | Patches the intro into the browser game (`web/index.html`), and puts in the current Steel Horizon and Pop modules (`src/music_horizon.js`, `src/music_pop.js`, copied from `music/engines/` in the repo). Re-appliable on a file that already has the intro; anchors on exact strings. |
| `tools/render_frames.js`, `tools/flow_test.js` | Playwright checks: still frames of chosen times, and the boot, skip, menu and game flow. |

Rebuild: `node src/build_script.js intro_script.json`, `python3 src/build_assets.py <ace4combat dir> intro_assets.json`,
then `python3 src/build_intro.py <game.html> . [out.html]`.

## Clock, music and the timeline

Time is seconds from the first sample of the music cue, `music.module` = `HorizonMusic` (Steel Horizon,
the heavy C64-style mix from the music work). Drive the picture from the audio clock, not the frame counter, so a slow frame never puts picture and music
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

Music cue times are unchanged from version 2 (build-up 8, 10, 12 s; hit 12.05; bar 1.70667 s), so any score written
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
  room (in the browser game `drawCIC`, in the C port `src/cic.c`), drawn in code from the shot's `t0` and `t1`. The
  room is a 3D mesh made in code (`assets/figure/figure.js` `room`, `src/room.c`): a refit-era gun ship's combat
  centre on our fictional carrier, lit by its own lamps, each screen's content mapped onto its face. Its
  `cam` keys only hold a still pose for engines that need one. A dissolve to or from it fades the drawn room over the
  3D picture.
- `in {type, dur, dir?}` (version 3): how the shot starts.
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
(the game title, huge, slams in from 1.9× over 0.22 s with a glint; version 3 uses the `smoketitle` effect instead),
`subtitle` (version 3: small, centred just under the smoke title at 42% of the height plus 0.115 × min(width, 1.6 × height),
fading in over 1.2 s), `press` ("PRESS ANY KEY"). Cards fade out over
their last 0.5 s.

## Effects

`{t0, t1, type, ...}`: `grade` (`warm` tint, `vignette` darkness, `grain` strength: the film look),
`haze` (heat shimmer over the screen band `y0`–`y1` as fractions of the height, `amount` in pixels at 360 lines;
also draw it behind every lit afterburner), `fade` (`from`→`to` alpha of `color`, over everything), `flash` (same, inside the picture),
`letterbox` (`amount`, or `from`→`to`; bars to 2.35:1, at least 9% of the height), `alarm` (red vignette pulsing with
`period`), `radar` (scope inset, `contacts` blips at `bearing`), `shake` (camera jitter of a few tenths of a degree
fading over the window, scaled by `amount`).

`smoketitle` (version 3) `{t0, t1, text, pos, form [f0, f1], seed}`: the last kill's fire and smoke turn into a cloud
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

`env.sun` (version 2) overrides the map's dawn sun direction: it is lowered to 3.3 degrees so the sun sits right
behind the carrier's island in the 400 mm opening shot. Light everything with it.

`env.map` / `time_of_day` / `weather` name the map preset the scene uses (Twin Isles, dawn, scattered). The data an
engine needs from them is in `intro_assets.json`; the browser draws sky and sea from the dawn colours, puff clouds per
the maps format, and Twin Isles land at 400 m. `env.extra_clouds` adds a cumulus on the climb path, in the maps
format (`x, z, base, top, puffs [dx, dy, dz, r]`). `env.clear_clouds` (version 3) lists `[x, z, r]` circles of
open sky: leave out every map cloud whose centre is inside one, so the last chase does not fly through a towering cumulus. The camera flies through it: a puff contains the camera when the
distance to its centre is under its radius; fade the picture toward the cloud colour by how deep inside it is.

## Playing it

- Start on a key press or click (browsers need a gesture before audio). Any key, click, gamepad or HOTAS button
  after the first 0.5 s skips to the menu; the menu then opens over the last shot.
- "Intro at start" in the menu turns the intro off; "Watch intro" plays it again.
