# Code Review: Cell-Navigation & Starport Ticket-Collector Travel (F_0.4.7–F_0.4.11)

**Review Date**: 2026-07-23 (Codex) + 2026-07-24 (manual addendum)
**Version**: 0.4.11
**Files Reviewed**:

- `MMOCoreORB/bin/scripts/managers/sim_player_manager.lua`
- `MMOCoreORB/src/server/zone/objects/building/BuildingObject.idl`
- `MMOCoreORB/src/server/zone/objects/building/BuildingObjectImplementation.cpp`
- `MMOCoreORB/src/server/zone/objects/creature/ai/AiAgentImplementation.cpp`
- `MMOCoreORB/src/server/zone/objects/creature/simplayer/CellNavDiagLog.h`
- `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerController.{cpp,h}`
- `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.{cpp,h}`
- `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPvPController.{cpp,h}`

**Plan**: `docs/1-plans/F_0.4.11_starport-ticket-collector-travel.plan.md` (bundles
F_0.4.7 diagnostics, F_0.4.8 entry fix, F_0.4.9 egress, F_0.4.10 nearest-portal,
F_0.4.11 ticket-collector travel; see also the earlier per-pass reviews for
F_0.4.7/0.4.9/0.4.10 under `.claude/skills/codex-code-review/state/`).

> **Scope note.** The pre-existing owner-owned working-tree edits
> `death_watch_wraith.lua` and `rifle_t21.lua`, and the `engine3` submodule
> change, are **excluded from this release commit** (unrelated owner NERF/tuning
> work) and were not raised as findings. Diagnostic scaffolding is retained but
> now dormant behind a config flag (see the manual addendum, item D).

---

## Executive Summary

This change adds default-off, ticket-collector-based interplanetary travel for
miners and PvP squads, including interior departure routing, bounded failure
recovery, and per-bot arrival-hollow exit, built on the F_0.4.8–F_0.4.10
cell-entry / egress / nearest-portal fixes. Seven Major findings were raised
across the Codex review iterations; all were addressed, with no findings
overridden or left open.

**APPROVED** (Codex loop, multi-round) + owner live-verification addendum.

---

## Changes Overview

The implementation adds a tri-state starport interior resolver, directed
collector approaches with cell-egress suppression, real-distance boarding gates,
delayed and bounded retries, and safe outdoor recovery. PvP travel now
coordinates leader/member arrival exits through an idempotent OID barrier and
suppresses immediate replanning after terminal collector-departure failure.
Configuration remains simulation-only and default-off in
`sim_player_manager.lua`; existing miner and PvP travel behavior remains selected
while the gate is disabled.

---

## Findings (Codex)

### Critical Issues

None.

### Major Issues

#### 1. Outdoor starports were misclassified as resolver failures

**Location**: `SimPlayerManager.cpp:26495`

The original resolver treated the presence of any nearby portal-layout building
as evidence that an interior should exist, causing genuinely outdoor starports to
return `RESOLVE_FAILED`. The final implementation requires a cell-bearing
building to contain the travel point; a populated query without such a building
returns `STARPORT_NO_INTERIOR`, while only an empty query returns
`STARPORT_RESOLVE_FAILED` at `SimPlayerManager.cpp:26500`.

**Disposition: Addressed.**

#### 2. Miner arrival resolver retries exhausted synchronously and cancellation could strand the miner

**Locations**: `SimPlayerController.cpp:2031`, `SimPlayerController.cpp:2052`

A transient `RESOLVE_FAILED` recursively re-entered the resolver, exhausting the
attempt budget immediately; cancellation during an arrival phase could then
release normal mining behavior while the miner remained inside the destination
hollow. The resolver now schedules a delayed two-second retry; arrival
cancellation invalidates controller work, clears all agent movement and cached
paths, relocates the miner outside, updates its home anchor, clears the travel
state, and resumes recovery.

**Disposition: Addressed.**

#### 3. PvP leader and member arrival retries lacked a guaranteed terminal path

**Locations**: `SimPvPController.cpp:603`, `SimPvPController.cpp:1216`

Repeated resolver/path failures could keep a leader retrying indefinitely while
members did not enforce the same retry cap and could permanently hold the squad
arrival barrier. Both now enforce the configured attempt limit and call
`abandonArrivalExit` on exhaustion (relocate outside + remove the OID from the
barrier).

**Disposition: Addressed.**

#### 4. The PvP arrival barrier was non-idempotent and death-unsafe

**Locations**: `SimPlayerManager.cpp:29532`, `:29658`, `:29760`

The original integer completion counter could be decremented more than once per
participant and mishandled the final participant dying. The final implementation
records pending leader/member OIDs in `arrivalExitPending`, removes each OID at
most once, and finalizes when the vector empties; death handling snapshots
barrier state under `pvpSquadMutex` and invokes the idempotent completion
callback only after releasing that lock.

**Disposition: Addressed.**

#### 5. Terminal PvP collector cancellation immediately replanned the same failed leg

**Locations**: `SimPlayerManager.cpp:28369`, `:29616`

`abandonPvpRoutedTravel` now clears the route and announcement state and installs
a suppression window; departure-intent planning honors it, so the immediate retry
cycle uses the normal city-shuttle target instead of reselecting the unreachable
collector leg.

**Disposition: Addressed.**

#### 6. Arrival recovery teleports left stale movement and home state active

**Locations**: `SimPlayerController.cpp:2068`, `SimPvPController.cpp:656`, `:1260`

Recovery `switchZone` teleports now invalidate controller work, set `OBLIVIOUS`,
clear patrol/saved/current paths, relocate outdoors, and update `homeLocation`
under the same agent lock — so an `OBLIVIOUS` bot cannot re-`PATHING_HOME` back
into the hollow before barrier completion.

**Disposition: Addressed after two review iterations.**

#### 7. Member abandonment terminated the member tick chain

**Location**: `SimPvPController.cpp:1291`

Member abandonment now calls `assertFollow` and reschedules a generation-aware
`ArrivalCheckTask`, preserving death reporting, combat scanning, and follow
self-healing.

**Disposition: Addressed.**

### Minor Issues / Suggestions

None.

---

## Checklist

- [x] 1. Functional Requirements — Passed; travel, fallback, recovery, and barrier flows conform to the plan.
- [x] 2. Code Quality — Passed.
- [x] 3. Architectural Compliance — Passed; relocation and locking follow the established boarding choreography.
- [x] 4. Distributed Object / IDL Discipline — Passed; IDL changes confined to the source declaration; no generated files hand-edited.
- [x] 5. Lua/C++ Boundary — Passed; the feature gate and travel tunables live in `sim_player_manager.lua`.
- [x] 6. AI-Economy / Simulation Safety — Passed; behavior is simulation-only and default-off.
- [x] 7. Error Handling — Passed; resolver failures, bounded exhaustion, deaths, cancellation, and stale movement recover gracefully.
- [x] 8. Security — Not applicable; no auth/credential/REST/external-input surface changed.
- [x] 9. Performance — Passed; world queries occur at travel transitions, not population-wide per-tick loops.

---

## Manual Addendum (2026-07-24) — post-Codex fixes, owner live-verified

The following were implemented after the Codex loop, during Phase-3
live-verification, and are covered by manual review + in-game verification (the
travel/movement surface is hard to unit-test; verified against the live REST
dashboard and `cellnav.log` per project standard):

- **A. Starport containment-margin fix (`SimPlayerManager::resolveStarportInteriorWaypoint`).**
  Live traces showed PvP collector departures wall-hugging: enclosed-hollow
  starports bake the ticket collector ~10 m outside the building's collision AABB,
  so strict point-in-AABB containment returned `NO_INTERIOR` and the bot walked
  straight at the walled hollow. Fixed with a two-pass selector — strict
  containment stays primary (no regression); a bounded horizontal margin
  (`ticketCollectorTravel.interiorContainmentMarginMeters`, default 15, clamped
  0–40) rescues the hollow case, safe because the next cell-bearing building is
  always 100 m+ away. **Live-verified: 66/66 PvP collector approaches now enter
  the interior (`moveToInterior`), 0 wall-hugs.**

- **B. NPC pivot-swivel fixes (`AiAgentImplementation::findNextPosition`).** Two
  direction-calc defects that produced a ~90/180° facing spin at path pivots: (1)
  a near-zero per-step move vector applied a meaningless heading (`atan2(0,0)`) —
  now the facing is held when not advancing; (2) a negative-angle wrap added `+PI`
  (a 180° flip) instead of `+2*PI` — corrected to match the proven patrol-side
  calc (`~:3894`). **Live-verified "way better."** A third, deeper cell-crossing
  coordinate-space contribution was investigated and **reverted** (a world-space
  heading regressed in-cell facing, since a cell-parented creature's facing is
  parent-relative); it is logged as a future item along with the separate in-cell
  waypoint-feed wiggle.

- **C. Combat wall-clip — logged, not implemented.** With squads now boarding in
  starport hollows, an outside squad can engage an enemy standing in the (cell-0,
  parent-null) hollow and the direct combat mover clips through the exterior wall.
  The owner's intended end-state — LoS-gated *attacks* plus portal-aware pursuit
  to converge — is recorded as a future design item in
  `docs/ai-cell-navigation-design.md`; no combat-targeting code changed this
  release.

- **D. Diagnostics behind a flag (not stripped).** All cell-nav instrumentation
  (`CellNavDiagLog` → `bin/log/cellnav.log`) is retained but gated by a single
  master flag `CellNavDiagLog::setLoggingEnabled()`, wired to
  `cellNavDiag.logging` (default **false**), so production carries the full
  instrumentation dormant at zero cost; the diagnostic test-bot spawn
  (`cellNavDiag.enabled`) is likewise default off.

---

## Verdict

**APPROVED**

Build completed cleanly with `-Werror`; Lua validation (`luac -p`) passed. No unit
tests were added under the documented travel/movement hard-to-mock rationale;
real-bot travel and swivel behavior were verified live against the dashboard and
`cellnav.log`. Diagnostic scaffolding is intentionally retained (flag-gated)
rather than stripped, per owner direction. Deferred future items: portal-aware
combat pursuit + LoS-gated attack, the cell-crossing heading garbage, and the
in-cell waypoint-feed wiggle. This work remains on `feat/cellnav-entry-diagnostics`
and is **not** merged into `miner-ai` (owner policy: stay off `miner-ai` until
production-ready; PvP convergence work still pending).
