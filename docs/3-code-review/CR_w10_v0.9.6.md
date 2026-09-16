# Code Review: Novice Early-Game Loop (P.10g)

**Review Date**: 2026-09-15  
**Version**: 0.9.6  
**Files Reviewed**:

- `MMOCoreORB/bin/scripts/managers/sim_player_manager.lua`
- `MMOCoreORB/src/server/zone/managers/combat/CombatManager.cpp`
- `MMOCoreORB/src/server/zone/managers/combat/CombatManager.h`
- `MMOCoreORB/src/server/zone/managers/player/PlayerManagerImplementation.cpp`
- `MMOCoreORB/src/server/zone/objects/creature/ai/AiAgent.idl`
- `MMOCoreORB/src/server/zone/objects/creature/ai/AiAgentImplementation.cpp`
- `MMOCoreORB/src/server/zone/objects/creature/ai/events/AiBehaviorEvent.h`
- `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimHunterController.cpp`
- `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimHunterController.h`
- `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerController.cpp`
- `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerController.h`
- `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp`
- `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.h`
- `MMOCoreORB/utils/engine3`
- `VERSION`
- `docs/1-plans/ROADMAP_p10-playerbot-parity.md`
- `docs/ARCHI.md`
- `docs/ai-pve-playerbot-design.md`

**Plan**: `docs/1-plans/F_0.9.6_p10g-novice-early-game-loop.plan.md`

---

## Executive Summary

The change implements the planned novice progression loop, including early-game movement, combat, recovery, training, respawn, telemetry, parity scenarios, and runtime configuration. All correctness and lifecycle findings raised during the iterative review were addressed; the remaining observations are explicitly owner-deferred behavior and the pending live-verification receipt for the final unreachable-target skip-set change.

**APPROVED with observations**

---

## Changes Overview

The implementation adds a gated early-game state machine for planned player bots, integrates progression-aware equipment and XP handling, and introduces production-safe death, recovery, navigation, and defensive-combat behavior. Successive live checks drove fixes for disconnected navigation areas, clone wounds, recovery scheduling, target selection, weapon range, group aggression, multi-attacker defense, and unreachable-target livelock. Default-off movement, combat, death, and kill-XP telemetry was added to make live failures diagnosable without changing normal behavior.

---

## Findings

### Critical Issues

None.

### Major Issues

1. **Live gate activation did not reconcile the equipped weapon — addressed.**  
   `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp:11109` now compares the desired progression weapon XP type with the recorded live state and reapplies progression outside manager mutexes when they differ.

2. **Entering grind mode leaked an active demand order — addressed.**  
   `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimHunterController.cpp:2436` declines the active order and resets order, lair, doctor-request, work-loop, interior-approach, hybrid-movement, and mission-offer state during early-game entry.

3. **The maintenance death fallback bypassed normal cloner respawn handling — addressed.**  
   `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp:16541` routes collected corpses through `onPveHunterDied` before removal, while the body remains resolvable and outside `pveMutex`.

4. **Death finalization could run again after the identity/body mapping changed — addressed.**  
   `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp:20517` validates the current identity-to-body mapping and tombstone before mutating death state, preventing stale or replacement-body callbacks from double-counting.

5. **Order preemption did not release the mission lair and retained mission identifiers — addressed.**  
   `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp:20016` centralizes lair cleanup in order decline, while `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimHunterController.cpp:2436` clears the associated mission state.

6. **Successful loadout reconciliation left the recorded weapon XP type stale — addressed.**  
   `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp:8305` records the actual equipped XP type after releasing the agent lock; failed equips remain mismatched so a later sweep retries.

7. **Death finalization published its tombstone before storing the cloner override — addressed.**  
   `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp:20498` resolves the override candidate before claiming the death, then validates, counts, and stores it in the same critical section.

8. **The configured production novice city leaked into parity-harness identities and explicit harness spawns — addressed.**  
   `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp:11240` gives harness-exempt identities a harness-specific home, and `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp:16110` avoids production-home resolution when an explicit spawn override exists.

9. **Using the persisted harness home still mismatched the parity-test body location — addressed.**  
   `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp:11247` resolves harness-minted identities to the shuttleport nearest the configured parity-test spawn, aligning anchors and rest points with the body’s actual planet.

10. **No eligible attacker could leave the bot in stale combat indefinitely — addressed.**  
    `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimHunterController.cpp:2708` resets the phantom-combat counter when appropriate and clears stale combat after the established six-tick threshold without destroying an active recovery path.

11. **Sweep-first deaths bypassed controller-only death-site and killing-blow capture — addressed.**  
    `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp:20508` resolves the death site and consumes the killing-blow entry in the shared death finalizer before claiming and removing the corpse.

12. **Death-zone deduplication by position suppressed later deaths at the same location — addressed.**  
    `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp:11414` deduplicates death records by body object ID, so a new body dying at the same coordinates creates a fresh avoidance record.

13. **A body respawned while the gate was off could retain fatal legacy clone wounds after entering early-game mode — addressed.**  
    `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimHunterController.cpp:2521` caps wounds on early-game entry, and `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimHunterController.cpp:3107` reconciles the cap during recovery so regeneration can reach the configured rest threshold.

14. **Self-defense applied the proactive minimum-TTK rule and fled from trivially weak attackers — addressed.**  
    `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimHunterController.cpp:2600` bypasses only the minimum TTK when `allowInCombat` is true, retaining the maximum TTK and upper-level safety bounds.

15. **The player-like HAM override altered parity-harness bodies — addressed.**  
    `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp:16264` identifies explicit overrides, harness professions, and harness-exempt identities before applying the novice HAM override at `SimPlayerManager.cpp:16218`.

16. **Attackers joining after Engage began were never included in the flee decision — addressed.**  
    `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimHunterController.cpp:2081` centralizes the all-attacker safety check, and `SimHunterController.cpp:3064` runs it on every Engage tick so a newly joined unwinnable attacker causes an immediate defensive flee.

17. **Live-verification evidence was requested before the workflow-authorized verification phase — accepted workflow override.**  
    The objection was valid under `docs/ARCHI.md`: server restart and live verification belong to TRIP-verify after review convergence. The plan deliberately retains the final receipt item at `docs/1-plans/F_0.9.6_p10g-novice-early-game-loop.plan.md:1028`; no code claim states that the latest verification passed.

### Minor Issues

1. **The design and roadmap documentation were stale — addressed.**  
   The completed design is documented in `docs/ai-pve-playerbot-design.md:575`, and the implementation status is reflected in `docs/1-plans/ROADMAP_p10-playerbot-parity.md:6`.

### Suggestions

1. **Owner-deferred combat-survival refinements remain open — accepted product decision.**  
   After-flee target suppression, an incoming-damage check, and further flee-path/pack-ambush handling remain explicitly unselected at `docs/1-plans/F_0.9.6_p10g-novice-early-game-loop.plan.md:997`. These are documented follow-ups, not blockers for the selected implementation.

2. **Run the final live receipt against the bounded unreachable-target skip set.**  
   Earlier live verification exposed the alternating-target livelock, and the code now maintains a bounded expiring skip set. The post-fix live receipt remains tracked at `docs/1-plans/F_0.9.6_p10g-novice-early-game-loop.plan.md:1028`.

---

## Checklist

- [x] 1. Functional Requirements — Plan behavior and selected owner decisions are implemented; all review correctness findings are resolved.
- [x] 2. Code Quality — State transitions, helpers, configuration, telemetry, and ownership boundaries are explicit and warning-clean.
- [x] 3. Architecture — Manager/controller responsibilities and mutex choreography follow the established project patterns.
- [x] 4. IDL — IDL changes are scoped to required AI state and preserve existing integration patterns.
- [x] 5. Lua/C++ Integration — Configuration names, defaults, clamps, dashboard output, and parity-harness operations are aligned.
- [x] 6. AI-Economy / Simulation Safety — Code review passes. *(Post-synthesis annotation: the outstanding TRIP-verify receipt for the unreachable-target skip set was subsequently recorded — run `20260915-225334`, 43/43 PASS, report `docs/4-unit-tests/live_p10g-novice-early-game_20260915-225334-p10g-novice-early-game.md`.)*
- [x] 7. Error Handling — Failed equips retry, stale combat clears, path failures back off, and death finalization is idempotent.
- [x] 8. Security — No new external trust boundary or privileged user-controlled execution path is introduced.
- [x] 9. Performance — Diagnostic gates default off, skip collections are bounded, and expensive or locking work remains outside hot critical sections where practical.

---

## Verdict

**APPROVED with observations**

All code-review blockers were resolved. The TRIP-verify gate this verdict made promotion conditional on has since been satisfied: run `20260915-225334` recorded **43/43 PASS** against the v9 contract, with two owner-approved post-hoc contract amendments documented in the report (a D19 clause the behaviour does not require, and a forced-death probe whose 600 s minimum fight length made the subject unable to engage and therefore unable to die). Owner-deferred combat refinements remain documented and do not invalidate the selected scope.

---

# Companion chunk: F_0.9.6-D1 — Sim-combat observability

**Plan**: `docs/1-plans/F_0.9.6-D1_sim-combat-observability.plan.md`
**Review thread**: separate Codex loop (state file
`docs__1-plans__F_0.9.6-D1_sim-combat-observability.plan.md.review.txt`)
**Verdict**: **APPROVED**

This chunk ships the default-off, allowlist-gated `SimCombatDiagLog`
(`bin/log/simcombat.log`, `combatDiag.logging`) plus the `pveActivity.combatDiag`
dashboard section, tracing behaviour-tree ticks, attack selection/enqueue and the
full kill-XP path (`KILL_XP_CANDIDATE` / `KILL_XP_DISSEMINATE` / `KILL_XP_AWARD`).
It is released together with P.10g because it is the instrument the P.10g
kill-XP and combat assertions are measured with.

Findings raised and addressed on that thread:

1. **[Major]** Append-only logs reused `run=1` after every restart — run IDs are
   now timestamp-seeded (`SimCombatDiagLog.h:147`, `:159`).
2. **[Major]** Attack-enqueue evidence was malformed and the two enqueue paths
   suppressed one another — separators and independent rate-limit keys corrected
   (`AiAgentImplementation.cpp:3588`, `:3602`, `:3691`).
3. **[Minor]** The agent snapshot reported only the empty legacy `aiTemplate`
   field — `customAiMap` included (`AiAgentImplementation.cpp:5523`, `:5528`).
4. **[Minor]** The dashboard exposed the snapshot only as an opaque `diag` string
   — raw and materialized fields now both available (`SimPlayerManager.cpp:21052`,
   `:21055`).
5. **[Minor]** Negative primary HAM values fell through the unsigned parser and
   became strings — signed/unsigned parsing separated (`SimPlayerManager.cpp:21083`,
   `:21089`).
6. **[Major]** Empty `aiTemplate=` tokens made the combat dashboard return HTTP
   502 — safe range overload adopted (`SimPlayerManager.cpp:21066`, `:21071`).
   (This is the engine3 `subString` boundary class of defect.)
7. **[Major]** The disposable live-verification configuration had not been
   restored — verified restored by MD5 with diagnostics, parity harness,
   progression and all capability gates default-off (`sim_player_manager.lua:673`,
   `:1163`, `:1711`, `:1716`).
8. **[Major, owner-reported]** Unlocked `startCombat` harness calls — `startCombat`
   now cross-locks the defender against an already-held attacker lock
   (`CombatManager.cpp:106`), matching established production callers.

No new correctness, safety, architectural or performance issues were found on the
final pass; every checklist item passes, including IDL/lock discipline and
simulation safety with gates restored default-off.

---

## Release note

Both chunks ship with every gate **default-off**:
`playerBotProgression.earlyGameEnabled`, `earlyGame.realWeaponDamage`,
`pveConfig.novice.enabled`, `playerBotProgression.awardKillXp`,
`trainingEnabled`, and `combatDiag.logging`. Known behaviours deliberately left
unfixed (owner decisions, recorded in the plan and the live report): no memory of
a creature the bot fled from, no incoming-damage term in target eligibility,
flee paths that can pin against off-navmesh geometry, and no telemetry for a live
body's applied skill mods.
