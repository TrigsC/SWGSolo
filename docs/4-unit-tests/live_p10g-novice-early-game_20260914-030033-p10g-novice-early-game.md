# Live verification — F_0.9.6 + D1 + guard + harness fixes

**Run ID**: `20260914-030033-p10g-novice-early-game`
**Plans**: `docs/1-plans/F_0.9.6_p10g-novice-early-game-loop.plan.md`,
`docs/1-plans/F_0.9.6-D1_sim-combat-observability.plan.md` (Phase 3 records the follow-up fixes)
**Branch**: `feat/p10g-novice-early-game-loop` (uncommitted, 15 files, +3775/−179)
**Supersedes**: `live_p10g-novice-early-game_20260913-171447-p10g-novice-early-game.md`
**Verdict**: **LIVE_VERIFICATION_FAIL**. No receipt was recorded.
**Superseded by**: `live_p10g-novice-early-game_20260914-173645-p10g-novice-early-game.md`

## Summary

The harness, guard and D1 observability layers are **fully verified**:
- Run A: 60/60 parity matrix across both boots
- Run B: guard, gate-off path
- Run C: trace, kill-XP trace and guard gate-on path

That also confirms every follow-up fix in the tree: the dashboard 502 fix, the harness governor exemption, the per-victim kill-XP oracle with its quiet period, and the `startCombat` attacker-lock fix. No hang, no crash and no REST error occurred in any boot.

Verification **fails on the product's headline behaviour** (Run D). A gated novice enters grind mode, but **never leaves its home spawn point**. It sat in `LeaveCity` with 0 m displacement for 1230 s, so it never selects a target, kills, rests or trains. The death/cloner probe could not be exercised.

## Static gate

| Check | Result |
|---|---|
| Incremental build, `-Werror` | PASS: no-op, binary 2026-09-13 23:26:35, no source newer |
| `luac -p` (shipped config + 5 run configs) | PASS |
| GoogleTest | N/A: no suite covers these files |

## Restarts

Six guarded `server-cycle.sh` restarts (A phase A, A phase B, B, C, D, restore). Each was ready in 8–12 s with 0 connected players.

## Assertions

| # | Assertion | Expected | Actual | Result |
|---|---|---|---|---|
| U1 | Clean startup | fresh who.json | every boot, 8–12 s | PASS |
| U2 | One live core3 per boot | 1 non-zombie | 634205 / 634875 / 635432 / 635943 / 636581 / 638142 | PASS |
| U3 | No crash markers in gdb.log delta | 0 | 0 (gdb.log unchanged at 961371 all run) | PASS |
| U4 | No hang: every dashboard poll 200 | 0 non-200 | 0 across A, B, C and D watchers and hang monitors | PASS |
| U5 | No `RESTServer ERROR` in core3_api.log delta | 0 | 0 | PASS |
| A1 | Phase A 59 PASS + restart probe AWAITING, 0 FAIL | — | exactly that, 260 s | PASS |
| A2 | Phase B 60/60 | 60/60 | 60/60; restart probe PASS | PASS |
| A3 | Six F_0.9.6 scenarios | PASS | all PASS | PASS |
| A4 | Both real-kill scenarios | PASS | unarmed 24.9 s, rifle 3.8 s | PASS |
| A5 | `novice_box_precondition_refuses_body` (governed opt-in) | PASS | PASS | PASS |
| A6 | orphanRecords / rosterWithoutRecord | 0 / 0 | 0 / 0 | PASS |
| A7 | D1 logging off: simcombat.log untouched | size + mtime unchanged | 1294727 / 1789342641 across both boots | PASS |
| B1 | Guard gate-off refusal | `precondition_gate_off_awardKillXp` | exactly that | PASS |
| B2 | No step executed | durationMs 0, killXp.kills 0 | 0 ms, all 11 steps PENDING, kills 0 | PASS |
| C1 | Both kill scenarios with trace on | PASS | PASS (24.9 s / 3.8 s) | PASS |
| C2 | Guard gate-on refusal | `precondition_gate_on_trainingEnabled`, 0 ms | exactly that | PASS |
| C3 | One wall-clock run id | 1, > 1.7e12 | `1789355691113` | PASS |
| C4 | Trace lines key=value | 0 malformed | 0 / 179 | PASS |
| C5 | Attack enqueue per kill bot | ≥ 1 `enqueued` | unarmed bot 5, rifle bot 1 | PASS |
| C6 | Kill-XP trace per kill | `event_queued`, `reason=included`, both grants `granted=1` | both kills: unarmed 85 + 8, rifle 85 + 8 | PASS |
| C7 | Dashboard while traced | writeErrors 0, HTTP 200 | 0, all 200 | PASS |
| C8 | Allowlist empties after cleanup | tracedCount 0 | 0 | PASS |
| D1 | Novice enters grind | ≤ 360 s | identity 84 at 108 s | PASS |
| D2 | LeaveCity → SelectTarget → Engage | ≤ 20 min | **stuck in `LeaveCity` 1230 s** | **FAIL** |
| D3 | Body displacement | ≥ 300 m | **0 m over 225 positions** | **FAIL** |
| D4 | TTK window on Engage | 20–900 s | no Engage sample | **FAIL** |
| D5 | Real kill + XP via kill path | ≥ 1 | 0 kills, 0 XP, 0 awards | **FAIL** |
| D6 | Recover after kill | observed | no kill | **FAIL** |
| D7 | Train once XP threshold met | ≤ 120 s | threshold never reached (1000 XP needed) | NOT REACHED |
| D8 | Demand independence | hunts Δ0, signalUnits 0 | hunts Δ0, signalUnits {0} | PASS |
| D9 | Weapon xpType == next box xpType | 0 mismatches | 0 (`combat_meleespecialize_unarmed`) | PASS |
| D10 | No credit mutation | unchanged | (0, 0) throughout | PASS |
| D11 | Death → validated cloner respawn | deaths ≥ 1, respawns ≥ 1 | not exercised (no movement/combat) | NOT RUN |
| D12 | ≤ 1 identity minted | roster Δ ≤ 1 | 21 → 21 | PASS |

**Required assertions: 32. Passed: 26. Failed: 5 (D2–D6). Not reached/run: 2 (D7, D11).**

## Failure — novice never leaves its spawn point (D2–D6)

Evidence: `…runD-samples.jsonl` (242 samples at 5 s), `…runD-analysis.txt`, `…runD-dashboard.json`.

| Fact | Value |
|---|---|
| Novice | identity 84, brawler/brawler, body `281475045564299`, Keren (naboo) |
| earlyGame row, entire run | `activity=grind`, `graduated=false`, `phase=LeaveCity`, `targetOid=0`, kills/deaths/flees/rest/respawns 0, `anchorFallbacks=0`, `emptyScanRotations=0` |
| Position, entire run | (1371.6, 2747.9), the home spawn point |
| Body state (combatDiag row) | present, HAM 418/411/466, not in combat, no behaviour event (controller-driven) |
| Server log for this body | only `SimPveBodyAttached identity=84` at 114 s |

What code reading establishes (read-only; no implementation edits from verify mode):
- `beginEarlyGame` ran through anchor selection, because the row was published with `LeaveCity` and no anchor fallback was recorded.
- The `LeaveCity` branch of `runEarlyGameTick` calls `moveTo(earlyGameAnchor)` whenever the controller state is not `MOVING`/`CALCULATING_PATH`.
- `scanForTarget` is **not** the blocker: a successful scan calls `selectTarget`, which publishes `Engage`, and that never happened.
- So either the active tick never re-enters `runEarlyGameTick` after `beginEarlyGame`, or the move request leaves the controller in `MOVING`/`CALCULATING_PATH` without motion.

**Observability gap for the fix.** The dashboard does not expose the hunter controller's movement state, path request/result, or active-tick cadence for an early-game body. The only movement log (`travelDiag`) covers relocation legs and is read at boot. Recommended TRIP-2 work, default-off and run-scoped:
- publish per-early-game-body telemetry (controller `state`, last `moveTo` target and timestamp, path result, last active-tick timestamp, anchor position)
- or add `EARLY_*` travelDiag events for the walk

After that, fix the stall and re-run Run D.

Note: the harness F_0.9.6 scenarios assert activity and loadout but never movement, which is why this was not caught before the live loop ran.

## Harness hygiene observation (non-blocking)

Roster identities 272–276, **including brawler 274**, remain from the earlier hung run whose cleanup never executed. The harness-minted exemption set is in memory only, so after a restart a leftover harness brawler becomes a production identity (274 showed a grind row, with no body because 84 held the slot). Boot-time stale-row handling (`harnessRowsStale`) only covers `profession=harness`. Worth a follow-up so a crashed or hung run cannot leak governor-managed identities.

## Evidence

All under `/home/swgemu/workspace/Core3/MMOCoreORB/bin/log/trip-verify-20260914-030033-p10g-novice-early-game.*`:
- `contract.md`
- `runA-phaseA-{dashboard.json,watch.txt}`, `runA-phaseB-{dashboard.json,watch.txt}`
- `runB-{dashboard.json,watch.txt}`
- `runC-{dashboard.json,watch.txt,simcombat.log,killxp-analysis.txt}`
- `runD-{samples.jsonl,analysis.txt,dashboard.json,hangmonitor.txt}`

## Cleanup confirmation

- `sim_player_manager.lua` restored to md5 `670340af6b9d1af9a0b2b70b39972456` in the container and the worktree.
- After restart, every capability gate reads false, the harness and novice mode are disabled, and `combatDiag.logging=false` (server pid 638142).
- Sampler, watchers and hang monitors stopped.
- No database, roster or unrelated log was cleared. Identity 84's progression is unchanged by Run D: no XP was earned.

## Verdict

**LIVE_VERIFICATION_FAIL.** Return to `TRIP-2-implement`:
1. Add default-off early-game movement telemetry, since the stall cannot be diagnosed from current signals.
2. Fix the `LeaveCity` stall.
3. Re-run Run D, including the D11 death/cloner probe.

Runs A–C need no rework unless the fix touches their code paths.
