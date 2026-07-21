# Code Review: PvE Hunter Combat-Targeting Fixes

**Review Date**: 2026-07-21  
**Version**: 0.4.5  
**Files Reviewed**:

- `MMOCoreORB/src/server/zone/objects/creature/CreatureObjectImplementation.cpp`
- `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimHunterController.cpp`
- `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimHunterController.h`
- `docs/4-unit-tests/COVERAGE-DEBT.md`
- `docs/ARCHI.md`
- `docs/ai-pve-playerbot-design.md`

(The working tree also contained pre-existing, out-of-scope owner changes to
`death_watch_wraith.lua`, `rifle_t21.lua`, and the `engine3` submodule; these were
explicitly excluded from the F_0.4.5 commit — see the Major/Minor findings.)

**Plan**: `docs/1-plans/F_0.4.5_p84-hunter-combat-targeting.plan.md`

---

## Executive Summary

This change prevents neutral PvE hunters from targeting real players and makes combat follow the actual active attacker safely across concurrent controller ticks. All code findings were addressed or explicitly overridden as out of scope. The mandatory live-dashboard/in-game verification was completed by the owner on 2026-07-21 (see Live-Verification Addendum).

APPROVED

---

## Changes Overview

The patch adds a symmetric player-safety guard to `CreatureObjectImplementation::isAttackableBy` and introduces defender-driven target selection, bounded follow-target hysteresis, bilateral combat cleanup, and species-aware target promotion. Review iterations established single-writer ownership for mission-target state, protected asynchronous kill credit during retargeting, and made defender-list inspection concurrency-safe. Architecture/design documentation and the coverage-debt ledger were updated for the resulting behavior and test strategy.

---

## Findings

### Critical Issues

None.

### Major Issues

- **Unrelated shared combat-template tuning** — `MMOCoreORB/bin/scripts/mobile/dungeon/death_watch_bunker/death_watch_wraith.lua:10`, `MMOCoreORB/bin/scripts/object/weapon/ranged/rifle/rifle_t21.lua:118`. The working tree contains broad damage reductions unrelated to P.8.4. **Disposition: accepted with override** — confirmed as pre-existing owner tuning that is excluded from this feature commit and handled separately.

- **Concurrent tick loops could race mission-target state** — `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimHunterController.cpp:219`, `:227`, `:813`. `onTick` and `runActiveTick` were independently scheduled and could both mutate `targetOid` and observer state. **Disposition: addressed** — `onTick` now defers entirely during HUNTING and uses only interceptor combat during travel, leaving `runActiveTick` as the sole HUNTING writer.

- **Defender-list snapshot could race concurrent removal** — `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimHunterController.cpp:1048`. Iterating using a separately read size allowed a concurrent `removeDefender` to invalidate an index. **Disposition: addressed** — defender references and the follow OID are snapshotted under the hunter lock, then filtered outside it using retained `ManagedReference` values.

- **Retargeting could discard pending kill credit** — `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimHunterController.cpp:1131`, `:1398`. Promoting another species-valid attacker before the queued destruction handoff ran changed `targetOid`, causing the handoff to reject the legitimate kill. **Disposition: addressed** — an observed dead mission target now defers retargeting until `onHuntDestruction` processes and credits it.

- **Dead pre-observer targets could suppress combat indefinitely** — `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimHunterController.cpp:1138`, `:1268`. The initial destruction guard also deferred targets that died before observer registration, although no handoff could arrive to clear them. **Disposition: addressed** — deferral now requires `observerTargetOid == targetOid` and a non-null `targetObserver`; unobserved deaths fall through to normal retargeting or cleanup.

- **Required live behavior verification is incomplete** — `docs/1-plans/F_0.4.5_p84-hunter-combat-targeting.plan.md`, `docs/4-unit-tests/COVERAGE-DEBT.md`. The checklist requires live-dashboard or in-game verification for SimPlayer behavior, specifically confirming player safety and attacker re-engagement. **Disposition: resolved** — completed by the owner on 2026-07-21 (see Live-Verification Addendum).

### Minor Issues

- **Unrelated `engine3` modification in the working tree** — `MMOCoreORB/utils/engine3/MMOEngine/src/engine/core/TaskManager.h:136`. The submodule contains unrelated `LambdaTask` reference-lifetime edits. **Disposition: accepted with override** — confirmed as pre-existing and excluded from the feature commit under project policy.

- **Comments and design documentation described obsolete dual-writer behavior** — `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimHunterController.cpp:198`, `docs/ai-pve-playerbot-design.md`, `docs/1-plans/F_0.4.5_p84-hunter-combat-targeting.plan.md`. **Disposition: addressed** — documentation now records single-writer ownership, travel-interceptor quota behavior, locked snapshots, and the observer-gated destruction guard.

### Suggestions

None.

---

## Checklist

- [x] 1. Functional Requirements — Passed; all identified implementation defects were addressed.
- [x] 2. Code Quality — Passed.
- [x] 3. Architectural Compliance — Passed.
- [x] 4. Distributed Object / IDL Discipline — Passed; managed references and established locking patterns are used.
- [x] 5. Lua/C++ Boundary — Passed; the controller no longer has a dual-driver conflict.
- [x] 6. AI-Economy / Simulation Safety — Passed; live in-game verification completed by the owner (see addendum).
- [x] 7. Error Handling — Passed.
- [x] 8. Security — Passed; no authentication, credential, or endpoint changes.
- [x] 9. Performance — Passed; per-tick work remains bounded by the hunter's defender list.

---

## Verdict

**APPROVED**

Build, lint, and typecheck are clean and `CreatureObjectTest` passes 6/6. The absence of new AI-layer unit tests is covered by `docs/4-unit-tests/COVERAGE-DEBT.md` (acceptance bar is live-dashboard verification per ARCHI.md §Testing). The unrelated Lua tuning and `engine3` edits were explicitly overridden as out-of-scope working-tree changes and excluded from this commit. No code findings remain open.

Codex code review ran as a 4-round loop and converged with no remaining code findings; the consolidated review above was `PROMOTION_READY` pending only the live gate.

## Live-Verification Addendum (owner-verified, not Codex-re-reviewed)

The sole open item at synthesis time was mandatory live in-game verification. The owner
restarted the server and confirmed on 2026-07-21 that both bugs are resolved ("tested and
bugs seem resolved"):

- **Bug 1 (player safety)** — the neutral hunter no longer damages or locks onto a real
  player caught in its area attack.
- **Bug 2 (attacker re-engagement)** — the hunter re-fires on the creature actually
  attacking it rather than pinning to a fled first target.

This addendum records owner-verified live behavior; it was not re-reviewed by Codex.
