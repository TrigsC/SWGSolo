# Code Review: P.8.3 PvE Hunter Combat & Movement Realism

**Review Date**: 2026-07-18
**Version**: 0.4.4
**Files Reviewed**:

- `MMOCoreORB/bin/scripts/managers/sim_player_manager.lua`
- `MMOCoreORB/bin/web/aieconomy-dashboard/app.js`
- `MMOCoreORB/bin/web/aieconomy-dashboard/index.html`
- `MMOCoreORB/bin/web/aieconomy-dashboard/styles.css`
- `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimHunterController.cpp`
- `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimHunterController.h`
- `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerController.cpp`
- `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerController.h`
- `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp`
- `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.h`
- `MMOCoreORB/utils/engine3`
- `docs/4-unit-tests/COVERAGE-DEBT.md`
- `docs/ai-pve-playerbot-design.md`

**Plan**: `docs/1-plans/F_0.4.4_p83-hunter-combat-movement-realism.plan.md`

---

## Executive Summary

This change adds realistic hunter combat, hybrid navmesh/overland movement, corrected buffs and phase announcements, and a read-only hunter-position dashboard. Iterative review identified movement cancellation and asynchronous task-lifecycle defects; all were addressed without leaving open findings.
APPROVED

---

## Changes Overview

Hunters now enter combat through the real combat manager with aligned ranged weapons, while hybrid movement follows navmeshes inside cities and terrain-aware paths outside them. The change also corrects HAM buff application and phase announcements, adds bounded Lua movement tunables, and exposes existing hunter telemetry through the new `#/wilds` dashboard route. Design documentation and the coverage-debt ledger were updated for the live-verified AI behavior.

---

## Findings

### Critical Issues

None.

### Major Issues

1. **Canceled hybrid routes could revive stale movement** — `SimHunterController.h:127`, `SimPlayerController.cpp:263`, `SimPlayerController.cpp:764`
   The initial implementation preserved `finalDestination` across disengagement without distinguishing legitimate interrupted travel from canceled routes. The final implementation gates IDLE resume and late path acceptance through `shouldResumeHybridTravel()`.
   **Disposition: addressed.**

2. **Arrival-task TOCTOU allowed terminal routes to resume after cancellation** — `SimHunterController.cpp:368`, `SimHunterController.cpp:370`, `SimHunterController.cpp:1331`, `SimHunterController.cpp:1333`
   An arrival task could pass its generation check, wait for the agent lock, and observe the old open gate after disengagement. Cleanup now sets `missionCleanupRequested` before disengagement, and completion clears `orderActive` before disengagement.
   **Disposition: addressed.**

3. **Navmesh debounce repathing bypassed the cancellation gate** — `SimPlayerController.cpp:742`, `SimPlayerController.cpp:756`
   The mode-transition branch previously allowed an IDLE hybrid controller to request a path without checking whether travel was still valid. IDLE debounce repathing now requires `shouldResumeHybridTravel()`.
   **Disposition: addressed.**

4. **Generation invalidation stranded legitimate active travel** — `SimHunterController.cpp:1086`, `SimPlayerController.cpp:730`, `SimPlayerController.cpp:764`
   A generation bump in `disengageTarget()` invalidated the arrival watchdog required to recover from a mid-travel combat interruption. The bump was removed; the live arrival loop now survives combat and resumes only while the order gate remains valid.
   **Disposition: addressed.**

5. **Order-level gating still allowed canceled pursuit destinations to revive** — `SimHunterController.cpp:1173`, `SimHunterController.cpp:1178`, `SimHunterController.cpp:1195`, `SimHunterController.cpp:1198`
   Target destruction below quota and target-unavailable handling leave the overall order active, so order-level gating alone could still resume movement toward a corpse or vanished target. Both pursuit-cancellation paths now clear the hybrid destination, satisfying the cancellation requirement at `docs/1-plans/F_0.4.4_p83-hunter-combat-movement-realism.plan.md:296`.
   **Disposition: addressed.**

### Minor Issues

1. **Terminal-accept and heading-to-lair announcements were missing** — `SimHunterController.cpp:319`, `SimHunterController.cpp:353`
   The two missing phase-transition calls were added, completing the depart, accept-terminal, heading-to-lair, and return announcement mapping while retaining existing cooldown behavior.
   **Disposition: addressed.**

### Suggestions

None.

---

## Checklist

- [x] 1. Functional Requirements — passed
- [x] 2. Code Quality — passed
- [x] 3. Architectural Compliance — passed
- [x] 4. Distributed Object / IDL Discipline — passed; no IDL/autogen changes or unresolved lock-order defects
- [x] 5. Lua/C++ Boundary — passed
- [x] 6. AI-Economy / Simulation Safety — passed; hunter-only opt-in, existing configuration gates, simulation-only harvest, and live verification retained
- [x] 7. Error Handling — passed
- [x] 8. Security — passed; the dashboard addition is frontend-only and uses the existing authenticated telemetry source
- [x] 9. Performance — passed; pathfinding work is bounded and no new population-wide per-tick scans were introduced

---

## Verdict

**APPROVED**

All review findings were addressed. No overrides or accepted open issues remain. The requester reported clean lint, a warning-free `-Werror` build, clean Lua/JavaScript syntax checks, live AI verification, and an appropriate coverage-debt entry; `git diff --check HEAD` was also clean.

---

## Addendum — post-review live-verification hardening

The `APPROVED` verdict above covers the F_0.4.4 implementation as reviewed by the
Codex loop. During owner in-game verification, several combat-realism gaps were
found and fixed iteratively. **These follow-on changes were each built
warning-clean (`-Werror`) and owner-verified live in-game (this project's stated
acceptance bar), but were not put back through the Codex review loop.** They are
recorded here for a faithful release record. Additional files touched:
`AiAgentImplementation.cpp` and the `death_watch_wraith.lua` mobile template.

- **Two-way combat requires `ATTACKABLE`** (`SimPlayerManager.cpp` spawn;
  `AiAgentImplementation.cpp:6579`). A creature only retaliates against a target
  its `isAttackableBy` accepts, which rejects any `pvpStatusBitmask` lacking
  `ObjectFlag::ATTACKABLE`. Hunter bodies now spawn `PLAYER | ATTACKABLE`. To keep
  the neutral bot from becoming player-attackable, `isAttackableBy` returns false
  for a real player attacking a faction-0 `getSimPlayerBot()` (mirroring a neutral
  player); creature/NPC AI still falls through to the faction-0 rules. The
  `TangibleObject` overload routes CreatureObject attackers through the same
  guarded method, so there is no bypass.

- **Self-defense against non-target attackers** (`SimHunterController::onTick`,
  `defendAgainstInterceptor`). The hunter previously fought only its scan-acquired
  mission target, so an interceptor mid-travel — or a creature that aggros at the
  lair before a target is acquired (`scanForTarget` early-returns while in combat)
  — went unanswered and it died passively. `onTick` (the arrival-cadence hook,
  live through both travel and the lair stand) now fights whatever is attacking
  when there is no live acquired target, and steps aside when a mission target is
  engaged. It never moves the bot.

- **Combat cadence** (`SimHunterController::engageTarget`,
  `defendAgainstInterceptor`). The controller never woke the AI behavior tree, so
  attacks fired only opportunistically (a target could be aimed at but unshot for
  minutes). Both engage paths now call `activateAiBehavior(true)` after
  `startCombat`, matching the working `SimPvPController` (`:842`).

- **Buff magnitude and combat template** (`sim_player_manager.lua`,
  `death_watch_wraith.lua`). Enhancement buff modifiers were a token `+100`
  (imperceptible); raised to `+2500`. The body template moved off `artisan` (no
  combat skills/attacks) to the owner-selected neutral combat template
  `death_watch_wraith` with the `rifle_t21`; the wraith template was tuned down
  from its Death Watch Bunker boss stats (HAM 120000 → 1000/1500, damage
  1020/1750 → 500/600) for hunter use. This re-tunes the shared DWB dungeon NPC —
  acceptable in this no-players economy sandbox.

**Verdict for the release: APPROVED with observations** — the reviewed core is
`APPROVED`; the live-hardening addendum is owner-verified but not Codex-reviewed.
