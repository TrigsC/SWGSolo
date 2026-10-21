# Code Review: P.10p G2: Rally and Buffs for Grouped PlayerBots

**Review Date**: 2026-10-21  
**Version**: 0.9.17  
**Files Reviewed**:

- `MMOCoreORB/bin/scripts/managers/sim_player_manager.lua`
- `MMOCoreORB/bin/web/aieconomy-dashboard/app.js`
- `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimBotGrouping.h`
- `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimHunterController.cpp`
- `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimHunterController.h`
- `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerController.cpp`
- `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerController.h`
- `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp`
- `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.h`
- `MMOCoreORB/src/tests/SimBotGroupingTest.cpp`
- `MMOCoreORB/utils/engine3`
- `docs/1-plans/ROADMAP_p10-playerbot-parity.md`
- `docs/ai-pve-playerbot-design.md`

**Plan**: `workspace/Core3/docs/1-plans/F_0.9.16_p10p-g2-rally-buffs.plan.md`

---

## Executive Summary

This change adds grouped PlayerBot rally directives, routed buff trips, rally holds, readiness reporting, shuttle fallback, and supporting diagnostics and verification scenarios. All blocking findings were addressed, the sole workflow finding was accepted under the documented release process, and reported lint, build, unit, and live verification gates passed. APPROVED

---

## Changes Overview

The implementation adds directive parsing and policy in `SimBotGroupRally.h`, integrates directive and group-buff state into the PlayerBot manager and controllers, and extends Lua configuration, diagnostics, dashboard output, design documentation, and verification scenarios. Subsequent iterations corrected route invalidation, destination selection, readiness state, phrase parsing, and several verification-harness races and flakes.

---

## Findings

### Critical Issues

None.

### Major Issues

1. **Failed shuttle route remained reusable** — `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp:36790`. A suppressed shuttle could retain its failed legs and repeatedly select the same route instead of falling back to walking. **Disposition: addressed** by rotating the route epoch, clearing legs, and resetting the leg index at `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp:36804`.

2. **Leader/custom destinations could not select `meet_city` for buffing** — `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp:36478`. These destinations supplied no city to the buff-city chooser. **Disposition: addressed** by resolving the nearest routable city to the destination point at `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp:36482`.

3. **Chained rally phrases were classified as unknown places** — `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimBotGroupRally.h:225`. Multi-word buff, meet, and leader phrases were not wholly excluded from unknown-place scanning. **Disposition: addressed** by marking complete phrase spans at `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimBotGroupRally.h:228`, with regressions at `MMOCoreORB/src/tests/SimBotGroupRallyTest.cpp:88`.

### Minor Issues

1. **Ready and status responses used operation outcome instead of current body buffs** — `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimHunterController.cpp:1003`. This could report an incorrect readiness line after partial or fallback buff outcomes. **Disposition: addressed** by computing body buff needs, persisting `holdHasBuffs` at `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp:36429`, and using it for hold-status replies at `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp:64510`.

2. **New rally gtest was ignored and absent from the ordinary change inventory** — `MMOCoreORB/.gitignore:25`. **Disposition: accepted with override** because repository convention requires test files to be force-added during release, explicitly documented at `docs/1-plans/F_0.9.16_p10p-g2-rally-buffs.plan.md:814`.

3. **Travel-leg counter could advance before the scenario re-baselined it** — `MMOCoreORB/bin/scripts/managers/sim_player_manager.lua:3618`. Moving the leader first left a race in which the intended leg was excluded from the delta. **Disposition: addressed** by resetting the counter baseline before moving the stand-in; the operation is implemented at `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp:25018`.

4. **Attacker positioning retried combat rather than selecting a clear position first** — `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp:25370`. Repeated combat attempts could introduce side effects while searching around obstructed geometry. **Disposition: addressed** by probing four positions with LOS checks at `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp:25373` and starting combat only after selection.

5. **Armor mitigation verification could still finish with zero armored hits** — `MMOCoreORB/bin/scripts/managers/sim_player_manager.lua:3197`. Polling during combat did not prevent a weak target from dying before it attacked. **Disposition: addressed** by requesting 1,500 target HAM and applying it to all three primary pools under the target lock before combat at `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp:22161`.

### Suggestions

None.

---

## Checklist

- [x] 1. Functional Requirements — passed
- [x] 2. Code Quality — passed
- [x] 3. Architectural Compliance — passed
- [x] 4. Distributed Object / IDL Discipline — passed
- [x] 5. Lua/C++ Boundary — passed
- [x] 6. AI-Economy / Simulation Safety — passed
- [x] 7. Error Handling — passed
- [x] 8. Security — passed
- [x] 9. Performance — passed

---

## Verdict

**APPROVED**

No open findings remain. The ignored-test observation is covered by the plan’s established release-time `git add -f` workflow. `git diff HEAD --check` was clean; lint and warning-clean builds were reported clean, SimBot tests passed 200/200 with the server stopped, and the rally scenarios received live verification during the iteration cycle.

