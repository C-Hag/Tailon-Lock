# The AI pilots' checklist

The hands-on checklist (`TEST_GUIDE.md` section 5) is for a person at the controls. This one is for the CPU
pilots. It is ticked once per review round by whoever reads the round's reports (a person or a Claude thread),
on the commit under test. A box is ticked only from evidence in the logs (the audit's report line, a death line,
a still), never from memory. Anything not ticked is a finding: it gets a line in the round's record, an owner and
a fix, and the round is run again.

## How a round runs

```
A4C_AUDIT_DEATHS=1 TailonLock --audit --out audit.log                         # data sheets, systems, 1008 matches
TailonLock --audit --part views --shots views/ --out views.log               # the pictures
tools/playtest solo --games 56 --per-run 1 --jobs N                          # every mode with every jet, drawn
tools/playtest local --games 40 --per-pair 4 --ways address,relay,bad-address,bad-relay --jobs N
tools/certify digest audit.log                                               # the same bits as every system
```

`--audit --part sorties --only ID --from K --sorties 1` replays one match exactly; `A4C_AUDIT_TRACE=NAME` traces
one pilot every quarter second.

## 1. Getting airborne

- [ ] Every jet takes off from the catapult and from the runway, in arcade and realistic handling; no take-off
      run ends off the runway or in the sea
- [ ] After take-off the jet climbs away: no jet levels off below 150 m or sinks back
- [ ] No jet stalls in its first minute of flight

## 2. Flying

- [ ] No crash into the ground that was not caused by being hit (`CRASHED` deaths with no attacker within 6 s): at
      most 1 in 50 matches
- [ ] At the map's edge the jet turns back without flying into the ground or leaving the arena
- [ ] Over hills the terrain avoidance clears every ridge; in realistic handling too
- [ ] No pilot pulls more g than its jet allows; G-LOC only where the rules have G effects on, and rare
- [ ] No jet circles in one place for more than 3 minutes without a reason (an enemy near, landing, the tanker)
- [ ] No stall that lasts more than 10 s; every stall is recovered without a crash

## 3. Fuel, landing and rearming

- [ ] A jet low on fuel or out of weapons heads home in time: no flameout on the way home
- [ ] It lands: touches down on the runway (or deck), stops on it, refuels and rearms, and takes off again
- [ ] No touch-and-go loops: a landing attempt that fails comes round once and lands the next time
- [ ] At the tanker: joins, refuels and leaves without hitting it
- [ ] After the 30-minute sudden death (tanker gone, no refuelling) the pilots fight on and do not fly home to refuel

## 4. Fighting

- [ ] Pilots lock their target and fire missiles from inside their range; guns only when close and on target
- [ ] Decoys: flares and chaff go out against a missile on the way, not at random
- [ ] No friendly fire and no locks on a wingman
- [ ] Two jets in a dogfight do not orbit each other for minutes without a shot: every long fight ends
- [ ] Each jet wins between 30 % and 70 % of its matches against the others (the audit's won/lost line); the
      drone (dr7) is allowed to win more, as its description promises, but not every match
- [ ] Hard pilots beat easy ones more often than not

## 5. Matches

- [ ] Every match ends: no timeout in the audit, no unfinished match in the play tests
- [ ] Matches are short: the audit's length lines average 7.25 minutes at most, and at most 5 % of the matches reach
      the 30-minute sudden death (the share over 10 minutes is reported). (It was 6.5; the owner, 2026-11-03: "6.5 to
      7 is okay", when the CPU's landing rework made fewer jets lost on the way home and the matches longer; then
      2026-11-04: "Yes 7.25", when the runway rule (no landing on a busy runway) took the mean to 7.21.)
- [ ] Every mode works with every jet: 1P, 2P, co-op, defend, defend1, drone, drone2
- [ ] Ground targets get attacked and destroyed in the modes that have them
- [ ] Defend mode: the carrier is defended and the waves come; the match ends either way
- [ ] The match's winner and reason match what happened (the last jet down, or the targets gone)

## 6. The same game everywhere

- [ ] The audit's digest matches `tests/golden/audit.sha256` (`tools/certify digest`) on this system
- [ ] Every network match agrees on both computers, through the bad-internet proxy too
- [ ] No physics fault (`A4C_CHECK`): no NaN, no jet under the ground, no speed or g out of bounds

## 7. What the pilot sees

- [ ] The views check passes: no holes, sun and horizon where they belong, no number cut, no giant cloud disc
- [ ] The instruments read the jet's own speed, height and heading
- [ ] The HUD shows the lock, low fuel, the g warning and PULL UP when they apply

## The round's record

Each round adds a block to the review log (kept with the round's logs, and summarised to the coordinator):

```
Round N, commit abcdef123456, system macos|linux|windows, date
  ticked: 1 2 3 4 5 6 7 (or the boxes left open)
  findings: what, where (map, jet, mode, seed, sortie), the log line, the owner (sim/AI or renderer/panels/tools)
  fixed: commit, and the case that proves it (the replayed sortie or match)
```

Rules for a fix: find the cause, never loosen a check to pass it unless the check itself is wrong (and say so in
the commit); one small commit per bug; replay the failing case, then run the whole round again from the start,
because a fix can break what passed. Changes to the sim or the AI keep every system computing the same bits:
the digest is retaken in the fix's commit and checked on Linux, Windows and macOS. The loop stops when a whole
round finds nothing new; then the release certificate runs on that commit.
