# Code Review: F_0.7.5 cross-building provider staging (PvE hunter buff approach)

**Review Date**: 2026-09-01  
**Version**: 0.8.2  
**Files Reviewed**:

- `MMOCoreORB/bin/scripts/managers/sim_player_manager.lua`
- `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimHunterController.cpp`
- `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimHunterController.h`
- `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp`
- `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.h`
- `MMOCoreORB/utils/engine3`
- `VERSION`

**Plan**: no plan — unplanned change

---

## Executive Summary

This change stages cross-building PvE buff-provider approaches through the destination building’s exterior anchor before entering its cell, resolving the reproducible Theed doctor-path failure. All code-review blockers were addressed or explicitly overridden by the owner, and the shipped configuration passed a seven-assertion live verification.

APPROVED with observations

---

## Changes Overview

`SimHunterController` now detects cross-building provider approaches, moves first to the resolved cantina or med-center exterior anchor, and then enters using the refreshed provider snapshot. `SimPlayerManager` adds the staging gate and wider dual-anchor hospital resolution while retaining cached publication and existing lock choreography.

The Lua configuration ships the selected traversal and staging gates enabled while disabling diagnostic logging, zero-clip probing, and the scenario harness. Ancillary changes strengthen `LambdaTask` ownership in the engine submodule and advance `VERSION`.

---

## Findings

### Critical Issues

None.

### Major Issues

#### Diagnostic observation profile was enabled

**Location**: `MMOCoreORB/bin/scripts/managers/sim_player_manager.lua:692`, `:715-832`, `:842`  
**Disposition**: addressed, with owner override for selected production gates.

The initial tree enabled expensive zero-clip probing, per-path logging, and the traversal harness. The final profile disables cell-navigation logging, traversal logging, all zero-clip probe/enforcement gates, and the harness at lines 692, 731, 751-767, and 842. The owner explicitly approved shipping the remaining traversal gates enabled; their rationale is documented at lines 715-729.

#### Verification gate was incomplete and scenario accounting omitted scenario F

**Location**: `docs/4-unit-tests/live_f075-cross-building-staging_20260901-064000-f075-release.md:13-54`; `MMOCoreORB/bin/scripts/managers/sim_player_manager.lua:841-842`  
**Disposition**: addressed.

The initial live evidence reported B/C/D passing and A/E failing while omitting F; the corrected result established A/E/F as intentional red reproductions. The temporary six-scenario diagnostic set was subsequently removed and the harness disabled. Release run `20260901-064000-f075-release` verified the exact shipped profile and passed all seven assertions, including real-doctor completion in Theed, four doctor interactions across four cities, zero synthetic fallbacks, and bounded anomaly counters.

#### Staging was not subordinate to structure traversal

**Location**: `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimHunterController.cpp:602-616`  
**Disposition**: addressed.

The original condition checked only the staging flag, allowing staging to alter legacy movement when structure traversal was disabled. The final condition also requires `isStructureTraversalFeatureEnabled()` at line 609, preserving the documented configuration boundary.

### Minor Issues

#### Cross-building staging initially defaulted on

**Location**: `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.h:1764-1771`; `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp:5994-6001`, `:7463-7468`, `:7723-7728`; `MMOCoreORB/bin/scripts/managers/sim_player_manager.lua:1239-1242`  
**Disposition**: addressed, with owner-approved deployment override.

The compiled field and both configuration reset paths now default to false. Lua explicitly enables the feature in the shipped deployment profile after the owner’s 2026-09-01 decision; parsing preserves false when the field is absent.

#### Temporary scenario-count comments were inconsistent

**Location**: `docs/4-unit-tests/live_f075-cross-building-staging_20260901-064000-f075-release.md:34`; `MMOCoreORB/bin/scripts/managers/sim_player_manager.lua:841-842`  
**Disposition**: addressed.

Intermediate comments alternated between five and six scenarios and between 31 and 32 total cases. The temporary scenarios are now removed, the harness is disabled, and the receipt records the matrix as restored to 26 scenarios.

### Suggestions

None.

---

## Checklist

- [x] 1. Functional Requirements — passed
- [x] 2. Code Quality — passed
- [x] 3. Architectural Compliance — passed
- [x] 4. Distributed Object / IDL Discipline — passed; no IDL/generated-code changes, managed references and existing lock choreography retained
- [x] 5. Lua/C++ Boundary — passed; staging remains a Lua-configured manager tunable with a compiled default-off fallback
- [ ] 6. AI-Economy / Simulation Safety — passed with caveats; the exact shipped profile passed 7/7 live assertions, but the disabled 26-scenario harness was not rerun
- [x] 7. Error Handling — passed; arrival, provider disappearance, path failure, and flow completion clear staging state and degrade gracefully
- [x] 8. Security — passed; no security-sensitive surface changed
- [x] 9. Performance — passed; expensive diagnostics ship disabled and hospital scans remain cached rather than per-tick

---

## Verdict

**APPROVED with observations**

No Critical, Major, or Minor findings remain open. The owner explicitly approved shipping traversal and cross-building staging enabled while keeping expensive diagnostics off. The release run recorded 21 `zSanityViolations` but could not localize them to outdoor cells because traversal logging was disabled; it also relied on the prior v0.8.1 receipt for the 26-scenario traversal matrix. These limitations are documented at `docs/4-unit-tests/live_f075-cross-building-staging_20260901-064000-f075-release.md:56-68` and do not contradict the targeted 7/7 production-flow verification.
