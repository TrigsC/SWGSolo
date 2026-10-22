# Code Review: P.10p G3 — Group Missions, Waypoints, Reveal and Assist

**Review Date**: 2026-10-22  
**Version**: 0.9.18  
**Files Reviewed**:

- `MMOCoreORB/bin/conf/config.lua`
- `MMOCoreORB/bin/scripts/managers/sim_player_manager.lua`
- `MMOCoreORB/bin/web/aieconomy-dashboard/app.js`
- `MMOCoreORB/src/server/zone/managers/mission/MissionManager.idl`
- `MMOCoreORB/src/server/zone/managers/mission/MissionManagerImplementation.cpp`
- `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimBotGroupRally.h`
- `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimHunterController.cpp`
- `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimHunterController.h`
- `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp`
- `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.h`
- `MMOCoreORB/src/server/zone/objects/group/tasks/UpdateNearestMissionForGroupTask.cpp`
- `MMOCoreORB/src/server/zone/objects/group/tasks/UpdateNearestMissionForGroupTask.h`
- `MMOCoreORB/src/server/zone/objects/mission/MissionObjectiveImplementation.cpp`
- `MMOCoreORB/src/tests/SimBotGroupRallyTest.cpp`
- `MMOCoreORB/utils/engine3` (pre-existing dirty submodule; not part of this release)
- `docs/1-plans/ROADMAP_p10-playerbot-parity.md`
- `docs/4-unit-tests/TESTING.md`
- `docs/ai-pve-playerbot-design.md`

**Plan**: `workspace/Core3/docs/1-plans/F_0.9.17_p10p-g3-group-missions.plan.md`

---

## Executive Summary

This change adds default-off grouped PlayerBot missions, shared group waypoints and rewards, mission chat and reveal behavior, leader assistance, and a bounded parity verification harness. All correctness findings were resolved, and the warning-clean build, 213/213 affected tests, scoped harness evidence, and final owner in-client pass satisfy the approval gate. Remaining observations are intentional worktree exclusions or release-step actions.

APPROVED with observations

---

## Changes Overview

The implementation adds manager-owned group mission plans, terminal assignment, nearest-mission selection, hold/reveal movement, leader-assist behavior, reward splitting, and stock group-waypoint integration. It also centralizes the mission limit, adds configurable Lua vocabulary and chat lines, expands dashboard diagnostics, and makes the parity harness scoped, bounded, and continue-on-failure capable. Post-receipt fixes were verified in the real client, including direction placement, hold release, holder-led relocation, coordinates, named chat speakers, and clone-buff guidance.

---

## Findings

### Critical Issues

1. **Live-only capability gates enabled — accepted with override.**  
   `MMOCoreORB/bin/scripts/managers/sim_player_manager.lua:3692-3696`  
   The grouping, directive, and mission gates remain enabled in owner-marked live-only worktree lines. The implementer confirmed these lines are never staged; committed G3 defaults remain off.

2. **Harness teardown could release real-player groups — addressed.**  
   `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp:66312` and `:26753`  
   Teardown now snapshots harness leader OIDs and releases only groups led by those stand-ins. Production gate-off paths retain the intentionally global release.

### Major Issues

1. **Mission lines swallowed by the unmatched-command return — addressed.**  
   `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp:67066`  
   The early return now requires that the parsed line is neither a directive nor a mission.

2. **GRAB ignored requested count and server capacity — addressed.**  
   `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp:18780` and `:18799`  
   `wantCount` is passed through and clamped against the shared server mission limit and current held-offer count.

3. **Closed GRAB batches could remain stuck in wait — addressed.**  
   `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp:18915`, `:19003`, and `:66370`  
   Every batch-closing path now sets `runRequested`; reconciliation remains the sole timeout closer and performs the complete post-actions.

4. **Controller retained a stale mission-lair OID — addressed.**  
   `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimHunterController.cpp:956` and `:1208`  
   The controller refreshes the lair OID from mission context each tick and from successful reveal results.

5. **Death handling corrupted mission stages — addressed.**  
   `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp:66800`  
   GRAB stages resume at `MISSION_GRAB_TRAVEL`; later stages resume through `MISSION_WAIT`. Other active grabbers and the dying identity are handled independently.

6. **Reward-split failure paths lost the holder’s reward — addressed.**  
   `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp:39295` and `:39377`  
   Every early exit and an empty eligible-member list pay the holder alone with divisor one.

7. **Leader assist could override self-defence — addressed.**  
   `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimHunterController.cpp:1683`  
   Leader assist runs only when there is no live attacker targeting the bot itself.

8. **Mission teardown left stale stock group waypoints — addressed.**  
   `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp:66554`  
   Teardown schedules the stock nearest-mission refresh directly for each player-member planet.

9. **Player mission planet came from the owner’s current zone — addressed.**  
   `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp:18345`  
   Player missions are now resolved against the waypoint’s planet CRC.

10. **`continueOnFailure` performed whole-run cleanup — addressed.**  
    `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp:24836`  
    Failed scenarios run their own `remove*` teardown and despawn helpers without destroying shared harness bodies or unrelated groups. Whole-run cleanup remains only for completion and infrastructure failures.

11. **Ignored new tests could be omitted from the commit — accepted with override.**  
    `MMOCoreORB/.gitignore:25` and `docs/1-plans/F_0.9.17_p10p-g3-group-missions.plan.md:783`  
    Test files remain ignored by the repository pattern, but the plan explicitly requires `git add -f`, matching the established SimBot test workflow.

12. **Affected SimBot tests had not been rerun — addressed.**  
    `docs/4-unit-tests/live_f0917-g3-group-missions_20261022-021139-f0917-g3-group-missions.md:30`  
    The final receipt records 213/213 passing after the owner-pass fixes.

13. **Reveal abandonment leaked held-offer capacity — addressed.**  
    `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimHunterController.cpp:1190`, `:1203`, and `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp:68210`  
    Both reveal-abandon paths and SKIP remove unrevealed offers under `pveMutex`.

14. **Latest parser delta lacked an exact-delta test run — addressed.**  
    `MMOCoreORB/src/tests/SimBotGroupMissionTest.cpp:100` and `docs/4-unit-tests/live_f0917-g3-group-missions_20261022-021139-f0917-g3-group-missions.md:33`  
    The owner-phrase test is included in the final 213/213 run.

15. **Leader proximity bypassed hold-short travel from the terminal — addressed.**  
    `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimHunterController.cpp:1128`  
    The proximity release now requires the holder to be at the hold radius; the independent “leader already nearer the lair” rule remains. Live confirmation is recorded at `docs/4-unit-tests/live_f0917-g3-group-missions_20261022-021139-f0917-g3-group-missions.md:115`.

16. **Reveal “Follow me” used the wrong bot’s name — addressed.**  
    `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp:19071`, `:19090`, and `:19125`  
    BOT_OFFER reveals capture and prefer the holder identity; player-owned entries retain the lowest-grouped-bot fallback.

### Minor Issues

1. **Dirty `engine3` submodule appeared in the worktree — accepted with override.**  
   `MMOCoreORB/utils/engine3:1`  
   This is a pre-existing owner worktree condition and is excluded from the change.

2. **Original scoped live verification was incomplete — addressed.**  
   `docs/4-unit-tests/live_f0917-g3-group-missions_20261022-021139-f0917-g3-group-missions.md:1` and `:49`  
   The receipt records the scoped harness outcomes, accepted direct evidence, and final owner approval.

3. **Killer evidence and failure classification could disagree — addressed.**  
   `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp:25228`, `:25254`, and `:25284`  
   Both evidence formatting and classification use the same scenario-killer predicate.

4. **Reveal retry count could carry into the next mission entry — addressed.**  
   `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimHunterController.cpp:843`  
   `beginGroupMission` resets the defer counter.

5. **Post-fix live verification and receipt updates were pending — addressed.**  
   `docs/4-unit-tests/live_f0917-g3-group-missions_20261022-021139-f0917-g3-group-missions.md:103`  
   The final receipt covers named chat, direction parsing and placement, hold release, holder-led relocation, coordinate callouts, directive wake-up, and clone-buff guidance. It records final owner approval at `:107` and 213/213 tests at `:33`.

### Suggestions

None.

---

## Checklist

- [x] 1. Functional Requirements — passed
- [x] 2. Code Quality — passed
- [x] 3. Architectural Compliance — passed
- [x] 4. Distributed Object / IDL Discipline — passed
- [x] 5. Lua/C++ Boundary — passed
- [x] 6. AI-Economy / Simulation Safety — passed with accepted live-only worktree gate override; committed defaults remain off and final in-game verification is recorded
- [x] 7. Error Handling — passed
- [x] 8. Security — passed
- [x] 9. Performance — passed

---

## Verdict

**APPROVED with observations**

No blocking findings remain. The live-only gate lines, force-add test workflow, and pre-existing dirty `engine3` submodule are accepted exclusions rather than release content. The warning-clean build, Lua validation, 213/213 affected tests, scoped receipt, and final owner in-client approval satisfy the review gate; roadmap and changelog release metadata are intentionally completed by the following TRIP release step.

