# Live verification — F_0.9.6 + D1 + guard + harness fixes + LeaveCity fixes + self-defence

**Run ID**: `20260914-173645-p10g-novice-early-game`
**Plans**: `docs/1-plans/F_0.9.6_p10g-novice-early-game-loop.plan.md`,
`docs/1-plans/F_0.9.6-D1_sim-combat-observability.plan.md`
**Branch**: `feat/p10g-novice-early-game-loop` (uncommitted, 16 modified files + 7 untracked, +4386/−182 excluding engine3)
**Supersedes**: `live_p10g-novice-early-game_20260914-030033-p10g-novice-early-game.md`
**Verdict**: **LIVE_VERIFICATION_FAIL**. No receipt was recorded.
**Superseded by**: `live_p10g-novice-early-game_20260914-205211-p10g-novice-early-game.md` (the diagnostic run 20260914-184412 found the real cause: clone wounds pinned the novice at 1 HP)

## Summary

This tree adds the following on top of the 030033 scope:
- movement telemetry;
- outdoor-only grind anchors with path-failure backoff;
- the Mos Eisley novice spawn override;
- parity-planet homes for harness identities;
- novice self-defence, including the Codex-requested phantom-combat age-out.

Runs A–C pass in full (A 60/60 across both boots, B guard gate-off, C D1 trace, kill-XP trace and guard gate-on). No hang, no crash and no REST error occurred in any boot.

Run D shows the LeaveCity stall is fixed: the novice leaves Mos Eisley (432 m), scans, re-anchors, dies and clones back on Tatooine. Verification still **fails on the loop**:
- The novice was **killed twice on the same anchor route**, at tatooine (3000.6, −4753.4) and (2997.6, −4734.6), z ≈ 53.
- Both times it was walking at full speed (4.8 m/s) one sample before death.
- Neither death registered a self-defence engagement or flee.
- It never reached Engage, so there was no kill, XP, rest or training.

Run D was stopped at t+593 s instead of running 60 min: the route is deterministic from the cloner, and nothing on the dashboard or in the logs can say what killed the novice (see Observability gap).

## Static gate

| Check | Result |
|---|---|
| Incremental build, `-Werror` | PASS: `SimHunterController.cpp` recompiled and linked warning-clean, binary md5 `92f00c71…` |
| `luac -p` (shipped config + A/B/C/D run configs) | PASS |
| GoogleTest | N/A: no suite covers these files |
| Codex code review | APPROVED (self-defence round 2; the MAJOR about stale combat with no eligible attacker was fixed) |

## Restarts

Six guarded `server-cycle.sh` restarts (A phase A, A phase B, B, C, D, restore). Each was ready in 8–12 s with 0 connected players. The Sep 5 zombie `core3` (pid 2114320, state Z) was ignored as designed.

## Assertions

| # | Assertion | Expected | Actual | Result |
|---|---|---|---|---|
| U1 | Clean startup | fresh who.json | every boot, 8–12 s | PASS |
| U2 | One live core3 per boot | 1 non-zombie | 686000 / 686636 / B / C / D / 689157 (+ Sep 5 zombie) | PASS |
| U3 | No crash markers in gdb.log delta | 0 | 0 (gdb.log unchanged at 961371) | PASS |
| U4 | No hang: every dashboard poll 200 | 0 non-200 | 0 across all watchers and the Run D sampler (117 samples, 0 errors) | PASS |
| U5 | No `RESTServer ERROR` in core3_api.log delta | 0 | 0 | PASS |
| A1 | Phase A 59 PASS + restart probe AWAITING, 0 FAIL | — | exactly that, 280 s | PASS |
| A2 | Phase B 60/60 | 60/60 | 60/60; `retained_record_survives_restart` PASS | PASS |
| A3 | Six F_0.9.6 scenarios | PASS | all PASS | PASS |
| A4 | Both real-kill scenarios | PASS | unarmed 30.6 s, rifle 3.8 s | PASS |
| A5 | `novice_box_precondition_refuses_body` | PASS | PASS | PASS |
| A6 | orphanRecords / rosterWithoutRecord | 0 / 0 | 0 / 0 (both boots) | PASS |
| A7 | D1 logging off: simcombat.log untouched | size + mtime unchanged | 1348469 / 1789355781 across both boots | PASS |
| B1 | Guard gate-off refusal | `precondition_gate_off_awardKillXp` | exactly that | PASS |
| B2 | No step executed | durationMs 0, killXp.kills 0 | 0 ms, kills 0 | PASS |
| C1 | Both kill scenarios with trace on | PASS | PASS (28.9 s / 3.8 s) | PASS |
| C2 | Guard gate-on refusal | `precondition_gate_on_trainingEnabled`, 0 ms | exactly that | PASS |
| C3 | One wall-clock run id | 1 | `1789408140290` | PASS |
| C4 | Trace lines key=value | 0 malformed | 0 / 202 | PASS |
| C5 | Attack enqueue per kill bot | ≥ 1 `enqueued` | unarmed bot 6, rifle bot 2 | PASS |
| C6 | Kill-XP trace per kill | `event_queued`, `reason=included`, both grants `granted=1` | both kills: 85 + 8 each | PASS |
| C7 | Dashboard while traced | writeErrors 0, HTTP 200 | 0, 8/8 polls 200 | PASS |
| C8 | Allowlist empties after cleanup | tracedCount 0 | 0 | PASS |
| D1 | Novice enters grind | ≤ 360 s | identity 84 at 112 s | PASS |
| D2 | LeaveCity → SelectTarget → Engage | ≤ 20 min | LeaveCity ⇄ SelectTarget only; killed twice before any Engage | **FAIL** |
| D3 | Body displacement | ≥ 300 m | 432 m | PASS |
| D4 | TTK window on Engage | 20–900 s | no Engage sample | **FAIL** |
| D5 | Real kill + XP via kill path | ≥ 1 | 0 kills, 0 XP, 0 awards | **FAIL** |
| D6 | Recover after kill | observed | no kill | **FAIL** |
| D7 | Train once XP threshold met | ≤ 120 s | threshold never reached (1000 XP needed) | NOT REACHED |
| D8 | Demand independence | hunts Δ0, signalUnits 0 | hunts Δ0, signalUnits {0} | PASS |
| D9 | Weapon xpType == next box xpType | 0 mismatches on a live body | 47/47 live samples `combat_meleespecialize_unarmed`; the analyzer's 8 mismatches are all corpse samples at the death position | PASS |
| D10 | No credit mutation | unchanged | (0, 0) throughout | PASS |
| D11 | Death → validated cloner respawn | deaths ≥ 1, respawns ≥ 1, new body, same planet | natural death at ~t+252, respawn at t+388: body `…069826` → `…071252`, tatooine; second death t+483 (respawn not awaited) | PASS (natural death; probe not needed) |
| D12 | ≤ 1 identity minted | roster Δ ≤ 1 | 21 → 21 | PASS |
| D13 | Unprovoked attack → defence counters rise | engagements + flees ≥ 1 | **2 deaths while walking with no target; engagements 0, flees 0** | **FAIL** |

**Required assertions: 33. Passed: 27. Failed: 5 (D2, D4, D5, D6, D13). Not reached: 1 (D7).**

## Failure — novice killed on its anchor route with no self-defence (D2, D4–D6, D13)

Evidence: `…-D-samples.jsonl` (117 samples at 5 s), `…-D-core3.log`.

| Fact | Death 1 | Death 2 |
|---|---|---|
| Body | `281475046069826` (attached t+112) | `281475046071252` (cloned t+388) |
| Route | Mos Eisley west → SelectTarget at (2990, −4648) t+222 → no target, re-anchor south | identical: SelectTarget at (2990, −4648) t+453 → re-anchor south |
| Last moving sample | t+247 (2995.2, −4730.0) 4.8 m/s | t+478 (2996.0, −4727.5) 4.8 m/s |
| Death position | (3000.6, −4753.4) | (2997.6, −4734.6), z 52.9 |
| Server death log | `SimPveHunterDied identity=84` uptime 259 s | uptime 485 s |
| earlyGame row | `phase=LeaveCity`, `targetOid=0`, `defensiveEngagements=0`, `defensiveFlees=0`, `flees=0` | same |

Established facts:
- The body went from full walking speed to dead within about one 5 s sample, both times at the same place on the same deterministic route.
- `runActiveTick` calls `runEarlyGameTick` on every tick. The LeaveCity branch re-ticks every 1000 ms while walking, and `handleEarlyGameSelfDefense` runs before the phase branches. So a combat state visible to that tick for more than ~1 s should have recorded an engagement or a flee.
- `onTick`'s interceptor defence also runs in `IDLE_HOME` (the controller phase during grind) and fights back via `defendAgainstInterceptor`. It never records the early-game defence counters.
- No lua static spawn exists within 60 m of the death spot. core3.log has nothing for the body besides attach and death.
- The novice body is `sim_playerbot_novice`: level 3, HAM 400–500, 25–40 damage.

**Observability gap.** Nothing available can say what killed the novice, how long it was in combat, or whether the early-game tick saw combat:
- The dashboard has no attacker, HAM or combat-state field for an early-game body.
- `combatDiag` is read only at boot (`loadLuaConfig` from `initialize`), so it cannot be enabled mid-run. Even when on, it logs only the bot's own attack choices, not damage taken or the attacker.

Recommended TRIP-2 work, default-off and run-scoped:
1. Early-game combat telemetry per body:
   - last attacker oid, template and level;
   - HAM at each tick while in combat;
   - `isInCombat` first-seen time;
   - the self-defence decision (winnable, ttk, below flee line, no eligible attacker);
   - the last `defendAgainstInterceptor` action.
2. Diagnose and fix the kill.
3. Vary the anchor bearing (or blacklist the sector) after a death on the same route, so a single hazard cannot trap the loop.

## Harness hygiene observation (non-blocking, carried over)

Identity 274 (a leftover harness brawler from the earlier hung run) again showed a grind row with no body. See the 030033 report.

## Evidence

All under `/home/swgemu/workspace/Core3/MMOCoreORB/bin/log/trip-verify-20260914-173645-p10g-novice-early-game*`:
- `.contract.md`
- `-A-phaseA.json`, `-A-phaseB.json`
- `-B.json`
- `-C.json`, `-C-simcombat.log`, `-C-traced-baseline.json`
- `-D-samples.jsonl`, `-D-core3.log`

## Cleanup confirmation

- `sim_player_manager.lua` restored to md5 `7d2d9de91ed85da9a81ec881580bfae8` in the container and the worktree.
- After restart (pid 689157) the harness, early-game mode, `combatDiag.logging` and progression all read false; tracedCount is 0.
- Sampler and monitor stopped.
- No database, roster or unrelated log was cleared. Identity 84's progression is unchanged by Run D: 0 XP, 0 credits; roster deaths +2 are real.

## Verdict

**LIVE_VERIFICATION_FAIL** (D2, D4, D5, D6, D13), with an **observability gap** on the cause. Return to `TRIP-2-implement`:
1. Add early-game combat telemetry.
2. Fix the route death and self-defence trigger.
3. Add anchor variation after a death.
4. Re-run Run D.

Runs A–C need no rework unless the fix touches their code paths.
