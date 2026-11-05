# Review fixes, 2026-11-02: tier 1

The fixes for the nine tier-1 findings of the second review (`review/challenge-results.md`), on branch
`claude/review-fixes` on top of `6ce8dc9`. Each fix is its own commit, with a regression test that fails on the
code before it and passes after.

## The audit digest

Measured the certify way: a Release build (`-DA4C_WERROR=ON`), then
`--audit --part sorties --sorties 112 --only <jet>` with `A4C_AUDIT_DEATHS=1` for each of the 9 aircraft, then
`tools/certify digest`, on macOS arm64.

| After | Digest | Why |
|---|---|---|
| #1-#5, #8, #9 (relay, net, sound, settings, saving) | `1c21bdaf` (the reference) | none of them touches the simulation |
| #6 (the players let go of every control on a pause) | `1c21bdaf`, unchanged | the audit never pauses: `game_let_go` is called only from `pause_game` |
| #7 (a refuelled jet goes to a free catapult, or waits) | `1c21bdaf` → `24a9fbf7c32db997dee903f5275b7b0ae049dd46c371e16603b7c75ab87c4ea6` | see below |

**Why #7 changes it.** In the old code a CPU jet that finished refuelling on the carrier's deck was put on the
starboard bow catapult even when another jet was sitting there, and the next tick's collision test killed both.
The audit's carrier-defence matches on IRON COAST hit exactly this: for example af40's log had, at t=690,
BANDIT 4 (`HOMEWARD`, refuel 1) and BANDIT 5 (0 kt, on the catapult) both dying in a MID-AIR COLLISION 3 m above
the deck at the same point. With the fix the refuelled jet takes the port catapult instead (or waits), so those
matches run on differently from that moment. Four of the nine logs differ (af40, lf27, sy26, ks55); the other
five are byte-for-byte the same in every line the digest reads. Across the nine logs the collision death lines
go from 986 to 976 and those at 0 kt (a jet standing on the catapult) from 13 to 7. Wins, losses and draws are
the same for every aircraft; lf27 has one crash fewer, sy26 one more shot down and one more crash with two
collisions fewer.

`tests/golden/audit.sha256` is not changed here: the new reference is taken once Linux and Windows give the same
digest.

## Tiers 2 and 3

The remaining 40 findings (every CONFIRMED and PARTLY one of tiers 2 and 3 except #43, already fixed in
`3945576`), and the network spin found by certify's block 7. The digest was taken the same way, all 9 aircraft
side by side.

| After | Digest | Why |
|---|---|---|
| every fix that does not mean to change a match (#10-#17, #19-#30, #32-#39, #41, #42, #44-#50, the spin) | `24a9fbf7`, unchanged | none touches a match the audit flies: the network, relay, platform, loader, editor, UI and drawing fixes don't run in it, #17 (16 targets) and #38 (the carrier the jet stands on) and #39 (north-south bases) are the same on the shipped maps, and the spin fix changes only when the real-time loop sleeps |
| #18 (the carrier's attackers' load) | `24a9fbf7` → `f66254d59bf07466bbd1cbceca839198b902648e91f12222a440a760253d4718` | the first wave of a carrier defence now flies the defence's load, not the strike table's (on hard, no radar missiles), and each wave comes with its whole load: six logs differ (af40, ecf90, sy26, vk33, ks55, nf24), every aircraft's wins, losses and draws are the same, deaths move by one here and there |
| #40 (the carrier's size; a solo defence ends with its last jet) | `f66254d5` → `4ea0b7702c52da2975c6fd8e96b7cb01edbef69bb2272acd40553b40fb5c29e7` | the shipped carriers are 320 x 86, so the footprint is the same; but 11 solo defences that flew on with every pilot out of lives and ended "held out" are now lost when the last jet goes down: lf27 61-50-1 → 56-55-1, ja46 50-62 → 48-64, sy26 60-51-1 → 57-54-1, nf24 48-64 → 47-65 |
| #31 (the CPU hands use fmath, not libm) | `4ea0b770`, unchanged on macOS | the hands' atan2f and acosf calls do not decide anything in the sorties on this Mac; on another system's libm they could have, which is what the fix is for |

The digest after all of tier 1, 2 and 3 is **`4ea0b7702c52da2975c6fd8e96b7cb01edbef69bb2272acd40553b40fb5c29e7`**
on macOS. Linux (the Linux VM, gcc 14.3, head `dd4c92c`) gives the same digest. `tests/golden/audit.sha256` is left
as it was, for the new reference to be taken when Windows gives the same too.

**#39, decided.** The fix keeps every air base's heading snapped to the nearer of north and south (logged when a
map's heading is moved) instead of making the runway logic work at any heading. The author chose on 2026-11-03 to
keep it that way: no change.

**The Linux build.** The Linux parts had not been compiled when the fixes were made. Building them with gcc and
clang -Werror found four errors, fixed in `dd4c92c`: `resync` (#19) was called before its declaration, the driver
name copy could cut a name short (now a name too long gives none), and three test stubs (`THEMES` in test_editor
and test_ui, `render_scene` in test_command) were incomplete. After it ctest passes 29 of 29 with both compilers.
test_plat_pads needs write access to /dev/uinput (root) and was not run.

**Pictures and songs.** The four reference pictures (menu, cockpit, title card, hangar) taken with the software
and the GPU renderer, and all 14 songs (60 s of each through --render-music), are byte for byte the same as from
the tier-1 build. Fixes that could change a picture in some other case: #47 (only when the software renderer's
24000-item alpha list overflows, which a counter showed never happens in the audit's 194 views or 4000 frames of
the menu demo) and #48 (the lost-controller pause now lights its QUIT button; not a reference picture).

## The certificate on 436bcb3, stopped

The macOS platform ladder on 436bcb3 (the code before these fixes), run 2, passed P0-P6 and blocks 1-7 of P7
(build, tests, gate, smoke, 56 offline matches, 40 network matches, all agreeing) and was stopped on 2026-11-03 at
07:20 in block 8 (24 solo games under ASan, 3 finished, no failure) by the author's choice: 436bcb3 still has the
network spin, so each game took a whole core and the block would have run most of a day. The ladder restarts from
P0 on the merged head that has these fixes. Its logs are kept in cert-logs/plat-436bcb3-run2.log and
cert-logs/plat-436bcb3-run2-p7/.
