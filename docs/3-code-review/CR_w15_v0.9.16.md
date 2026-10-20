# Code Review: P.10p G1: Looking For Bot (LFB) and Joining a Player's Group

**Review Date**: 2026-10-20  
**Version**: 0.9.16  
**Files Reviewed**:

- `MMOCoreORB/bin/scripts/managers/sim_player_manager.lua`
- `MMOCoreORB/bin/web/aieconomy-dashboard/app.js`
- `MMOCoreORB/src/server/chat/ChatManagerImplementation.cpp`
- `MMOCoreORB/src/server/zone/managers/group/GroupManager.cpp`
- `MMOCoreORB/src/server/zone/objects/creature/commands/InviteCommand.h`
- `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimHunterController.cpp`
- `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimHunterController.h`
- `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerController.cpp`
- `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerController.h`
- `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp`
- `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.h`
- `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimBotGrouping.h` (new)
- `MMOCoreORB/src/tests/SimBotGroupingTest.cpp`
- `docs/1-plans/ROADMAP_p10-playerbot-parity.md`
- `docs/ai-pve-playerbot-design.md`

**Plan**: `docs/1-plans/F_0.9.15_p10p-lfb-grouping.plan.md`

---

## Executive Summary

The change implements the G1 PlayerBot grouping lifecycle: LFB discovery, invitation handling, following, defensive assistance, interplanetary travel, group commands, release, death, and rejoin. All production-code findings were addressed; remaining observations concern explicitly excluded temporary verification configuration and the required stopped-server verification window.

APPROVED with observations

---

## Changes Overview

The implementation adds grouping policy and state, chat and invite hooks, controller-owned movement and combat behavior, dashboard observability, and a grouping verification harness. It reuses stock group membership operations while keeping identity-keyed grouping state under a dedicated manager mutex. Verification-time fixes cover reply formatting, movement resumption and repathing, reconciliation, Group-tab chat, availability classification, status reporting, harness isolation, and dead-bot command handling.

---

## Findings

### Critical Issues

- **Live-only settings appeared enabled in the working-tree diff** — `MMOCoreORB/bin/scripts/managers/sim_player_manager.lua:1191`, `:2139`, `:2151`, `:2156`, `:2560`, `:2584`. These are owner-controlled verification or pre-existing LIVE-ONLY working-copy settings. The release-scoped grouping gate is default-off at `:2180`, and temporary settings are excluded when release hunks are staged. **Disposition: accepted with override.**

### Major Issues

- **Group assist omitted attackers found only on a member’s threat map** — `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimHunterController.cpp:665`. Assist scanning now combines threat-map and defender-list candidates before validating the attacker. **Disposition: addressed.**

- **Same-planet ROUTE could enter an interplanetary-only ticket-collector path** — `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp:35444`, `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimHunterController.cpp:760`, `docs/1-plans/F_0.9.15_p10p-lfb-grouping.plan.md:371`. The planner rejects same-planet off-world routing and the controller falls back to WALK. Intra-planet shuttle travel was moved to G2 by plan amendment. **Disposition: addressed.**

- **Closing the grouping gate could leave a stale offer and `groupInviterID`** — `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp:62386`. The gate-off task now drops the matching offer and clears the inviter ID when it still belongs to that inviter. **Disposition: addressed.**

- **DEAD_REJOIN records were excluded from group-command processing** — `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp:62936`, `:62943`, `:63010`, `:63034`. The snapshot now includes all non-RELEASING records, allowing cloning status and allowing `leave` to cancel a pending rejoin. **Disposition: addressed.**

- **A bot arriving from an interplanetary leg could remain WAITING without issuing its first follow path** — `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimHunterController.cpp:911`, `:920`, `:924`, `docs/1-plans/F_0.9.15_p10p-lfb-grouping.plan.md:631`. A WAITING mover may now be replaced when no follow path has ever been issued, while active-mover and movement-hold protections remain intact. **Disposition: addressed.**

- **The installed Lua verification copy appeared to remove the earlier parity scenario matrix** — `MMOCoreORB/bin/scripts/managers/sim_player_manager.lua:1191`, `:1196`, `:1198`, `docs/1-plans/F_0.9.15_p10p-lfb-grouping.plan.md:816`. The 77-scenario file is an explicitly temporary TRIP-verify selection containing the 58-scenario P.10 set plus 19 grouping scenarios. The owner’s full 182-plus-19 source matrix is restored during verification cleanup, and the temporary selection is not staged. **Disposition: accepted with override.**

### Minor Issues

- **Harness leaders failed join revalidation** — `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp:63102`. Harness-leader recognition is evaluated before the inviter lock and accepted alongside player creatures. **Disposition: addressed.**

- **A leave command could schedule a stale status response after release** — `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp:63033`. Leave now releases the addressed bot and continues before delayed status scheduling. **Disposition: addressed.**

- **No-assist verification used insufficient engagement evidence and the wrong hold interval** — `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp:25004`, `:25010`, `MMOCoreORB/bin/scripts/managers/sim_player_manager.lua:2124`. The assertion now holds from assertion entry, observes real threat/defender engagement, and covers faction exclusion plus neutral assistance. **Disposition: addressed.**

- **XP verification could reject an already-dead victim or miss an award recorded during the preceding step** — `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp:25064`, `:25105`. An already-killed victim is accepted, and the production award is matched by the fresh victim OID without a step-start cutoff. **Disposition: addressed.**

- **The harness XP finishing blow used a deadlock-prone lock order** — `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp:25075`, `docs/1-plans/F_0.9.15_p10p-lfb-grouping.plan.md:671`. It now follows KillCommand choreography by locking the killer and cross-locking the victim. **Disposition: addressed.**

- **No-loot verification used global or completed-loot-only evidence** — `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp:18900`, `:25132`. Claim-queue evidence is recorded under the loot mutex per identity and corpse and checked alongside completed-loot evidence. **Disposition: addressed.**

- **Grouping harness actions did not consistently exercise production effects** — `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp:24823`, `:24844`, `:24853`, `docs/1-plans/F_0.9.15_p10p-lfb-grouping.plan.md:659`. The harness now uses stock kick/disband operations and production observations for chat, decline, XP, loot, release, and combat. Request isolation, scenario resets, hold windows, step budgets, faction setup, and attacker durability were also corrected. **Disposition: addressed.**

- **Trailing whitespace caused `git diff --check HEAD` to fail** — `MMOCoreORB/bin/scripts/managers/sim_player_manager.lua:2130`. The line belongs to the generated temporary verification copy and is absent from the restored source used for release staging. **Disposition: accepted with override.**

- **Affected tests cannot run while the live server holds the shared BDB environment** — `docs/ARCHI.md:937`, `docs/1-plans/F_0.9.15_p10p-lfb-grouping.plan.md:814`, `:858`. Lint and the warning-clean build passed; SimBot tests and live verification run in the mandatory stopped-server TRIP-verify window. The unrelated `armor_mitigation_live` flake passed on rerun and was not attributed to this delta. **Disposition: accepted environmental constraint and external release gate.**

### Suggestions

None.

---

## Checklist

- [x] 1. Functional Requirements — passed
- [ ] 2. Code Quality — passed with caveat: the installed temporary verification copy contains excluded whitespace and scenario-selection artifacts
- [x] 3. Architectural Compliance — passed
- [x] 4. Distributed Object / IDL Discipline — not applicable; no IDL or generated-file changes
- [x] 5. Lua/C++ Boundary — passed
- [ ] 6. AI-Economy / Simulation Safety — passed with caveat: owner-controlled verification settings are visible in the working tree but excluded from release staging
- [x] 7. Error Handling — passed
- [x] 8. Security — passed
- [x] 9. Performance — passed

---

## Verdict

**APPROVED with observations**

No production-code findings remain open. Approval incorporates the explicit exclusion of the temporary TRIP-verify Lua copy and does not waive the documented stopped-server SimBot and live-verification release gate.

---

## Release Gate (added at release)

The deferred gate has been met:

- `SimBot*` gtests: 190/190 with the server stopped;
- TRIP-verify: `LIVE_VERIFICATION_PASS`, 27/27 assertions, including 77/77 harness scenarios on the
  final build and the owner's real-client pass;
- the temporary verification Lua was replaced by the owner's live working copy before release;
- only grouping hunks are staged.

Report: `docs/4-unit-tests/live_f0915-lfb-grouping_20261019-180238-f0915-lfb-grouping.md`.
