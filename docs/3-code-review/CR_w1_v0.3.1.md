# Code Review: P.6.5e Combat Stalemate Break + Collector Wait Jitter

**Review Date**: 2026-07-15  
**Version**: 0.3.1  
**Files Reviewed**:

- `MMOCoreORB/bin/scripts/managers/sim_player_manager.lua`
- `MMOCoreORB/bin/web/aieconomy-dashboard/app.js`
- `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp`
- `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.h`
- `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPvPController.cpp`
- `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPvPController.h`
- `MMOCoreORB/utils/engine3`
- `VERSION`
- `docs/4-unit-tests/COVERAGE-DEBT.md`
- `docs/ai-pvp-mimetic-travel-design.md`

**Plan**: `workspace/Core3/docs/1-plans/F_0.3.1_p65e-stalemate-break.plan.md`

---

## Executive Summary

This change breaks progress-free SimPvP combat stalemates, suppresses immediate reacquisition of the cleared defender, and gives squads deterministic collector wait offsets. Both minor findings from the review loop were addressed, and no critical or major issues remain.

APPROVED

---

## Changes Overview

`SimPvpBotController` now tracks current HEALTH/ACTION/MIND progress for itself and its main defender, clearing combat after the configured idle period and temporarily ignoring that defender in its own target scan. `SimPlayerManager` adds runtime configuration, telemetry, logging, collector jitter, and relocation-safe state resets for leaders and members. Lua and dashboard surfaces expose the feature, coverage debt and design documentation record its verification contract, and the working tree also includes version metadata and engine task-reference ownership updates.

---

## Findings

### Critical Issues

None.

### Major Issues

None.

### Minor Issues

#### Member boarding did not reset stalemate state

- **Location**: `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp:23619`
- **Finding**: The first implementation reset stalemate progress and defender suppression through leader relocation only. Existing members were teleported directly, allowing a recent ignored defender OID to survive boarding.
- **Disposition**: **Addressed.** `SimPvpBotController::resetStalemateState()` now clears both progress and the ignore pair at `SimPvPController.h:80`. Full relocation delegates to it at `SimPvPController.cpp:73`, while member boarding invokes the scoped reset at `SimPlayerManager.cpp:23623-23626` without invalidating the follower tick chain.

#### Coverage-debt entry was missing

- **Location**: `docs/1-plans/F_0.3.1_p65e-stalemate-break.plan.md:250`
- **Finding**: The new combat-progress tracker had no corresponding gtest and the plan promised a hard-to-cover coverage-debt record, but the initial implementation omitted that ledger entry.
- **Disposition**: **Addressed.** `docs/4-unit-tests/COVERAGE-DEBT.md:9` now records the tracker and re-engage suppression, explains the missing SimPvP scaffolding, and points to the design document’s §15.2 live-verification escape plan.

### Suggestions

None.

---

## Checklist

- [x] 1. Functional Requirements — passed
- [x] 2. Code Quality — passed
- [x] 3. Architectural Compliance — passed
- [x] 4. Distributed Object / IDL Discipline — passed; no IDL/autogen changes or lock-order deviations
- [x] 5. Lua/C++ Boundary — passed
- [ ] 6. AI-Economy / Simulation Safety — passed with accepted operational caveat; behavior is gated and simulation-only, while live verification remains owner-deferred per `docs/ai-pvp-mimetic-travel-design.md:745`
- [x] 7. Error Handling — passed
- [x] 8. Security — passed
- [x] 9. Performance — passed

---

## Verdict

**APPROVED**

Both review findings were resolved without introducing new issues. The supplied incremental `-Werror` build, Lua syntax check, and JavaScript syntax check are clean; no SimPvP gtest suites are affected, and the uncovered tracker is recorded in the coverage-debt ledger. Post-restart dashboard and in-game verification remains an accepted owner-controlled deployment follow-up under design §15.2.
