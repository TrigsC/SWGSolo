# Code Review: Scout harvesting (P.10q)

**Review Date**: 2026-11-17  
**Version**: 0.9.21  
**Files Reviewed**:

- `MMOCoreORB/bin/scripts/managers/sim_player_manager.lua`
- `MMOCoreORB/bin/web/aieconomy-dashboard/app.js`
- `MMOCoreORB/src/server/zone/managers/creature/CreatureManagerImplementation.cpp`
- `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimHunterController.cpp`
- `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp`
- `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.h`
- `MMOCoreORB/src/server/zone/managers/creature/CreatureHarvestYield.h` (new)
- `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimBotHarvest.h` (new)
- `MMOCoreORB/src/tests/SimBotHarvestTest.cpp` (new)
- `docs/1-plans/ROADMAP_p10-playerbot-parity.md`
- `docs/ai-pve-playerbot-design.md`

**Plan**: `docs/1-plans/F_0.9.20_p10q-scout-harvesting.plan.md`

---

## Executive Summary

This change gives trained roster PlayerBots stock-compatible corpse harvesting, Scout XP, resource storage and bazaar selling, with group support, listing reservation, and safe bag-room reclamation. All actionable review findings were addressed, automated tests passed 240/240, the scoped harness passed 7/7, and the owner client pass completed successfully. APPROVED

---

## Changes Overview

The implementation replaces simulated three-family harvest credits with real corpse harvesting, resource containers, Scout XP, supply-aware family selection, and resource bazaar listings. The live-client pass added group-owned corpse claims, grouped-bot loot processing, resource listing reservation, deferred-XP parity, and controlled disposal of low-value held loot. The final test delta added harness counter mappings, reserve and bag-room scenarios, listing cleanup, and restoration of temporary test overrides.

---

## Findings

### Critical Issues

1. **Verification-only production settings present in the working copy**  
   **Location**: `MMOCoreORB/bin/scripts/managers/sim_player_manager.lua:3800`, `:3803`, `:3820`, `:4327`, `:4431`, `:4438`, `:4454`  
   Active progression gates and other owner-designated live settings were visible in the working-tree diff.  
   **Disposition**: Accepted with override. These lines pre-date this branch, are explicitly owner-managed live-only state, and are excluded when the release change is staged. The harvesting configuration intended to ship remains at `sim_player_manager.lua:3811` and `:4086`.

### Major Issues

1. **Equal positive demand used fixed family order instead of corpse yield**  
   **Location**: `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimBotHarvest.h:89`; `MMOCoreORB/src/tests/SimBotHarvestTest.cpp:55`  
   Equal positive need could select a smaller corpse family solely because it appeared first in the fallback order.  
   **Disposition**: Addressed. Equal need now prefers the family with the larger available corpse yield, with fixed order used only for a remaining tie.

2. **Absent resource attributes reduced computed quality**  
   **Location**: `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimBotHarvest.h:147`; `MMOCoreORB/src/tests/SimBotHarvestTest.cpp:63`  
   Spawn attributes reported as zero were included in the mean even though valid SWG attributes are in the 1–1000 range.  
   **Disposition**: Addressed. Non-positive values are ignored, with explicit regression coverage.

3. **Resource deposit could partially top up stacks before a later failure**  
   **Location**: `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp:22336`, `:22347`, `:22363`  
   Existing containers could be mutated before container creation or transfer failed, leaving a partial deposit.  
   **Disposition**: Addressed. Capacity, bag space, container creation, and transfer are resolved before the non-failing stack-top-up commit.

4. **Failed or refused deposits did not preserve stock extraction semantics**  
   **Location**: `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp:22300`, `:22304`, `:22308`  
   Deferring extraction until after bag checks meant refused deposits did not consume the harvested spawn units, unlike the stock corpse path.  
   **Disposition**: Addressed. Extraction now occurs exactly once under the spawn lock immediately after harvest commitment and before any bag refusal, while inventory mutations remain all-or-nothing.

5. **New regression tests and live verification were pending**  
   **Location**: `MMOCoreORB/src/tests/SimBotHarvestTest.cpp:55`, `:63`; `MMOCoreORB/bin/scripts/managers/sim_player_manager.lua:2278`, `:2304`, `:2322`; `docs/1-plans/F_0.9.20_p10q-scout-harvesting.plan.md:566`  
   Earlier review rounds preceded the final rerun and the Phase 3 reserve/discard receipt.  
   **Disposition**: Addressed. SimBot tests passed 240/240; the seven scoped harness scenarios passed; listing cleanup runs at `sim_player_manager.lua:2299`; counter mappings and baselines are present at `SimPlayerManager.cpp:26046`, `:26095`, `:27783`, and `:27784`; temporary loot-cap state is restored at `SimPlayerManager.cpp:27052`.

### Minor Issues

None.

### Suggestions

None.

---

## Checklist

- [x] 1. Functional Requirements — passed
- [x] 2. Code Quality — passed
- [x] 3. Architectural Compliance — passed
- [x] 4. Distributed Object / IDL Discipline — passed
- [x] 5. Lua/C++ Boundary — passed
- [x] 6. AI-Economy / Simulation Safety — passed; owner-approved mutations are gated and live-verified
- [x] 7. Error Handling — passed
- [x] 8. Security — passed; no new authentication or credential exposure
- [x] 9. Performance — passed

---

## Verdict

**APPROVED**

All actionable Critical and Major findings were resolved. The remaining accepted override concerns owner-controlled live-only working-copy settings, which are excluded during release staging; no open code findings remain. Final evidence records a warning-clean build, clean Lua parsing, 240/240 SimBot tests, 7/7 scoped harness scenarios, successful group harvesting and bag-room behavior in the client pass, and no leftover harness listings from this run.
