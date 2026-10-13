# Code Review: F_0.9.8 P.10i — Lair Tactics and Mission Graduation

**Review Date**: 2026-10-13
**Version**: 0.9.8
**Files Reviewed**:

- `MMOCoreORB/bin/scripts/managers/sim_player_manager.lua`
- `MMOCoreORB/bin/web/aieconomy-dashboard/app.js`
- `MMOCoreORB/src/server/zone/managers/mission/DestroyMissionLairObserverImplementation.cpp`
- `MMOCoreORB/src/server/zone/objects/creature/ai/AiAgentImplementation.cpp`
- `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimHunterController.cpp`, `.h`
- `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerController.cpp`, `.h`
- `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp`, `.h`
- `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimBotAreaAttackGuard.h`, `SimBotDispatchPolicy.h`, `SimBotMissionGraduation.h`, `SimBotMissionPlacement.h`, `SimBotTrainingOrder.h`, `SimBotTravelCombat.h`, `SimEarlyGameDeathLog.h`, `SimHunterTravelDiag.h`, `SimPveRelocationDeadline.h` (new)
- `MMOCoreORB/src/tests/SimBot{AreaAttackGuard,DispatchPolicy,MissionGraduation,MissionPlacement,TrainingOrder,TravelCombat}Test.cpp`, `SimPveRelocationDeadlineTest.cpp` (new, gitignored by `*test*`)
- `docs/1-plans/F_0.9.8*.plan.md`, `docs/4-unit-tests/COVERAGE-DEBT.md`, `docs/ARCHI.md`

**Plan**: `docs/1-plans/F_0.9.8_p10i-lair-tactics-and-mission-graduation.plan.md`, with `F_0.9.8b` to `F_0.9.8e`

---

## Executive Summary

F_0.9.8 makes roster PlayerBots complete destroy missions end to end: mission-loop fixes, graduation at box 4, wave-aware lair fighting, progression-first dispatch, travel combat, and stock-style lair placement. It was built as 42 commits over three weeks, each reviewed by Codex as it landed, and reviewed once more at release.

APPROVED

---

## Changes Overview

The work is concentrated in `SimHunterController` (lair engagement, wave tactics, travel combat, retreat) and `SimPlayerManager` (dispatch, offer generation, lair reveal and placement, config, counters). Decisions were extracted into seven pure headers with GoogleTest suites (100 tests). Observability is `missiondiag.log` events plus `pveActivity` counters. The proven gates are committed on by owner decision.

---

## Review history

This release did not go through one review loop. Each change had its own Codex session (state files under `.claude/skills/codex-code-review/state/F_0.9.8*`), about 28 in all.

- Findings raised in those loops were fixed or answered in the same loop; none was left open on a code defect.
- Many sessions ended `REQUEST_CHANGES` with a clean code review and a single open gate: the checklist's mandatory live verification for SimPlayer behaviour, which waited on an owner restart. Those are closed by the live reports, the last being `docs/4-unit-tests/live_f098-lair-tactics-release_20261013-184338.md` (10/10).
- The release review (2026-10-13) covered the four commits that had not been reviewed: `4b37e62ed8` (tick rearm on `HEALING` resume), `861f38438b` (flee-leg cap), `300c39cc5c` (stock placement) and `8fbefd61fc` (defaults on, plan docs). Result: no findings, `APPROVED` in one round.

---

## Findings

### Critical Issues

None open. The loops found and fixed, among others: a lair engagement that rearmed the tick on only one branch (orphaned controller), and `onPathFound` clearing combat state on every accepted path, which would have wiped a fight on the run.

### Major Issues

None open.

### Minor Issues

None open.

### Suggestions

- A general wake-up safety net for orphaned controller ticks, instead of per-site guards. Not built; the per-site generation guard is in place at the four known sites.
- Log which spawn sub-check rejects a point in `isSpawningPermittedAt`. Not built; stock placement made it less pressing.

---

## Checklist

- [x] 1. Functional Requirements — passed against the plan's "Release scope"; original Phases 1 to 6 were not built and are dispositioned there
- [x] 2. Code Quality — passed
- [x] 3. Architectural Compliance — passed
- [x] 4. Distributed Object / IDL Discipline — passed; no IDL change in the release
- [x] 5. Lua/C++ Boundary — passed; tunables are in Lua
- [x] 6. AI-Economy / Simulation Safety — passed; gates on by owner decision after live runs, master progression gates still off in the commit
- [x] 7. Error Handling — passed; placement failure defers, bounded
- [x] 8. Security — not applicable
- [x] 9. Performance — passed; arrival search bounded at 256 tries, 22 at most observed

---

## Verdict

**APPROVED**

Things a future reader should know. The plan's scorer and tactic sub-state machine were replaced by owner-specified rules; mission credits moved back to F_0.9.2. Walk deaths remain the main open behaviour item. During this release's testing gate, running `core3 runUnitTests` against the live server was followed by a Berkeley DB panic and a deadlock; the databases were recovered with `db5.3_recover` from a verified backup, and unit tests now run only with the server stopped.
