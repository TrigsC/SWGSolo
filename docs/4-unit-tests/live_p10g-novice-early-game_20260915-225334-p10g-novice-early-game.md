# Live verification — F_0.9.6 novice early-game loop (v9 contract)

**Run ID**: `20260915-225334-p10g-novice-early-game`

**Plans**:
- `docs/1-plans/F_0.9.6_p10g-novice-early-game-loop.plan.md`
- `docs/1-plans/F_0.9.6-D1_sim-combat-observability.plan.md`

**Branch**: `feat/p10g-novice-early-game-loop`, uncommitted: 16 modified files, +5655/−195 excluding engine3.

**Binary**: `f3ab9c165ed592aab0eb5f3200f91e73`, built 2026-09-15 20:52:28 — unchanged since the v8 run.

**Contract**: v9, final md5 `2e201320` (as first deployed `078bdc40`, plus Amendment 1 and Amendment 2, both owner-approved and recorded below).

**Supersedes**: `live_p10g-novice-early-game_20260915-205544-p10g-novice-early-game.md` (42/43, D7 unreachable).

**Verdict**: **LIVE_VERIFICATION_PASS — 43/43.**

## Summary

The full novice early-game loop is verified live on this binary, including the parts the two previous runs could not reach.

- Runs A, B, C: 22/22.
- Run D: 21/21 on subject identity **274**, a roster novice with one trained box.
- The whole loop ran end to end: leave city → select target → engage → kill → XP → recover → **train** → repeat, with deaths and cloner respawns.
- 22 kills in the main window, every one paid (D5, D20). Training reached and executed: the XP threshold hit at t+4470 s and `combat_brawler_unarmed_01` was trained 10 s later (**D7**).
- Spawner roaming exercised properly: eight spawn eggs anchored, seven reached within 60 m (**D17**).
- Death and respawn verified three times, each with a captured cause and a death-site zone (**D11**, **D15**), and every body spawned at full 800 HAM (**D14**), confirming heal-on-death.
- The 185828 Engage livelock stays fixed: **D21** finds no target-selection stall across 1596 live samples.
- No crash, hang or REST error on any of the six boots.

## Static gate

| Check | Result |
|---|---|
| Incremental build, `-Werror` | PASS: no-op, binary `f3ab9c16` |
| `luac -p`: shipped `4731478d` + A/B/C/D/Dprobe/Dprobe2 | PASS |
| GoogleTest | N/A: no suite covers these files |
| Codex code review | APPROVED (skip set, round 1, no new findings) |
| `git diff --check` | clean |

## Restarts

| Boot | Config md5 | core3 pid | Ready |
|---|---|---|---|
| A phase A | `266e0b70` | 796641 | 12 s |
| A phase B | `266e0b70` | 797409 | 7 s |
| B | `55cf3a95` | 797941 | 7 s |
| C | `fe442ef3` | 798462 | 12 s |
| D | `4be29595` | 799032 | 8 s |
| restore | `4731478d` | 810396 | 9 s |

Both probe windows were applied to the Run D boot by hot edit, with no restart: `v5_Dprobe` (`9df8076a`) at epoch 1789519352, then `v5_Dprobe2` (`40047e9c`) at epoch 1789524754.

## Assertions

| # | Assertion | Expected | Actual | Result |
|---|---|---|---|---|
| U1 | Clean startup | fresh who.json | every boot, 7–12 s | PASS |
| U2 | One live core3 per boot | 1 non-zombie | 796641 / 797409 / 797941 / 798462 / 799032 / 810396 | PASS |
| U3 | No crash markers | 0 | gdb.log unchanged at 961371, 0 markers; 0 in the 31 MB pre-truncation core3.log delta and 0 in the current file | PASS |
| U4 | No hang: every dashboard poll 200 | 0 non-200 | watcher polls 13 + 6 + 7 + 8, all 200; samplers 1680 samples, 0 errors | PASS |
| U5 | No `RESTServer ERROR` in core3_api.log delta | 0 | 0 | PASS |
| A1 | Phase A: 59 PASS + restart probe AWAITING, 0 FAIL | — | exactly that; runId 1789512958488 | PASS |
| A2 | Phase B 60/60 | 60/60 | 60/60; `retained_record_survives_restart` PASS (667 ms) | PASS |
| A3 | Six F_0.9.6 scenarios | PASS | all PASS (1254 / 752 / 1004 / 753 / 1004 / 2053 ms) | PASS |
| A4 | Both real-kill scenarios | PASS | unarmed 22.9 s, rifle 3.9 s | PASS |
| A5 | `novice_box_precondition_refuses_body` | PASS | PASS (1004 ms) | PASS |
| A6 | orphanRecords / rosterWithoutRecord | 0 / 0 | settled 0 / 0 on both boots, record ids identical to roster ids. Phase A's final snapshot read `orphanRecords 1` / 23 records — the documented cached mid-cleanup value; two settled reads 45 s and 135 s later showed 22 records and 0 / 0, and phase B showed 21 records and 0 / 0 | PASS |
| A7 | simcombat.log untouched while logging off | size + mtime unchanged | 1625086 / 1789506599 across both boots | PASS |
| B1 | Guard gate-off refusal | `precondition_gate_off_awardKillXp` | exactly that; runId 1789513610868 | PASS |
| B2 | No step executed | durationMs 0, killXp.kills 0 | 0 ms; kills 0, awards 0, botKillsSeen 0, all drop counters 0 | PASS |
| C1 | Both kill scenarios, trace on | PASS | unarmed 20.9 s, rifle 3.8 s | PASS |
| C2 | Guard gate-on refusal | `precondition_gate_on_trainingEnabled`, 0 ms | exactly that | PASS |
| C3 | One wall-clock run id | 1 | `1789513736048` across 157 lines | PASS |
| C4 | Trace lines key=value | 0 malformed | 0 / 157 | PASS |
| C5 | Attack enqueue per kill bot | ≥ 1 `enqueued` | …531667: 4, …531670: 5, …531678: 2 | PASS |
| C6 | Kill-XP trace per kill | `event_queued`, `reason=included`, grants `granted=1` | 4 × included, 4 × event_queued; identity 459 unarmed 85 + general 8, identity 460 rifle 85 + general 8, all `granted=1` | PASS |
| C7 | Dashboard while traced | writeErrors 0, HTTP 200 | 0, 8/8 | PASS |
| C8 | Allowlist empties after cleanup | tracedCount 0 | 0 | PASS |
| D1 | Novice enters grind | ≤ 360 s | 97 s | PASS |
| D2 | LeaveCity → SelectTarget → Engage | ≤ 1500 s | first Engage 162 s; the ordered triple appears in the phase sequence | PASS |
| D3 | Displacement | ≥ 300 m | 522 m over 1365 positions | PASS |
| D4 | TTK window on self-selected Engage | 0–900 s | 413 samples, TTK 1.1–9.3 s, none out of window | PASS |
| D5 | Real kill + XP | ≥ 1 within 2400 s | 22 kills, first at 207 s; +1708 XP by `nextXpNeeded` drops; awards +46; `lastAwardSource=kill` | PASS |
| D6 | Recover after a kill; no frozen tick | kill → Recover; tick age < 10 s | Recover after the first kill; max `lastTickAgeMs` 4.8 s over 1568 live samples | PASS |
| D7 | Train once threshold met | threshold reached, then ≤ 120 s | threshold at 4470 s for `combat_brawler_unarmed_01`; trained 10 s later | PASS |
| D8 | Demand independence | hunts Δ0, signalUnits 0 | Δ0, {0} | PASS |
| D9 | weaponXpType == nextXpType on live bodies | 0 mismatches | 1365 live samples, 0 mismatches (84 corpse samples excluded) | PASS |
| D10 | No credit mutation | unchanged | (0, 0) | PASS |
| D11 | Death → validated respawn | deaths ≥ 1, respawns ≥ 1, new body, same planet | Amendment 2 probe: deaths +2, respawns +2, 3 distinct bodies, tatooine | PASS |
| D12 | ≤ 1 identity minted | roster Δ ≤ 1 | 21 → 21 | PASS |
| D13 | Unprovoked attack → self-defence | allowed decision + counters rise | 17 unprovoked samples; decisions `engage_phase`, `flee_other_attacker`, `flee_recovering`; 0 disallowed; counters +88 | PASS |
| D14 | 800 HAM, heal-on-death | max 800 all pools; first sample ≥ 0.95 | four bodies (…531264, …553228, …554236, …555315) each first sampled at 1.0; 0 samples with max ≠ 800 | PASS |
| D15 | Every death has a captured cause + zone | 0 missing / uncaptured | 3 deaths, 3 bodies, 0 missing, 0 uncaptured, 3 death zones; killers sevorrt lvl 7 and criminal slicer lvl 14 ×2 | PASS |
| D16 | No Recover livelock | no span > 900 s without HAM rising | 46 spans, longest 140 s | PASS |
| D17 | Spawner roaming | anchor on a spawn egg, reached | 8 eggs anchored (2, 3, 6, 8, 9, 11, 12, 13), 7 reached within 60 m; first at 4015 s | PASS |
| D18 | PACK filter | ≥ 1 target, ≥ 80% resolved, 0 PACK-only | 14 self-selected targets, 13 resolved (93%), 0 PACK-only; dwarf_nuna_hue, eopie_hue, mynock_hue, worrt_hue | PASS |
| D19 | All-attackers flee (as amended) | ≥ 1 `flee_other_attacker` in Engage | 2 for the subject (defenders histogram {1: 2}); 12 more for novice 84, including one with 3 defenders | PASS |
| D20 | Kill-XP accounting | every kill paid; 0 subject drops; lines ≈ counters | 22 kills, 0 unpaid, 0 subject drop lines, 1 drop line vs counter Δ 1, `botKillsSeen` +199 | PASS |
| D21 | No target-selection stall | none | none across 1596 live samples | PASS |

**Required assertions: 43. Passed: 43.**

## Deviations and amendments

Four things differed from the contract as first written. All are recorded here and in the contract file itself; two are owner-approved amendments.

**1. No novice was minted; the second existing novice became active (mechanism deviation).**
The contract says the Run D configs mint a fresh brawler novice by raising `pveConfig.novice.distribution` count 1 → 2. The governor's loop is `while (have < row.count)`, and two identities already matched brawler + `brawler` plan — 84 and 274 — so `have` was already 2 and nothing minted. `records` stayed 21 throughout. What the change did do is let **both** novices grind, where only 84 did before. The intended outcome (a subject whose next box is reachable) was achieved by a different route.

**2. Subject selection rule changed before any D assertion was read.**
The v5/v8 analyzers excluded identity 274 by hardcoded id. 274 is not harness-owned: its profession is `brawler`, and the parity harness mints its own identities (459/460 in Run C). Both earlier datasets in fact show grind ids [84, 274], so the exclusion had been hiding a production novice. The v9 analyzer now picks the grind identity with the fewest trained boxes, skipping only `profession == "harness"`. Subject selection is taken from the earliest sample carrying a grind row, so it cannot flip when the subject trains its box. Re-scoring the old v8 dataset under this rule targets 274 and gives 9/21; that does not change the v8 report, which named 84 as its subject.

**3. Amendment 1 — D19 measurement (owner-approved, post-hoc).**
Original: "≥ 1 `flee_other_attacker` line for the subject in Engage with `defenders` ≥ 2." Amended: the `defenders ≥ 2` clause is dropped; the count is reported instead. The decision legitimately fires with a single listed defender when the creature judged unwinnable is the only entry in the defender list and is not the current target. Raw counts: 14 such lines in the run — 2 for subject 274 (both `defenders=1`), 12 for novice 84 (one with `defenders=3`). **D19 passes under the amendment and would fail under the original wording.**

**4. Amendment 2 — D11 probe configuration (owner-approved, post-hoc).**
The contract's probe sets `minTtkSeconds` 600, which requires a target that takes ten minutes to kill; nothing near the novice's anchors qualified. Measured over that first probe window (300 samples): subject in `LeaveCity` 268 / `SelectTarget` 32, **0 targets**, kills frozen at 22, deaths 0, minimum HAM fraction 0.99. The probe scaffold, not the code, prevented the death. The amended probe (`v5_Dprobe2`, `40047e9c`) is identical except `minTtkSeconds` 0; under it the subject engaged, dropped to 0.13 HAM, died twice and respawned twice inside 25 minutes. D11 is judged on the Amendment 2 window; every other assertion is judged on the main 90-minute window.

## Evidence fragility finding — core3.log truncated in place mid-run

`core3.log` was 68 MB at the Run D offset, grew past 99 MB (the 31 MB delta captured for analysis), and then **truncated in place** to a few hundred KB with no rotated sibling; the current file begins mid-run at 02:36:56. Two of the three in-window death lines were lost from the log as a result.

This did not cost the assertion, because the sampled telemetry carries the same `SimPveEarlyGameDeath …` line verbatim in `combat.lastDeath`, and the dashboard's `deathZones` array independently recorded all three sites. The analyzer now merges death lines from both sources; **the assertion text is unchanged**, only where the line is read from. Three deaths, three lines, three zones, none uncaptured.

Process lesson for future runs: offset-based log deltas are fragile across a multi-hour verification. Capture the delta per phase, or copy the log at each boundary.

## Evidence

Under `/home/swgemu/workspace/Core3/MMOCoreORB/bin/log/trip-verify-20260915-225334-p10g-novice-early-game*`:
- `.contract.md` (v9 + both amendments, md5 `2e201320`)
- `-A-phaseA.json`, `-A-phaseB.json`
- `-B.json`
- `-C.json`, `-C-simcombat.log`
- `-D-samples.jsonl` (90 min), `-D-probe-samples.jsonl` (probe 1), `-D-probe2-samples.jsonl` (Amendment 2 probe)
- `-D-core3.log` (31 MB pre-truncation delta), `-D-core3-reconstructed.log` (delta + surviving current log)
- `-D-analysis.txt`, `-D-analysis-with-probe.txt`, `-D-analysis-final.txt`

## Cleanup confirmation

- `sim_player_manager.lua` restored to md5 `4731478d8c96cf0393c06ef2d483cfa9`; container and worktree identical.
- After the restore restart (pid 810396): harness false, progression false, every `playerBotProgression` gate false, `earlyGame.enabled` false, `combatTelemetry` / `movementTelemetry` off, `combatDiag.logging` false, tracedCount 0. The skip-set knobs read 60 / 8 and `levelBandAbove` is back to 3. `records` reads 0 because progression is gated off at boot.
- Both probe configs were reverted by the same restore; the novice distribution count is back to 1.
- All samplers and watchers stopped.
- No database, roster or unrelated log was cleared.
- Progression persisted and is real: during this run subject 274 trained `combat_brawler_unarmed_01` (skills 1 → 2, 22 kills, 3 deaths), and novice 84 continued from its earlier 2 boxes. Neither identity was retired or reset.

## Verdict

**LIVE_VERIFICATION_PASS — 43/43**, with two owner-approved amendments (D19 wording, D11 probe configuration), one mechanism deviation (no mint; the existing second novice became active), one subject-selection rule change made before any D assertion was read, and one evidence-source merge for D15 forced by log truncation. Receipt recorded against this report.
