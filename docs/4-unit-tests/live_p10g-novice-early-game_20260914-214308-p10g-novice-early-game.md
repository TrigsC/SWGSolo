# Live verification — F_0.9.6 + Recover tick-stall fix

**Run ID**: `20260914-214308-p10g-novice-early-game`
**Plans**: `docs/1-plans/F_0.9.6_p10g-novice-early-game-loop.plan.md`,
`docs/1-plans/F_0.9.6-D1_sim-combat-observability.plan.md`
**Branch**: `feat/p10g-novice-early-game-loop` (uncommitted, 16 modified files, +5075/−182 excluding engine3)
**Supersedes**: `live_p10g-novice-early-game_20260914-205211-p10g-novice-early-game.md`
**Verdict**: **LIVE_VERIFICATION_FAIL**. No receipt was recorded.

## Summary

This run verifies the Recover tick-stall fix on top of the 205211 scope:
- Recover keeps its 1 s active tick.
- A pinned recovery walk re-flees after 4 s without progress.
- An out-of-combat stall re-issues the rest walk after 10 s.

Runs A–C pass in full, and A6 was clean this time (0/0 on both boots). No crash, hang or REST error in any boot.

**The fix itself is verified live:**
- The novice (identity 84) spawned at 0.90 HAM and walked six anchors.
- At 684 s a greater desert womp rat (level 22, above the ceiling) hit it for 332 health. Self-defence chose `flee`.
- The Recover tick came back one second later, still in combat: at 691 s it logged `SimPveEarlyGameCombat … phase=Recover … decision=flee_recovering`. In 205211 the tick never ran again after the flee.
- The body died at 692 s (226 damage), and the killing blow was captured.
- It respawned at 90% HAM (D11).
- Across 193 live-body samples the early-game tick was never older than 4.6 s.

**Run D still fails, now for a different reason: the novice never found an eligible target.**
- In six scans over 20 minutes, no creature within the 96 m scan radius passed the early-game filter: level ≤ skillTier 2 + `levelCeilingOffset` 2 = 4, and TTK 20–900 s.
- So there was no Engage, kill or XP, and the kill → Recover → Train loop (D4–D7) could not be exercised.
- This is a target-availability and config problem, not a stalled state machine. Run D was stopped when D2's deadline (1200 s) passed.

## Static gate

| Check | Result |
|---|---|
| Incremental build, `-Werror` | PASS: no-op, binary 2026-09-14 21:36:59, md5 `4bfd40bd…` |
| `luac -p` (shipped config `b4a48275…` + v5 A/B/C/D/Dprobe) | PASS |
| GoogleTest | N/A: no suite covers these files |
| Codex code review | APPROVED: Recover tick-stall fix, round 1, no findings |

## Restarts

Six guarded `server-cycle.sh` restarts (A phase A, A phase B, B, C, D, restore). Each was ready in 7–12 s with 0 connected players. The Sep 5 zombie core3 (pid 2114320) was ignored.

## Assertions

| # | Assertion | Expected | Actual | Result |
|---|---|---|---|---|
| U1 | Clean startup | fresh who.json | every boot, 7–12 s | PASS |
| U2 | One live core3 per boot | 1 non-zombie | 705250 / 705858 / B / C / 707447 / 708931 | PASS |
| U3 | No crash markers in gdb.log delta | 0 | 0 (gdb.log unchanged at 961371) | PASS |
| U4 | No hang: every dashboard poll 200 | 0 non-200 | 0 across all watchers and the Run D sampler (242 samples, 0 errors) | PASS |
| U5 | No `RESTServer ERROR` in core3_api.log delta | 0 | 0 | PASS |
| A1 | Phase A 59 PASS + restart probe AWAITING, 0 FAIL | — | exactly that (13/13 polls 200) | PASS |
| A2 | Phase B 60/60 | 60/60 | 60/60; `retained_record_survives_restart` PASS (693 ms) | PASS |
| A3 | Six F_0.9.6 scenarios | PASS | all PASS | PASS |
| A4 | Both real-kill scenarios | PASS | unarmed 20.9 s, rifle 3.8 s | PASS |
| A5 | `novice_box_precondition_refuses_body` | PASS | PASS | PASS |
| A6 | orphanRecords / rosterWithoutRecord | 0 / 0 | boot 1 final 0 / 0 (records 22); boot 2 final 0 / 0 (records 21) | PASS |
| A7 | D1 logging off: simcombat.log untouched | size + mtime unchanged | 1464090 / 1789419960 across both boots | PASS |
| B1 | Guard gate-off refusal | `precondition_gate_off_awardKillXp` | exactly that (6/6 polls 200) | PASS |
| B2 | No step executed | durationMs 0, killXp.kills 0 | 0 ms, kills 0, awards 0 | PASS |
| C1 | Both kill scenarios with trace on | PASS | PASS (unarmed 20.9 s / rifle 4.0 s; the rifle cone shot also killed 3 bystanders and the per-victim oracle still settled) | PASS |
| C2 | Guard gate-on refusal | `precondition_gate_on_trainingEnabled`, 0 ms | exactly that | PASS |
| C3 | One wall-clock run id | 1 | `1789422909548` | PASS |
| C4 | Trace lines key=value | 0 malformed | 0 / 169 | PASS |
| C5 | Attack enqueue per kill bot | ≥ 1 `enqueued` | unarmed bot 4, rifle bot 1 | PASS |
| C6 | Kill-XP trace per kill | `event_queued`, `reason=included`, both grants `granted=1` | unarmed 85 + 8; rifle target 85 + 8 (+ 3 cone bystanders 147 + 14 each) | PASS |
| C7 | Dashboard while traced | writeErrors 0, HTTP 200 | 0, 7/7 polls 200 | PASS |
| C8 | Allowlist empties after cleanup | tracedCount 0 | 0 | PASS |
| D1 | Novice enters grind | ≤ 360 s | identity 84 at 104 s | PASS |
| D2 | LeaveCity → SelectTarget → Engage | ≤ 20 min | six SelectTarget scans, **no Engage by 1200 s** | **FAIL** |
| D3 | Body displacement | ≥ 300 m | 635 m | PASS |
| D4 | TTK window on Engage | 20–900 s | no Engage sample | **FAIL** |
| D5 | Real kill + XP via kill path | ≥ 1 within 40 min | 0 kills, 0 XP, 0 awards (run stopped at the D2 deadline) | **FAIL** |
| D6 | Recover after a kill, loop continues, no frozen tick | kill → Recover → Train; tick age < 10 s | tick part holds: max live-body `lastTickAgeMs` 4.6 s over 193 samples, and the tick ran through a Recover flee in combat. **No kill occurred**, so kill → Recover → Train was not observed | **FAIL** |
| D7 | Train once XP threshold met | ≤ 120 s | threshold never reached (1000 XP needed) | NOT REACHED |
| D8 | Demand independence | hunts Δ0, signalUnits 0 | hunts Δ0, signalUnits {0} | PASS |
| D9 | Weapon xpType == next box xpType on live bodies | 0 mismatches | 191 live samples, 0 mismatches (51 corpse samples excluded) | PASS |
| D10 | No credit mutation | unchanged | (0, 0) throughout | PASS |
| D11 | Death → validated cloner respawn | deaths ≥ 1, respawns ≥ 1, new body, same planet | natural death 692 s; respawn 829 s: body `…414669` → `…420599`, tatooine, HAM 0.90 | PASS (natural death) |
| D12 | ≤ 1 identity minted | roster Δ ≤ 1 | 21 → 21 | PASS |
| D13 | Unprovoked attack → self-defence | decision allowed + counters rise | attacked while scanning (no target): `flee`, defensiveFlees +1. Next tick, still in combat during Recover: `flee_recovering` (core3.log 691 s). Dead 1 s later. The 5 s samples missed the in-combat state, so the analyzer reads NOT_EXERCISED; the log line is the evidence | PASS |
| D14 | Novice HAM healthy | min pool ≥ 0.05 after 60 s; ≥ 0.70 within 10 min | both bodies spawn at 0.90; the only low reading was the fatal fight | PASS |
| D15 | Every death has a captured cause + zone | 0 missing / uncaptured | 1 death: `greater_desert_womp_rat.iff` lvl 22, 226 dmg; zones 1 | PASS |
| D16 | No Recover livelock | no span > 900 s without HAM rising | no Recover span > 900 s | PASS |

**Required assertions: 38. Passed: 33. Failed: 4 (D2, D4, D5, D6). Not reached: 1 (D7).**

## Failure — no eligible early-game target around Mos Eisley (D2, D4–D6)

Evidence: `…-D-samples.jsonl` (242 samples at 5 s), `…-D-core3.log`, `…-D-analysis.txt`.

Measured:
- **Scans:** six SelectTarget phases over 20 min. Each ran its scan attempts and re-anchored with no target. The first three were 451–505 m from Mos Eisley's centre.
- **Creatures met:** the only creature that engaged the novice was a greater desert womp rat, level 22, which killed it.
- **Filter:** the early-game scan accepts any non-bot AI within 96 m that is not in combat, is attackable, has level ≤ skillTier + `levelCeilingOffset` (2 + 2 = 4), and has a TTK of 20–900 s. `targetMatchesSpecies` returns true in early-game mode, so the species filter is not involved.

Spawn-table facts (read-only):
- **Newbie tables:**
  - The easy newbie table (CIRCLE 800 m at 3496, −4784) is mostly levels 5–10. Only kreetle and dwarf eopie (3) and hermit, insane kitonak, squatter and moisture farmer (4) are ≤ 4.
  - The medium newbie table (RING 800–1400 m) is mostly levels 5–10.
- **Harder tables:** `mos_eisley_se_medium` (levels 10–14) and `mos_eisley_se_hard` (levels 13–22) are large rectangles that also cover the Mos Eisley surroundings. The level-22 killer is from `se_hard`.
- **Spawn triggers:** spawn areas trigger on `isPlayerOrSimPresenceCreature`, so presence bots, including the novice, do cause spawns. Lair difficulty scales with the triggering creature (`getPlayerSpawnLevel`).

Hypotheses disproved during the run:
1. The city's 900 m `NOWORLDSPAWNAREA` circle does **not** block these tables: the flag is only checked for world spawn areas, and these are plain `SPAWNAREA`.
2. Spawning is **not** player-only.

Not proven: exactly which creatures were within 96 m at each scan, and why none qualified. There is no per-candidate scan telemetry.

**This needs an owner design decision before another Run D. Options:**
1. **Level ceiling:** raise `earlyGame.levelCeilingOffset` for low tiers so the level 5–10 newbie fauna qualifies. This trades against the no-cheat TTK intent.
2. **Placement:** move novice spawn or grind anchors to where ≤ level 4 fauna is dense, or restrict anchors to newbie spawn regions away from the `se_*` rectangles.
3. **Verification:** have Run D use the harness's kill-target spawner to place a level-appropriate target near the novice, to prove kill → Recover → Train independently of ecology.
4. **Observability:** add default-off scan telemetry (candidates seen, rejection reason per candidate) so the next run can measure target availability instead of inferring it.

## Evidence

All under `/home/swgemu/workspace/Core3/MMOCoreORB/bin/log/trip-verify-20260914-214308-p10g-novice-early-game*`:
- `.contract.md` (v6)
- `-A-phaseA.json`, `-A-phaseB.json`
- `-B.json`
- `-C.json`, `-C-simcombat.log`
- `-D-samples.jsonl`, `-D-core3.log`, `-D-analysis.txt`

## Cleanup confirmation

- `sim_player_manager.lua` restored to md5 `b4a48275af85729749b3f2ad4ebe4d18` in the container and the worktree.
- After restart (pid 708931) the harness, early-game mode, `combatTelemetry`, `combatDiag.logging` and progression all read false; tracedCount is 0.
- Sampler and both monitors stopped.
- No database, roster or unrelated log was cleared. Identity 84's progression is unchanged (0 XP, 0 credits); its roster deaths +1 are real.

## Verdict

**LIVE_VERIFICATION_FAIL** (D2, D4, D5, D6).
- The Recover tick-stall fix is verified live; no code defect remains open from 205211.
- The blocker is early-game target availability around Mos Eisley under the level-4 ceiling.
- That needs an owner decision (options above) before implementation and another Run D.
