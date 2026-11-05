# Audit and fairness at every CPU difficulty, 2026-11-01

The certificate's audit (`--audit --part sorties --sorties 112 --fired --only JET`, one process an aircraft) run once at
each CPU difficulty (`--diff 0`, `1`, `2`): 112 matches for each of the eight jets and the drone, 3,024 matches in all,
on commit 0b86cac. Every seat is flown by the CPU at that level. The jet under test flies the hangar loadouts in turn
(its own pick, as the CPU's, then heat, radar, air-to-ground and mixed), so the loadout table under each level is the
fairness set: a loadout a player could pick in the hangar, against CPU pilots on their own picks.

"Fired by the jet" and the hit rates are the jet under test's; "fired by the others" counts every other jet in its
matches. The gun line counts every jet.

## What it shows

- Easy and normal CPU pilots never fired a radar missile (0 at either level, against 38-129 for each jet's opponents on
  hard), although about 104 CPU jets a level carried them. The radar missile came first only beyond 2.5 km or head-on,
  and the radar shot's envelope ended at about the same range: in a chase from behind there was no shot to take.
- On hard the decoys win: heat-seekers hit 26-38 % (60-72 % on easy and normal). Hard matches last 16.0 minutes on
  average and 21 % reach the 30-minute sudden death.
- Every jet wins 30-70 % at every level; the widest spread is on hard (JA 46 37 %, LF-27 42 %, Vk-33 63 %). The drone
  wins 88-92 %.
- The gun hits 4 % of its rounds on easy, 9 % on normal, 20 % on hard.
- Fairness: on normal the radar loadout wins 42 % (its heat-seekers hit 52 %, its radar missiles 43 %); every other
  loadout at every level wins 50-63 %.
- The NF-24's 97 % wins came from the CPU-doctrine audit of 2026-11-01, before the fuel, turnaround and missile
  changes. Re-measured on rc-1103 (b373a20) it wins 72-40 in the certificate's audit and 69-43 on hard, and after the
  circuit fix (fcbcc5c) 64-47 and 66-46: inside 30-70 %, resolved.
- The mean match's limit (hands-on item 25) is 7.0 minutes, no longer 6.5: the owner, 2026-11-03, "6.5 to 7 is okay"
  (the landing rework on claude/rcx-landing loses fewer jets on the way home, and matches run longer for it). Sudden
  death stays at 5 % at most.

## Easy

| jet | matches | won / lost / drawn | win | kills / deaths | crashed | fired by the jet: heat, radar, ground | hit: heat, radar, ground | fired by the others: heat, radar, ground | mean length | over 10 / 30 min |
|---|---|---|---|---|---|---|---|---|---|---|
| af40 | 112 | 66 / 46 / 0 | 59 % | 259 / 244 | 16 | 410, 116, 4 | 65 %, 64 %, 75 % | 2297, 0, 10 | 8.5 min | 34 / 3 % |
| lf27 | 112 | 62 / 50 / 0 | 55 % | 203 / 265 | 37 | 343, 103, 3 | 63 %, 51 %, 67 % | 2348, 0, 9 | 8.8 min | 30 / 3 % |
| ecf90 | 112 | 62 / 50 / 0 | 55 % | 233 / 263 | 25 | 376, 118, 1 | 65 %, 58 %, 100 % | 2418, 0, 15 | 9.3 min | 33 / 4 % |
| ja46 | 112 | 58 / 54 / 0 | 52 % | 204 / 278 | 25 | 326, 100, 1 | 63 %, 65 %, 100 % | 2319, 0, 20 | 8.4 min | 30 / 3 % |
| sy26 | 112 | 64 / 48 / 0 | 57 % | 255 / 264 | 15 | 464, 111, 4 | 61 %, 55 %, 100 % | 2370, 0, 17 | 8.4 min | 35 / 3 % |
| vk33 | 112 | 61 / 51 / 0 | 54 % | 196 / 260 | 26 | 327, 104, 0 | 60 %, 60 %, - | 2357, 0, 15 | 8.4 min | 32 / 4 % |
| ks55 | 112 | 67 / 44 / 1 | 60 % | 242 / 234 | 5 | 407, 115, 1 | 61 %, 63 %, 100 % | 2283, 0, 15 | 7.8 min | 31 / 2 % |
| nf24 | 112 | 65 / 47 / 0 | 58 % | 216 / 242 | 33 | 345, 100, 0 | 64 %, 65 %, - | 2368, 0, 21 | 8.8 min | 31 / 4 % |
| dr7 | 112 | 99 / 13 / 0 | 88 % | 285 / 159 | 1 | 513, 0, 0 | 67 %, -, - | 180, 99, 0 | 4.1 min | 10 / 1 % |

Gun, every jet in these matches: 25911 rounds, 1093 hit a jet (4 %). CPU loadouts chosen (heat/radar/ground x jets): 6/0/1 x2484, 6/0/0 x124, 6/2/0 x104.
Match length over all aircraft: mean 8.1 min, over 10 min 30 %, over 30 min 3 %.

Fairness: the jet under test flies each hangar loadout (as a player would pick it) against CPU pilots on their own picks; the eight jets together (not the drone):

| loadout | matches | won / lost | win | kills / deaths | hit: heat, radar, ground |
|---|---|---|---|---|---|
| its own pick | 192 | 115 / 76 | 60 % | 327 / 375 (0.87) | 61 %, 63 %, 100 % |
| heat | 192 | 108 / 84 | 56 % | 403 / 455 (0.89) | 61 %, -, - |
| radar | 176 | 97 / 79 | 55 % | 404 / 434 (0.93) | 67 %, 56 %, - |
| air-to-ground | 168 | 100 / 68 | 60 % | 378 / 367 (1.03) | 66 %, -, 67 % |
| mixed | 168 | 85 / 83 | 51 % | 296 / 419 (0.71) | 57 %, 65 %, - |

## Normal

| jet | matches | won / lost / drawn | win | kills / deaths | crashed | fired by the jet: heat, radar, ground | hit: heat, radar, ground | fired by the others: heat, radar, ground | mean length | over 10 / 30 min |
|---|---|---|---|---|---|---|---|---|---|---|
| af40 | 112 | 62 / 49 / 1 | 56 % | 296 / 247 | 8 | 400, 128, 2 | 67 %, 62 %, 100 % | 2383, 0, 10 | 6.6 min | 25 / 4 % |
| lf27 | 112 | 65 / 46 / 1 | 59 % | 267 / 254 | 13 | 384, 115, 0 | 66 %, 63 %, - | 2512, 0, 11 | 7.3 min | 30 / 4 % |
| ecf90 | 112 | 64 / 44 / 4 | 59 % | 311 / 259 | 6 | 455, 137, 0 | 65 %, 58 %, - | 2453, 0, 9 | 6.3 min | 25 / 2 % |
| ja46 | 112 | 64 / 47 / 1 | 58 % | 283 / 271 | 17 | 373, 132, 1 | 71 %, 65 %, 100 % | 2570, 0, 11 | 7.7 min | 32 / 4 % |
| sy26 | 112 | 58 / 50 / 4 | 54 % | 240 / 256 | 10 | 320, 107, 0 | 69 %, 64 %, - | 2591, 0, 5 | 7.4 min | 27 / 4 % |
| vk33 | 112 | 55 / 52 / 5 | 51 % | 216 / 208 | 11 | 376, 89, 1 | 64 %, 55 %, 100 % | 2639, 0, 9 | 7.2 min | 27 / 1 % |
| ks55 | 112 | 63 / 45 / 4 | 58 % | 314 / 263 | 6 | 399, 140, 0 | 72 %, 57 %, - | 2412, 0, 13 | 5.3 min | 17 / 0 % |
| nf24 | 112 | 53 / 57 / 2 | 48 % | 278 / 279 | 10 | 342, 131, 0 | 68 %, 68 %, - | 2347, 0, 12 | 5.1 min | 15 / 0 % |
| dr7 | 112 | 100 / 11 / 1 | 90 % | 298 / 149 | 0 | 444, 0, 0 | 75 %, -, - | 140, 103, 0 | 2.3 min | 4 / 0 % |

Gun, every jet in these matches: 11721 rounds, 1027 hit a jet (9 %). CPU loadouts chosen (heat/radar/ground x jets): 6/0/1 x2484, 6/0/0 x124, 6/2/0 x104.
Match length over all aircraft: mean 6.1 min, over 10 min 22 %, over 30 min 2 %.

Fairness: the jet under test flies each hangar loadout (as a player would pick it) against CPU pilots on their own picks; the eight jets together (not the drone):

| loadout | matches | won / lost | win | kills / deaths | hit: heat, radar, ground |
|---|---|---|---|---|---|
| its own pick | 192 | 114 / 67 | 63 % | 487 / 385 (1.26) | 81 %, 75 %, 100 % |
| heat | 192 | 104 / 84 | 55 % | 476 / 428 (1.11) | 69 %, -, - |
| radar | 176 | 74 / 102 | 42 % | 377 / 437 (0.86) | 52 %, 43 %, - |
| air-to-ground | 168 | 101 / 65 | 61 % | 441 / 352 (1.25) | 66 %, -, 100 % |
| mixed | 168 | 91 / 72 | 56 % | 424 / 435 (0.97) | 65 %, 79 %, - |

## Hard

| jet | matches | won / lost / drawn | win | kills / deaths | crashed | fired by the jet: heat, radar, ground | hit: heat, radar, ground | fired by the others: heat, radar, ground | mean length | over 10 / 30 min |
|---|---|---|---|---|---|---|---|---|---|---|
| af40 | 112 | 66 / 46 / 0 | 59 % | 208 / 213 | 18 | 574, 137, 5 | 32 %, 50 %, 80 % | 2640, 129, 26 | 16.8 min | 75 / 18 % |
| lf27 | 112 | 47 / 65 / 0 | 42 % | 97 / 260 | 45 | 425, 12, 7 | 29 %, 75 %, 71 % | 2731, 118, 22 | 18.7 min | 78 / 28 % |
| ecf90 | 112 | 69 / 43 / 0 | 62 % | 185 / 221 | 31 | 552, 27, 6 | 38 %, 52 %, 83 % | 2448, 54, 14 | 16.5 min | 72 / 20 % |
| ja46 | 112 | 41 / 71 / 0 | 37 % | 63 / 267 | 68 | 377, 14, 4 | 26 %, 36 %, 50 % | 2762, 107, 21 | 18.7 min | 74 / 29 % |
| sy26 | 112 | 59 / 53 / 0 | 53 % | 138 / 231 | 26 | 537, 59, 5 | 29 %, 51 %, 80 % | 2904, 93, 14 | 18.9 min | 77 / 26 % |
| vk33 | 112 | 71 / 41 / 0 | 63 % | 131 / 195 | 46 | 520, 23, 6 | 35 %, 48 %, 67 % | 2727, 74, 20 | 20.1 min | 77 / 31 % |
| ks55 | 112 | 62 / 50 / 0 | 55 % | 192 / 238 | 25 | 570, 18, 7 | 38 %, 67 %, 71 % | 2451, 102, 12 | 15.8 min | 65 / 20 % |
| nf24 | 112 | 69 / 43 / 0 | 62 % | 301 / 221 | 27 | 748, 144, 5 | 38 %, 51 %, 80 % | 2470, 100, 19 | 14.1 min | 66 / 17 % |
| dr7 | 112 | 103 / 9 / 0 | 92 % | 285 / 62 | 0 | 466, 0, 0 | 63 %, -, - | 97, 38, 0 | 4.2 min | 4 / 0 % |

Gun, every jet in these matches: 12806 rounds, 2535 hit a jet (20 %). CPU loadouts chosen (heat/radar/ground x jets): 6/0/1 x1606, 4/2/1 x878, 6/0/0 x124, 4/4/0 x91, 6/2/0 x13.
Match length over all aircraft: mean 16.0 min, over 10 min 65 %, over 30 min 21 %.

Fairness: the jet under test flies each hangar loadout (as a player would pick it) against CPU pilots on their own picks; the eight jets together (not the drone):

| loadout | matches | won / lost | win | kills / deaths | hit: heat, radar, ground |
|---|---|---|---|---|---|
| its own pick | 192 | 96 / 96 | 50 % | 246 / 336 (0.73) | 30 %, 59 %, 74 % |
| heat | 192 | 101 / 91 | 53 % | 286 / 420 (0.68) | 34 %, -, - |
| radar | 176 | 104 / 72 | 59 % | 284 / 385 (0.74) | 29 %, 48 %, - |
| air-to-ground | 168 | 92 / 76 | 55 % | 256 / 336 (0.76) | 39 %, -, 73 % |
| mixed | 168 | 91 / 77 | 54 % | 243 / 369 (0.66) | 39 %, 55 %, - |

# After the radar rule (the next commit)

Easy and normal CPU pilots now carry radar missiles to strike missions (easy 5/1/1, normal 4/2/1; a jet without a gun
keeps 6/0/1) and shoot them with less discipline than an ace: the radar missile first in a chase from 1.5 km (easy) or
1.8 km (normal), shots out to 1.25 (easy) or 1.0 (normal) of the missile's flight time where an ace waits for 0.55, on a
lock held 1.5 s or 1.0 s (an ace 0.6 s). Hard is unchanged, its matches identical. The same audit again:

## Easy

| jet | matches | won / lost / drawn | win | kills / deaths | crashed | fired by the jet: heat, radar, ground | hit: heat, radar, ground | fired by the others: heat, radar, ground | mean length | over 10 / 30 min |
|---|---|---|---|---|---|---|---|---|---|---|
| af40 | 112 | 67 / 44 / 1 | 60 % | 258 / 237 | 11 | 469, 72, 4 | 66 %, 43 %, 75 % | 2060, 134, 8 | 8.1 min | 30 / 3 % |
| lf27 | 112 | 64 / 48 / 0 | 57 % | 222 / 260 | 27 | 411, 60, 4 | 64 %, 42 %, 75 % | 2106, 146, 8 | 8.1 min | 28 / 2 % |
| ecf90 | 112 | 66 / 46 / 0 | 59 % | 240 / 246 | 26 | 457, 65, 1 | 65 %, 37 %, 100 % | 2127, 124, 11 | 9.2 min | 34 / 3 % |
| ja46 | 112 | 61 / 51 / 0 | 54 % | 203 / 268 | 29 | 395, 60, 1 | 63 %, 37 %, 100 % | 2124, 125, 22 | 8.8 min | 30 / 4 % |
| sy26 | 112 | 66 / 46 / 0 | 59 % | 260 / 248 | 18 | 506, 63, 3 | 64 %, 43 %, 100 % | 2101, 140, 17 | 8.5 min | 33 / 4 % |
| vk33 | 112 | 53 / 59 / 0 | 47 % | 207 / 265 | 27 | 394, 57, 1 | 62 %, 47 %, 100 % | 2095, 114, 13 | 8.5 min | 34 / 4 % |
| ks55 | 112 | 68 / 43 / 1 | 61 % | 250 / 251 | 9 | 477, 69, 1 | 62 %, 51 %, 100 % | 2095, 106, 16 | 8.2 min | 33 / 2 % |
| nf24 | 112 | 66 / 46 / 0 | 59 % | 219 / 253 | 32 | 383, 72, 0 | 66 %, 53 %, - | 2170, 134, 19 | 8.8 min | 32 / 3 % |
| dr7 | 112 | 99 / 13 / 0 | 88 % | 284 / 158 | 1 | 510, 0, 0 | 67 %, -, - | 214, 66, 0 | 4.0 min | 9 / 1 % |

Gun, every jet in these matches: 25786 rounds, 1139 hit a jet (4 %). CPU loadouts chosen (heat/radar/ground x jets): 6/0/1 x1606, 5/1/1 x878, 6/0/0 x124, 6/2/0 x104.
Match length over all aircraft: mean 8.0 min, over 10 min 29 %, over 30 min 3 %.

Fairness: the jet under test flies each hangar loadout (as a player would pick it) against CPU pilots on their own picks; the eight jets together (not the drone):

| loadout | matches | won / lost | win | kills / deaths | hit: heat, radar, ground |
|---|---|---|---|---|---|
| its own pick | 192 | 108 / 83 | 57 % | 338 / 390 (0.87) | 65 %, 36 %, 100 % |
| heat | 192 | 113 / 79 | 59 % | 394 / 442 (0.89) | 62 %, -, - |
| radar | 176 | 97 / 79 | 55 % | 422 / 419 (1.01) | 65 %, 46 %, - |
| air-to-ground | 168 | 99 / 69 | 59 % | 365 / 368 (0.99) | 65 %, -, 67 % |
| mixed | 168 | 94 / 73 | 56 % | 340 / 409 (0.83) | 64 %, 48 %, - |

## Normal

| jet | matches | won / lost / drawn | win | kills / deaths | crashed | fired by the jet: heat, radar, ground | hit: heat, radar, ground | fired by the others: heat, radar, ground | mean length | over 10 / 30 min |
|---|---|---|---|---|---|---|---|---|---|---|
| af40 | 112 | 64 / 46 / 2 | 58 % | 305 / 249 | 6 | 456, 63, 0 | 71 %, 48 %, - | 2077, 103, 9 | 6.0 min | 21 / 2 % |
| lf27 | 112 | 76 / 34 / 2 | 69 % | 284 / 251 | 10 | 443, 56, 0 | 73 %, 39 %, - | 2250, 108, 12 | 7.6 min | 33 / 5 % |
| ecf90 | 112 | 61 / 46 / 5 | 57 % | 332 / 253 | 7 | 503, 59, 0 | 73 %, 51 %, - | 2057, 94, 11 | 5.8 min | 21 / 2 % |
| ja46 | 112 | 71 / 40 / 1 | 64 % | 301 / 277 | 14 | 457, 39, 0 | 77 %, 41 %, - | 2202, 91, 10 | 7.1 min | 29 / 3 % |
| sy26 | 112 | 64 / 43 / 5 | 60 % | 267 / 259 | 10 | 383, 47, 0 | 77 %, 40 %, - | 2228, 102, 7 | 7.0 min | 28 / 3 % |
| vk33 | 112 | 51 / 56 / 5 | 48 % | 234 / 227 | 10 | 417, 51, 1 | 69 %, 25 %, 100 % | 2281, 118, 9 | 6.9 min | 26 / 4 % |
| ks55 | 112 | 71 / 36 / 5 | 66 % | 352 / 245 | 2 | 488, 67, 0 | 76 %, 52 %, - | 2008, 97, 13 | 5.1 min | 12 / 0 % |
| nf24 | 112 | 56 / 54 / 2 | 51 % | 315 / 276 | 10 | 441, 50, 0 | 75 %, 56 %, - | 2032, 89, 10 | 4.8 min | 12 / 0 % |
| dr7 | 112 | 98 / 13 / 1 | 88 % | 297 / 148 | 0 | 445, 0, 0 | 74 %, -, - | 176, 67, 0 | 2.4 min | 4 / 0 % |

Gun, every jet in these matches: 11591 rounds, 1083 hit a jet (9 %). CPU loadouts chosen (heat/radar/ground x jets): 6/0/1 x1606, 4/2/1 x878, 6/0/0 x124, 6/2/0 x104.
Match length over all aircraft: mean 5.9 min, over 10 min 21 %, over 30 min 2 %.

Fairness: the jet under test flies each hangar loadout (as a player would pick it) against CPU pilots on their own picks; the eight jets together (not the drone):

| loadout | matches | won / lost | win | kills / deaths | hit: heat, radar, ground |
|---|---|---|---|---|---|
| its own pick | 192 | 115 / 64 | 64 % | 499 / 394 (1.27) | 85 %, 51 %, 100 % |
| heat | 192 | 114 / 75 | 60 % | 485 / 431 (1.13) | 69 %, -, - |
| radar | 176 | 93 / 81 | 53 % | 514 / 422 (1.22) | 73 %, 41 %, - |
| air-to-ground | 168 | 95 / 71 | 57 % | 433 / 358 (1.21) | 66 %, -, - |
| mixed | 168 | 97 / 64 | 60 % | 459 / 432 (1.06) | 88 %, 47 %, - |


# Aces: weaker flares, the gun, the bingo figure and no tanker chase (2026-11-02)

Hard only; easy and normal are unchanged, bit for bit (their audits' digests the same as before). Each change measured
on its own (hard, 1008 matches each), then together:

| hard | mean length | over 30 min | heat-seekers hit | crashes | dry tanks before sudden death | wins (8 jets) | drone |
|---|---|---|---|---|---|---|---|
| before | 17.4 min | 23.6 % | 33 % | 842 | 460 | 37-63 % | 92 % |
| the gun rule alone (then also against the drone) | 17.8 min | 23.1 % | 35 % | 951 | 527 | 46-61 % | 99 % |
| flares at 0.35, every 1.2 s, alone | 12.2 min | 10.1 % | 53 % | 477 | 299 | 41-77 % | 86 % |
| the bingo figure without its 50 % cap | 21.5 min | 38.1 % | 36 % | 1359 | 466 | 39-63 % | 93 % |
| no tanker chase (realistic) alone | 17.6 min | 24.0 % | 33 % | 800 | 398 | 38-65 % | 91 % |
| the reserve 1.0 in arcade alone | 14.7 min | 13.1 % | 36 % | 714 | 476 | 37-61 % | 92 % |
| all of it (as committed) | 8.9 min | 2.2 % | 53 % | 330 | 247 | 42-63 % | 88 % |

- At the old 0.9 and 0.7 s an ace's flares beat two heat-seekers in three; at 0.35 and 1.2 s (chaff unchanged) half
  get through. Alone this raised the NF-24 to 75-77 % wins; with the gun rule (an ace takes the close fight to the
  gun, never against the drone) every jet wins 42-63 %.
- The bingo figure's cap was not what ran jets dry: without it an ace went home at the 80 % ceiling every time and
  matches grew longer. Of the realistic dry tanks, four in ten were jets still behind their own tanker's drogue, in
  afterburner to catch it; an ace in realistic handling goes home instead, and flies to the bingo figure itself (no
  reserve on top, which sent aces home half the time).
- Still short of the 6-minute aim: 8.9 minutes, the time going home and back the rest.

| jet | win | kills / deaths | crashed | mean length | over 10 / 30 min |
|---|---|---|---|---|---|
| af40 | 55 % | 222 / 216 | 6 | 8.5 min | 29 / 2 % |
| lf27 | 53 % | 164 / 235 | 27 | 9.3 min | 31 / 3 % |
| ecf90 | 52 % | 159 / 239 | 13 | 8.6 min | 29 / 1 % |
| ja46 | 42 % | 112 / 268 | 39 | 9.6 min | 32 / 4 % |
| sy26 | 59 % | 199 / 244 | 13 | 9.0 min | 31 / 2 % |
| vk33 | 55 % | 128 / 198 | 16 | 9.9 min | 36 / 3 % |
| ks55 | 60 % | 189 / 227 | 6 | 9.4 min | 32 / 3 % |
| nf24 | 63 % | 267 / 231 | 20 | 7.2 min | 24 / 0 % |
| dr7 | 88 % | 288 / 77 | 0 | 3.1 min | 3 / 0 % |

The certificate's audit (every level): mean 9.6 -> 7.2 min, over 30 minutes 8.1 -> 2.3 %; crashes 526 -> 295 in 1008
matches, dry tanks before sudden death 324 -> 216. Its digest is now 24d92200 (tests/golden/audit.sha256).
