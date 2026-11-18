# Code Review: F_0.9.21 P.10r: Artisan Surveying and Sampling

**Review Date**: 2026-11-18  
**Version**: `0.9.22`  
**Files Reviewed**:

- `MMOCoreORB/bin/scripts/managers/sim_player_manager.lua`
- `MMOCoreORB/bin/web/aieconomy-dashboard/app.js`
- `MMOCoreORB/src/server/zone/managers/resource/resourcespawner/ResourceSpawner.cpp`
- `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerController.cpp`
- `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerController.h`
- `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp`
- `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.h`
- `MMOCoreORB/src/server/zone/managers/resource/resourcespawner/ResourceSampleYield.h` (new)
- `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimBotSampling.h` (new)
- `MMOCoreORB/src/tests/SimBotSamplingTest.cpp` (new)
- `docs/1-plans/ROADMAP_p10-playerbot-parity.md`
- `docs/4-unit-tests/COVERAGE-DEBT.md`
- `docs/ai-miner-navigation-design.md`
- `docs/ai-pve-playerbot-design.md`

**Plan**: `docs/1-plans/F_0.9.21_p10r-artisan-surveying.plan.md`

---

## Executive Summary

The implementation now delivers stock-parity surveying and sampling for roster artisans, including tool-class enforcement, resource acquisition and XP, human-market floors, stale-stack handling, legacy-miner retirement, persistent tool recovery, dashboard reporting, and the stock client sample effect. All blocking findings were addressed; the remaining release-staging exceptions and real-controller coverage limitations are explicitly accepted or documented with verification and escape plans.

**APPROVED**

---

## Changes Overview

The change attaches artisan bodies to the miner controller and implements autonomous surveying, sampling, progression, tool management, bazaar trips, and legacy-miner capacity replacement. It shares stock sampling calculations, preserves human-listing price floors, recovers survey tools lost during restart reconciliation, and reports the new activity through progression telemetry and the dashboard. Verification finished with a warning-clean build, 247/247 SimBot tests, a 13/13 harness run, and successful owner in-game checks.

---

## Findings

### Critical Issues

None remain open.

### Major Issues

1. **Addressed — Bazaar dispatch could overlap an active artisan bazaar leg.** Availability now rejects controllers whose `artisanBazaarLeg` is not `NONE`, preventing a new assignment during the trip (`MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerController.h:1139`, `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerController.h:1143`).

2. **Addressed — Sampled human-price floors failed open when listing objects could not be loaded within the scan budget.** Unresolved resource listings set `sampledFloorIncomplete`, and sampled candidates are skipped rather than priced without a trustworthy floor (`MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp:17495`, `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp:17497`, `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp:17562`).

3. **Addressed — Resource subtype routing omitted valid harvested and sampled listings.** The scan now admits resource listings under the appropriate harvesting or sampling gate, including organic sampled flora (`MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp:17432`, `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp:17436`).

4. **Addressed — The creature-family consistency filter rejected sampled flora sharing organic auction subtypes.** Family matching now applies only when both the listing subtype and loaded spawn have creature families; familyless flora remains keyed by its resource type (`MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp:17500`, `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp:17503`).

5. **Addressed — Legacy-miner retirement could race sampling or controller lifetime across threads.** Retirement is now requested atomically but executed by the miner on its own work-loop thread after confirming an idle boundary (`MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerController.cpp:6188`, `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerController.cpp:6200`, `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerController.cpp:6211`).

6. **Addressed — A stale retirement request could remove a miner after the calculated surplus disappeared.** Eligibility is re-evaluated immediately before teardown; stale requests are cancelled without resetting the miner (`MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp:41435`, `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp:41497`).

7. **Addressed — Idle `WAITING` miners never re-entered `startSimLoop`, while the first scheduled-check design could retire a moving or surveying miner.** A generation-gated check handles genuinely idle `IDLE`/`WAITING` miners, while loop-boundary execution handles completed work without trusting stale action state (`MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerController.cpp:6192`, `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerController.cpp:6205`, `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerController.cpp:6207`). Live retirement reached and held the expected target (`docs/4-unit-tests/live_f0921-artisan-surveying_20261117-225454-f0921-artisan-surveying.md:68`).

8. **Addressed — Survey tools lost by boot reconciliation could not be re-granted because their one-time receipts survived.** Receipt loading now nets grants against explicit revocations and reconciled grant-object losses, while the reconciliation loss site revokes the in-memory receipt (`MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp:15873`, `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp:15909`, `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp:25102`). The four real artisans were each re-granted exactly once per lost tool class (`docs/4-unit-tests/live_f0921-artisan-surveying_20261117-225454-f0921-artisan-surveying.md:67`).

9. **Addressed — Real artisans passed a resource type where the sample path expected a spawn name, producing `noSpawn`.** Intelligent sampling now passes `intelligentResourceName`, with a current-spawn lookup by type and zone as the fallback (`MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerController.cpp:7610`, `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp:23113`). Live miner-controller sampling produced units and XP (`docs/4-unit-tests/live_f0921-artisan-surveying_20261117-225454-f0921-artisan-surveying.md:65`).

10. **Addressed — Receipt recovery initially lacked sufficient verification for approval.** The final harness passed 13/13 in one boot, and the restart/reconcile path received a successful live manual integration check (`docs/4-unit-tests/live_f0921-artisan-surveying_20261117-225454-f0921-artisan-surveying.md:62`, `docs/4-unit-tests/live_f0921-artisan-surveying_20261117-225454-f0921-artisan-surveying.md:67`). The remaining two-boot automation work is explicitly recorded (`docs/4-unit-tests/COVERAGE-DEBT.md:36`).

### Minor Issues

1. **Addressed — Stale held-item discards incremented `harvestStaleDiscarded` twice.** The caller-side increment was removed; destruction owns the single counter update (`MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp:9588`).

2. **Addressed — Sampling harness scenarios used novice skill values that rounded below the stock two-unit minimum.** The scenarios now train `crafting_artisan_survey_01` before evaluating deposit and merge behavior (`MMOCoreORB/bin/scripts/managers/sim_player_manager.lua:2377`, `MMOCoreORB/bin/scripts/managers/sim_player_manager.lua:2395`).

3. **Addressed — The stale-listing harness lacked listing-fee funds and expected the wrong terminal row state.** It now grants the bank credits and asserts the bazaar’s retained closed state as `retrieved` (`MMOCoreORB/bin/scripts/managers/sim_player_manager.lua:2434`, `MMOCoreORB/bin/scripts/managers/sim_player_manager.lua:2442`).

4. **Addressed — The sample-rate unit test used a density that failed the stock density floor before reaching the intended rate-failure branch.** The fixture now uses density `.40` (`MMOCoreORB/src/tests/SimBotSamplingTest.cpp:20`).

5. **Addressed — Spawn-name bridging and legacy retirement were reachable only through real `SimMinerController` bodies and lacked a recorded coverage disposition.** Both limitations, live checks, and fixture escape plans are documented (`docs/4-unit-tests/COVERAGE-DEBT.md:37`, `docs/4-unit-tests/COVERAGE-DEBT.md:38`).

6. **Addressed — Artisans knelt but did not broadcast the survey tool’s stock sample effect.** The manager resolves an eligible held tool, reads its configured animation, and broadcasts it at sample start (`MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerController.cpp:7543`, `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp:23233`). The client-only coverage limitation is documented, and the owner confirmed the correct effect in game (`docs/4-unit-tests/COVERAGE-DEBT.md:40`, `docs/4-unit-tests/live_f0921-artisan-surveying_20261117-225454-f0921-artisan-surveying.md:74`).

7. **Addressed — Dashboard wilds rows did not clearly identify live artisans or current sampling work.** Artisan labeling, live-presence sorting, and the `Sampling <resource>` phase are now present (`MMOCoreORB/bin/web/aieconomy-dashboard/app.js:1507`, `MMOCoreORB/bin/web/aieconomy-dashboard/app.js:1514`, `MMOCoreORB/bin/web/aieconomy-dashboard/app.js:1520`, `MMOCoreORB/bin/web/aieconomy-dashboard/app.js:1558`).

### Suggestions

1. **Accepted override — Live-only Lua gates differ from the release configuration.** These are the owner’s running settings and are intentionally excluded when staging the release patch; the committed configuration retains the safe default-off gates (`MMOCoreORB/bin/scripts/managers/sim_player_manager.lua:3951`, `MMOCoreORB/bin/scripts/managers/sim_player_manager.lua:4582`).

2. **Accepted override — `SimBotSamplingTest.cpp` is ignored in the working tree.** The established release procedure stages it explicitly with `git add -f`, matching the preceding harvest-test workflow (`.gitignore:25`, `MMOCoreORB/src/tests/SimBotSamplingTest.cpp:12`).

3. **Accepted override — `MMOCoreORB/utils/engine3` is dirty.** The change is pre-existing, unrelated, and excluded from the release staging set under the standing repository rule (`MMOCoreORB/utils/engine3:1`).

4. **Accepted observation — Real-controller bazaar-trip orchestration remains a long-soak coverage item.** Pricing, reserve, and stale-stack rules are automated; the trip itself has an explicit live verification procedure and fixture escape plan (`docs/4-unit-tests/COVERAGE-DEBT.md:39`).

---

## Checklist

- [x] 1. Functional Requirements — Plan behavior is implemented, including sampling, XP, tool classes, market floors, stale stacks, retirement, recovery, and visuals.
- [x] 2. Architecture & Design — Stock sampling logic is shared; controller/manager responsibilities and thread boundaries follow existing project patterns.
- [x] 3. Code Quality — Warning-clean build and clean Lua/JavaScript syntax checks; failure reasons and telemetry are actionable.
- [x] 4. Security & Data Safety — Receipt reconciliation fails safely, retirement is controller-thread-owned, and no unsafe external interface was added.
- [x] 5. Testing — 247/247 SimBot tests passed, including seven new sampling tests; the final harness passed 13/13, with live checks covering real-controller-only paths.
- [x] 6. Performance & Scalability — Listing loads remain budgeted, controller work uses scheduled tasks, and no unbounded hot-loop work was introduced.
- [x] 7. Documentation — Plan, architecture/design documents, roadmap, live receipt, and coverage-debt entries reflect the final behavior and limitations.
- [x] 8. Distributed Systems & Integration — No IDL, wire-protocol, schema-migration, or generated-code mismatch was introduced.
- [x] 9. AI Safety Assessment — Autonomous actions remain gate-controlled, capacity-limited, class-eligible, and observable; owner-approved live behavior matched the intended limits.

---

## Verdict

**APPROVED**

All review findings are addressed or explicitly accepted under the release-staging and coverage-debt policies. Lint and type checking are clean, all 247 SimBot tests passed, the scoped harness passed 13/13 in one boot, and the owner’s in-game sample-effect check passed.
