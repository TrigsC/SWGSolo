# Code Review: Weapon Loadout: Equip, Protect and Upgrade (P.10m / F_0.9.12)

**Review Date**: 2026-10-17  
**Version**: `0.9.13`  
**Files Reviewed**:

- `MMOCoreORB/bin/scripts/managers/sim_player_manager.lua`
- `MMOCoreORB/bin/web/aieconomy-dashboard/app.js`
- `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimBotLoadout.h`
- `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp`
- `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.h`
- `MMOCoreORB/src/tests/SimBotLoadoutTest.cpp`

**Plan**: `workspace/Core3/docs/1-plans/F_0.9.12_p10m-weapon-loadout.plan.md`

---

## Executive Summary

The implementation now equips, protects, inspects, sells, and upgrades simulated-player weapon loadouts as planned. All review findings were addressed or explicitly accepted as intentional design, and final validation passed lint, warning-clean compilation, 157 tests, and the 34/34 live-verification gate.

APPROVED

## Changes Overview

The change adds pure loadout-selection policy, derived (never persisted) loadout selection, weapon reconciliation, bazaar protection and upgrade behavior, dashboard inspection data, Lua configuration, and ten focused tests. Verification-time fixes aligned buy-list retention with loadout protection, extended reconciliation to every activity when enabled, and made deferred reconciliation reliably retry. The feature remains default-off and dependent on the early-game progression path by design.

## Findings

### Critical Issues

None.

### Major Issues

1. **Weapon loadout appeared unintentionally dependent on the early-game gate.** Initial review found that enabling `weaponLoadout` alone could not exercise the feature. The dependency was confirmed as intentional because loadout selection refines the early-game weapon path; gate-requiring scenarios now explicitly declare both gates and therefore fail fast instead of silently exercising legacy behavior (`MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp:15022`, `MMOCoreORB/bin/scripts/managers/sim_player_manager.lua:2842`).  
   **Disposition: Accepted intentional decision; harness ambiguity addressed.**

2. **`SimBotLoadout.h` was initially absent from the tracked change.** The manager includes this header, so omission would have broken a clean checkout. It is now represented in the diff through intent-to-add and contains the complete policy implementation (`MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.h:35`, `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimBotLoadout.h:1`).  
   **Disposition: Addressed.**

3. **Required live verification had not yet been completed.** The final run passed all 25 harness scenarios and the production checks, including equip timing, XP attribution, retained inventory ownership, sell protection, consistency checks, and absence of new crash signatures (`docs/4-unit-tests/live_f0912-weapon-loadout_20261017-132722-f0912-weapon-loadout.md:7`, `docs/4-unit-tests/live_f0912-weapon-loadout_20261017-132722-f0912-weapon-loadout.md:86`, `docs/4-unit-tests/live_f0912-weapon-loadout_20261017-132722-f0912-weapon-loadout.md:93`).  
   **Disposition: Addressed — 34/34 `LIVE_VERIFICATION_PASS`.**

4. **Buy-list retention could preserve an inferior duplicate indefinitely.** When duplicate weapons shared an acquisition timestamp, the buy list could keep the inferior object while loadout protection kept the best object. Both retention sites now prioritize loadout-protected OIDs, then oldest acquisition time, then OID for deterministic behavior (`MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp:12594`, `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp:12927`, `workspace/Core3/docs/1-plans/F_0.9.12_p10m-weapon-loadout.plan.md:394`).  
   **Disposition: Addressed and live-verified.**

5. **Hunting bots could retain their spawn weapon indefinitely.** The original slow reconciliation sweep only visited grinding or template-complete bots, excluding graduated bots currently hunting. With the loadout gate enabled, reconciliation now considers every activity while retaining the old grind-only behavior when disabled (`MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp:16757`, `workspace/Core3/docs/1-plans/F_0.9.12_p10m-weapon-loadout.plan.md:391`, `docs/4-unit-tests/live_f0912-weapon-loadout_20261017-132722-f0912-weapon-loadout.md:86`).  
   **Disposition: Addressed and production-verified.**

6. **A combat-deferred full-sweep mismatch could wait until the next full sweep.** Identities discovered outside the dirty set were not guaranteed an ordinary-tick retry after combat deferral or equip failure. Queued mismatches now mark the identity dirty under the progression mutex; already-matching and successful paths continue to clear it (`MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp:9262`, `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp:16807`, `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp:16813`, `workspace/Core3/docs/1-plans/F_0.9.12_p10m-weapon-loadout.plan.md:393`).  
   **Disposition: Addressed and live-verified.**

### Minor Issues

1. **Loadout inspection was initially coupled to live gate state and recorded choices.** Inspection now uses side-effect-free preview selection when no live choice exists and constructs its protected list directly from the inspection snapshot, using the recorded source OID or preview choice as equipped state (`MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp:8874`, `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp:8963`, `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp:25025`, `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp:25034`).  
   **Disposition: Addressed.**

2. **Parity counter lookup and baselines omitted new upgrade and loadout counters.** Live behavior succeeded, but the harness read zero for the unregistered counter keys. Upgrade outcomes and loadout equip, fallback, hysteresis, and protection counters are now mapped and included in baseline capture (`MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp:19523`, `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp:19683`, `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp:19691`, `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp:20716`, `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp:20721`).  
   **Disposition: Addressed and live-verified.**

3. **Three upgrade scenarios could not pay the seller listing fee.** The affected seller fixtures now receive credits before listing their harness weapon, eliminating `list_harness_weapon_failed` without changing production policy (`MMOCoreORB/bin/scripts/managers/sim_player_manager.lua:2973`, `MMOCoreORB/bin/scripts/managers/sim_player_manager.lua:2995`, `MMOCoreORB/bin/scripts/managers/sim_player_manager.lua:3018`).  
   **Disposition: Addressed and live-verified.**

### Suggestions

None.

---

## Checklist

- [x] 1. Functional Requirements — Plan behavior is implemented; all 25 harness scenarios and production verification passed.
- [x] 2. Code Quality — Policy is isolated, deterministic, and covered by focused tests.
- [x] 3. Architectural Compliance — Configuration, manager integration, pure policy, and inspection follow established project boundaries.
- [x] 4. Distributed Object / IDL Discipline — No IDL or generated-object changes; shared progression state uses the established locking path.
- [x] 5. Lua/C++ Boundary — Lua owns configuration and scenarios; C++ owns object access, persistence, and policy integration.
- [x] 6. AI-Economy / Simulation Safety — Features remain default-off; inventory retention, listing, purchasing, credits, XP, and reconciliation were live-verified.
- [x] 7. Error Handling — Missing candidates and combat deferral degrade safely, and failed/deferred reconciliation is retried.
- [x] 8. Security — No new authorization boundary, exposed secret, or unsafe external-input path was introduced.
- [x] 9. Performance — Ordinary processing remains dirty-driven, with bounded periodic reconciliation and deterministic candidate selection.

## Verdict

**APPROVED**

All identified correctness and verification issues are resolved. The early-game dependency is an accepted product decision and is now explicit in the harness. Temporary live-verification settings were restored (`MMOCoreORB/bin/scripts/managers/sim_player_manager.lua:1191`, `MMOCoreORB/bin/scripts/managers/sim_player_manager.lua:3071`, `docs/4-unit-tests/live_f0912-weapon-loadout_20261017-132722-f0912-weapon-loadout.md:97`); previously approved live-only configuration and unrelated submodule dirt remain outside this feature’s promotion scope.
