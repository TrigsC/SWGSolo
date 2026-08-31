# Live Verification — F_0.8.1 Traversal Adoption by Production Controllers

**Run ID**: `20260830-223242-f081-release`
**Date**: 30-08-2026 22:32 (project week 7)
**Target**: `docs/1-plans/F_0.8.1_traversal-controller-adoption.plan.md`
**Verdict**: **LIVE_VERIFICATION_PASS** — 8 / 8 assertions

## Source and changed subsystems

Branch `feat/f081-traversal-controller-coverage`, HEAD `08df4a8254`
(code HEAD `249d53193d`; the tip commit is docs-only).

| commit | scope |
| --- | --- |
| `7b59646fe3` | config: hunter demand reserves (ungated, owner-approved standing tuning) |
| `72ce8d6752` | stage 1 — hunters adopt traversal; `acceptFoundPath` template method; PvP collector cell-local fix |
| `d2c6c7b5b2` | stage 2 — PvP collector approaches; indoor z-sanity skip; per-step anomaly oracle |
| `4169ad8111` | stage 3 — PvP starport waypoints; per-scenario anomaly oracle; suppression ordering fix |
| `30df586ce8` | review fix — anomaly carry across scripted death, atomic counters, locked fold |
| `249d53193d` | review fix — post-lock agent lifecycle recheck in `checkArrival` |

Subsystems: `SimHunterController`, `SimPvPController`, `SimPlayerController`,
`SimPlayerManager` (traversal harness oracle), `sim_player_manager.lua`.

## Static gate

- Build: warning-clean against `-Werror` (no recompilation needed; tree matched binary).
- Lua: `luac -p` clean on `sim_player_manager.lua` (the only changed `.lua`).
- Unit tests: no SimPlayer/traversal GoogleTest suite exists. `docs/4-unit-tests/COVERAGE-DEBT.md`
  records for this exact subsystem that the in-tree 26-scenario harness IS the test
  deliverable; plan §4 explains why the harness cannot drive a production controller and
  substitutes the `ST_PHASE` trace as the non-vacuity gate.

## Restart and readiness

`server-cycle.sh restart` — connected test players 0; ready with fresh `who.json` after 12s.

Pre-restart note: the REST dashboard listener was wedged (`http_code=000`, 60s timeout), a
known pre-existing condition in this project. The restart cleared it. Counters are
process-lifetime, so the post-restart zero is the baseline and is stricter than a
pre-restart snapshot would have been.

## Scenario configuration

Verification profile (restored afterwards): `structureTraversal.{enabled,logging,
hollowEscalationEnabled,farSideEgress}`, `zeroClip.{enabled,logging,enforce,
walkableConfirm,exitSetEnabled}`, `hollowDoorEgress.{observe,walk,useCellPortals}`,
`structureTraversalTest.enabled`, `cellNavDiag.logging` — all true.
Baseline `anomalyCounters` all zero at boot.

## Assertions

| # | Assertion | Expected | Actual | Result |
| --- | --- | --- | --- | --- |
| A1 | Clean startup, process alive, no crash/backtrace | none | no crash markers; `gdb.log` unchanged since 2026-08-17 (stale, unrelated) | **PASS** |
| A2 | Production agent OIDs appear in `ST_PHASE` (non-vacuity) | > 0 | 62 distinct agents, **61 production**, 1 harness | **PASS** |
| A3 | Phases ordered per agent | ordered | `outdoor_enter` 227 → `entered_structure` 189 → `target_cell_arrived` 205; `exit_requested` 30 / `exit_complete_outdoors` 29 | **PASS** |
| A4 | Production `ST_FAIL` bounded, no unbounded loop | bounded | 17 total: 1 harness (known scenario failure), 16 production across 5 agents in 3-attempt bouts; every agent resumes successful entries afterwards (e.g. `EAEAEAEAEFEFEFEAEAEAEAEA`) | **PASS** |
| A5 | Harness bot anomalies zero; indoor z-sanity zero | 0 / 0 | **0** and **0** | **PASS** |
| A6 | PvP travel completes; hunters buff | > 0 boardings, 0 fallbacks/orphans | 57 collector boardings, `collectorFallbacks` 0, `fallbacksTotal` 0, `orphanBots` 0, 98 legs executed; buff trips 9 started / 6 completed / 2 fallback with 6 doctor, 7 dancer, 7 musician interactions | **PASS** |
| A7 | 26-scenario matrix vs reference | 23 PASS / 3 FAIL, 0 divergences | **23 PASS / 3 FAIL, 0 divergences**; identical fail reasons `exit_budget_exceeded`, `target_cell_unresolved`, `combat_pause_not_observed` (all pre-date this work) | **PASS** |
| A8 | Cleanup: gates default-off, no harness bot leaked | clean | harness OID absent from `bots[]`, `scenarioCursor` 26; gates restored default-off; tree clean | **PASS** |

## Observations (not assertion failures)

- `zSanityViolations` 53 and `teleportsDetected` 3, all on production bots and all **outdoor**
  (`cell=0`); zero on the harness bot, matrix unaffected. `recordTraversalMovementStep` only
  samples while a traversal is active, so migrating the dominant leg surfaced pre-existing
  outdoor behaviour rather than changing it. Traces show smooth ~2.4m steps over steep
  downslope, not wall clips. Diagnostics-only; gates nothing.
- `resumeFailures` 1, on the combat-pause/resume path, within its configured attempt ladder.
- `exitStructure` remains adopted only by the harness — deliberate, tracked as
  `docs/1-plans/F_0.8.2_traversal-egress-adoption.plan.md`.

## Evidence

- Primary: `/home/swgemu/workspace/Core3/MMOCoreORB/bin/log/trip-verify-20260830-223242-f081-release.log` (3.2 MB)
- Matrix snapshot: `matrix_final_v.json` (26 scenarios, per-step outcomes)
- Earlier reference run for the A7 comparison: `bin/log/trip-verify-f081-postreview.log`

## Cleanup confirmation

Verification profile reverted to default-off and `luac`-verified; `git status` clean apart
from the owner-owned `engine3` submodule and the excluded `_aieconomy_wipe_backup_*`
directory. No databases, persistent rosters, or unrelated logs touched. No temporary
entities remain.

## Verdict

**LIVE_VERIFICATION_PASS**
