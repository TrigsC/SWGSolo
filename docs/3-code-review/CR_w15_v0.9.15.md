# Code Review: Food and Drink for PlayerBots (P.10o / F_0.9.14)

**Review Date**: 2026-10-19  
**Version**: 0.9.15  
**Files Reviewed**:

- `MMOCoreORB/bin/scripts/managers/sim_player_manager.lua`
- `MMOCoreORB/bin/web/aieconomy-dashboard/app.js`
- `MMOCoreORB/src/server/zone/managers/combat/CombatManager.cpp`
- `MMOCoreORB/src/server/zone/managers/combat/CombatManager.h`
- `MMOCoreORB/src/server/zone/objects/creature/ai/AiAgent.idl`
- `MMOCoreORB/src/server/zone/objects/creature/ai/AiAgentImplementation.cpp`
- `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimBotConsumables.h`
- `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimHunterController.cpp`
- `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp`
- `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.h`
- `MMOCoreORB/src/server/zone/objects/tangible/consumable/Consumable.idl`
- `MMOCoreORB/src/server/zone/objects/tangible/consumable/ConsumableImplementation.cpp`
- `MMOCoreORB/src/tests/SimBotConsumablesTest.cpp`
- `MMOCoreORB/utils/engine3`
- `VERSION`
- `docs/ARCHI.md`

**Plan**: `workspace/Core3/docs/1-plans/F_0.9.14_p10o-food-and-drink.plan.md`

---

## Executive Summary

This change adds player-parity stomach handling, configured food consumption, bazaar restocking, combat modifiers, telemetry, and live verification for roster PlayerBots. Two initial review rounds and four verification-time amendments were reviewed and approved; all findings are resolved, with 177/177 tests and 68/68 live assertions passing.

APPROVED

---

## Changes Overview

The implementation introduces pure consumable policy helpers, a shared player/bot buff constructor, a transient AI parity flag, and food-aware combat accuracy and defense seams. `SimPlayerManager` owns gated stomach, vault, restock, lifecycle, dashboard, and harness behavior, while `SimHunterController` consumes before target, lair, and interceptor engagements. Live verification drove corrections to AI accuracy fallback handling, bazaar candidate filtering, travel-engagement consumption, and harness setup.

---

## Findings

### Critical Issues

None.

### Major Issues

1. **New policy test was ignored and absent from the reviewable change set — addressed.**  
   The repository rule at `MMOCoreORB/.gitignore:25` initially excluded the test. `MMOCoreORB/src/tests/SimBotConsumablesTest.cpp:1` is now intent-to-add and present in the diff; the final gate reports 177/177 passing tests at `docs/4-unit-tests/live_f0914-food-and-drink_20261018-212652-f0914-food-and-drink.md:29`.

2. **Required live verification was incomplete — addressed after an accepted workflow-stage override.**  
   Review initially accepted that TRIP-verify follows code-review convergence. The plan item is now complete at `docs/1-plans/F_0.9.14_p10o-food-and-drink.plan.md:468`, with `LIVE_VERIFICATION_PASS` at `docs/4-unit-tests/live_f0914-food-and-drink_20261018-212652-f0914-food-and-drink.md:8` and 68/68 final assertions at `:89`.

3. **Food-only accuracy buffs discarded the stock AI fallback — initial correction superseded and fully addressed.**  
   Live verification found that subtracting the food buff produced zero and incorrectly selected `chanceHit * 100`; the first correction restored only the template statistic, as recorded at `docs/4-unit-tests/live_f0914-food-and-drink_20261018-212652-f0914-food-and-drink.md:37`. The final helper restores the supplied stock fallback at `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimBotConsumables.h:266`.

4. **Template-only restoration still missed the level-based accuracy fallback — addressed.**  
   The second live probe established that stock AI accuracy falls through to `100 + 2 * level`, recorded at `docs/4-unit-tests/live_f0914-food-and-drink_20261018-212652-f0914-food-and-drink.md:38`. The unchanged fallback body is exposed at `MMOCoreORB/src/server/zone/objects/creature/ai/AiAgent.idl:1448`, implemented at `MMOCoreORB/src/server/zone/objects/creature/ai/AiAgentImplementation.cpp:663`, and used by combat at `MMOCoreORB/src/server/zone/managers/combat/CombatManager.cpp:2001`. Final live accuracy and defense assertions pass at `docs/4-unit-tests/live_f0914-food-and-drink_20261018-212652-f0914-food-and-drink.md:74`.

5. **Consumable scans exhausted their load budget on unrelated bazaar listings — addressed.**  
   The production failure is recorded at `docs/4-unit-tests/live_f0914-food-and-drink_20261018-212652-f0914-food-and-drink.md:40`. `ConsumableEntry` now carries the client game-object type at `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimBotConsumables.h:88`, policy parsing populates it at `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp:11302`, and raw candidates are filtered before object loads at `:15528`. Production purchasing passes at `docs/4-unit-tests/live_f0914-food-and-drink_20261018-212652-f0914-food-and-drink.md:81`.

6. **Production bots bought food but did not eat during travel ambushes — addressed by owner-approved amendment.**  
   The amended requirement is recorded at `docs/1-plans/F_0.9.14_p10o-food-and-drink.plan.md:40`. `defendAgainstInterceptor` now calls the existing gated consumption entry point after its already-fighting return and before taking the hunter lock at `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimHunterController.cpp:6948`. The final amended build passed 59/59 scenarios at `docs/4-unit-tests/live_f0914-food-and-drink_20261018-212652-f0914-food-and-drink.md:43`.

### Minor Issues

1. **`maxRestockLoadsPerVisit` was enforced separately for each consumable need — addressed.**  
   One shared counter is initialized at `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp:15567`, enforced at `:15583`, and incremented at `:15615`.

2. **Consumable telemetry survived identity retirement and reaping — addressed.**  
   Reaper cleanup removes last-consumed and restock state at `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp:20507`; both retirement paths perform equivalent cleanup at `:30854` and `:30900`.

3. **Verification scenarios had persistence, diagnostics, and listing-fee defects — addressed.**  
   The last-dose scenario now flushes before its ledger assertion at `MMOCoreORB/bin/scripts/managers/sim_player_manager.lua:3442`. Combat-probe failures include measured deltas at `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp:24329`. SELL and restock sellers receive listing-fee credits at `MMOCoreORB/bin/scripts/managers/sim_player_manager.lua:3509`, `:3523`, and `:3564`, and the unsupported counter assertion was removed from the protected-food scenario at `:3500`.

### Suggestions

None.

---

## Checklist

- [x] 1. Functional Requirements — passed
- [x] 2. Code Quality — passed
- [x] 3. Architectural Compliance — passed
- [x] 4. Distributed Object / IDL Discipline — passed
- [x] 5. Lua/C++ Boundary — passed
- [x] 6. AI-Economy / Simulation Safety — passed; owner-approved P.10 mutations are gated and live-verified
- [x] 7. Error Handling — passed
- [x] 8. Security — passed
- [x] 9. Performance — passed

---

## Verdict

**APPROVED**

All review and verification findings are addressed, and no critical, major, or minor issues remain open. The pre-existing `armor_mitigation_live` timing flake failed twice but passed on an unchanged build and was not attributed to this feature (`docs/4-unit-tests/live_f0914-food-and-drink_20261018-212652-f0914-food-and-drink.md:41`). The owner’s live-only Lua configuration, separate `docs/ARCHI.md` compaction, and existing `engine3` submodule state were excluded from the feature judgment.

