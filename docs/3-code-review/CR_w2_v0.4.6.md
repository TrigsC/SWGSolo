# Code Review: P.8.6 — Player-Mimetic Real Buffs (Hunter Buff Acquisition)

**Review Date**: 2026-07-21
**Version**: 0.4.6
**Files Reviewed**:

- `MMOCoreORB/bin/scripts/custom_scripts/smart_entertainer_helper.lua`
- `MMOCoreORB/bin/scripts/managers/sim_player_manager.lua`
- `MMOCoreORB/bin/scripts/screenplays/custom/smartDoctorBuffer.lua`
- `MMOCoreORB/bin/web/aieconomy-dashboard/app.js`
- `MMOCoreORB/src/server/zone/objects/creature/ai/AiAgentImplementation.cpp`
- `MMOCoreORB/src/server/zone/objects/creature/ai/LuaAiAgent.cpp`
- `MMOCoreORB/src/server/zone/objects/creature/ai/LuaAiAgent.h`
- `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimHunterController.cpp`
- `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimHunterController.h`
- `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerController.cpp`
- `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerController.h`
- `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp`
- `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.h`
- `docs/4-unit-tests/COVERAGE-DEBT.md`

**Plan**: `docs/1-plans/F_0.4.6_p86-hunter-real-buffs.plan.md`

> Owner-owned `death_watch_wraith.lua`, `rifle_t21.lua`, and the `engine3`
> submodule are intentionally EXCLUDED from this release (not committed), per the
> standing PvE-train policy.

---

## Executive Summary

This change replaces unconditional synthetic hunter buffs with need-driven interaction with real doctor, musician, and dancer providers, retaining a synthetic fallback and default-off feature gate. The review identified two Major correctness concerns and one Minor dashboard issue; the correctness defects were fixed, while the nonpersistent shared queue concern was accepted with an explicit graceful-degradation override.

APPROVED with observations

---

## Changes Overview

The controller now detects missing or expiring buff families, approaches in-world providers, performs player-mimetic watch/listen or doctor-screenplay interactions, and resumes hunting after verification or timeout fallback. Supporting changes add Lua configuration, provider discovery and bridge methods, generation-token validation, dashboard telemetry, and coverage-debt documentation. The implementation remains simulation-only and is gated by `realBuffs.enabled` at `MMOCoreORB/bin/scripts/managers/sim_player_manager.lua`.

---

## Findings

### Critical Issues

None.

### Major Issues

1. **Stale doctor cancellation could abort a newer request**
   **Location**: `MMOCoreORB/bin/scripts/screenplays/custom/smartDoctorBuffer.lua`
   **Disposition**: Addressed. The current bot generation and expiry are persisted with the active target and reloaded across thread-local Lua states. `botCancel` now prefers that authoritative persisted generation, preventing a stale generation-N cancellation from terminating generation N+1.

2. **Shared doctor queue and queued bot tokens are not persisted across Lua states**
   **Location**: `MMOCoreORB/bin/scripts/screenplays/custom/smartDoctorBuffer.lua`
   **Disposition**: Accepted with override. The queue remains memory-local, matching the pre-existing owner doctor screenplay behavior. The implementer accepted possible queue-position loss under rare contention because the controller deadline remains authoritative and routes the hunter to synthetic fallback without stripping buffs, crashing, or stranding the hunt loop. Queue contention remains a live-verification observation rather than a release blocker.

3. **No-strip sim-bot doctor flow retained clone wounds**
   **Location**: `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimHunterController.cpp`
   **Disposition**: Addressed. `finishPveBuffProviderFlow` now clears all HAM wounds and shock under the established agent lock after either real-provider success or synthetic fallback, restoring the healing previously supplied by `applyHunterBuffs(clearWounds=true)`.

### Minor Issues

1. **Wilds dashboard showed `needed · 0s` while real buffs were disabled**
   **Location**: `MMOCoreORB/bin/web/aieconomy-dashboard/app.js`
   **Disposition**: Addressed. The roster now renders a dedicated `disabled` chip whenever `pve.realBuffsEnabled` is false.

### Suggestions

None.

---

## Checklist

- [x] 1. Functional Requirements — Passed; plan behavior is implemented and the identified correctness defects were resolved.
- [x] 2. Code Quality — Passed; changes are localized, documented where synchronization is non-obvious, and `git diff --check HEAD` is clean.
- [x] 3. Architectural Compliance — Passed; controller, manager, screenplay, provider, and dashboard responsibilities follow established project boundaries.
- [x] 4. Distributed Object / IDL Discipline — Passed; no autogen edits or IDL annotation issues, and distributed objects use managed references with established locking.
- [x] 5. Lua/C++ Boundary — Passed; tunables remain in Lua and no competing behavior driver was introduced.
- [x] 6. AI-Economy / Simulation Safety — Passed; behavior is simulation-only, default-off, and covered by the supplied live-dashboard verification.
- [x] 7. Error Handling — Passed with accepted observation; provider failures, timeouts, stale requests, and queue loss degrade to safe synthetic fallback.
- [x] 8. Security — Passed; bot request input and resolved objects are validated, with no new unauthenticated dashboard route or sensitive-data exposure.
- [x] 9. Performance — Passed; no unbounded structures, per-tick population scans, or other material hot-path regressions were found.

---

## Verdict

**APPROVED with observations**

The stale-cancellation race, clone-wound retention, and disabled-dashboard display are resolved. Nonpersistent shared-doctor queue state remains intentionally unchanged under an accepted graceful-degradation override and should be exercised during the recorded live-verification pass. The supplied clean lint, `-Werror` build/typecheck, Lua validation, zero affected test suites, and coverage-debt entry at `docs/4-unit-tests/COVERAGE-DEBT.md` satisfy the approval gate.

---

## Live-Hardening Addendum (post-Codex, owner-verified — NOT Codex re-reviewed)

The two Codex code-review rounds above ran before live verification. The
following changes were made after that APPROVED verdict; they were self-reviewed
and **live-verified by the owner**, not re-run through the Codex loop (mirrors the
F_0.4.4 live-hardening pattern).

- **Provider resolution "absent" fixed (the key live bug).** First live-verify
  with `realBuffs.enabled=true` showed zero real interactions
  (`doctorInteractions/musicianListens/dancerWatches = 0`, `syntheticFallbacks`
  climbing) and `providerResolveState` = `absent` for the provider cities —
  every hunter fell back to synthetic. Root cause (Codex investigation, grounded):
  the buffer NPCs sit inside building cells that are **delayed-load containers**;
  `GroundZone::getInRangeObjects` returns the building but skips not-yet-loaded
  cell containers, so the NPC contents were invisible. Fix in
  `SimPlayerManager::resolvePveBuffProviders`: `loadProviderBuildingCells` locates
  the matching provider building (template filter `hospital` / `cantina`),
  force-loads its cells (`CellObject::getContainerObjectsSize()` when
  `!isContainerLoaded()`), then removes/rescans so the already-spawned screenplay
  NPCs are enumerable before name matching. World query stays outside all
  manager/agent locks; generic zone-query semantics are unchanged. (One compile
  fix applied by hand: `SortedVector<TreeEntry*>::getUnsafe` returns a raw
  pointer, not a `Reference`.)
- **Navmesh finding (owner hypothesis, resolved).** The buffer building interiors
  move on **POB portal/floor graphs**, not the outdoor Recast mesh, and those
  graphs exist — no static navmesh blocker. Interior approach is reachable.
- **Redundant death-buff hook reverted.** A hook that removed tracked buffs in
  `SimHunterController::runActiveTick`'s `isDead()` block was removed: the stock
  death path (`CreatureManager::notifyDestruction` →
  `clearBuffs(removeAll=true)`) already clears all buffs, then `spawnPveIdentityBody`
  builds a fresh empty body, so respawns already re-detour for buffs.
- **Scan radius `40 → 400` (owner).** `providerScanRadiusMeters` (Lua) and the
  `pveBuffProviderScanRadiusMeters` C++ defaults were widened from 40 m to 400 m
  so the outdoor med-center / cantina anchor reaches the interior provider NPC.
- **Live-verify PASS (owner):** with the cell-load fix + 400 m radius, a hunter
  **ran to the providers and obtained real buffs** (the reported success). Build
  clean `-Werror`, links `core3`; luac clean.
