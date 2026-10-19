# Code Review: Armor Loadout (P.10n)

**Review Date**: 2026-10-18  
**Version**: `0.9.14`  
**Files Reviewed**:

- `MMOCoreORB/bin/scripts/managers/sim_player_manager.lua`
- `MMOCoreORB/bin/scripts/mobile/outfits/serverobjects.lua`
- `MMOCoreORB/bin/scripts/mobile/outfits/sim_playerbot_outfit_humanoid.lua`
- `MMOCoreORB/bin/scripts/mobile/outfits/sim_playerbot_outfit_ithorian.lua`
- `MMOCoreORB/bin/scripts/mobile/outfits/sim_playerbot_outfit_wookiee.lua`
- `MMOCoreORB/bin/scripts/mobile/sim_playerbot_novice.lua`
- `MMOCoreORB/bin/scripts/object/mobile/serverobjects.lua`
- `MMOCoreORB/bin/scripts/object/mobile/sim_playerbot_bodies.lua`
- `MMOCoreORB/bin/web/aieconomy-dashboard/app.js`
- `MMOCoreORB/src/server/zone/managers/combat/CombatManager.cpp`
- `MMOCoreORB/src/server/zone/managers/combat/CombatManager.h`
- `MMOCoreORB/src/server/zone/objects/creature/ai/AiAgent.idl`
- `MMOCoreORB/src/server/zone/objects/creature/ai/AiAgentImplementation.cpp`
- `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimBotAppearance.h`
- `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimBotArmorLoadout.h`
- `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp`
- `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.h`
- `MMOCoreORB/src/tests/SimBotArmorLoadoutTest.cpp`

**Plan**: `docs/1-plans/F_0.9.13_p10n-armor-loadout.plan.md`

---

## Executive Summary

This change gives roster PlayerBots player-equivalent armor selection, encumbrance, mitigation, SELL protection, and bazaar upgrades, then adds deterministic visible bodies, clothing, and armor swaps across ten species. All review findings were addressed or explicitly accepted by the owner; lint, warning-clean build, 170 SimBot tests, the 47-scenario matrix, production checks, and owner in-client verification passed.

APPROVED with observations

---

## Changes Overview

The implementation adds armor scoring and usability policy, transient body copies, stock mitigation integration, loadout reconciliation, diagnostics, dashboard reporting, and harness coverage. Amendment A replaces client-baked dressed bodies with deterministic vendor-derived bodies and server clothing, including species wear filtering, clothing stow/restore, stable appearance across respawns, and visible armor. Final verification recorded `LIVE_VERIFICATION_PASS`, 60/60 assertions, production armor purchases and mitigation across five species, and an owner in-client “All good.”

---

## Findings

### Critical Issues

- **Owner live-only configuration initially appeared to enable destructive production behavior** — `MMOCoreORB/bin/scripts/managers/sim_player_manager.lua:3404`, `:3440`, `:3484`. The owner confirmed these byte-identical, unstaged values were intentional live configuration; the temporary parity harness was restored off at `:1190-1196`. **Disposition: accepted with override.**

- **Harness buyer purchased a real player listing that cleanup subsequently deleted** — `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp:14672-14699`, `:14815-14831`, `:24014-24018`. Harness buyers now consider only harness-owned bags or objects registered by `createHumanListing`; production buyers are unchanged. The final weapon regressions passed with the owner’s listings present at `docs/4-unit-tests/live_f0913-armor-visible_20261018-163717-f0913-armor-visible.md:59-70`. **Disposition: addressed and live-verified.**

### Major Issues

- **Affected tests had not yet run during the initial review** — `MMOCoreORB/src/tests/SimBotArmorLoadoutTest.cpp:1`. The server-stopped SimBot suites subsequently passed 170/170, with the new templates and outfits passing `LuaMobileTest`; the evidence is recorded at `docs/4-unit-tests/live_f0913-armor-visible_20261018-163717-f0913-armor-visible.md:24-33`. **Disposition: addressed.**

- **Bazaar cache omitted armor coverage/hit-location state** — `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.h:804-812`, `SimPlayerManager.cpp:14935-14943`. The cache now stores the complete raw `ArmorStats`, including coverage. **Disposition: addressed.**

- **Cached armor scores could become stale after a policy reload** — `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp:14961-14969`. Cached raw stats are rescored against the current live policy at use. **Disposition: addressed.**

- **Combat could begin after the initial state snapshot but before armor mutation** — `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp:10093-10117`. Reconciliation now rechecks `agent->isInCombat()` under the final agent lock before additions or replacements. **Disposition: addressed.**

- **`SimBotArmorLoadout.h` was initially absent from the reviewed change set** — `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimBotArmorLoadout.h:1`. The file was intent-added and included in the complete HEAD diff. **Disposition: addressed.**

- **`armorLoadout` was read from the wrong Lua table** — `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp:10737-10745`. The parent field is now read before the `loadout` child table is pushed. **Disposition: addressed and production-verified.**

- **Transient armor clones inherited persistent storage state** — `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp:9665-9680`, `:9886-9892`. Clones are explicitly marked nonpersistent and teardown removes them from both the world and database. **Disposition: addressed.**

- **Armor was functionally equipped but invisible on the original client-baked bodies** — `MMOCoreORB/bin/scripts/mobile/sim_playerbot_novice.lua:39-63`, `MMOCoreORB/bin/scripts/object/mobile/sim_playerbot_bodies.lua:1-52`, `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp:9459-9590`. Amendment A added clothing-free vendor-derived bodies, deterministic appearance, species-valid server clothing, and clothing swaps. The owner confirmed visible armor and stable appearance in-client at `docs/4-unit-tests/live_f0913-armor-visible_20261018-163717-f0913-armor-visible.md:64-75`. **Disposition: addressed and in-client verified.**

- **Required appearance, body, and outfit files were untracked** — `MMOCoreORB/bin/scripts/object/mobile/serverobjects.lua:52`, `MMOCoreORB/bin/scripts/mobile/outfits/serverobjects.lua:30-32`, `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimBotAppearance.h:1`. All required files were intent-added and included in the reviewed HEAD diff. **Disposition: addressed.**

- **Random harness species selection made armor scenarios fail on bodies that correctly reject padded armor** — `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp:23126-23138`, `:29755-29774`. A harness-only `setBodyTemplate` override pins armor scenarios to a compatible human body without changing production or the seeded appearance-stability scenario. **Disposition: addressed and live-verified.**

- **Phase 5 live and in-client verification was incomplete during review** — `docs/1-plans/F_0.9.13_p10n-armor-loadout.plan.md:812-816`. The final run passed the restored 47-scenario matrix, production checks, and owner in-client validation at `docs/4-unit-tests/live_f0913-armor-visible_20261018-163717-f0913-armor-visible.md:50-75`. **Disposition: addressed.**

### Minor Issues

- **`LOADOUT_ARMOR` diagnostics omitted reconcile, drain, deferral, and spawn re-wear outcomes** — `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp:10295-10304`, `:29982-29985`. The missing action and reason events were added. **Disposition: addressed.**

- **`armor_usability_filters` incorrectly assumed padded gloves provided no coverage** — `MMOCoreORB/bin/scripts/managers/sim_player_manager.lua:3128-3142`. The scenario now expects the gloves to be worn and only the faction-restricted piece to be refused. **Disposition: addressed and live-verified.**

- **`armor_clothing_swap` used a chest plate that does not collide with the outfit shirt** — `MMOCoreORB/bin/scripts/managers/sim_player_manager.lua:3342-3357`. The scenario now uses boots, whose `shoe` arrangement stows and restores the outfit shoes. **Disposition: addressed and live-verified.**

- **`armor_mitigation_live` used one-location chest coverage and could finish without an armored hit** — `MMOCoreORB/bin/scripts/managers/sim_player_manager.lua:3182-3200`. The scenario now equips chest, leggings, helmet, and both biceps to cover every hit location. **Disposition: addressed and live-verified.**

### Suggestions

- **Dashboard did not expose the armor bazaar-upgrade gate** — `MMOCoreORB/bin/web/aieconomy-dashboard/app.js:1484-1497`. The armor panel now displays configured, loadout, and bazaar-upgrade gate states. **Disposition: addressed.**

---

## Checklist

- [x] 1. Functional Requirements — passed; all planned armor, appearance, persistence, and harness behaviors verified.
- [x] 2. Code Quality — passed.
- [x] 3. Architectural Compliance — passed; behavior remains within the sim/progression and stock combat seams.
- [x] 4. Distributed Object / IDL Discipline — passed; IDL changes are confined to source IDL and object access uses established managed-reference and lock patterns.
- [x] 5. Lua/C++ Boundary — passed; policy and scenario configuration remain in Lua while engine-sensitive armor and spawn behavior remain in C++.
- [x] 6. AI-Economy / Simulation Safety — passed; gates, market isolation, 60/60 live verification, and owner in-client verification complete.
- [x] 7. Error Handling — passed; failure paths restore clothing, destroy transient copies, and report actionable harness reasons.
- [x] 8. Security — passed; no authentication surface changed, and harness market isolation prevents recurrence of real-listing deletion.
- [x] 9. Performance — passed; cache bounds, snapshotting, cleanup, and per-spawn/per-reconcile work are proportionate.

---

## Verdict

**APPROVED with observations**

The owner accepted the explicitly marked live-only configuration as intentional and out of committed scope. All code and verification findings are closed; the remaining release-document/version work is the plan’s explicit release-stage task at `docs/1-plans/F_0.9.13_p10n-armor-loadout.plan.md:802`, not an implementation blocker.
