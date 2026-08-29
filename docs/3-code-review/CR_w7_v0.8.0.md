# Code Review: F_0.8.0 — Structure Traversal Foundation, including D7 Part 1 Phase 2 Zero-Clip Enforcement

**Review Date**: 2026-08-29  
**Version**: 0.8.0  
**Files Reviewed**:

- `MMOCoreORB/bin/scripts/ai/templates.lua`
- `MMOCoreORB/bin/scripts/managers/sim_player_manager.lua`
- `MMOCoreORB/src/server/zone/managers/collision/PathFinderManager.cpp`
- `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimHunterController.cpp`
- `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimHunterController.h`
- `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerController.cpp`
- `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerController.h`
- `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp`
- `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.h`
- `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPvPController.cpp`
- `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPvPController.h`
- `MMOCoreORB/utils/engine3`
- `VERSION`
- `docs/1-plans/F_0.8.0_structure-traversal-foundation.plan.md`
- `docs/4-unit-tests/COVERAGE-DEBT.md`
- `docs/ai-cell-navigation-design.md`

**Plan**: `docs/1-plans/F_0.8.0_structure-traversal-foundation.plan.md`
(defect proposals: `F_0.8.0-D1`, `-D2b`, `-D7`, `-D7p2`, `-D8`)

---

## Executive Summary

F_0.8.0 introduces the reusable structure-traversal state machine, repaired POB egress, D2b far-side egress, D8 hollow-door handling, a 26-scenario live harness, and default-off zero-clip observation and enforcement. All five actionable findings across the D2b and D7 reviews are addressed; final live acceptance produced 22 PASS / 4 FAIL with zero regressions and zero scenario differences.  
APPROVED with observations

---

## Changes Overview

The foundation centralizes cell-aware entry, egress, combat pause/resume, generation-safe asynchronous movement, bounded recovery, diagnostics, and dashboard reporting in `SimPlayerController` and `SimPlayerManager`. D2b rejects successful paths that return to the wrong side of a hollow structure, while D8 routes hollow-stuck bots through portal-backed door legs. D7 probes emitted paths for geometry intersections, optionally confirms apparent collisions against Recast, and can reject conclusively clipping paths with a bounded retry cap. The earlier four-round foundation review and D8 integration left no open review findings; all new behavior gates ship disabled by default at `MMOCoreORB/bin/scripts/managers/sim_player_manager.lua:714-818`.

---

## Findings

### Critical Issues

None.

### Major Issues

- **D2b harness exit assertion resolved the wrong building and failed open** — `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp:31120`, `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp:31353`. The harness now captures the owning building from the resolved enter target, rejects an unresolved building, and tests that exact building at exit. **Disposition: addressed**; live verification found 28 non-vacuous exit assertions, zero unresolved-building passes, and no regressions (`docs/4-unit-tests/live_d2b-harness-fix_20260828-101454-d2b-harness-fix.md:63-70`).

- **D2b harness runner was not genuinely single-writer** — `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp:31384`, `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp:31411`, `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.h:1206-1217`. Runner ownership is now established under the harness mutex for the complete invocation, and contended wake-ups use a bounded rerun latch. **Disposition: addressed**; the verified matrix produced all 26 results exactly once.

- **D7 clearance probe retained raw `TreeEntry*` objects after the zone query lock** — `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerController.cpp:326-335`, `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerController.cpp:355-362`. The snapshot now uses `SortedVector<ManagedReference<TreeEntry*> >`, keeping objects alive throughout the worker-thread narrow phase and Recast confirmation. **Disposition: addressed**.

- **D7 acceptance criterion 5.1 remains unmet** — `docs/4-unit-tests/live_d7p2-postreview_20260829-090817-d7p2-postreview.md:72-84`. Residual clipping is 3.4% of walked paths, down from 11.3%, but `crossesGeometry == 0` was not achieved; the remaining 36 paths clip through starport shells or debris after the bounded rejection cap. **Disposition: accepted with override** for this release because enforcement and walkable confirmation ship default-off at `MMOCoreORB/bin/scripts/managers/sim_player_manager.lua:731-753`; reaching zero requires a detour generator or additional navmesh coverage.

### Minor Issues

- **`capExhausted` was recorded before confirming that the path was walked** — `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerController.cpp:459-482`, `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerController.cpp:758-760`. The predicate now returns the exhaustion state without recording it, and the delivery branch increments the counter only after the route reaches `MOVING`. **Disposition: addressed**; the corrected live count is 36 (`docs/4-unit-tests/live_d7p2-postreview_20260829-090817-d7p2-postreview.md:26-41`).

- **The explanation of `getRecastPath`’s `len` value incorrectly claimed no caller depended on it** — `MMOCoreORB/src/server/zone/managers/collision/PathFinderManager.cpp:314`, `MMOCoreORB/src/server/zone/managers/collision/PathFinderManager.cpp:380`, `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerController.cpp:184-192`. The documentation now correctly distinguishes its use as an absolute-coordinate-based relative route comparator from a geometric path length; the confirmation measures actual length from returned points. **Disposition: addressed**.

- **Reference-counted snapshot increased median probe cost** — `docs/4-unit-tests/live_d7p2-postreview_20260829-090817-d7p2-postreview.md:43-70`. Probe p50 increased from 8,332 µs to 10,219 µs, approximately 20%, while p95 and maximum remained flat-to-better. **Disposition: accepted with override** as the measured cost of closing the lifetime hazard; the probe runs on the pathfinding worker and ships disabled.

### Suggestions

- **Replace the length-ratio walkability discriminator with maximum horizontal deviation from the original chord** — `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerController.cpp:202-216`, `docs/4-unit-tests/live_d7p2-postreview_20260829-090817-d7p2-postreview.md:86-89`. This would directly detect short lateral detours that can remain below the current 1.25 length ratio. **Disposition: deferred** as the recommended successor because changing the discriminator requires a new full live matrix and fresh measurements.

---

## Checklist

- [ ] 1. Functional Requirements — passed with accepted caveat: the integrated matrix remains 22 PASS / 4 FAIL, but D7 criterion 5.1 (`crossesGeometry == 0`) is not met and its enforcement gates ship off.
- [x] 2. Code Quality — passed; build completed under clang `-Werror` with zero warnings, Lua syntax checks passed, and all actionable review findings were corrected.
- [x] 3. Architectural Compliance — passed; traversal remains in the shared controller/manager layers and follows the asynchronous generation and configuration patterns documented by the project.
- [x] 4. Distributed Object / IDL Discipline — passed; the widened object-lifetime window is protected by `ManagedReference`, with no autogen or IDL discipline violations.
- [x] 5. Lua/C++ Boundary — passed; tunables and behavior gates remain in `sim_player_manager.lua`, default-off.
- [x] 6. AI-Economy / Simulation Safety — passed; the subsystem is simulation-only, gated, and live-verified through the 26-scenario matrix.
- [x] 7. Error Handling — passed; retries are bounded, inconclusive movement probes fail open, harness assertions fail closed, and anomaly counters remained zero.
- [x] 8. Security — passed; no authentication, credential, persistence, or externally exposed endpoint changes were introduced.
- [ ] 9. Performance — passed with accepted caveat: reference-counted snapshots raised p50 probe latency by approximately 20%, with p95 and maximum flat-to-better and the affected gate disabled by default.

---

## Verdict

**APPROVED with observations**

All review findings requiring source changes are addressed, the build and Lua gates are clean, hard-to-fixture coverage debt is recorded at `docs/4-unit-tests/COVERAGE-DEBT.md:22-25`, and post-review live verification passed at 22 PASS / 4 FAIL with zero regressions, zero scenario differences, true `capExhausted` count 36, and zero anomaly counters. This approval retains two explicit observations: D7’s zero-clipping criterion is not met, and the lifetime-safe snapshot adds approximately 20% at p50; accordingly, `zeroClip.enforce`, `zeroClip.walkableConfirm`, and all other new traversal gates ship default-off. Maximum horizontal deviation remains the recommended follow-up discriminator.
