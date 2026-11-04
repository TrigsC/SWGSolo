# Code Review: P.10p G5: Healing (players heal bots, Medic bots heal with real stims)

**Review Date**: 2026-11-04  
**Version**: 0.9.20  
**Files Reviewed**:

- `MMOCoreORB/bin/scripts/managers/sim_player_manager.lua`
- `MMOCoreORB/bin/web/aieconomy-dashboard/app.js`
- `MMOCoreORB/src/server/zone/objects/creature/CreatureObjectImplementation.cpp`
- `MMOCoreORB/src/server/zone/objects/creature/commands/HealDamageCommand.h`
- `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimBotHealing.h`
- `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimHunterController.cpp`
- `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimHunterController.h`
- `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp`
- `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.h`
- `MMOCoreORB/src/tests/SimBotHealingTest.cpp`
- `MMOCoreORB/utils/engine3`
- `docs/1-plans/ROADMAP_p10-playerbot-parity.md`
- `docs/ai-pve-playerbot-design.md`
- `VERSION`

**Plan**: `workspace/Core3/docs/1-plans/F_0.9.19_p10p-g5-healing.plan.md`

---

## Executive Summary

This change adds gated, real-stim healing between players and roster bots, including medical XP, bazaar restocking, group commands, self-healing, dashboard observability, and verification scenarios. Review iterations found production and harness correctness issues; all were addressed and verified by a warning-clean build, 228 passing SimBot tests, scoped harness runs, and live client testing. Intentional LIVE-ONLY working-tree configuration remains excluded from release staging.

APPROVED with observations

---

## Changes Overview

The stock healing path now recognizes authorized roster bots while preserving faction, range, LOS, cooldown, and damage-stim rules. Medic bots select patients through a pure policy, consume real vault stims, earn medical XP for healing others, restock through the debit-first bazaar path, and publish their order/readiness state.

The hunter controller owns healing movement and application, while `SimPlayerManager` owns authority, orders, claims, supplies, counters, and dashboard state. Lua configuration, parity scenarios, unit tests, roadmap, and design documentation were updated alongside the implementation.

---

## Findings

### Critical Issues

None.

### Major Issues

1. **Valid stim listings were treated as `Consumable` objects** — `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp:17249`.  
   Valid pharmaceutical listings could be skipped or have doses read through the wrong type. The implementation now casts to `StimPack`, excludes ranged/pet/droid variants, checks the actual medicine-use requirement, and reads the stim use count at `SimPlayerManager.cpp:17252-17260`.  
   **Disposition: Addressed.**

2. **Restocking could purchase unusable stim tiers and overbuy across tiers** — `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimBotHealing.h:403`.  
   Restocking now pools only doses usable at the bot’s healing ability, filters template requirements, treats any pending usable purchase as satisfying the need, and permits one pooled stim purchase per bazaar visit at `SimBotHealing.h:418-433`, `SimPlayerManager.cpp:12143`, `:16153`, `:17005`, and `:17848`. Regression coverage is present at `MMOCoreORB/src/tests/SimBotHealingTest.cpp:186-221`.  
   **Disposition: Addressed.**

3. **Pets and unauthorized creatures could enter the healing path and receive invalid medical-XP treatment** — `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimHunterController.cpp:2572`.  
   Candidate collection now accepts only players and authority-resolved roster bodies at `SimHunterController.cpp:2572-2577`; the application path independently refuses pets and unsupported creatures at `SimPlayerManager.cpp:9251-9257`.  
   **Disposition: Addressed.**

4. **Borrowed production identities could not arm the healing gate in parity scenarios** — `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp:29331`.  
   `setHealingGate` now resolves `production` and `production2` through the grouping identity resolver while retaining positional lookup for harness identities. The generic identity lookup also recognizes cached borrowed references.  
   **Disposition: Addressed.**

5. **Named-command scenarios used literal names and could not reliably target the borrowed live bots** — `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp:28382`.  
   `groupSay` now resolves `{production}` and `{production2}` to borrowed bots’ runtime first names using guarded literal replacement. The named scenario uses those placeholders and accepts exactly one real-order or no-Medic dispatch at `MMOCoreORB/bin/scripts/managers/sim_player_manager.lua:3640`.  
   **Disposition: Addressed.**

6. **Multi-counter harness baselines were stored under one combined key, allowing false passes and above-delta failures** — `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp:28067`.  
   `resetGroupingCounterBaseline` now splits, trims, and lowercases each counter exactly like `assertGroupingCounter`, then stores each baseline separately at `SimPlayerManager.cpp:28074-28089`.  
   **Disposition: Addressed.**

7. **Zero-delta healing assertions used an operation that did not parse `delta=0`** — `MMOCoreORB/bin/scripts/managers/sim_player_manager.lua:3641`.  
   The no-Medic and gate-off scenarios now use `assertGroupingCounter` with `minDelta=0` and `maxDelta=0`, after a positive counter has established that chat dispatch completed, at `sim_player_manager.lua:3641` and `:3644`.  
   **Disposition: Addressed.**

8. **Patient miss reasons were emitted for scratches below the automatic-heal threshold** — `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimBotHealing.h:303`.  
   Threshold eligibility now precedes range, refusal, and LOS classification. `PatientChoice.reasonIndex` identifies the actual subject, and LOS failures use the explicit `unreachable` reason at `SimBotHealing.h:311-369`. Regression coverage is at `MMOCoreORB/src/tests/SimBotHealingTest.cpp:83-109`.  
   **Disposition: Addressed.**

9. **A named heal order could select another injured member or falsely complete for an absent patient** — `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimBotHealing.h:297`.  
   When an ordered target is present, all other candidates are excluded at `SimBotHealing.h:297-306`. Newly received named orders also verify that the patient remains a live candidate and terminate as `patient_left` when absent at `SimHunterController.cpp:2637-2649`. Regression coverage for unhurt and out-of-range named targets is at `MMOCoreORB/src/tests/SimBotHealingTest.cpp:111-131`.  
   **Disposition: Addressed.**

10. **Automatic heal misses monopolized the group tick and froze follow, assist, and defense** — `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimHunterController.cpp:2689`.  
    The controller now posts the throttled faction/range/LOS line and returns `false` at `SimHunterController.cpp:2689-2703`, allowing assist and follow handling to continue through `:3157-3319`. Ordered-heal failures retain their earlier terminal return.  
    **Disposition: Addressed and live-verified.**

### Minor Issues

1. **Low-stim chatter could repeat because trigger and rearm logic counted different stacks** — `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimHunterController.cpp:685`.  
   Readiness, restocking, post-heal reporting, and latch rearming now use the same pooled usable-dose calculation at `SimHunterController.cpp:685-692`, `:2832`, and `SimBotHealing.h:403`.  
   **Disposition: Addressed.**

2. **Grouped-bot dashboard data omitted the current heal stage** — `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp:34857`.  
   Idle, approach, and apply transitions are published by the controller; inspect and grouped-bot rows expose `healStage`, order generation, and patient OID at `SimPlayerManager.cpp:34857`, `:35380`, and `:70800`.  
   **Disposition: Addressed.**

3. **Reported test evidence was temporarily stale after restock and latch fixes** — `MMOCoreORB/src/tests/SimBotHealingTest.cpp:83`.  
   The final evidence is a clean `-Werror` build and 228 passing SimBot tests, including threshold, ordered-target, restock-pooling, parser, selection, and faction cases.  
   **Disposition: Addressed.**

4. **Unrelated live configuration appeared in the full working-tree diff** — `MMOCoreORB/bin/scripts/managers/sim_player_manager.lua:388`.  
   Reserve changes, diagnostic logging, enabled live gates, the dirty `engine3` marker, and backup directories were identified by the owner as deliberate LIVE-ONLY or excluded working-tree state. Release staging is limited to the G5 hunks and documentation.  
   **Disposition: Accepted with owner override; excluded from release staging.**

### Suggestions

None.

---

## Checklist

- [x] 1. Functional Requirements — Passed; production and harness defects found during review were addressed.
- [x] 2. Code Quality — Passed; pure policy, controller execution, and manager ownership remain separated.
- [x] 3. Architectural Compliance — Passed; implementation follows the documented manager/controller and lock-ownership patterns.
- [x] 4. Distributed Object / IDL Discipline — Passed; managed references and established lock order are preserved, with no autogen edits.
- [x] 5. Lua/C++ Boundary — Passed; policy tunables and verification scenarios remain in manager Lua.
- [x] 6. AI-Economy / Simulation Safety — Passed with accepted staging observation; G5 is gated, live-verified, and LIVE-ONLY working-copy settings are excluded from release staging.
- [x] 7. Error Handling — Passed; patient-left, no-damage, faction, range, LOS, cooldown, supply, and timeout outcomes are explicit.
- [x] 8. Security — Passed; no authentication changes, sensitive-data exposure, or new unauthenticated endpoints.
- [x] 9. Performance — Passed; group scans remain bounded and linear, and the final controller fix removes tick starvation.

---

## Verdict

**APPROVED with observations**

All review findings are resolved or explicitly overridden by the owner. The archival record should retain that the full worktree contained intentional LIVE-ONLY configuration and excluded `engine3`/backup state; those items are not part of release staging. Final verification evidence is a warning-clean build, 228 passing SimBot tests, scoped parity-harness validation, and live client confirmation of the corrected healing and group-tick paths.
