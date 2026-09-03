# Code Review: XP per kill for roster PlayerBots (P.10b)

**Review Date**: 2026-09-03  
**Version**: 0.9.1  
**Files Reviewed**:

- `MMOCoreORB/bin/scripts/managers/sim_player_manager.lua`
- `MMOCoreORB/bin/web/aieconomy-dashboard/app.js`
- `MMOCoreORB/src/server/zone/managers/player/PlayerManager.idl`
- `MMOCoreORB/src/server/zone/managers/player/PlayerManagerImplementation.cpp`
- `MMOCoreORB/src/server/zone/managers/skill/SkillManager.cpp`
- `MMOCoreORB/src/server/zone/managers/skill/SkillManager.h`
- `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp`
- `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.h`
- `MMOCoreORB/utils/engine3`
- `VERSION`
- `docs/1-plans/ROADMAP_p10-playerbot-parity.md`
- `docs/ARCHI.md`

**Plan**: `/srv/games/swgemu-core3/workspace/Core3/docs/1-plans/F_0.9.1_p10b-kill-xp.plan.md`

---

## Executive Summary

This change awards roster PlayerBots kill XP using player-parity arithmetic, lifetime ceilings, persistence, observability, and default-off capability gates. All review findings were addressed or accepted with a documented override, and live verification passed 32/32 required assertions with the complete 28/28 scenario matrix.

APPROVED

---

## Changes Overview

The production destruction path now collects eligible PlayerBot threat-map contributions and defers XP awards to `SimPlayerManager`, preserving the established locking model. The progression store applies per-kill and lifetime caps, persists direct and `combat_general` XP, exposes counters to the dashboard, and provides deterministic and live-combat harness coverage. Documentation, configuration, version metadata, and the live-verification receipt were updated accordingly.

---

## Findings

### Critical Issues

None.

### Major Issues

1. **Scenario 28 used hardcoded XP expectations instead of a live oracle** — `SimPlayerManager.cpp:10138`, `SimPlayerManager.cpp:10213`, `SimPlayerManager.cpp:11534`, `sim_player_manager.lua:1361`.  
   **Disposition: Addressed.** The harness captures the victim’s recorded `baseXp`, the pre-combat global multiplier, and `killXpRate`, then reproduces the production truncation order for both direct and `combat_general` XP. The same computed sentinels are used after flush and reload.

2. **`awaitKillTargetDeath` could complete between the direct and `combat_general` grants** — `SimPlayerManager.cpp:10234`, `SimPlayerManager.cpp:10250`.  
   **Disposition: Addressed.** Settlement now requires both XP rows to equal their exact expected values. The live run confirmed both rows before and after the MySQL round trip (`docs/4-unit-tests/live_p10b-kill-xp_20260903-144819-p10b-kill-xp.md:77`).

3. **The required live matrix had not yet completed** — `docs/1-plans/F_0.9.1_p10b-kill-xp.plan.md:621`.  
   **Disposition: Addressed.** Verification subsequently passed 32/32 required assertions and the full 28/28 matrix (`docs/4-unit-tests/live_p10b-kill-xp_20260903-144819-p10b-kill-xp.md:6`, `docs/4-unit-tests/live_p10b-kill-xp_20260903-144819-p10b-kill-xp.md:89`). The shipped default-off configuration was restored and revalidated (`docs/4-unit-tests/live_p10b-kill-xp_20260903-144819-p10b-kill-xp.md:125`).

4. **An XP type already at its lifetime ceiling recorded a zero-value award as accepted** — `SimPlayerManager.cpp:9183`, `SimPlayerManager.cpp:9189`.  
   **Disposition: Addressed.** The ceiling counter still records the capped attempt, but the zero-available path now returns before incrementing accepted totals or overwriting the last-award source.

5. **Named identity references were pruned before an asynchronous `deleteIdentity` poll observed completion** — `SimPlayerManager.cpp:9879`, `SimPlayerManager.cpp:10892`, `SimPlayerManager.cpp:11389`.  
   **Disposition: Addressed.** The handler retains the named reference through request completion, while scenario cleanup still clears the reference map wholesale. The original failure, root cause, fix, and successful re-review are recorded in the verification receipt (`docs/4-unit-tests/live_p10b-kill-xp_20260903-144819-p10b-kill-xp.md:131`).

6. **The live oracle required an IDL accessor despite the plan initially specifying no IDL impact** — `PlayerManager.idl:592`.  
   **Disposition: Accepted with override.** The additive read-only accessor avoids deriving the expected result from the implementation under test. The plan records the rationale and generated-code implications (`docs/1-plans/F_0.9.1_p10b-kill-xp.plan.md:599`), and the regenerated build passed warning-clean.

### Minor Issues

None.

### Suggestions

1. **Temporary verification gates required restoration before promotion** — `sim_player_manager.lua:1146`, `sim_player_manager.lua:1387`, `sim_player_manager.lua:1388`, `sim_player_manager.lua:1390`.  
   **Disposition: Addressed.** The harness and progression gates are restored to false, the flush interval is restored to 60 seconds, and Boot D confirmed the shipped configuration remains inert (`docs/4-unit-tests/live_p10b-kill-xp_20260903-144819-p10b-kill-xp.md:122`).

---

## Checklist

- [x] 1. Functional Requirements — Passed; player-parity awards, caps, persistence, gating, and the production kill path were verified.
- [x] 2. Code Quality — Passed.
- [x] 3. Architectural Compliance — Passed; award work is deferred and manager mutexes remain non-nested.
- [x] 4. Distributed Object / IDL Discipline — Passed with the documented additive accessor override.
- [x] 5. Lua/C++ Boundary — Passed; configuration and oracle sentinels are validated across the boundary.
- [x] 6. AI-Economy / Simulation Safety — Passed; mutations are roster-scoped, gated default-off, and cleanup was verified.
- [x] 7. Error Handling — Passed; invalid, zero, capped, missing-identity, persistence, and cleanup paths were exercised.
- [x] 8. Security — Passed; no new external trust boundary or unauthorized mutation path was introduced.
- [x] 9. Performance — Passed; synchronous death-path work is bounded and persistence remains deferred.

---

## Verdict

**APPROVED**

All findings are closed. The IDL addition was accepted as a documented plan override, the asynchronous named-reference defect found during live verification was fixed and re-reviewed, and the final receipt records clean static gates, 32/32 required assertions, the complete 28/28 matrix, restored shipped gates, and successful cleanup with no harness residue.

