# Code Review: P.6.5b Starport Boarding Realism

**Review Date**: 2026-07-14  
**Version**: 0.2.0  
**Files Reviewed**:

- `MMOCoreORB/bin/scripts/managers/sim_player_manager.lua`
- `MMOCoreORB/bin/web/aieconomy-dashboard/app.js`
- `MMOCoreORB/src/server/zone/objects/creature/ai/AiAgentImplementation.cpp`
- `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerController.cpp`
- `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerController.h`
- `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp`
- `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.h`
- `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPvPController.cpp`
- `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPvPController.h`
- `MMOCoreORB/utils/engine3`
- `docs/ai-pvp-mimetic-travel-design.md`

**Plan**: `workspace/Core3/docs/1-plans/F_0.2.0_p65b-boarding-realism.plan.md`

---

## Executive Summary

This change makes PvP squads run to real starport boarding points, supports cell-aware movement into Theed Spaceport, adds tactical arrival routing, and exposes the behavior through Lua configuration and dashboard telemetry. All code findings were addressed; live verification remains deferred to the owner-controlled restart under the documented project policy.

APPROVED with observations

---

## Changes Overview

Route planning now occurs before squads begin their departure run, allowing interplanetary legs to target ticket collectors or starport-pad fallbacks while intra-planet legs continue using shuttle pads. The shared movement pipeline preserves cell coordinates, and `AiAgent` movement performs distance and interpolation calculations consistently across world and cell-local spaces. Tactical hot-destination avoidance, real-shuttle waiting configuration, counters, dashboard fields, and coverage-debt documentation were also added.

---

## Findings

### Critical Issues

None.

### Major Issues

#### Mixed coordinate spaces during in-cell movement

- **Location**: `MMOCoreORB/src/server/zone/objects/creature/ai/AiAgentImplementation.cpp:4771`
- **Original finding**: Cell-local patrol nodes were consumed using a model-space current position and a world-space destination, causing interior movement to stall.
- **Disposition**: **Addressed.** Cross-parent movement now expresses the current position in the next node’s coordinate space, and distance/interpolation use `nextMovementPosition.getPoint()` consistently (`AiAgentImplementation.cpp:4777-4795`). World-height snapping is restricted to outdoor movement at `AiAgentImplementation.cpp:4826`.

#### Starport fallback was not propagated to departure intent

- **Location**: `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp:22726`
- **Original finding**: Collector-resolution failures retained the city shuttle pad instead of using the cached starport pad.
- **Disposition**: **Addressed.** Every gated interplanetary leg now copies the cached boarding point, including fallback pads (`SimPlayerManager.cpp:22726-22742`). Departure intent returns those coordinates regardless of collector classification at `SimPlayerManager.cpp:22262-22270`.

#### Approval-gate evidence was initially absent

- **Location**: `docs/ai-pvp-mimetic-travel-design.md:588`
- **Original finding**: The initial review had no reliable build, syntax-check, test, or live-verification summary.
- **Disposition**: **Addressed with an accepted override.** The final gate reports clean incremental `-Werror` builds, clean `luac -p`, and clean `node --check`; no SimPvP gtest suites are affected. Uncovered paths have coverage-debt entries at `docs/4-unit-tests/COVERAGE-DEBT.md:6-7`. Live dashboard/in-game verification is intentionally deferred to the owner-controlled restart and remains specified at `docs/ai-pvp-mimetic-travel-design.md:588-604`.

### Minor Issues

#### Runtime resolver thresholds did not invalidate cached boarding points

- **Location**: `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp:21428`
- **Original finding**: Runtime changes to collector scan radius or Z-sanity updated configuration echoes but left cached resolutions unchanged.
- **Disposition**: **Addressed.** Previous threshold values are captured, changes are detected, and `pvpBoardingPointCache` is cleared under `pvpSquadMutex` at `SimPlayerManager.cpp:21441-21448`.

### Suggestions

None.

---

## Checklist

- [x] 1. Functional Requirements — Passed; planned routing, fallback, tactical-arrival, and boarding behavior are implemented.
- [x] 2. Code Quality — Passed; new coordinate and route logic is typed, localized, and documented.
- [x] 3. Architectural Compliance — Passed; manager/controller responsibilities and established movement patterns are preserved.
- [x] 4. Distributed Object / IDL Discipline — Passed; no IDL/autogen edits or new lock-order deviations.
- [x] 5. Lua/C++ Boundary — Passed; behavior remains structurally implemented in C++ with tunables in manager Lua.
- [ ] 6. AI-Economy / Simulation Safety — Passed with caveat; behavior is gated and simulation-only, while live verification awaits the owner-controlled restart.
- [x] 7. Error Handling — Passed; collector failures degrade to the cached starport pad and movement retains bounded watchdog recovery.
- [x] 8. Security — Passed; no authentication, secret-handling, or externally exposed route changes.
- [x] 9. Performance — Passed; collector scans are cached and no material per-tick population scan was added.

---

## Verdict

**APPROVED with observations**

All review findings were resolved in code. The only remaining observation is operational: the owner must complete the post-restart dashboard and in-game verification contract in `docs/ai-pvp-mimetic-travel-design.md:588-604`; this was accepted as a standing deployment-policy deferral rather than an open implementation defect.
