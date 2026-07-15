# Code Review: P.6.5d City-Loop Realism + Squad Cohesion

**Review Date**: 2026-07-14  
**Version**: 0.3.0  
**Files Reviewed**:

- `MMOCoreORB/bin/scripts/managers/sim_player_manager.lua`
- `MMOCoreORB/bin/web/aieconomy-dashboard/app.js`
- `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp`
- `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.h`
- `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPvPController.cpp`
- `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPvPController.h`
- `MMOCoreORB/utils/engine3`
- `docs/4-unit-tests/COVERAGE-DEBT.md`
- `docs/ai-pvp-mimetic-travel-design.md`

**Plan**: `docs/1-plans/F_0.3.0_p65d-city-loop-realism.plan.md`

---

## Executive Summary

This change separates SimPvP starport, city-shuttle, and hangout locations; introduces navmesh-validated cantina hangouts; and adds death-triggered squad break-off behavior. All findings raised during the review loop were addressed, and the supplied build and validation gates are clean.

APPROVED

---

## Changes Overview

SimPvP route planning now uses live city shuttle points for intra-planet travel while retaining starports for cross-planet legs. Hangouts can be manually configured or derived from nearby cantinas with navigation validation, caching, dashboard visibility, and retryable startup warmup. Squad cohesion behavior now tracks deaths in a rolling window, breaks squads away from killzones, preserves the decision across leader promotion, and temporarily avoids the affected city.

---

## Findings

### Critical Issues

None.

### Major Issues

#### Dashboard resolution could permanently cache startup fallbacks

- **Location**: `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp:9178`
- **Finding**: The dashboard eagerly resolved uncached city locations, allowing polls during startup to publish incomplete fallback results for the remainder of the session.
- **Disposition**: **Addressed.** The dashboard is now a peek-only cache consumer, and the resolver returns startup fallbacks without publishing them when the server or zone is not ready (`SimPlayerManager.cpp:21716`).

#### Server loading completion did not guarantee navmesh readiness

- **Location**: `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp:21744`
- **Finding**: The initial readiness guard relied on `isServerLoading()`, but navmesh jobs begin after server loading ends. Maintenance could therefore cache fallback hangouts before cold-start or rebuilt navmeshes became available.
- **Disposition**: **Addressed.** Cantina resolution probes navmesh availability at the configured city shuttle pad and returns without caching when no mesh is present (`SimPlayerManager.cpp:21751`). The maintenance warmup retries until every configured city has a published cache entry (`SimPlayerManager.cpp:25161`).

### Minor Issues

#### Duplicate warmup paths made the completion flag ineffective

- **Location**: `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp:25161`
- **Finding**: An unconditional per-tick resolver loop ran alongside the one-shot warmup, so the `pvpCityLocationsWarmedUp` flag did not actually gate the initial scan.
- **Disposition**: **Addressed.** The paths were consolidated into one retryable warmup, which marks completion only after every configured city is cached (`SimPlayerManager.cpp:25167`). Cantina configuration invalidation clears the cache and re-arms warmup (`SimPlayerManager.cpp:21578`).

### Suggestions

None.

---

## Checklist

- [x] 1. Functional Requirements — passed
- [x] 2. Code Quality — passed
- [x] 3. Architectural Compliance — passed
- [x] 4. Distributed Object / IDL Discipline — not applicable; no IDL changes
- [x] 5. Lua/C++ Boundary — passed
- [x] 6. AI-Economy / Simulation Safety — passed; owner-restart live verification remains deferred per design
- [x] 7. Error Handling — passed
- [x] 8. Security — passed
- [x] 9. Performance — passed

---

## Verdict

**APPROVED**

No findings remain open and no overrides were required. The supplied `-Werror` build, Lua syntax check, and JavaScript syntax check are clean. No affected SimPvP gtest suites exist; the `checkPvpBreakOff` coverage debt is recorded, and live verification remains intentionally deferred to the owner restart under the documented process.
