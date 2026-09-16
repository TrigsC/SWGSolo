# Live verification — F_0.9.6 + combat telemetry + route avoidance + novice clone-wound fix

**Run ID**: `20260914-205211-p10g-novice-early-game`
**Plans**: `docs/1-plans/F_0.9.6_p10g-novice-early-game-loop.plan.md`,
`docs/1-plans/F_0.9.6-D1_sim-combat-observability.plan.md`
**Branch**: `feat/p10g-novice-early-game-loop` (uncommitted, 16 modified + 8 untracked files, +5018/−182 excluding engine3)
**Supersedes**: `live_p10g-novice-early-game_20260914-173645-p10g-novice-early-game.md`
**Verdict**: **LIVE_VERIFICATION_FAIL**. No receipt was recorded.
**Superseded by**: `live_p10g-novice-early-game_20260914-214308-p10g-novice-early-game.md` (Recover tick-stall fix verified live; new blocker is target availability)

## Summary

This run verifies everything added since 173645:
- default-off combat telemetry with killing-blow capture;
- death-site anchor avoidance;
- the no-attacker flee;
- the novice clone-wound cap (`cloneWoundMaxFraction` 0.10, rest-first begin, `capEarlyGameWounds`).

The owner-approved diagnostic run `20260914-184412` had shown the 173645 route deaths were bodies spawning at 1/1/1 HAM.

**Runs A–C pass in full:**
- Run A: 59 + restart probe, then 60/60.
- Run B: guard gate-off.
- Run C: D1 trace, kill-XP trace and guard gate-on.

No crash, hang or REST error in any boot.

**Run D confirms the fixes that were targeted:**
- **HAM:** the novice spawns at 0.90 of max HAM (the 10% cap), not 1.
- **Self-defence:** when a giant worrt (level 13, above the level ceiling) attacked, the novice chose `flee`: flees +1, defensiveFlees +1.
- **Death capture:** the killing blow was recorded with template, level, distance and damage.
- **Route avoidance:** the death site was stored and the next anchor avoided it (deathZoneRejects 1).

**Run D fails on a new defect that blocks the whole loop: the early-game active tick stops after any Recover walk starts.**
- `beginEarlyGameRecovery` issues `moveTo(recoveryPoint)` without scheduling the next active tick, relying on the arrival or path-failure callback to resume the loop.
- The worrt kept attacking and the walk stalled short of its flee point, so no callback ever came.
- The combat tick counter froze at the flee tick and self-defence never ran again. The novice stood still until killed 35 s later.
- The post-kill path (`beginEarlyGameRecovery(false)`) has the same gap, so a flee or a kill freezes the loop every time.

Run D was stopped at ~460 s: the defect is deterministic and verification mode does not edit implementation.

## Static gate

| Check | Result |
|---|---|
| Incremental build, `-Werror` | PASS: no-op, binary 2026-09-14 20:37:22, md5 `c01015ea…` |
| `luac -p` (shipped config + v5 A/B/C/D/Dprobe) | PASS |
| GoogleTest | N/A: no suite covers these files |
| Codex code review | APPROVED: telemetry + route fix (2 rounds), clone-wound fix (2 rounds) |

## Restarts

Six guarded `server-cycle.sh` restarts (A phase A, A phase B, B, C, D, restore). Each was ready in 8–12 s with 0 connected players. The Sep 5 zombie core3 (pid 2114320) was ignored.

## Assertions

| # | Assertion | Expected | Actual | Result |
|---|---|---|---|---|
| U1 | Clean startup | fresh who.json | every boot, 8–12 s | PASS |
| U2 | One live core3 per boot | 1 non-zombie | 700088 / 700747 / B / C / 702320 / 703099 | PASS |
| U3 | No crash markers in gdb.log delta | 0 | 0 (gdb.log unchanged at 961371) | PASS |
| U4 | No hang: every dashboard poll 200 | 0 non-200 | 0 across all watchers and the Run D sampler (78 samples, 0 errors) | PASS |
| U5 | No `RESTServer ERROR` in core3_api.log delta | 0 | 0 | PASS |
| A1 | Phase A 59 PASS + restart probe AWAITING, 0 FAIL | — | exactly that (14/14 polls 200) | PASS |
| A2 | Phase B 60/60 | 60/60 | 60/60; `retained_record_survives_restart` PASS (668 ms) | PASS |
| A3 | Six F_0.9.6 scenarios | PASS | all PASS | PASS |
| A4 | Both real-kill scenarios | PASS | unarmed 30.9 s, rifle 3.9 s | PASS |
| A5 | `novice_box_precondition_refuses_body` | PASS | PASS | PASS |
| A6 | orphanRecords / rosterWithoutRecord | 0 / 0 | boot 1 snapshot **1** / 0 (harness identity 341, see note); boot 2 final 0 / 0, records 21 = pre-run, 341 absent | PASS (noted deviation) |
| A7 | D1 logging off: simcombat.log untouched | size + mtime unchanged | 1408460 / 1789408233 across both boots | PASS |
| B1 | Guard gate-off refusal | `precondition_gate_off_awardKillXp` | exactly that | PASS |
| B2 | No step executed | durationMs 0, killXp.kills 0 | 0 ms, kills 0, awards 0 | PASS |
| C1 | Both kill scenarios with trace on | PASS | PASS (28.7 s / 3.8 s) | PASS |
| C2 | Guard gate-on refusal | `precondition_gate_on_trainingEnabled`, 0 ms | exactly that | PASS |
| C3 | One wall-clock run id | 1 | `1789419867147` | PASS |
| C4 | Trace lines key=value | 0 malformed | 0 / 173 | PASS |
| C5 | Attack enqueue per kill bot | ≥ 1 `enqueued` | unarmed bot 6, rifle bot 2 | PASS |
| C6 | Kill-XP trace per kill | `event_queued`, `reason=included`, both grants `granted=1` | both kills: 85 + 8 each | PASS |
| C7 | Dashboard while traced | writeErrors 0, HTTP 200 | 0, 8/8 polls 200 | PASS |
| C8 | Allowlist empties after cleanup | tracedCount 0 | 0 | PASS |
| D1 | Novice enters grind | ≤ 360 s | identity 84 at 100 s | PASS |
| D2 | LeaveCity → SelectTarget → Engage | ≤ 20 min | LeaveCity → SelectTarget → **Recover (flee) → stall → death**; no Engage | **FAIL** |
| D3 | Body displacement | ≥ 300 m | 429 m | PASS |
| D4 | TTK window on Engage | 20–900 s | no Engage sample | **FAIL** |
| D5 | Real kill + XP via kill path | ≥ 1 within 40 min | 0 kills, 0 XP, 0 awards (run stopped at ~460 s) | **FAIL** |
| D6 | Recover after kill | observed | no kill | **FAIL** |
| D7 | Train once XP threshold met | ≤ 120 s | threshold never reached (1000 XP needed) | NOT REACHED |
| D8 | Demand independence | hunts Δ0, signalUnits 0 | hunts Δ0, signalUnits {0} | PASS |
| D9 | Weapon xpType == next box xpType on live bodies | 0 mismatches | 27 live samples, 0 mismatches (51 corpse samples excluded) | PASS |
| D10 | No credit mutation | unchanged | (0, 0) throughout | PASS |
| D11 | Death → validated cloner respawn | deaths ≥ 1, respawns ≥ 1, new body, same planet | natural death at ~253 s, respawn: body `…305292` → `…306871`, tatooine | PASS (natural death) |
| D12 | ≤ 1 identity minted | roster Δ ≤ 1 | 21 → 21 | PASS |
| D13 | Unprovoked attack → self-defence | decision allowed + counters rise, whenever attacked | first hit: decision `flee`, defensiveFlees +1. **Then attacked continuously for ~35 s with no further defence** (tick stopped) | **FAIL** |
| D14 | Novice HAM healthy | min pool ≥ 0.05 after 60 s; ≥ 0.70 within 10 min | both bodies spawn at 0.90; worst sample 0.596 after the worrt's first hit | PASS |
| D15 | Every death has a captured cause + zone | 0 missing / uncaptured | 1 death: `giant_worrt.iff` lvl 13, 6 m, 589 dmg; zones 1 | PASS |
| D16 | No Recover livelock | no span > 900 s without HAM rising | no span > 900 s (the stalled Recover ended in death after ~35 s) | PASS |

**Required assertions: 38. Passed: 32. Failed: 5 (D2, D4, D5, D6, D13). Not reached: 1 (D7).**

A6 note: `orphanRecords` is a cached count, recomputed by `reconcilePlayerBotProgression` after each flush.
- Boot 1's final snapshot caught that reconcile mid harness cleanup. Harness identity 341 (bodies destroyed `scenario_destroy` ×2 and `harness_cleanup`) had already left the roster, but its record delete was still pending.
- Boot 2 loaded 22 records: 21 pre-run plus retained restart identity 342. So 341's record never persisted, and boot 2's final snapshot is 0 / 0 with 21 records.
- Store integrity holds; the strict boot-1 reading is recorded here for the owner.

## Failure — the early-game tick stops once a Recover walk starts (D2, D4–D6, D13)

Evidence: `…-D-samples.jsonl` (78 samples at 5 s), `…-D-core3.log`, `…-D-analysis.txt`.

| Time (from ready) | Sample |
|---|---|
| 100 s | Body `…305292` attached; HAM 366/406, 414, 446 (min pool 0.90) |
| 210–215 s | SelectTarget at (2991, −4648); combat ticks 111 → 112, tick age ~3 s |
| ~218 s | Giant worrt attacks: health 366 → 242. Self-defence: attacker above the level ceiling → decision `flee`, `beginEarlyGameRecovery(true)`; flees 1, defensiveFlees 1 |
| 220 s | Recover, walking away; combat ticks **113** |
| 225–250 s | Combat ticks **stay 113**; `lastTickAgeMs` 8 s → 33 s. The body stops at (3018, −4594) from 235 s, ~14 m short of the 80 m flee point |
| ~253 s | `SimPveEarlyGameDeath … attackerTemplate=object/mobile/giant_worrt.iff attackerLevel=13 attackerDistance=6 totalDamage=589 … lastFight={}` |

Code facts:
- `beginEarlyGameRecovery` moving branch: `state = IDLE; moveTo(recoveryPoint);` with no `scheduleActiveTick`. Only the already-at-rest-point branch schedules.
- `handleEarlyGameSelfDefense` returns true after the flee, and `runEarlyGameTick` returns without scheduling.
- The only resumes are `onArrived` → `handleEarlyGameArrived` → `scheduleActiveTick(100)`, and `onPathFailed` → `scheduleActiveTick(100)`.
- A walk stalled by the attacker produces neither callback, so the loop stays frozen: no self-defence, no re-flee, no rest check.
- Every other early-game branch that walks (LeaveCity, SelectTarget re-anchor, Train, Engage) schedules its own 1 s tick. Recover is the exception.
- The post-kill path `beginEarlyGameRecovery(false)` shares the gap, so a successful kill would also stop the loop before Rest → Train.

Recommended TRIP-2 fix:
1. Schedule the next active tick (1 s) in `beginEarlyGameRecovery`'s moving branch, matching the other walking branches. The Recover branch already re-ticks every 1 s while `MOVING`.
2. In Recover, allow a re-flee when the recovery walk stops making progress while an ineligible attacker still hits. The current "never re-flee while `MOVING`" rule assumes the walk continues.
3. Re-run Run D: the kill → Recover → Train loop and the D11 probe.

Runs A–C need no rework unless the fix touches their code paths.

## Harness hygiene observation (non-blocking, carried over)

Identity 274, a leftover harness brawler from an earlier hung run, still has a grind row with no body.

## Evidence

All under `/home/swgemu/workspace/Core3/MMOCoreORB/bin/log/trip-verify-20260914-205211-p10g-novice-early-game*`:
- `.contract.md`
- `-A-phaseA.json`, `-A-phaseB.json`
- `-B.json`
- `-C.json`, `-C-simcombat.log`
- `-D-samples.jsonl`, `-D-core3.log`, `-D-analysis.txt`

Diagnostic run that found the clone-wound cause: `bin/log/trip-diag-20260914-184412-p10g-combat-diag-{samples.jsonl,core3.log}`.

## Cleanup confirmation

- `sim_player_manager.lua` restored to md5 `b4a48275af85729749b3f2ad4ebe4d18` in the container and the worktree.
- After restart (pid 703099) the harness, early-game mode, `combatTelemetry`, `combatDiag.logging` and progression all read false; tracedCount is 0.
- Sampler and both monitors stopped.
- No database, roster or unrelated log was cleared. Identity 84's progression is unchanged (0 XP, 0 credits); its roster deaths +1 are real.

## Verdict

**LIVE_VERIFICATION_FAIL** (D2, D4, D5, D6, D13). Return to `TRIP-2-implement`:
1. Add the missing Recover active tick.
2. Add the stalled-flee re-flee.
3. Re-run Run D.
