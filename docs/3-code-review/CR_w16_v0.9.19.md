# Code Review: P.10p G4: Group Tactics (pull, spin mode, take foods)

**Review Date**: 2026-10-27  
**Version**: 0.9.19  
**Files Reviewed**:

- `MMOCoreORB/bin/scripts/managers/sim_player_manager.lua`
- `MMOCoreORB/bin/web/aieconomy-dashboard/app.js`
- `MMOCoreORB/src/server/zone/objects/creature/ai/AiAgent.idl`
- `MMOCoreORB/src/server/zone/objects/creature/ai/AiAgentImplementation.cpp`
- `MMOCoreORB/src/server/zone/objects/creature/ai/bt/leaf/SimpleActions.h`
- `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimBotAreaAttackGuard.h`
- `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimBotGroupRally.h`
- `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimBotGroupTactics.h`
- `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimBotGrouping.h`
- `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimHunterController.cpp`
- `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimHunterController.h`
- `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp`
- `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.h`
- `MMOCoreORB/src/tests/SimBotGroupRallyTest.cpp`
- `MMOCoreORB/src/tests/SimBotGroupTacticsTest.cpp`
- `MMOCoreORB/utils/engine3`
- `VERSION`
- `docs/1-plans/ROADMAP_p10-playerbot-parity.md`
- `docs/ai-pve-playerbot-design.md`

**Plan**: `workspace/Core3/docs/1-plans/F_0.9.18_p10p-g4-group-tactics.plan.md`

---

## Executive Summary

This change adds gated group pull, spin, back-off, spin polling, and food commands, plus the supporting movement, combat, mission, dashboard, diagnostics, and verification infrastructure. Iterative code review and owner in-client verification exposed several real movement, combat-thread, and mission-lifecycle regressions; all blocking findings were addressed or explicitly superseded by owner decisions.  
APPROVED

---

## Changes Overview

Group tactic commands are parsed by `SimBotGroupTactics`, dispatched through `SimPlayerManager`, and executed by `SimHunterController` with generation-bound orders and explicit movement/combat leases. Pulling now approaches, confirms a real tag, and returns with phase-specific budgets; spin mode protects quiet lairs while allowing area-capable bots to engage an aggroed or explicitly ordered lair. The change also extends group mission coordination, food handling, dashboard state, diagnostics, unit tests, and live-verification scenarios.

---

## Findings

### Critical Issues

None.

### Major Issues

- **Pull tags were falsely confirmed by zero-damage threat entries or defender links — addressed.** A tag now requires the creature to aim at the bot or record positive damage from it; the pure state machine and tests exercise the rule. `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimHunterController.cpp:2194`, `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimBotGroupTactics.h:252`, `MMOCoreORB/src/tests/SimBotGroupTacticsTest.cpp:84`

- **Area-attack preference rejected the tangible lair target — addressed.** The selector accepts any tangible follow target while applying dead/state checks only to creatures. `MMOCoreORB/src/server/zone/objects/creature/ai/AiAgentImplementation.cpp:3144`

- **Area preference and normal special selection used divergent HAM/reserve rules — addressed.** Both paths now use one shared skill-derived-special refusal helper. `MMOCoreORB/src/server/zone/objects/creature/ai/AiAgentImplementation.cpp:3109`, `MMOCoreORB/src/server/zone/objects/creature/ai/AiAgentImplementation.cpp:3195`, `MMOCoreORB/src/server/zone/objects/creature/ai/AiAgentImplementation.cpp:3382`

- **Spin polling and cloning responses did not reflect each addressed bot correctly — addressed.** Poll replies use the bot’s own progression, HAM, buffs, and armor snapshot; cloning bots reply without receiving an order or counting as a gate rejection. `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimHunterController.cpp:2388`, `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimHunterController.cpp:2405`, `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp:68018`

- **New production and test headers were initially absent from the tracked change set — addressed.** `SimBotGroupTactics.h` and `SimBotGroupTacticsTest.cpp` are both present in the final diff. `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimBotGroupTactics.h:18`, `MMOCoreORB/src/tests/SimBotGroupTacticsTest.cpp:1`

- **Back-off suppressed attacks but did not reliably make bots leave the lair — addressed.** Back-off raises an in-combat movement lease, clears spin state, replaces the lair-bound path immediately, and releases the lease after reaching the leader. `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimHunterController.cpp:1298`, `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimHunterController.cpp:1303`, `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimHunterController.cpp:1785`, `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimHunterController.cpp:3324`

- **Harness identity and scenario bugs could hide or misreport G4 behavior — addressed.** `assertGroupTactics` resolves its identity before reading state; borrowed production references remain stable; pull assertions follow the adopted-attacker path; food uses a per-identity gate override without seeding a production vault. `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp:26370`, `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp:27249`, `MMOCoreORB/bin/scripts/managers/sim_player_manager.lua:3639`, `MMOCoreORB/bin/scripts/managers/sim_player_manager.lua:3640`

- **A creature or pack mate attacking before the planned tag aborted the pull — addressed.** Any attacker during approach/tag now proves the pull is active, becomes the target when necessary, and transitions directly to return. `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimHunterController.cpp:2033`

- **Returning pullers were pinned by their own chase and two independent self-defense paths — addressed.** The bot-side chase is dropped while creature aggro remains, non-running weapons re-drop it each tick, group-tick defense ignores all attackers during return, and the arrival-check thread honors the return lease. Ranged weapons retain the owner-requested fight-on-the-run behavior. `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimHunterController.cpp:1917`, `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimHunterController.cpp:1934`, `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimHunterController.cpp:2263`, `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimHunterController.cpp:2497`, `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimHunterController.cpp:3694`

- **Pull timeout was shared across approach and return, while approach could previously hang — addressed.** Approach and return now have independent budgets measured from their respective starts, with a regression test. `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimBotGroupTactics.h:239`, `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimBotGroupTactics.h:264`, `MMOCoreORB/src/tests/SimBotGroupTacticsTest.cpp:93`

- **Stale movement paths prevented both pull approach and return — addressed.** Entering return immediately replaces the approach path; the first approach tick replaces the existing follow path. Tag range now derives from the equipped weapon rather than the hunter’s 48 m default. `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimHunterController.cpp:1942`, `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimHunterController.cpp:2219`, `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimHunterController.cpp:2245`

- **Mission refresh silently stopped following after a leader issued `follow` — addressed.** Mission directives are published only while the plan is actively requested. `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp:18700`

- **Spin was tied to the revealed-mission branch and back-off lacked complete stage-independent behavior — addressed.** `runGroupSpin` now executes from the general group tick, back-off handles approaching or engaged bots outside missions, and the area guard preserves deliberate spin engagement. `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimHunterController.cpp:1692`, `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimHunterController.cpp:1785`, `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimHunterController.cpp:2571`, `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimHunterController.cpp:3376`

- **The lair footprint prevented bots from ever reaching the original 4 m spin point — addressed.** Engagement includes the existing footprint margin and falls back to engaging after a 15-second blocked approach. `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimHunterController.cpp:1877`, `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimHunterController.cpp:1884`, `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimBotAreaAttackGuard.h:96`

- **The intermediate spin trigger either waited too long for leader combat or could charge a quiet lair — accepted with owner override and addressed.** Final policy requires leader proximity plus either observable lair aggro or the explicit `attack the lair` command. `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimBotGroupTactics.h:322`, `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimHunterController.cpp:1838`, `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimHunterController.cpp:1856`

- **A completed lair could leave the mission chain permanently blocked after `follow` cleared holder context — addressed.** Entry completion is idempotent, manager-side lair destruction completes the entry independently of controller context, and remaining entries resume the run. `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp:19333`, `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp:19351`, `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp:40134`

- **Spin-go commands did not initially wake bots, and owner phrases were incomplete — addressed.** `TACTIC_SPIN_GO` joins the spin wake path; `have spin`, `stop spin`, and `stop spinning` are supported. `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp:68234`, `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp:68353`, `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimBotGroupTactics.h:31`

- **Group mission state blocked G2 buff trips and mission refresh overwrote active trips — addressed.** Only the holder owns solo mission-lair fields; non-mission directives cancel local mission state; mission publication skips active G2 trip stages. `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimHunterController.cpp:875`, `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimHunterController.cpp:1018`, `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimHunterController.cpp:1441`, `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp:18713`

- **Nearest-mission refresh could switch entries during an active lair fight — addressed.** A revealed, standing current lair now pins the entry until completion or abandonment. `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp:18733`

- **Spin could claim a bot more than a kilometer away and block normal travel — addressed.** A bot not already spinning is claimed only within 64 m of the lair. `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimHunterController.cpp:1831`

- **`onTick` could shed lair combat, and the first lease fix published the flag after combat became visible — addressed.** Spinning bots re-engage if combat drops; the atomic lease is release-stored before `startCombat`, acquire-read by `onTick`, and cleared on failure and teardown. `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimHunterController.cpp:1814`, `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimHunterController.cpp:3701`, `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimHunterController.cpp:9291`, `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimHunterController.cpp:9303`

- **`attack the lair` carried into later lairs; the first per-controller fix still deferred binding for distant bots — addressed after rework.** The manager now resolves and stores one lair OID when the command is spoken, and controllers require that exact OID. `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp:68124`, `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp:68194`, `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimHunterController.cpp:1856`

- **The mission holder’s path still aimed at the lair center, so a large arrival radius did not stop the in-flight path — addressed.** The target is now a radial point on the holder’s side of the lair, using a 40 m configurable stand-off and the smaller rally arrival radius. `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimHunterController.cpp:1331`, `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimHunterController.cpp:1343`, `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.h:151`, `MMOCoreORB/bin/scripts/managers/sim_player_manager.lua:3717`

- **The testing gate was temporarily incomplete after the pull phase-budget change — addressed.** The new regression test was compiled and then run with the server stopped; the final result was 218/218 with no database file-descriptor holders. `MMOCoreORB/src/tests/SimBotGroupTacticsTest.cpp:93`, `docs/4-unit-tests/live_f0918-g4-group-tactics_20261023-035349-f0918-g4-group-tactics.md:29`

### Minor Issues

- **Tactic diagnostic rate limiting allowed an order event to hide its outcome — addressed.** Rate limiting now keys by identity and event kind. `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp:69367`

- **Pull target diagnostics displayed an empty internal creature name — addressed.** Diagnostics now retain the displayed name. `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimHunterController.cpp:2152`

### Suggestions

- **Live verification timing — accepted with workflow override and subsequently completed.** The owner confirmed that scoped TRIP verification follows code-review convergence. Seven scoped harness boots, two post-harness boots, and repeated in-client passes ultimately produced accepted live evidence. `docs/4-unit-tests/live_f0918-g4-group-tactics_20261023-035349-f0918-g4-group-tactics.md:29`, `docs/4-unit-tests/live_f0918-g4-group-tactics_20261023-035349-f0918-g4-group-tactics.md:36`, `docs/4-unit-tests/live_f0918-g4-group-tactics_20261023-035349-f0918-g4-group-tactics.md:74`, `docs/4-unit-tests/live_f0918-g4-group-tactics_20261023-035349-f0918-g4-group-tactics.md:81`

- **`group_tactics_pull_nothing` live failure — accepted as an environment limitation, not a code defect.** Wild creatures occupied the stand-in’s known location; the no-target selection behavior remains unit-tested. `docs/4-unit-tests/live_f0918-g4-group-tactics_20261023-035349-f0918-g4-group-tactics.md:58`, `MMOCoreORB/src/tests/SimBotGroupTacticsTest.cpp:45`

- **Healing behavior — accepted owner scope decision.** Healing complaints were deferred to G5 and were not treated as G4 defects. `workspace/Core3/docs/1-plans/F_0.9.18_p10p-g4-group-tactics.plan.md:37`, `docs/4-unit-tests/live_f0918-g4-group-tactics_20261023-035349-f0918-g4-group-tactics.md:105`

---

## Checklist

- [x] 1. Functional Requirements — passed; all review and owner-verification regressions were resolved or explicitly overridden.
- [x] 2. Code Quality — passed.
- [x] 3. Architectural Compliance — passed.
- [x] 4. Distributed Object / IDL Discipline — passed.
- [x] 5. Lua/C++ Boundary — passed.
- [x] 6. AI-Economy / Simulation Safety — passed; gated behavior received scoped harness and in-client verification.
- [x] 7. Error Handling — passed.
- [x] 8. Security — passed; no authentication or sensitive-data regression.
- [x] 9. Performance — passed; no new unbounded per-tick population scan or cleanup issue found.

---

## Verdict

**APPROVED**

All critical and major findings are closed. Owner overrides for spin triggering, verification sequencing, the environmental no-target scenario, and G5 healing scope are recorded above; no open code-review finding blocks promotion.

