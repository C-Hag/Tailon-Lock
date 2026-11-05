# Test pilots and the test viewer

Tools for checking how the game flies, fights and reads, with nobody at the controls.

## Watching a match: `--watch`

    tailonlock --watch [--pilot all] [--view cockpit|chase|orbit|map] [--follow N] [--speed 1|2|4|8] [--report FILE]

CPU pilots fly every seat, match after match. Keys:

| Key | |
|---|---|
| 1-8, Tab | the jet to follow |
| C, V, O, M | cockpit, chase, orbit (arrows turn the camera), map |
| I | the check panel on or off |
| + and - | 1x to 8x speed |
| P, Space | pause |

The panel at the top right compares numbers with the world:

- **Instrument check**, every jet, every tick. The jet's real motion is measured from where it actually went in the
  world and held against what its cockpit gauges read: airspeed and climb from the distance flown, slip (nose against
  the path), the g from the change in velocity (filtered as the gauge is), altitude, and fuel, which may rise only
  while refuelling.
- **Picture check**, the jet on screen, each frame drawn. The horizon as the 3D world drew it against the attitude
  instruments (bank always, pitch from the cockpit), each enemy's HUD box against where the jet really is on screen,
  the range written under it, a lock only on something inside the radar cone, and the tanker's range on the HUD.

A reading off by more than its tolerance turns red; the OFF column is the share of the match it was off.

## Test pilots: `--pilot`

The CPU decides where to go; the test pilot's hands decide how that reaches the controls, through the same stick and
key shaping a person's input goes through (`input_shape`):

| Pilot | Hands |
|---|---|
| `fighter` FIGHTER PILOT | stick, 0.20 s reaction, smooth, eases the pull above 7.5 g |
| `simmer` SIM VETERAN | stick, 0.25 s, a little shake, pulls to the limit |
| `arcade` ARCADE PLAYER | keys at full deflection (tapped for part inputs), throttle at the stops, guns early |
| `rookie` NEWCOMER | keys, 0.35 s, over-corrects, looks away now and then, sprays, fires without a lock |

`all` takes turns. While they fly, each jet is measured (finding the enemy, time the enemy was out of the front view
in a close fight, overshoots, stalls, blackouts, hard roll reversals, rounds and hits, crashes, take-off, landings,
refuelling, fuel) and every jet goes through the mission checks: take-off, landing, refuelling at the base and at
the tanker, rearming, ground strikes, kill credit and the score. When a match ends, a table of rules turns the
numbers into each pilot's remarks. `--report FILE` writes it all as Markdown.

## Many matches, no window: `--test-pilots`

    tailonlock --test-pilots [--matches N] [--pilot NAME|all] [--seed S] [--diff easy|normal|hard]
                             [--pairs N] [--drone N] [--report FILE]

Whole matches as fast as they run (over a hundred a second), alternating one test pilot against a CPU pilot and
two test pilots against each other, arcade and realistic handling, every map. The picture check needs the viewer;
everything else runs. Returns non-zero when a mission check failed. `--diff` sets every CPU pilot's level (normal
by default); `--pairs N` adds N matches of 2 vs 2 (a test pilot and a CPU wingman against two CPU pilots) and
`--drone N` N matches of Beat the Drone, after the others.

The report ends with the **doctrine** section: how the CPU pilots fought, from what the jets did. Per jet: matches
and wins, kills and deaths, the time flown straight and level with an enemy inside 10 km (and the longest stretch,
and how often it passed 30 s), the merges (two enemies passing nose to nose inside 1 km) and who came out on the
other's tail 20 s later, overshoots, running dry in the air, and the kills by kind of fight: from range (the two
never within 1.5 km), at the merge (within 20 s of closing to 1.5 km) or in a turning fight (later). Then the
merges flown by the jet without a gun, the drone's Cobras and flips flown back to back (within 6 s of the last, or
before the speed came back to 150 m/s) and how long the speed takes to come back, and in 2 vs 2 how often both of
a side were on one enemy while another went free, and how often one of a pair was 1 km above the fight.

## Watching from another computer: `--broadcast`, `--spectate`

    tailonlock --watch --broadcast [PORT]            # shows and logs a 16-letter watch code (UDP 27962)
    tailonlock --spectate HOST[:PORT] --code CODE    # the same keys, the same checks

One way only: a viewer receives the match and cannot change it. The code is 80 bits, so it cannot be guessed offline
from a captured packet; a viewer must answer a fresh challenge sealed under it before anything is sent to its
address, and every stream packet is sealed (ChaCha20-Poly1305) and numbered. Explosions show; bullets and missiles
are the newest 30 and 24.

## The hands-on checklist: `tools/handson`

    tools/handson [--build DIR] [--report FILE] [--only N[,N...]]

The items a person checks by hand (docs/TEST_GUIDE.md section 5), done with the game's own test hooks instead of
hands: `--key F:NAME[:N]` presses and holds a key, `--click` clicks, `--event F:WHAT` sends window events (focus,
hide, show, a 2 s hitch, quit), `--window WxH` sets the window's size, and `A4C_STATE=1` logs every screen, sound
and window change. Each item ends PASS, FAIL or SKIP with its evidence, in build/handson/report.md. Items that need
pads, a HOTAS or the network ports run elsewhere (the SKIP line says where).

## What the test pilots changed in the game

Found by these tools and fixed: the G-meter shows the g really flown; the turn back at the arena edge is at most
4 g; the nose rotates up over half a second after lift-off; the base refuels only a jet standing or rolling slowly
on it, and the CPU pilots land for it (onto the centre line, about 105 m/s down the glide, a flare on the sink
rate, go-around and touch-and-go, turned round for the take-off run, and round again rather than onto a runway or deck another jet is on or climbing out from, while a jet about to take off holds for one on short final); speed brakes at idle in arcade too; bingo
fuel counts the circuit. For the pilot: G (H for player 2, the right stick on a pad) holds the eyes on the enemy
(padlock); PULL UP comes 6 s before the ground; SPEED LOW before the stall; a lead pipper for the gun inside
1.8 km; BINGO with the distance home.

## The cheater: `--pilot cheat` and `tools/cheathunt`

A test pilot that plays to win by any trick, as a pentester attacks a system (src/cheat.c): mashing every
control, shooting from behind the spawn protection, hiding in the valleys, sitting outside the arena, hanging
under the ceiling, healing and rearming on the base mid-fight, hiding beside the tanker, spamming flares and
missiles without a lock, flipping every switch every tick. The rule watch holds every jet to the rules each tick
(stores, fuel and hull only up on the base, never past a full load; the gun and missiles no faster than their
cycles; no damage to or from a protected jet; inside the arena, under the ceiling, over the ground; no lock or gun
hit through a mountain; no points from nothing; no two jets through each other) and reports each broken rule with
when and where, and whether each trick paid.

    tools/cheathunt [--storms N] [--seed S]

adds the cheater at the keyboard: storms of keys, chords, clicks and window events in every mode and from the
intro, with the physics check on, looking for a crash, a hang or a stuck screen; every storm is repeatable from
the command in build/cheathunt/report.md.

Found and fixed: the radar locked on jets and targets behind a mountain; the ceiling held the height but not the
nose, so a jet hung there pointing up with the climb gauge at 50,000 ft/min; shooting from behind the spawn
protection (shooting now ends it).

## The cheater on the network: `tools/netattack`

    tools/netattack [--seed S] [--only replay,tamper,plaintext,junk,outsider,codes,watch]

A real network match between two copies on this computer, the guest joined through a man-in-the-middle proxy
that sees and changes every packet but has no game code: old packets replayed, packets with a byte changed,
made-up unsealed match packets, junk of every size, a third address sending both sides replays and junk; then a
stranger guessing codes (the host stops after 10, kept by design) and the watch stream without its code. Each
match must finish with both sides agreeing and the attacks refused in the logs. The host runs on UDP 28960
(A4C_NET_PORT, a test hook that also stops the LAN beacons), off the ports real games and other test runs use.

## The CPU pilots' doctrine and its measures: `--doctrine-measure`

The CPU pilots (src/core/ai.c) fight to a doctrine. Each jet has a fighting style: shooters (af40, sy26, nf24)
fight with sensors and missiles and stay out of slow turning fights; turners (vk33, ks55, ecf90) take the close
fight; the energy fighter (lf27) stays fast, fights the two-circle and is never slow and nose high; the ambusher
(ja46) makes one fast pass and goes. A pilot fights only what it has seen: on radar inside 60 degrees of the nose
(16 km on a big jet, less the stealthier it is, under 7 km at the top score) or by eye to 5 km (half that in the
rear quarter); it looks round every 2.5, 1.2 or 0.6 s (easy, normal, ace) and remembers a lost contact for 4, 8 or
12 s. At the merge it picks the one-circle or the two-circle fight by which one suits its jet (the tighter circle
or the quicker turn). The jet without a gun (the sy26, missiles only, for players too), or a shooter out of missiles, never merges: it separates and comes
back for a shot from range. Ace ja46, af40 and sy26 hit and leave and come back from a new
direction; the ace's perch is on the sun's side of the enemy. The vk33 fights only near its base (out to 16 km on
a full tank, 11 km on half) and an enemy drags its fight away from that base. With an enemy in sight no pilot
flies straight and level for more than 25 s (aces 18). The drone's Cobras, flips and J-turns come one at a time,
only above 150 m/s and 1200 m, each followed by an unload. Two CPU pilots of a side split the targets, and on one
enemy the second flies top cover. A missile is turned onto the beam, with decoys (chaff for a radar missile) once
it is there. On a strike mission an ace may fly a bait run, then turn to fight. An ace with a gun takes a close fight (or any fight once its missiles are gone) to the gun
rather than leaving, except against the drone; its flares against a heat-seeker are less certain than its chaff; it
flies to the bingo figure with no reserve on top, and in realistic handling goes home rather than after its tanker. Easy pilots keep the old
missile defence and fly no style.

    tailonlock --doctrine-measure

prints, from the simulation as it is: the speed back after the drone's Cobra and after a nose-high slow-down in
the jets (only the drone flies past the stall), the radar lock's reach against each jet and the lock cone, each
missile against no defence, decoys, a turn onto the beam, both, and a last-ditch break, and how long each jet's
tank lasts at full power and at cruise.
