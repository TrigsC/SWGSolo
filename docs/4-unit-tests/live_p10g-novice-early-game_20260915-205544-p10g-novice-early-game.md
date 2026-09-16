# Live verification — F_0.9.6 novice early-game loop (v8 contract)

**Run ID**: `20260915-205544-p10g-novice-early-game`

**Plans**:
- `docs/1-plans/F_0.9.6_p10g-novice-early-game-loop.plan.md`
- `docs/1-plans/F_0.9.6-D1_sim-combat-observability.plan.md`

**Branch**: `feat/p10g-novice-early-game-loop`, uncommitted: 16 modified files, +5655/−195 excluding engine3.

**Binary**: `f3ab9c165ed592aab0eb5f3200f91e73`, built 2026-09-15 20:52:28.

**Contract**: v8, md5 `3490f52d`, copied to the evidence directory before the first boot and unchanged during the run.

**Supersedes**: `live_p10g-novice-early-game_20260915-185828-p10g-novice-early-game.md` (FAIL 34/42).

**Verdict**: **LIVE_VERIFICATION_PASS**, owner-accepted, at **42/43**. D7 was not reached. The owner reviewed the gap and directed a PASS receipt: *"Record PASS, We will add more validation/visualization to the bots skills and such at a later time."* This is not a clean 43/43, and the receipt carries the same 42/43 count.

## Summary

This run verifies the fix for the 185828 Run D livelock: the single-slot unreachable-target skip became a bounded set (`unreachableSkipSeconds` 60, `unreachableSkipMaxTargets` 8).

- Runs A, B and C pass in full, 22/22.
- Run D passes 20/21, including the new **D21** stall assertion that reproduces the old defect on the old data (657 stalled windows) and finds none here.
- The novice completed the whole early-game loop: 13 kills, every one paid, Recover after each, one natural death and respawn at full HAM, two spawner anchors reached, 8 defensive flees from a stronger creature joining a fight, and no PACK creature ever self-selected.
- No boot had a crash, hang or REST error.

**D7 (training) was not reachable in the window.** Identity 84 had already trained `combat_brawler_unarmed_01` during quick check `20260915-175249`, so its next box costs 5000 XP. It earned 1161 XP in 75 minutes and still needed 3028 (2830 after a further 18-minute extension window). Reaching the threshold needs roughly 3 more hours at about 89 XP per kill. This is a contract sizing gap for a carried-over identity, not a defect: training on this build is evidenced by four harness scenarios and by quick check 175249, where the box trained 25 s after the threshold.

## Static gate

| Check | Result |
|---|---|
| Incremental build, `-Werror` | PASS: no-op (IDL step only), binary `f3ab9c16` |
| `luac -p`: shipped `4731478d` + v5 A/B/C/D/Ddiag/Dprobe | PASS |
| GoogleTest | N/A: no suite covers these files |
| Codex code review | APPROVED (skip set, round 1, no new findings) |
| `git diff --check` | clean |

## Restarts

Six guarded `server-cycle.sh` restarts: A phase A, A phase B, B, C, D and the restore. Each was ready in 7–12 s with 0 connected players. The Sep 5 zombie core3 (pid 2114320) was ignored.

| Boot | Config md5 | core3 pid | Ready |
|---|---|---|---|
| A phase A | `266e0b70` | 788192 | 8 s |
| A phase B | `266e0b70` | 788819 | 12 s |
| B | `55cf3a95` | 789367 | 12 s |
| C | `fe442ef3` | 789910 | 12 s |
| D | `f8440a11` | 790473 | 7 s |
| restore | `4731478d` | 795747 | 8 s |

`v5_Dprobe` (`88ead1bc`) was not needed: Run D had a natural death.

## Assertions

| # | Assertion | Expected | Actual | Result |
|---|---|---|---|---|
| U1 | Clean startup | fresh who.json | every boot, 7–12 s | PASS |
| U2 | One live core3 per boot | 1 non-zombie | 788192 / 788819 / 789367 / 789910 / 790473 / 795747 | PASS |
| U3 | No crash markers | 0 | gdb.log unchanged at 961371; 0 in the core3.log delta | PASS |
| U4 | No hang: every dashboard poll 200 | 0 non-200 | 14 + 7 + 7 + 8 watcher polls; Run D sampler 900 samples and extension 212 samples, 0 errors | PASS |
| U5 | No `RESTServer ERROR` in core3_api.log delta | 0 | 0 | PASS |
| A1 | Phase A: 59 PASS + restart probe AWAITING, 0 FAIL | — | exactly that; runId 1789505902283 | PASS |
| A2 | Phase B 60/60 | 60/60 | 60/60; `retained_record_survives_restart` PASS (680 ms) | PASS |
| A3 | Six F_0.9.6 scenarios | PASS | all PASS | PASS |
| A4 | Both real-kill scenarios | PASS | unarmed 20.9 s, rifle 3.8 s | PASS |
| A5 | `novice_box_precondition_refuses_body` | PASS | PASS (1004 ms) | PASS |
| A6 | orphanRecords / rosterWithoutRecord | 0 / 0 | boot 1: 0 / 0 (records 22); boot 2: 0 / 0 (records 21) | PASS |
| A7 | simcombat.log untouched while logging off | size + mtime unchanged | 1571662 / 1789499541 across both boots | PASS |
| B1 | Guard gate-off refusal | `precondition_gate_off_awardKillXp` | exactly that | PASS |
| B2 | No step executed | durationMs 0, killXp.kills 0 | 0 ms, kills 0, awards 0, botKillsSeen 0 | PASS |
| C1 | Both kill scenarios, trace on | PASS | unarmed 26.9 s, rifle 3.9 s | PASS |
| C2 | Guard gate-on refusal | `precondition_gate_on_trainingEnabled`, 0 ms | exactly that | PASS |
| C3 | One wall-clock run id | 1 | `1789506508576` (166 lines) | PASS |
| C4 | Trace lines key=value | 0 malformed | 0 / 166 | PASS |
| C5 | Attack enqueue per kill bot | ≥ 1 `enqueued` | unarmed bot …614: 5; rifle bot …624: 2 | PASS |
| C6 | Kill-XP trace per kill | `event_queued`, `reason=included`, both grants `granted=1` | unarmed 85 + 8 (identity 435); rifle 85 + 8 (identity 436); all four grants `granted=1`; drop counters agreed (botKillsSeen 2, kills 2, dropped 0) | PASS |
| C7 | Dashboard while traced | writeErrors 0, HTTP 200 | 0, 8/8 | PASS |
| C8 | Allowlist empties after cleanup | tracedCount 0 | 0 | PASS |
| D1 | Novice enters grind | ≤ 360 s | 99 s | PASS |
| D2 | LeaveCity → SelectTarget → Engage | ≤ 1500 s | first Engage at 304 s | PASS |
| D3 | Displacement | ≥ 300 m | 699 m over 859 positions | PASS |
| D4 | TTK window on self-selected Engage | 0–900 s | 174 samples, TTK 6.5–9.6 | PASS |
| D5 | Real kill + XP | ≥ 1 within 2400 s | 13 kills, first at 339 s; +1161 XP; awards +26; `lastAwardSource=kill` | PASS |
| D6 | Recover after a kill; no frozen tick | kill → Recover; tick age < 10 s | Recover after the first kill; max `lastTickAgeMs` 3.0 s over 848 live samples | PASS |
| D7 | Train once threshold met | threshold reached, then ≤ 120 s | threshold never reached: 4189 XP needed at start, 3028 at end (2830 after the extension). ~3 h more required | **NOT REACHED** |
| D8 | Demand independence | hunts Δ0, signalUnits 0 | Δ0, {0} | PASS |
| D9 | weaponXpType == nextXpType on live bodies | 0 mismatches | 851 live samples, 0 mismatches (49 corpse samples excluded) | PASS |
| D10 | No credit mutation | unchanged | (0, 0) | PASS |
| D11 | Death → validated respawn | deaths ≥ 1, respawns ≥ 1, new body, same planet | 1 natural death, respawns +1, 2 bodies, tatooine | PASS |
| D12 | ≤ 1 identity minted | roster Δ ≤ 1 | 21 → 21 | PASS |
| D13 | Unprovoked attack → self-defence | allowed decision + counters rise | 12 unprovoked samples; decisions `engage_phase`, `flee_other_attacker`, `flee_recovering`, `no_attacker`; 0 disallowed; counters +103 | PASS |
| D14 | 800 HAM, heal-on-death | max 800 on all pools; first sample ≥ 0.95 | both bodies first min-pool fraction 1.0; 0 samples with max ≠ 800 | PASS |
| D15 | Every death has a captured cause + zone | 0 missing / uncaptured | 1 death, cause captured (`womp_rat_hue.iff` level 13), 1 zone | PASS |
| D16 | No Recover livelock | no span > 900 s without HAM rising | 34 spans, longest 195 s | PASS |
| D17 | Spawner roaming | anchor on a spawn egg, reached | spawners 0 and 12 anchored and reached within 60 m; first at 794 s | PASS |
| D18 | PACK filter | ≥ 1 target, ≥ 80% resolved, 0 PACK-only | 16 self-selected targets, 13 resolved (81%), 0 PACK-only; templates lesser_desert_womp_rat, rill, worrt_hue | PASS |
| D19 | All-attackers flee | ≥ 1 `flee_other_attacker`, defenders ≥ 2 | 8 lines, e.g. rill level 6 engaged with rill level 9 at 4 m | PASS |
| D20 | Kill-XP accounting | every kill paid; 0 novice drops; lines ≈ counters | 13 kills, 0 unpaid, 0 novice drop lines, 0 drop lines vs counter Δ 0, botKillsSeen +77 | PASS |
| D21 | No target-selection stall | no 900 s Engage/SelectTarget window without a kill, re-anchor or 50 m | none over 851 live samples | PASS |

**Required assertions: 43. Passed: 42. Not reached: 1 (D7).**

## The fix, verified (D21)

The 185828 failure was an Engage ping-pong: two kreetles off the city navmesh, each path failure overwriting the other's single-slot skip, for 70 minutes with 0 kills.

- `earlyGameSkipTargetOid` / `...UntilMs` became parallel `Vector<uint64>` lists with one expiry per target.
- `rememberEarlyGameUnreachableTarget` drops expired and duplicate entries and evicts the soonest-expiring entry at capacity; `isEarlyGameTargetSkipped` gates the scan.
- Both knobs are Lua-configured and clamped, and appear on the dashboard (`unreachableSkipSeconds` 60, `unreachableSkipMaxTargets` 8).

D21 measures the symptom directly rather than the mechanism. On the 185828 samples it reports 657 stalled windows; on this run, none. The novice re-anchored normally, reaching two different spawn eggs, and kept killing throughout the 75 minutes.

## D7 — why the threshold was out of reach

- Identity 84 is not a fresh novice: it trained `combat_brawler_unarmed_01` in quick check `20260915-175249` (threshold at t+1900, trained by t+1925, 25 s).
- Its next box, `combat_brawler_unarmed_02`, costs 5000 XP of `combat_meleespecialize_unarmed`.
- Run D earned 1161 XP in 75 minutes (13 kills at roughly 89 XP each), ending 3028 short. An extra 18-minute extension window (212 samples, 2 more kills, addendum evidence only) closed that to 2830.
- So D7 needs about a 4.5-hour Run D for this identity, or a fresh novice.

Training on this build is evidenced instead by the parity harness, which reads the **live body's** SKILLBOX mod and requires it to be at least the creature template's baseline. All four scenarios passed in both Run A boots:
- `body_respawn_retains_skill_and_mod`
- `body_mid_life_reapplication`
- `body_mod_never_below_template_baseline`
- `body_mid_life_training_updates_live_body`

The mechanism behind them: `buildBodyProgressionSnapshot` sums every trained skill's `Skill::getSkillModifiers`, `applyProgressionToBody` writes them as SKILLBOX mods at template value plus delta, and it runs both at spawn (`spawnPveIdentityBody`) and immediately after training (`trainPlayerBotSkill` → `reapplyPlayerBotProgressionToLiveBody`). `AiAgentImplementation::getSkillMod` returns the CreatureObject value first, and `CombatManager` reads `attack_accuracy` + `melee_accuracy` for AI attackers, the attack command's accuracy mod, and melee damage bonuses.

**Contract debt (carried to the plan):** D7 assumes a fresh novice. For a carried-over identity, either size the Run D window to the next box's cost, or let D7 cite this harness evidence.

**Owner item deferred:** more validation and visualisation of a bot's skills and applied mods. Nothing today reports one live body's skill mods, and the dashboard publishes `skillMods` only for a plan's goal box, so a specific box's grant is not readable from the dashboard.

## Evidence

Under `/home/swgemu/workspace/Core3/MMOCoreORB/bin/log/trip-verify-20260915-205544-p10g-novice-early-game*`:
- `.contract.md` (v8, md5 `3490f52d`)
- `-A-phaseA.json`, `-A-phaseB.json`
- `-B.json`
- `-C.json`, `-C-simcombat.log`
- `-D-samples.jsonl`, `-D-core3.log`, `-D-analysis.txt`
- `-D-extension-samples.jsonl` (addendum, outside the contract window)

## Cleanup confirmation

- `sim_player_manager.lua` restored to md5 `4731478d8c96cf0393c06ef2d483cfa9`; the container file and the worktree file are identical.
- After the restore restart (pid 795747) the dashboard reads false for harness, progression, every `playerBotProgression` gate, `earlyGame.enabled`, `combatTelemetry`, `realWeaponDamage` and `combatDiag.logging`; tracedCount 0. The new knobs read 60 / 8.
- The sampler and all watchers are stopped.
- No database, roster or unrelated log was cleared.
- Identity 84 keeps its real progression. Across Run D and the extension window — after four restarts — it held `combat_brawler_novice` + `combat_brawler_unarmed_01`, skillsTrained 2, skillPointsSpent 17, 0 credits, with XP needed falling 4189 → 3028 → 2830. A direct MySQL read was attempted for the same fact but the client was refused (`swgemu@localhost`, access denied) from the container shell, so persistence is cited from the dashboard samples rather than from the tables.

## Verdict

**LIVE_VERIFICATION_PASS** at 42/43, owner-accepted, with D7 not reached and its debt recorded. The 185828 livelock is fixed and measured by D21. The receipt records 42/43 and names the D7 deviation.
