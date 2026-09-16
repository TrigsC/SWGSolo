# Live verification — F_0.9.6 novice early-game loop (v7 contract)

**Run ID**: `20260915-185828-p10g-novice-early-game`

**Plans**:
- `docs/1-plans/F_0.9.6_p10g-novice-early-game-loop.plan.md`
- `docs/1-plans/F_0.9.6-D1_sim-combat-observability.plan.md`

**Branch**: `feat/p10g-novice-early-game-loop`, uncommitted: 16 modified files, +5588/−195 excluding engine3.

**Binary**: `8f04fe5c`, built 2026-09-15 17:48.

**Supersedes**: `live_p10g-novice-early-game_20260914-214308-p10g-novice-early-game.md`

**Verdict**: **LIVE_VERIFICATION_FAIL**. No receipt was recorded.

## Summary

Runs A, B and C pass in full (22/22). No boot had a crash, hang or REST error.

Run D fails, but not in the parts this chunk added. HAM, heal-on-death and the level band behave as designed:
- the novice spawned at 800 HAM in every pool;
- it engaged within 6 minutes;
- it chose kreetles at level 3 with TTK about 5 s.

It then **ping-pongs between two unreachable kreetles for 70 minutes**:
- Both kreetles sat off the Mos Eisley city navmesh the novice stood on, at (3081..3095, −5016..−5040).
- Each path attempt failed in the hybrid mover and set the Engage **single-slot** unreachable-target skip (`earlyGameSkipTargetOid`, 60 s).
- Skipping the second kreetle overwrote the skip on the first, so the next scan picked the first one again.
- The scan therefore never came back empty, and the novice never re-anchored.
- Result: 0 kills, 0 XP, and no spawner anchor in the window. The mover logged 15,313 exit-resolve failures (about 3.6/s).

The three quick checks earlier today did not meet this geometry. The 45-min check `20260915-175249` verified the full loop live: 17 kills, all paid, Train reached, `flee_other_attacker` ×4, spawner anchor. The defect is a real livelock the contract caught.

## Static gate

| Check | Result |
|---|---|
| Incremental build, `-Werror` | PASS: no-op (IDL step only), binary `8f04fe5c` unchanged |
| `luac -p`: shipped `2d149d79…` + v5 A/B/C/D/Ddiag/Dprobe | PASS |
| GoogleTest | N/A: no suite covers these files |
| Codex code review | APPROVED for all changes in scope (all-attackers round 2; kill-XP drop counters) |

## Restarts

Six guarded `server-cycle.sh` restarts, one each for A phase A, A phase B, B, C, D and the restore. Each was ready in 7–8 s with 0 connected players. The zombie core3 from Sep 5 (pid 2114320) was ignored.

| Boot | Config md5 | core3 pid | Ready |
|---|---|---|---|
| A phase A | `60bacd0e` | 779491 | 8 s |
| A phase B | `60bacd0e` | 780129 | 7 s |
| B | `51e4f84b` | 780653 | 8 s |
| C | `a10e7b8c` | 781143 | 8 s |
| D | `978e80a4` | 781685 | 7 s |
| restore | `2d149d79` | 786115 | 8 s |

## Contract changes before any Run D evaluation

The contract was copied into the evidence directory at run start. It was then clarified in two places and replaced (md5 `971b5c27`) during Run A, before any Run D sample existed:

- **D13:** the allowed decisions now include `engage_phase`. A 5 s sample can land after the Engage tick that recorded it.
- **D20(a):** the award window starts at max(kill − 30 s, previous kill). The award task can land before the controller counts the kill.

Both were found by dry-running the analyzer on the 45-min quick-check data. With them, that data scored 20/20.

## Assertions

| # | Assertion | Expected | Actual | Result |
|---|---|---|---|---|
| U1 | Clean startup | fresh who.json | every boot, 7–8 s | PASS |
| U2 | One live core3 per boot | 1 non-zombie | 779491 / 780129 / 780653 / 781143 / 781685 / 786115 | PASS |
| U3 | No crash markers | 0 | gdb.log unchanged at 961371; 0 crash markers in the core3.log delta | PASS |
| U4 | No hang: every dashboard poll 200 | 0 non-200 | A1 13/13, A2 7/7, B 6/6, C 8/8; Run D sampler 900 samples, 0 errors | PASS |
| U5 | No `RESTServer ERROR` in core3_api.log delta | 0 | 0 | PASS |
| A1 | Phase A: 59 PASS + restart probe AWAITING, 0 FAIL | — | exactly that; runId 1789498855007 | PASS |
| A2 | Phase B 60/60 | 60/60 | 60/60; `retained_record_survives_restart` PASS (705 ms) | PASS |
| A3 | Six F_0.9.6 scenarios | PASS | all PASS | PASS |
| A4 | Both real-kill scenarios | PASS | unarmed 32.6 s, rifle 3.8 s | PASS |
| A5 | `novice_box_precondition_refuses_body` | PASS | PASS (1003 ms) | PASS |
| A6 | orphanRecords / rosterWithoutRecord | 0 / 0 | boot 1: 0 / 0 (records 22); boot 2: 0 / 0 (records 21) | PASS |
| A7 | simcombat.log untouched while logging is off | size and mtime unchanged | 1513330 / 1789422994 across both boots | PASS |
| B1 | Guard gate-off refusal | `precondition_gate_off_awardKillXp` | exactly that | PASS |
| B2 | No step executed | durationMs 0, killXp.kills 0 | 0 ms, kills 0, awards 0, botKillsSeen 0 | PASS |
| C1 | Both kill scenarios, trace on | PASS | unarmed 26.9 s, rifle 4.1 s | PASS |
| C2 | Guard gate-on refusal | `precondition_gate_on_trainingEnabled`, 0 ms | exactly that | PASS |
| C3 | One wall-clock run id | 1 | `1789499450928` (199 lines) | PASS |
| C4 | Trace lines key=value | 0 malformed | 0 / 199 | PASS |
| C5 | Attack enqueue per kill bot | ≥ 1 `enqueued` | unarmed bot 5, rifle bot 1 | PASS |
| C6 | Kill-XP trace per kill | `event_queued`, `reason=included`, both grants `granted=1` | unarmed 85 + 8 (id 411); rifle 85 + 8 (id 412); plus 3 cone bystanders at 147 + 14 each; every grant `granted=1`. Drop counters agreed: botKillsSeen 5, kills 5, dropped 0 | PASS |
| C7 | Dashboard while traced | writeErrors 0, HTTP 200 | 0, 8/8 | PASS |
| C8 | Allowlist empties after cleanup | tracedCount 0 | 0 | PASS |
| D1 | Novice enters grind | ≤ 360 s | identity 84 at 106 s | PASS |
| D2 | LeaveCity → SelectTarget → Engage | ≤ 1500 s | first Engage at 341 s | PASS |
| D3 | Displacement | ≥ 300 m | 498 m | PASS |
| D4 | TTK window on self-selected Engage | 0–900 s | 751 samples, TTK 4.8–5.0 | PASS |
| D5 | Real kill + XP | ≥ 1 within 2400 s | 0 kills, 0 awards: the livelock below | **FAIL** |
| D6 | Recover after a kill; no frozen tick | kill → Recover; tick age < 10 s | tick healthy (max 3.7 s over 882 live samples), but no kill | **FAIL** |
| D7 | Train once threshold met | ≤ 120 s | never reached (4189 XP needed) | NOT REACHED |
| D8 | Demand independence | hunts Δ0, signalUnits 0 | Δ0, {0} | PASS |
| D9 | weaponXpType == nextXpType on live bodies | 0 mismatches | 882 live samples, 0 mismatches | PASS |
| D10 | No credit mutation | unchanged | (0, 0) | PASS |
| D11 | Death → respawn | deaths ≥ 1, respawns ≥ 1 | no natural death; the probe was not run because the verdict was already FAIL | NOT RUN |
| D12 | ≤ 1 identity minted | roster Δ ≤ 1 | 21 → 21 | PASS |
| D13 | Unprovoked attack → self-defence | allowed decision + counters | no sampled in-combat/no-target state and no logged non-Engage decision. Defence counters did rise by 12 (5 engagements, 7 flees), before the stall began | NOT EXERCISED |
| D14 | 800 HAM, heal-on-death | max 800 on all pools; first sample ≥ 0.95 | body `…242315` first sample 1.0; 0 samples with max ≠ 800 | PASS |
| D15 | Every death has a captured cause + zone | 0 missing | 0 deaths | PASS |
| D16 | No Recover livelock | no span > 900 s without HAM rising | 4 spans, longest 15 s | PASS |
| D17 | Spawner roaming | anchor on a spawn egg, reached | no spawner anchor: the novice never re-anchored after 341 s | **FAIL** |
| D18 | PACK filter | ≥ 1 target, ≥ 80% resolved, 0 PACK-only | 2 targets (both kreetle, HERD), 1 resolved (50%) | **FAIL** |
| D19 | All-attackers flee | ≥ 1 `flee_other_attacker` | 0: no fight took place | **FAIL** |
| D20 | Kill-XP accounting | every kill paid; 0 novice drops; lines ≈ counters | vacuous for the novice (0 kills). 0 novice drop lines; 1 drop line vs counter Δ 1 (non-roster bot); botKillsSeen +49 | PASS |

**Required assertions: 42. Passed: 34. Failed: 5 (D5, D6, D17, D18, D19). Not reached / not run / not exercised: 3 (D7, D11, D13).**

## Failure — Engage ping-pong between two unreachable targets

Evidence: `…-D-samples.jsonl` (900 samples at 5 s), `…-D-core3.log`, `…-D-analysis.txt`. Live dashboard state was captured before the restore.

Timeline for identity 84, body `…242315`:

1. **106–336 s: normal start.** Grind start, LeaveCity to ring anchor 1 and then anchor 2, several short Recover and flee cycles (defence +12), and one Train visit.
2. **341 s: first Engage.**
   - Target kreetle `…242722` (level 3, HERD, TTK 5.0) at (3081, −5016).
   - At 351 s the bot switched to kreetle `…242719`.
   - At that tick one `decision=engage` line was logged at 3 m (`inCombatMs=0`). Combat did not continue.
3. **351–4514 s: stall.** The novice alternated `…242722` ↔ `…242719` every 5–10 s.
   - It logged 440 Engage and 86 SelectTarget phase transitions.
   - Movement state was IDLE on 751 of 752 Engage samples.
   - The last move request was 4486 s old at capture, and the state age was 4192 s.
   - The novice's position drifted only 29 m in 70 minutes.
4. **Live state at capture:**
   - phase Engage, target `…242719`;
   - movement `leg=2 onMesh=1 targetOnSameMesh=0 exitResolved=0 exitResolveFailures=15313 navmeshTaskFailures=15325 probeEndPoly=0`;
   - `pathFailures` 1, anchor still ring anchor 3 (3417, −5282).

Mechanism, from code:
- The novice is on a city NavArea mesh and the kreetles are off it.
- The hybrid mover cannot resolve a navmesh exit toward them. `probeEndPoly=0` is the known off-poly exit failure.
- Each failure reaches `handleEarlyGamePathFailed`. In Engage it sets `earlyGameSkipTargetOid = targetOid` for 60 s and returns to SelectTarget (`SimHunterController.cpp:2927-2928`).
- The skip is a **single slot**. Failing target B overwrites the skip for target A, so the next scan (`:3944-3946`) is free to pick A again, and so on.
- Because a target is always found, the empty-scan rotation never runs and the novice never re-anchors to a spawner or the ring.

Side effect: about 3.6 navmesh exit-resolve task failures per second for 70 minutes. The server stayed healthy, but it is wasted pathfinder work.

This is a controller defect, not an ecology or configuration problem. Fix options, for the owner to decide:
1. **Bounded skip set.** Remember several unreachable targets (for example 8 oids, each with its own expiry) instead of one.
2. **Give up on the location.** After N consecutive unreachable targets from one position or anchor, count it as an empty scan and move on to the next spawner or ring anchor.
3. **Throttle the mover.** Limit repeated exit-resolve attempts in the hybrid mover. This is a separate cost fix.

## Evidence

All files are under `/home/swgemu/workspace/Core3/MMOCoreORB/bin/log/trip-verify-20260915-185828-p10g-novice-early-game*`:
- `.contract.md` (v7, md5 `971b5c27`)
- `-A-phaseA.json`, `-A-phaseB.json`
- `-B.json`
- `-C.json`, `-C-simcombat.log`
- `-D-samples.jsonl`, `-D-core3.log`, `-D-analysis.txt`

## Cleanup confirmation

- `sim_player_manager.lua` restored to md5 `2d149d790cc3f411aea8b796299d1449`. The container file and the worktree file are identical.
- After the restart (pid 786115) the dashboard reads false for: harness, progression, every `playerBotProgression` gate, `earlyGame` enabled, `combatTelemetry`, `realWeaponDamage` and `combatDiag.logging`. tracedCount is 0.
- The sampler and all watchers have stopped.
- No database, roster or unrelated log was cleared.
- Identity 84 keeps its earlier real progression: `combat_brawler_unarmed_01` trained in quick check 175249, 4189 XP to the next box, 0 credits.

## Verdict

**LIVE_VERIFICATION_FAIL** (D5, D6, D17, D18, D19; plus D7 not reached, D11 not run, D13 not exercised). The cause is a new controller livelock: the single-slot unreachable-target skip ping-pongs between two off-mesh kreetles. It needs a fix, then the testing gate, Codex review and live verification again.
