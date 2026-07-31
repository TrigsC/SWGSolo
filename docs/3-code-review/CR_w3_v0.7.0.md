# Code Review: P.6.6 PvP Controller-Driven Combat Engagement + P.6.6b Squad Aggro-Sharing

**Version**: 0.7.0
**Week**: 3
**Plans**:
- `docs/1-plans/F_0.7.0_p66-pvp-combat-engagement-rework.plan.md` (P.6.6 + P.6.6a traversal suppression + Fix A/B)
- `docs/1-plans/F_0.7.1_p66b-squad-aggro-sharing.plan.md` (P.6.6b)

**Codex loops**: F_0.7.0 — multi-round, converged to a single open Major (default-off config, resolved here); F_0.7.1 — multi-round, converged approved with observations.

---

## Executive Summary

P.6.6 replaces emergent stock-tree PvP combat with a full controller-driven
engagement pipeline: 100 m scan + cell-aware approach (no clipping through
starports) + LOS/weapon-range engage + weapon-stock hold band + no-stacking, with a
dynamically-swapped `simPvpCombat` no-op-MOVE AI map so the stock tree never
double-drives movement while engaged. P.6.6a suppresses combat while a member is
mid-starport-traversal (traversal wins). Fix A stops world-NPC drag-in via a shared
eligibility helper; Fix B makes an in-range bot engage-and-hold under LOS loss
(stock combat enforces damage-LOS) instead of clearing combat and dying passively.
P.6.6b adds default-off, squad-local shared-aggro so idle squadmates converge on a
teammate's fight over the same cell-aware lane with a convergence-specific radius
(300 m) and timeout (60 s), TTL keep-alive, and failed-target suppression.

**Verdict: APPROVED**

The single open Major from the F_0.7.0 loop — "restore the feature's checked-in
default-off configuration" — is **resolved in this release**: `controllerDrivenEngage`
and `logCombatMovement` are `false` (default-off), matching the plan and the
project's gated/simulation-only standard. With that resolved, both loops are clean.

---

## F_0.7.0 (P.6.6) — findings, all addressed

1. Defender-side bots never enter the controller lane — Addressed (shared eligibility adoption, `SimPvPController.cpp:598/669`).
2. Combat approach can fork two arrival chains — Addressed (generation-change handoff, `SimPlayerController.cpp:1004`).
3. Successful engagement retains an expired approach timeout — Addressed (clock reset on engage, `SimPvPController.cpp:379`).
4. Default ranged re-approach unreachable behind the 72 m leash — Addressed (controller uses approach radius, `SimPvPController.cpp:572`).
5. Starport diagnostics record traversal intent as success — Addressed (require observed cell entry, `SimPvPController.cpp:1269/1767`).
6. Adopted defenders retain a stale active-approach timestamp — Addressed (normalize holding state, `SimPvPController.cpp:475`).
7. Combat and interior traversal fight for movement — Addressed (traversal predicate + suppression, `SimPvPController.h:97` / `.cpp:553`, leader `1254/1344`, member `1923`).
8. Final live verification pending — Addressed by this release's TRIP-verify live soak.
9. Restore default-off config — **RESOLVED in this release** (`controllerDrivenEngage=false`, `logCombatMovement=false`).

Incremental fixes:
- World-NPC defender adoption (Fix A) — shared eligibility helper (players or enabled sim-bots), `SimPvPController.cpp:159`; used by adoption (`602`) and scan (`780`).
- LOS clear-combat churn (Fix B) — in-range targets engage-and-hold; LOS telemetry counts false→true transitions only, `SimPvPController.cpp:458/466-471`.

## F_0.7.1 (P.6.6b) — findings, all addressed

- **Major** — convergence timeout made the configured radius unreachable — Addressed (dedicated 60 s `squadAggroConvergeTimeoutMillis`, bounded 1k–300k ms, used only while `combatIsConvergenceTarget`).
- **Minor** — one unsuitable contact blocked later valid shared targets — Addressed (`getPvpSquadSharedCombatTargets` returns all live OIDs; controller iterates eligibility).
- **Minor** — P.6.6b test coverage neither implemented nor entered as debt — Addressed (`COVERAGE-DEBT.md` entry).
- **Minor** — live PvP verification pending — Addressed by this release's TRIP-verify live soak.

## Live verification (this release)

TRIP-verify at the **squad-level acceptance bar** (per-member contribution attribution
accepted as documented debt — current telemetry aggregates per squad, not per member),
owner-authorized. Fresh restart, disposable test server, `controllerDrivenEngage` +
`squadAggroSharing` enabled at runtime (shipped default-off). Confirmed: convergences climb
(3→13); **4/4 acquisition on 3 patrol squads**; `engagementsInRange` up to 4; squad-level real
combat (14+ controller `combatEngage`/startCombat + 19+ PvP kills); self-limiting (shared
targets rise then fall on 4 squads; `targetUnavailable` releases); LOS gate counts but never
clears combat (Fix B holds); traversal suppression preserved; no crash, no forbidden mutation,
fully reversible. Report: `docs/4-unit-tests/live_p66b-squad-aggro_20260731-155900.md`.
Documented debt: per-member contribution telemetry + P.6.6 approach-timeout closing-efficiency follow-up.

## Checklist (combined)

- Functional Requirements: **Pass** (default-off contract resolved; LOS revision internally consistent).
- Code Quality: Pass.
- Architectural Compliance: Pass.
- Distributed Object / IDL Discipline: Pass — no IDL/generated edits; agent locks taken outside `pvpSquadMutex`.
- Lua/C++ Boundary: Pass — tunables Lua-configured and explicitly bounded.
- AI-Economy / Simulation Safety: Pass — simulation-only; all new gates default-off.
- Error Handling: Pass.
- Security: Pass — no auth/endpoint/secret/input-surface changes.
- Performance: Pass — bounded shared-contact cap; no new raycasts; verbose combat logging disabled by default.
- Build/testing: incremental `-Werror` clean; `luac -p` clean; controller state machine recorded in `COVERAGE-DEBT.md`; `git diff --check HEAD` clean.

## Verdict

**APPROVED**
