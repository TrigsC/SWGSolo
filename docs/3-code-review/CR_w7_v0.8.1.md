# Code Review: F_0.8.1 — Traversal Adoption by Production Controllers

**Review Date**: 2026-08-31  
**Version**: 0.8.1  
**Files Reviewed**:

- `MMOCoreORB/bin/scripts/managers/sim_player_manager.lua`
- `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimHunterController.cpp`
- `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerController.cpp`
- `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerController.h`
- `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp`
- `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.h`
- `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPvPController.cpp`
- `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPvPController.h`
- `docs/1-plans/F_0.8.1_traversal-controller-adoption.plan.md`
- `docs/4-unit-tests/COVERAGE-DEBT.md`
- `MMOCoreORB/utils/engine3` — sole current `git diff --name-only HEAD` entry; pre-existing owner-owned dirty submodule, excluded from the feature review

**Plan**: `docs/1-plans/F_0.8.1_traversal-controller-adoption.plan.md`

---

## Executive Summary

The change migrates hunter and PvP building-entry legs onto the structure-traversal controller, fixes cell-local routing and traversal lifecycle ordering, and makes the live harness anomaly oracle per-agent, respawn-safe, and race-free. All review findings were addressed or explicitly overridden by the owner, and the final 26-scenario live matrix matched the established 23 PASS / 3 FAIL reference.

**APPROVED with observations**

---

## Changes Overview

Hunters adopt traversal for indoor buff-provider approaches at `SimHunterController.cpp:516`. PvP collector, starport departure, and arrival-reentry legs adopt traversal at `SimPvPController.cpp:1296`, `SimPvPController.cpp:1312`, `SimPvPController.cpp:1377`, `SimPvPController.cpp:1536`, and `SimPvPController.cpp:2105`, preserving legacy behavior when the default-off feature gate is disabled.

The change also introduces per-controller atomic anomaly tallies, destroyed-body carry accounting for harness respawns, and a post-lock lifecycle recheck that prevents queued movement tasks from operating on world-destroyed agents. Indoor z-sanity limitations are recorded as coverage debt, and production egress migration remains deliberately deferred.

---

## Findings

### Critical Issues

None.

### Major Issues

#### Hunter demand reserves changed production behavior

- **Location**: `MMOCoreORB/bin/scripts/managers/sim_player_manager.lua:371`, `MMOCoreORB/bin/scripts/managers/sim_player_manager.lua:381`, `MMOCoreORB/bin/scripts/managers/sim_player_manager.lua:387`, `MMOCoreORB/bin/scripts/managers/sim_player_manager.lua:399`, `MMOCoreORB/bin/scripts/managers/sim_player_manager.lua:405`
- **Finding**: The initial change hid an ungated increase of several hunter demand reserves to 500,000 inside an otherwise feature-gated traversal commit.
- **Disposition**: **Accepted with owner override.** The owner confirmed these are intentional standing tuning values. The behavior change was isolated into commit `7b59646fe3`, with its effect and revert values documented explicitly.

### Minor Issues

#### Harness anomaly counts were lost across scripted death and respawn

- **Location**: `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.h:1222`, `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp:31121`, `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp:31246`, `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp:31405`
- **Finding**: Scenario 22 replaces its controller mid-scenario, so reading only the replacement controller silently discarded anomalies recorded by the destroyed body.
- **Disposition**: **Addressed.** Per-slot carry counters now fold destroyed-controller tallies and combine them with the live controller for step and scenario assertions. The final `death_or_incapacity_recovery` scenario passed at `MMOCoreORB/bin/log/trip-verify-f081-postreview.log:7828`.

#### Anomaly counter reads and teardown folding were racy

- **Location**: `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerController.h:391`, `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerController.h:399`, `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerController.cpp:4193`, `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerController.cpp:4238`, `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp:30630`
- **Finding**: Plain `uint64` counters were read off-thread while movement tasks wrote them, and the original teardown fold did not synchronize with the writer.
- **Disposition**: **Addressed.** Counters are atomic with relaxed loads/increments, appropriate for monotonic tallies, and the final fold occurs while holding the existing agent lock.

#### A queued arrival task could record after the final teardown fold

- **Location**: `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerController.cpp:4447`, `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerController.cpp:4459`, `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp:30636`
- **Finding**: A task could pass the initial zone check, wait on the agent lock, then resume after teardown to refill patrol nodes, move a world-destroyed agent, and increment a counter already folded into the harness carry.
- **Disposition**: **Addressed.** `checkArrival` rechecks `agent->getZone()` immediately after acquiring the agent lock. A writer now either completes before teardown obtains the lock or observes the cleared zone afterward and exits without rescheduling.

#### Final live verification was initially outstanding

- **Location**: `docs/1-plans/F_0.8.1_traversal-controller-adoption.plan.md:74`, `MMOCoreORB/bin/log/trip-verify-f081-postreview.log:381`, `MMOCoreORB/bin/log/trip-verify-f081-postreview.log:2865`, `MMOCoreORB/bin/log/trip-verify-f081-postreview.log:7760`, `MMOCoreORB/bin/log/trip-verify-f081-postreview.log:11111`
- **Finding**: Approval was withheld until the amended anomaly oracle and lifecycle synchronization were exercised by the live test deliverable.
- **Disposition**: **Addressed.** Independent reduction of the archived log confirms 26 outcomes: 23 PASS and the same three pre-existing failures with identical reasons. It also confirms 75 distinct `ST_PHASE` agents, zero indoor z-sanity events, zero harness-agent anomalies, and no crash signatures.

### Suggestions

#### Restore indoor z-sanity when a cell-aware floor reference is available

- **Location**: `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerController.cpp:4206`, `docs/4-unit-tests/COVERAGE-DEBT.md:26`
- **Observation**: Indoor z-sanity is deliberately skipped because the current floor query returns terrain height and produces false positives inside multi-level buildings.
- **Disposition**: **Open, accepted coverage debt.** The ledger records an escape plan using the plural floor-collision query to select the nearest valid indoor floor.

#### Production `exitStructure` adoption remains deferred

- **Location**: `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPvPController.cpp:1485`
- **Observation**: Migrated entry legs still use the legacy bounded egress path because traversal egress can mistake a neighboring building’s cell for a new entry and loop until its attempt cap.
- **Disposition**: **Open, explicitly deferred.** The attempted migration was measured and reverted; the current legacy egress remains bounded and was not a regression introduced by this change.

#### Production diagnostic counters vary with combat load

- **Location**: `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerController.cpp:2568`, `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerController.cpp:2964`, `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerController.cpp:2989`
- **Observation**: The final run recorded 20 bounded `ST_FAIL/path_failed` events and six resume-cap failures under substantially higher combat-pause volume.
- **Disposition**: **Accepted observation.** Each resume-associated failure followed the configured three-attempt ladder; the harness oracle remained clean, no scenario diverged from the reference, and affected production agents subsequently completed other building entries.

---

## Checklist

- [x] 1. Functional Requirements — Passed; all planned entry migrations are present and the live matrix matched the reference.
- [x] 2. Code Quality — Passed; naming, typing, comments, and helper boundaries are appropriate.
- [x] 3. Architectural Compliance — Passed; controller ownership and configuration patterns match `ARCHI.md`.
- [x] 4. Distributed Object / IDL Discipline — Passed; no IDL/autogen edits, references are managed, and lock choreography matches existing writers.
- [x] 5. Lua/C++ Boundary — Passed; tunable gates remain in Lua and no dual movement driver was introduced.
- [x] 6. AI-Economy / Simulation Safety — Passed; traversal remains simulation-only, default-off, and live-verified.
- [x] 7. Error Handling — Passed; traversal, egress, resume, and path failures degrade through bounded recovery paths with diagnostics.
- [x] 8. Security — Passed; no authentication, endpoint, credential, or untrusted-input surface changed.
- [x] 9. Performance — Passed; added work is constant-time counter accounting and lifecycle validation with no new population-wide per-tick scan.

---

## Verdict

**APPROVED with observations**

All actionable review findings are resolved. The hunter reserve change is an explicit owner override rather than hidden verification scaffolding; indoor z-sanity and production traversal egress remain documented follow-ups. The final build was clean with zero new warnings, the coverage-debt rationale remains valid, gates were restored default-off at `MMOCoreORB/bin/scripts/managers/sim_player_manager.lua:715` and `MMOCoreORB/bin/scripts/managers/sim_player_manager.lua:827`, and the live matrix completed with zero reference divergences.
